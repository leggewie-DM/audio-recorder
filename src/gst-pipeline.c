/*
 * Copyright (c) Team audio-recorder.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License (GPL3), or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Library General Public License 3 for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License 3 along with this program; if not, see /usr/share/common-licenses/GPL file
 * or <http://www.gnu.org/licenses/>.
*/
#include <math.h>
#include "gst-pipeline.h"
#include "audio-sources.h"

/*
 Create Gstreamer pipelines for recording.

 Please see src/media-profiles.c file. It has some hard-coded audio profiles.
*/

static GstElement *pipeline_create_simple(PipelineParms *parms, gchar **err_msg);
static GstElement *pipeline_create_complex(PipelineParms *parms, gchar **err_msg);

//static GstElement *pipeline_create_simple_VAD(PipelineParms *parms, gchar **err_msg);
static GstElement *pipeline_create_complex_VAD(PipelineParms *parms, gchar **err_msg);

static GString *pipeline_create_command_str_simple(PipelineParms *parms);
static GString *pipeline_create_command_str_complex(PipelineParms *parms);

void pipeline_free_parms(PipelineParms *parms) {
    if (!parms) return;
    g_free(parms->source);
    str_list_free(parms->dev_list);
    g_free(parms->profile_str);
    g_free(parms->file_ext);
    g_free(parms->filename);

    g_free(parms);
}

static GstElement *create_element(const gchar *elem, const gchar *name) {
    GstElement *e = gst_element_factory_make(elem, name);
    if (!GST_IS_ELEMENT(e)) {
        LOG_ERROR("Cannot create element \"%s\" (%s).\n", elem, name);
        return NULL;
    }
    return e;
}

GstElement *pipeline_create(PipelineParms *parms, gchar **err_msg) {
    if (!parms) return NULL;

    // Create a GStreamer pipeline for audio recording
    GstElement *pipeline = NULL;

    // Wash the device list. User may have disconnected microphones and webcams.
    // Invalid devices will crash the GStreamer pipeline.
    // Remove invalid devices.
    GList *new_list = audio_sources_wash_device_list(parms->dev_list);

    if (g_list_length(new_list) < 1) {
        new_list = g_list_append(new_list, NULL);
    }

    // Zero or one device?
    if (g_list_length(new_list) < 2) {

        // Create a simple pipeline that can record from max 1 device
        pipeline = pipeline_create_simple(parms, err_msg);

    } else {

        // Create a complex pipeline that can record from 2 or more devices
        pipeline = pipeline_create_complex(parms, err_msg);
    }

    // Free new_list
    str_list_free(new_list);
    new_list = NULL;

    return pipeline;
}

static GstElement *pipeline_create_simple(PipelineParms *parms, gchar **err_msg) {
    // Create a simple pipeline that can record from one (1) device only.
    //
    // Typical pipeline:
    // $ gst-launch-1.0 pulsesrc device=alsa_output.pci-0000_04_02.0.analog-stereo.monitor
    //        ! queue
    //        ! level name=level
    //        ! audioconvert ! vorbisenc ! oggmux ! filesink location=test1.ogg
    //
    // Get list of available devices:
    // $ pactl list | grep -A2 'Source #' | grep 'Name: ' | cut -d" " -f2
    // $ pactl list short sources | cut -f2

    GstElement *pipeline = gst_pipeline_new("Audio-Recorder");

    // Source
    const gchar *source_name = (parms->source ? parms->source : "pulsesrc");
    GstElement *source = create_element(source_name, NULL);

    // Set device
    const gchar *device = g_list_nth_data(parms->dev_list, 0);
    if (device) {
        g_object_set(G_OBJECT(source), "device", device, NULL);
    }

    GstElement *level = create_element("level", "level");

    gchar *str = g_strdup_printf("capsfilter caps=%s", parms->profile_str);

    GError *error = NULL;
    GstElement *bin = gst_parse_bin_from_description(str, TRUE, &error);
    if (error) {
        // Set err_msg
        gchar *tmp = g_strdup_printf("%s. (%s)", error->message, str);
        *err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), tmp);
        g_free(tmp);
        g_error_free(error);
        g_free(str);
        goto LBL_1;
    }

    g_free(str);

    GstElement *resample = create_element("audioresample", NULL);
    GstElement *convert = create_element("audioconvert", NULL);

    // Filesink. Caller must set its "location" property.
    GstElement *filesink = create_element("filesink", "filesink");

    gst_bin_add_many(GST_BIN(pipeline), source, level, resample, convert, bin, filesink, NULL);

    // Link
    if (!gst_element_link_many(source, level, resample, convert, bin, filesink, NULL)) {
        *err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), "Cannot link.");
        goto LBL_1;
    }

    // Ok
    return pipeline;

LBL_1:
    // Got an error
    return NULL;
}

static GstElement *pipeline_create_complex(PipelineParms *parms, gchar **err_msg) {
    // Create a complex pipeline using the audiomixer or GstAdder elements.
    // Ref: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-adder.html
    // This can record from 2 or more devices.

    // Typical pipeline (using the "audiomixer" element):
    // $ gst-launch-1.0 audiomixer name=mixer mix.
    //      ! audioconvert
    //      ! vorbisenc ! oggmux
    //      ! filesink location=test1.ogg
    //      pulsesrc device=alsa_output.pci-0000_00_1b.0.analog-stereo.monitor ! queue ! mix.
    //      pulsesrc device=alsa_input.usb-Creative_Technology_Ltd._VF110_Live_Mic ! queue ! mix.

    // Using the "adder" element:
    // $ gst-launch-1.0 adder name=mixer mix.
    //      ! audioconvert
    //      ! vorbisenc ! oggmux
    //      ! filesink location=test1.ogg
    //      pulsesrc device=alsa_output.pci-0000_00_1b.0.analog-stereo.monitor ! queue ! mix.
    //      pulsesrc device=alsa_input.usb-Creative_Technology_Ltd._VF110_Live_Mic ! queue ! mix.
    //
    // Get list of available devices:
    // $ pactl list | grep -A2 'Source #' | grep 'Name: ' | cut -d" " -f2
    // $ pactl list short sources | cut -f2

    GstElement *pipeline = gst_pipeline_new("Audio-Recorder");

    // Create either "audiomixer" or "adder". Audiomixer is an improved version of adder.
    GstElement *mixer = create_element("audiomixer", "mixer");
    if (!GST_IS_ELEMENT(mixer)) {
        mixer = create_element("adder", "mixer");
    }

    // Level (dB) data
    GstElement *level = create_element("level", "level");

    // Create a GstCapsfilter + all encoder elements from the GNOME's profile_str.
    // See: gconf-editor,  system -> gstreamer -> 0.10 -> audio -> profiles.
    gchar *str = g_strdup_printf("capsfilter caps=%s", parms->profile_str);

    GError *error = NULL;
    GstElement *bin = gst_parse_bin_from_description(str, TRUE, &error);
    if (error) {
        // Set err_msg
        gchar *tmp = g_strdup_printf("%s. (%s)", error->message, str);
        *err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), tmp);
        g_free(tmp);
        g_error_free(error);
        g_free(str);
        goto LBL_1;
    }

    g_free(str);

    GstElement *resample = create_element("audioresample", NULL);
    GstElement *convert = create_element("audioconvert", NULL);

    // Filesink. Caller must set its "location" property.
    GstElement *filesink = create_element("filesink", "filesink");

    gst_bin_add_many(GST_BIN(pipeline), mixer, level, resample, convert, bin, filesink, NULL);

    // Link
    if (!gst_element_link_many(mixer, level, resample, convert, bin, filesink, NULL)) {
        *err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), "Cannot link.");
        goto LBL_1;
    }

    // Now create audio source for all devices
    GList *item = g_list_first(parms->dev_list);
    while (item) {
        // Device name
        const gchar *device = (gchar*)item->data;

        // Source
        const gchar *source_name = (parms->source ? parms->source : "pulsesrc");
        GstElement *source = create_element(source_name, NULL);
        g_object_set(G_OBJECT(source), "device", device, NULL);

        //Queue
        GstElement *queue = create_element("queue", NULL);
        gst_bin_add_many(GST_BIN(pipeline), source, queue, NULL);

        // Link source# -> queue#
        gst_element_link(source, queue);

        // Link queue# -> mixer
        // Gstreamer 1.0:
        gst_element_link_pads(queue, NULL, mixer, NULL);

        // Next source
        item = g_list_next(item);
    }

    // Ok
    return pipeline;

LBL_1:
    // Got an error
    return NULL;
}

GstElement *pipeline_create_VAD(PipelineParms *parms, gchar **err_msg) {
    if (!parms) return NULL;
    // Create a GStreamer pipeline for VAD, Voice Activity Detection.

    GstElement *pipeline = NULL;

    // Wash the device list. User may have disconnected microphones and webcams.
    // Invalid devices will crash the GStreamer pipeline.
    // Remove invalid devices.
    GList *new_list = audio_sources_wash_device_list(parms->dev_list);

    if (g_list_length(new_list) < 1) {
        new_list = g_list_append(new_list, NULL);
    }

    pipeline = pipeline_create_complex_VAD(parms, err_msg);

    // Free new_list
    str_list_free(new_list);
    new_list = NULL;

    return pipeline;
}

#if 0
static GstElement *pipeline_create_simple_VAD(PipelineParms *parms, gchar **err_msg) {
    // Creater a simple pipelinewwaY TO MAKE for VAD, Voice Activity Detection.
    //
    // Typical pipelines:
    // gst-launch pulsesrc device=alsa_input.usb-Creative_Technology_Cam_Socialize !
    //            cutter name=cutter threshold=0.3 run-length=4000000000 ! fakesink

    // Or using the GstVader element (see .../gst-plugin/src/)
    // gst-launch pulsesrc device=alsa_input.usb-Creative_Technology_Cam_Socialize !
    //            vader name=cutter threshold=0.3 run-length=4000000000 ! fakesink"

    GstElement *pipeline = gst_pipeline_new("Voice Activity Detector");

    // Source
    const gchar *source_name = (parms->source ? parms->source : "pulsesrc");
    GstElement *source = create_element(source_name, NULL);

    // Set device
    const gchar *device = g_list_nth_data(parms->dev_list, 0);
    if (device) {
        g_object_set(G_OBJECT(source), "device", device, NULL);
    }

    // Do we find a GstVader element? (we compile it from source. see .../audio-recorder/gst-plugin/ folder)
    // Vader is a VAD (Voice Activity Detector), it's an improved version of GstCutter.
    // Ref: https://sourceforge.net/projects/cmusphinx/
    // Ref: https://developer.gnome.org/gst-plugins-libs/0.11/gst-plugins-good-plugins-cutter.html
    GstElement *cutter = gst_element_factory_make("vader", "cutter");

    if (!GST_IS_ELEMENT(cutter)) {
        // Standard GstCutter element should do fine
        cutter = create_element("cutter", "cutter");
    }

    // Disable "cutter" messages. Set threshold to 1.0.
    // We set this value later.
    g_object_set(G_OBJECT(cutter), "threshold", 1.0, NULL);

    // Fakesink
    GstElement *fakesink = create_element("fakesink", NULL);

    gst_bin_add_many(GST_BIN(pipeline), source, cutter, fakesink, NULL);

    // Link
    if (!gst_element_link_many(source, cutter, fakesink, NULL)) {
        *err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), "Cannot link.");
        goto LBL_1;
    }

    // Ok
    return pipeline;

LBL_1:
    // Got an error
    return NULL;
}
#endif

static GstElement *pipeline_create_complex_VAD(PipelineParms *parms, gchar **err_msg) {
    // Create a complex pipeline for VAD. Use audiomixer or GstAdder elements.
    // Ref: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-adder.html
    // https://cgit.freedesktop.org/gstreamer/gst-plugins-bad/tree/gst/audiomixer
    // For 2 more devices.

    // Typical pipelines:
    // $ gst-launch-1.0 audiomixer name=mix
    //        ! level name=level
    //        ! fakesink
    //        { pulsesrc device=alsa_output.pci-0000_04_02.0.analog-stereo.monitor ! queue ! mix. }
    //        { pulsesrc device=alsa_input.pci_8086_24c5_alsa_WebCam ! queue ! mix. }
    //
    // $ gst-launch-0.10 audiomixer name=mix ! level ! fakesink { pulsesrc ! queue ! mix. }

    GstElement *pipeline = gst_pipeline_new("Voice Activity Detector");

    // Mix audio from several sources.
    // Use "audiomixer" or "adder". Audiomixer is an improved version of adder.
    GstElement *mixer = create_element("audiomixer", "mixer");
    if (!GST_IS_ELEMENT(mixer)) {
        mixer = create_element("adder", "mixer");
    }

    // Level (dB) data
    GstElement *level = create_element("level", "level");

    // Fakesink
    GstElement *fakesink = create_element("fakesink", "fakesink");

    gst_bin_add_many(GST_BIN(pipeline), mixer, level, fakesink, NULL);

    // Link
    if (!gst_element_link_many(mixer, level, fakesink, NULL)) {
        *err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), "Cannot link.");
        goto LBL_1;
    }

    // Now create audio source for all devices
    GList *item = g_list_first(parms->dev_list);
    while (item) {
        // Device name
        const gchar *device = (gchar*)item->data;

        // Source
        const gchar *source_name = (parms->source ? parms->source : "pulsesrc");
        GstElement *source = create_element(source_name, NULL);

        if (device) {
            g_object_set(G_OBJECT(source), "device", device, NULL);
        }

        // Queue
        GstElement *queue = create_element("queue", NULL);

        gst_bin_add_many(GST_BIN(pipeline), source, queue, NULL);

        // Link source# -> queue#
        gst_element_link(source, queue);

        // Link queue# -> mixer
        // Gstreamer 1.0:
        gst_element_link_pads(queue, NULL, mixer, NULL);

        // Next source
        item = g_list_next(item);
    }

    // Ok
    return pipeline;

LBL_1:
    // Got an error
    return NULL;
}

#if 0
static GstElement *pipeline_create_test(PipelineParms *parms, gchar **err_msg) {
    GstElement *pipeline = NULL;
    gchar *pipeline_cmd = g_strdup_printf ("%s name=source"
                                           " ! level name=level"
                                           " ! queue"
                                           " ! audioconvert"
                                           " ! %s"
                                           " ! filesink name=filesink",
                                           parms->source,
                                           parms->profile_str);

    LOG_DEBUG("\n\n");
    LOG_DEBUG("Going to create pipeline:\ngst-launch %s\n\n", pipeline_cmd);

    GError *err = NULL;
    pipeline = gst_parse_launch(pipeline_cmd, &err);
    g_free(pipeline_cmd);

    if (err) {
        // Set err_msg
        *err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), err->message);
        g_error_free(err);
        goto LBL_1;
    }

    // Get the "source0" element
    GstElement *source = gst_bin_get_by_name(GST_BIN(pipeline), "source");

    if (!GST_IS_ELEMENT(source)) {
        // Set err_msg
        *err_msg = g_strdup_printf(_("Cannot find audio element %s.\n"), parms->source);
        goto LBL_1;
    }

    // Set audio device
    if (g_list_length(parms->dev_list) > 0) {
        gchar *device = parms->dev_list->data;
        g_object_set(G_OBJECT(source), "device", device, NULL);
    }

    g_object_unref(source);

    // Ok
    return pipeline;

LBL_1:
    // Got an error
    return NULL;
}

static GstElement *pipeline_create_TEST(PipelineParms *parms, gchar **err_msg) {
    GstElement *pipeline = NULL;

    gchar *pipeline_cmd = g_strdup_printf ("pulsesrc device=alsa_output.pci-0000_01_00.1.hdmi-stereo.monitor"
                                           " ! queue"
                                           " ! level name=level"
                                           " ! audioconvert"
                                           " ! audio/x-raw,rate=44100,channels=2 ! vorbisenc name=enc quality=0.5 ! oggmux"
                                           " ! filesink name=filesink");

    LOG_DEBUG("\n\n");
    LOG_DEBUG("Going to create pipeline:\ngst-launch %s\n\n", pipeline_cmd);

    GError *err = NULL;
    pipeline = gst_parse_launch(pipeline_cmd, &err);
    g_free(pipeline_cmd);

    if (err) {
        // Set err_msg
        *err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), err->message);
        g_error_free(err);
        goto LBL_1;
    }

    // Ok
    return pipeline;

LBL_1:
    // Got an error
    return NULL;
}
#endif

GString *pipeline_create_command_str(PipelineParms *parms) {
    // Return a gst-launch command for the given pipeline and parameters.
    // User can run this command in a terminal window to test various pipelines for recording.

    if (!parms) return NULL;

    // Create a gst-launch command
    GString *str = NULL;

    // Wash the device list. User may have disconnected microphones and webcams.
    // Invalid devices may crash the GStreamer pipeline.
    // Remove invalid devices.
    GList *new_list = audio_sources_wash_device_list(parms->dev_list);

    if (g_list_length(new_list) < 1) {
        new_list = g_list_append(new_list, NULL);
    }

    // Zero or one device?
    if (g_list_length(new_list) < 2) {

        // Create a simple gst-launch command. It can record from 1 device only.
        str = pipeline_create_command_str_simple(parms);

    } else {

        // Create a complex gst-launch command. This can record from 2 or more devices.
        str = pipeline_create_command_str_complex(parms);
    }

    // Free new_list
    str_list_free(new_list);
    new_list = NULL;

    // Caller must free this with g_string_free.
    return str;
}

static GString *pipeline_create_command_str_simple(PipelineParms *parms) {
    // Return a gst-launch command that can record from 1 device only.

    // Typical command string:
    // "gst-launch-1.0 pulsesrc device=alsa_output.pci-0000_04_02.0.analog-stereo.monitor
    //        ! queue
    //        ! level name=level
    //        ! audioconvert ! vorbisenc ! oggmux ! filesink location=test.ogg"

    guint major, minor, micro, nano;
    gst_version(&major, &minor, &micro, &nano);

    GString *str = g_string_new(NULL);
    g_string_append_printf(str, "gst-launch-%d.0 ", major);

    // Add -e (--eos-on-shutdow) option.
    // This sends an EOS before shutting the pipeline down. This keeps the recorded file readable.
    str = g_string_append(str, " -e ");

    // Source
    const gchar *source_name = (parms->source ? parms->source : "pulsesrc");

    str = g_string_append(str, source_name);
    str = g_string_append(str, " ");

    // Set device
    const gchar *device = g_list_nth_data(parms->dev_list, 0);
    if (device) {
        g_string_append_printf(str, "device=%s \\\n", device);
    } else {
        str = g_string_append(str, " \\\n");
    }

    str = g_string_append(str, "! queue \\\n");

    str = g_string_append(str, "! audioresample ! audioconvert \\\n");

    str = g_string_append(str, "! ");

    str = g_string_append(str, parms->profile_str);

    str = g_string_append(str, " \\\n");

    g_string_append_printf(str, "! filesink location=%s\n", parms->filename);

    // Caller must free this with g_string_free.
    return str;
}

static GString *pipeline_create_command_str_complex(PipelineParms *parms) {
    // Return a gst-launch command that can record from two (2) or more audio input devices.
    // This uses audiomixer or GstAdder elements to mix input from several sources.

    // Typical command string:
    //  "gst-launch-1.0 -e audiomixer name=mixer mix.
    //      ! audioconvert
    //      ! vorbisenc ! oggmux
    //      ! filesink location=test.ogg
    //      pulsesrc device=alsa_output.pci-0000_00_1b.0.analog-stereo.monitor ! queue ! mix.
    //      pulsesrc device=alsa_input.usb-Creative_Technology_Ltd._VF110_Live_Mic ! queue ! mix."

    guint major, minor, micro, nano;
    gst_version(&major, &minor, &micro, &nano);

    GString *str = g_string_new(NULL);
    g_string_append_printf(str, "gst-launch-%d.0", major);

    // Add -e (--eos-on-shutdow) option.
    // This sends an EOS before shutting the pipeline down. This keeps the recorded file readable.
    str = g_string_append(str, " -e ");

    // Use "audiomixer" or "adder". Audiomixer is an improved version of adder.
    GstElement *e = create_element("audiomixer", "mixer");
    if (GST_IS_ELEMENT(e)) {
        str = g_string_append(str, " audiomixer name=mixer \\\n");
        gst_object_unref(GST_OBJECT(e));
    } else {
        str = g_string_append(str, " adder name=mixer \\\n");
    }

    str = g_string_append(str, "! level \\\n");

    str = g_string_append(str, "! audioresample ! audioconvert \\\n");

    str = g_string_append(str, "! ");

    str = g_string_append(str, parms->profile_str);

    str = g_string_append(str, " \\\n");

    g_string_append_printf(str, "! filesink location=%s \\\n", parms->filename);

    // Now create audio source for all devices
    GList *item = g_list_first(parms->dev_list);
    while (item) {
        // Device name
        const gchar *device = (gchar*)item->data;

        // Source
        const gchar *source_name = (parms->source ? parms->source : "pulsesrc");

        g_string_append_printf(str, " %s device=%s ! queue ! mixer. %s\n", source_name, device, (item->next ? "\\" : ""));

        // Next source
        item = g_list_next(item);
    }

    // Caller must free this with g_string_free.
    return str;
}


