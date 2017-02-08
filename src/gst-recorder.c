/*
 * Copyright (c) Linux community.
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
#include "gst-recorder.h"
#include "rec-manager.h"
#include "media-profiles.h"
#include "log.h"
#include "support.h"
#include "dconf.h"
#include "utility.h"
#include "audio-sources.h"

#include "timer.h"

#include "gst-pipeline.h"

#include <gst/pbutils/missing-plugins.h>

#define AUDIO_RECORDER 101
#define AUDIO_RECORDER_ERR1 -1

// GStreamer pipeline
static GstElement *g_pipeline = NULL;

// Flag to test if EOS (end of stream) message was seen
static gboolean g_got_EOS_message = FALSE;

static gboolean rec_level_message_cb(GstBus *bus, GstMessage *message, void *user_data);

GstElement *rec_create_pipeline(PipelineParms *parms, GError **error);

void rec_set_state_to_null();

static gchar *rec_create_filename(gchar *track, gchar *artist, gchar *album);
static gchar *rec_generate_unique_filename();
static gchar *check_audio_folder(gchar *audio_folder);
static gchar *rec_get_profile_id();

void rec_module_init() {
    LOG_DEBUG("Init gst-recorder.c.\n");

    g_pipeline = NULL;
}

void rec_module_exit() {
    LOG_DEBUG("Clean up gst-recorder.c.\n");

    // Stop evt. recording
    rec_stop_recording(FALSE);
}

void rec_set_state_to_null() {
    // Copyright notice:
    // I have copied this function from gnome-media-2.30.0/grecord.

    LOG_DEBUG("\n--------- rec_set_state_to_null() ----------\n");

    // Set state of pipeline to GST_STATE_NULL.
    GstMessage *msg;
    GstState cur_state, pending;
    GstBus *bus;

    if (!GST_IS_PIPELINE(g_pipeline)) return;

    gst_element_get_state(g_pipeline, &cur_state, &pending, 0);

    if (cur_state == GST_STATE_NULL && pending == GST_STATE_VOID_PENDING)
        return;

    if (cur_state == GST_STATE_NULL && pending != GST_STATE_VOID_PENDING) {
        gst_element_set_state (g_pipeline, GST_STATE_NULL);
        return;
    }

    gst_element_set_state(g_pipeline, GST_STATE_READY);
    gst_element_get_state(g_pipeline, NULL, NULL, -1);

    bus = gst_element_get_bus(g_pipeline);
    if (GST_IS_BUS(bus)) {
        while ((msg = gst_bus_pop(bus))) {
            gst_bus_async_signal_func(bus, msg, NULL);

            if (msg) {
                gst_message_unref(msg);
            }
        }
        gst_object_unref(bus);
    }

    gst_element_set_state(g_pipeline, GST_STATE_NULL);
}

void rec_pause_recording()  {
    // Valid pipeline?
    if (!GST_IS_PIPELINE(g_pipeline)) return;

    // Get recording state
    gint state = -1;
    gint pending = -1;
    rec_get_state(&state, &pending);

    // Already paused?
    if (state == GST_STATE_PAUSED) return;

    LOG_DEBUG("\n--------- rec_pause_recording() ----------\n");

    // Reset timer
    timer_module_reset(GST_STATE_PAUSED);

    // Pause the stream
    gst_element_set_state(g_pipeline, GST_STATE_PAUSED);
}

void rec_continue_recording()  {
    // Valid pipeline?
    if (!GST_IS_PIPELINE(g_pipeline)) return;

    // Get recording state
    gint state = -1;
    gint pending = -1;
    rec_get_state(&state, &pending);

    // Already playing?
    if (state == GST_STATE_PLAYING) return;

    LOG_DEBUG("\n--------- rec_continue_recording() ----------\n");

    // Reset timer
    timer_module_reset(GST_STATE_PLAYING);

    // Continue recording
    gst_element_set_state(g_pipeline, GST_STATE_PLAYING);
}

void rec_update_gui() {
    // Recording state has changed (PLAYING, PAUSED, STOPPED/NULL).

    // Set widgets and GUI.
    rec_manager_update_gui();
}

gboolean rec_start_recording() {
    LOG_DEBUG("\n--------- rec_start_recording() ----------\n");

    // Get recording state
    gint state = -1;
    gint pending = -1;
    rec_get_state(&state, &pending);

    // Already recording?
    if (state == GST_STATE_PLAYING) {
        // TODO: FIXME.
        // Some players do not send STOP message when they change a track.
        // Audio-recorder simply continues to record to the same file.
        //
        LOG_DEBUG("gst-recorder.c, rec_start_recording()  FIX THIS.\n");
        return TRUE;

        // Is it paused?
    } else if (state == GST_STATE_PAUSED) {
        // Continue recording
        rec_continue_recording();
        return TRUE;
    }

    // Get data from Gsettings

    gchar *track_name = NULL;
    gchar *artist_name = NULL;
    gchar *album_name = NULL;

    gchar *profile_id = NULL;

    // Track-name sent from media players, Skype
    conf_get_string_value("track/track-name", &track_name);
    str_trim(track_name);
    purify_filename(track_name, TRUE/*purify_all*/);

    // Artist sent from media players
    conf_get_string_value("track/artist-name", &artist_name);
    str_trim(artist_name);
    purify_filename(artist_name, TRUE/*purify_all*/);

    // Album sent from media players
    conf_get_string_value("track/album-name", &album_name);
    str_trim(album_name);
    purify_filename(album_name, TRUE/*purify_all*/);

    // Get last (saved, recorded) filename with full path
    gchar *last_file_name = NULL;
    conf_get_string_value("track/last-file-name", &last_file_name);
    str_trim(last_file_name);

    LOG_DEBUG("Start recording to a new file. track-name=%s, artist=%s, album=%s\n",
              track_name ? track_name : "<generated automatically>", artist_name, album_name);

    // Stop current activity
    rec_stop_recording(FALSE);

    // Reset timer (prepare for GST_STATE_PLAYING state)
    timer_module_reset(GST_STATE_PLAYING);

    // Create a new GStreamer pipeline and start recording.

    // Clear static variables; call with NULLs
    rec_level_message_cb(NULL, NULL, NULL);

    // Variables
    gboolean ret = FALSE;

    // Parms to construct a pipeline
    PipelineParms *parms = g_malloc0(sizeof(PipelineParms));

    // Append to file?
    conf_get_boolean_value("append-to-file", &parms->append);

    if (parms->append && g_file_test(last_file_name, G_FILE_TEST_IS_REGULAR)) {
        // Record to an existing file
        parms->filename = g_strdup(last_file_name);
    }
    // Did we get a track-name from a Media Player, Skype?
    else if (str_length(track_name, NAME_MAX) > 0) {
        // We have track, album and artist names. Create filename from these.
        parms->filename = rec_create_filename(track_name, artist_name, album_name);

    } else  {
        // Simply generate a new, unique filename from date+time pattern
        parms->filename = rec_generate_unique_filename();
    }

    // Purify the final filename, remove illegal characters. Edit in place.
    purify_filename(parms->filename, FALSE);

    // Can we write to path/filename?
    if (!is_file_writable(parms->filename)) {
        gchar *msg = g_strdup_printf(_("Cannot write to file \"%s\".\n"), parms->filename);

        // Show error text
        LOG_DEBUG("Cannot write to file \"%s\".\n", parms->filename);

        rec_manager_set_error_text(msg);
        g_free(msg);

        goto LBL_1;
    }

    // Save the last file name (so we can continue from it later on (if append==TRUE))
    conf_save_string_value("track/last-file-name", parms->filename);

    // Get the saved media profile id (aac, mp3, cdlossless, cdlossy, etc).
    profile_id = rec_get_profile_id();

    // Get partial pipline for this profile_id
    parms->profile_str = profiles_get_pipeline(profile_id);
    parms->file_ext = profiles_get_extension(profile_id);


#if 0
    // Debugging:
    gchar *device_id = NULL;
    conf_get_string_value("audio-device-id", &device_id);

    gchar *device_name = NULL;
    conf_get_string_value("audio-device-name", &device_name);

    gint device_type = -1;
    conf_get_int_value("audio-device-type", &device_type);

    LOG_DEBUG("audio-device-id=%s\n", audio_device_id);
    LOG_DEBUG("audio-device-name=%s\n", audio_device_name);
    LOG_DEBUG("audio-device-type=%d\n", audio_device_type);

    g_free(device_name);
    g_free(device_id);
#endif

    // Show filename in the GUI
    rec_manager_set_filename_label(parms->filename);

    // Get audio source and device list
    gchar *audio_source = NULL;
    parms->dev_list = audio_sources_get_device_NEW(&audio_source);
    parms->source = audio_source;


    // Test if the pipeline will be valid.
    // Test if the appropriate GStreamer plugin has been installed.
    gchar *err_msg = NULL;
    gboolean test_OK = profiles_test_plugin(profile_id, &err_msg);

    if (!test_OK) {
        // Missing Gstreamer plugin!

        // Display error message in the GUI (set red label)
        rec_manager_set_error_text(err_msg);

        LOG_ERROR(err_msg);

        g_free(err_msg);

        // Stop and reset everything
        rec_stop_and_reset();

        goto LBL_1;
    }


    // Now build a GStreamer pipeline with parms
    GError *error = NULL;
    g_pipeline = rec_create_pipeline(parms, &error);

    ret = TRUE;

    // Ok?
    if (error) {
        // Got errors.

        // Display error message in the GUI (set red label)
        rec_manager_set_error_text(error->message);

        // Stop and reset everything
        rec_stop_and_reset();

        g_error_free(error);

        ret = FALSE;
    } else {
        // Alles Ok. Clear error label in the GUI.
        rec_manager_set_error_text(NULL);
    }

    LOG_DEBUG("------------------------\n");

LBL_1:

    g_free(profile_id);

    g_free(track_name);
    g_free(artist_name);
    g_free(album_name);

    g_free(last_file_name);

    pipeline_free_parms(parms);
    parms = NULL;

    return ret;
}

gint64 rec_get_stream_time() {
    // Return current recording time in seconds

    GstFormat format = GST_FORMAT_TIME;
    gint64 val = -1;
    gint secs = 0L;

    // Valid pipeline?
    if (!GST_IS_ELEMENT(g_pipeline)) return 0L;

    // Stream/recording time
    // Ref: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstElement.html#gst-element-query-position
    if (gst_element_query_position(g_pipeline, format, &val) && val != -1) {
        secs = val / GST_SECOND;
    }
    return secs;
}

void rec_stop_recording(gboolean delete_file) {
    // Stop recording, terminate pipeline

    if (!GST_IS_PIPELINE(g_pipeline)) return;

    // Get recording state
    gint state = -1;
    gint pending = -1;
    rec_get_state(&state, &pending);

    // Already stopped?
    if (state != GST_STATE_NULL) {
        // Reset timer
        timer_module_reset(GST_STATE_NULL);
    }

    LOG_DEBUG("rec_stop_recording(%s)\n", (delete_file ? "delete_file=TRUE" : "delete_file=FALSE"));

    // Send EOS message. This will terminate the stream/file properly. This is very important for ACC (.m4a) files.
    gst_element_send_event(g_pipeline, gst_event_new_eos());
    gst_element_send_event(g_pipeline, gst_event_new_eos());

    g_usleep(GST_USECOND * 5);

    // Set pipeline state to NULL
    rec_set_state_to_null();

    // Shutdown the entire pipeline
    gst_element_set_state(g_pipeline, GST_STATE_NULL);

    // Destroy it
    if (GST_IS_OBJECT(g_pipeline))
        gst_object_unref(GST_OBJECT(g_pipeline));

    g_pipeline = NULL;

    LOG_DEBUG("--------- Pipeline closed and destroyed ----------\n\n");

    // Delete the recorded file?
    if (delete_file) {
        // Get last saved file name
        gchar *filename = NULL;
        conf_get_string_value("track/last-file-name", &filename);

        LOG_DEBUG("Deleted file:\"%s\"\n", filename);

        // Remove it
        g_remove(filename);
        g_free(filename);

        // Erase last saved file name
        conf_save_string_value("track/last-file-name", "");

        // Update filename in the GUI
        rec_manager_set_filename_label("");
    }

    // Notice:
    // The rec_state_changed_cb() function will reset the GUI by calling rec_manager_update_gui();
}

void rec_stop_and_reset() {
    // Stop recording
    rec_stop_recording(FALSE);

    // Reset window and its widgets
    rec_manager_update_gui();
}

void rec_get_state(gint *state, gint *pending) {
    /*
    state:
    GST_STATE_VOID_PENDING
    GST_STATE_NULL
    GST_STATE_READY
    GST_STATE_PAUSED
    GST_STATE_PLAYING
    */
    *state = GST_STATE_NULL;
    *pending = GST_STATE_NULL;

    // The pipeline has been created?
    if (!GST_IS_ELEMENT(g_pipeline)) return;

    // The state is?
    // Ref: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstElement.html#gst-element-get-state
    gst_element_get_state(g_pipeline, (GstState*)state, (GstState*)pending, 0);
}

gboolean rec_is_recording() {
    // Get recording state
    gint state = -1;
    gint pending = -1;
    rec_get_state(&state, &pending);

    gboolean ret = FALSE;

    // Is playing?
    switch (state) {
    case GST_STATE_PLAYING:
        ret = TRUE;
        break;
    }
    return ret;
}

static gboolean rec_level_message_cb(GstBus *bus, GstMessage *message, void *user_data) {
    if (!GST_IS_MESSAGE(message)) return TRUE;

    static guint64 last_stream_time_t = 0L;
    static guint64 last_stream_time_fz = 0L;

    // Calling with NULL arguments?
    // This will reset the static variables, then exit.
    if (!message) {
        last_stream_time_t = 0L;
        last_stream_time_fz = 0L;
        return TRUE;
    }

    guint64 stream_time = 0L;

    if (message->type == GST_MESSAGE_ELEMENT) {
        const GstStructure *s = gst_message_get_structure (message);
        const gchar *name = gst_structure_get_name(s);

#if 0 
         // Print all field names:
         gint n = gst_structure_n_fields(s);
         gint i = 0;
         for (i=0; i < n; i++) {
             const gchar *fn = gst_structure_nth_field_name(s, i);
             GType t = gst_structure_get_field_type(s, "rms");
             g_print("%s field:%s (%s)\n", name, fn, g_type_name(t));
         }

#endif

        if (g_str_equal(name, "level")) {
            // Ref: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-level.html

            //Field names:
            //level field:endtime (GValueArray)
            //level field:timestamp (GValueArray)
            //level field:stream-time (GValueArray)
            //level field:running-time (GValueArray)
            //level field:duration (GValueArray)
            //level field:rms (GValueArray)
            //level field:peak (GValueArray)
            //level field:decay (GValueArray)

            GstClockTime endtime;
            if (!gst_structure_get_clock_time(s, "endtime", &endtime)) {
                LOG_ERROR("rec_level_message_cb: Cannot read endtime.\n");
            } else {
                stream_time = GST_TIME_AS_SECONDS(endtime);
            }

            gdouble rms_dB = 0;
            gdouble rms_norm = 0;

            gdouble peak_dB = 0;
            gdouble peak_norm = 0;

            const GValue *value = gst_structure_get_value(s, "rms");
            GValueArray *rms_arr = (GValueArray*)g_value_get_boxed(value);

            guint channels = rms_arr->n_values;
            if (channels < 1) goto LBL_1;

            value = gst_structure_get_value(s, "peak");
            GValueArray *peak_arr = (GValueArray*)g_value_get_boxed(value);

            guint i = 0;
            for (i = 0; i < channels; ++i) {
                // Read RMS (decibel) value
                value = rms_arr->values + i;
                rms_dB = rms_dB + g_value_get_double(value);

                // Read pulse peak (decibel) value
                value = peak_arr->values + i;
                peak_dB = peak_dB + g_value_get_double(value);
            }

            // Take average for all channels
            rms_dB = rms_dB / channels; 
            peak_dB = peak_dB / channels; 

            // Normalize, convert dB to [0 - 1.0]
            rms_norm = rms_norm + exp(rms_dB / 20);
            if (rms_norm > 1.0) rms_norm = 1.0;

            peak_norm = peak_norm + exp(peak_dB / 20);
            if (peak_norm > 1.0) peak_norm = 1.0;

            #if 0 
                    // Test & debug:
                    gint channels;
                    gdouble rms_dB, peak_dB, decay_dB;
                    gdouble rms;
                    const GValue *array_val;
                    const GValue *value;
                    GValueArray *rms_arr, *peak_arr, *decay_arr;
                    gint i;

                    array_val = gst_structure_get_value (s, "rms");
                    rms_arr = (GValueArray *) g_value_get_boxed (array_val);

                    array_val = gst_structure_get_value (s, "peak");
                    peak_arr = (GValueArray *) g_value_get_boxed (array_val);

                    array_val = gst_structure_get_value (s, "decay");
                    decay_arr = (GValueArray *) g_value_get_boxed (array_val);

                    channels = rms_arr->n_values;

                    g_print ("endtime: %" GST_TIME_FORMAT ", channels: %d\n", GST_TIME_ARGS (endtime), channels);

                    for (i = 0; i < channels; ++i) {
                        g_print ("channel %d\n", i);

                        value = rms_arr->values + i;
                        rms_dB = g_value_get_double (value);

                        value = peak_arr->values + i;
                        peak_dB = g_value_get_double (value);

                        value = decay_arr->values + i;
                        decay_dB = g_value_get_double (value);

                        rms = pow(10, rms_dB / 20);
                        //gdouble norm_rms = exp(rms_dB / 20);

                        gdouble norm_P = pow(10, peak_dB / 20);	

                        g_print ("channel %d,    RMS: %f dB, peak: %f dB, decay: %f dB, norm RMS %3.3f, norm Peak %3.3f\n", i, rms_dB, peak_dB, decay_dB, rms, norm_P);
                        g_print ("normalized rms value: %f\n", rms);
                    }

            #endif

            // Update level bar
            rec_manager_update_level_bar(rms_norm, peak_norm); 

            // Update time label in the GUI.
            if (stream_time - last_stream_time_t >= 1/*seconds*/) {
                guint hours = (guint)(stream_time / 3600);
                stream_time = stream_time - (hours*3600);

                // Count only to 23 hours
                // if (hours > 99) hours = 23;

                guint minutes = (guint)(stream_time / 60);
                guint seconds = stream_time - (minutes*60);

                // Show stream time
                gchar *time_txt = g_strdup_printf("%02d:%02d:%02d", hours, minutes, seconds);
                rec_manager_set_time_label(time_txt);
                g_free(time_txt);

                // Save last stream_time
                last_stream_time_t = stream_time;
            }


            // Update file size in the GUI.
            if (stream_time < 10 || (stream_time - last_stream_time_fz > 3/*seconds*/)) {
                // Update more frequently if stream_time < 10 seconds, after that, update each 3.rd second.

                gchar *filename = rec_manager_get_output_filename();
                gchar *size_txt = NULL;

                // Get file size.
                // Ref: https://developer.gnome.org/glib/2.34/glib-File-Utilities.html#g-stat
                GStatBuf fstat;
                if (g_stat(filename, &fstat))
                    LOG_ERROR("Cannot get file information of %s.\n", filename);
                else
                    size_txt = format_file_size(fstat.st_size);

                // Show file size
                rec_manager_set_size_label(size_txt);

                g_free(filename);
                g_free(size_txt);

                // Save last stream_time
                last_stream_time_fz = stream_time;
            }
        }
    }

LBL_1:
    // TRUE: Continue calling this function
    return TRUE;
}

static void rec_state_changed_cb(GstBus *bus, GstMessage *msg, void *userdata) {
    if (!GST_IS_MESSAGE(msg)) return;

    // We are only interested in the top-level pipeline.
    if (GST_MESSAGE_SRC(msg) != GST_OBJECT(g_pipeline)) return;

    GstState old_state, new_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, NULL);

    GstState state, pending;
    gst_element_get_state(g_pipeline, &state, &pending, 0);
    LOG_DEBUG("Pipeline state changed from %s to: %s  (stat=%s pending=%s).\n",
              gst_element_state_get_name(old_state), gst_element_state_get_name(new_state),
              gst_element_state_get_name(state), gst_element_state_get_name(pending));

    // Pipeline state changed from PAUSED to: READY  (stat=READY pending=VOID_PENDING).

    switch (new_state) {

    case GST_STATE_PLAYING:
        // We are playing. Inform the GUI.
        rec_manager_update_gui();
        break;

    case GST_STATE_READY:
        // Recording may have stopped. Inform the GUI.
        rec_manager_update_gui();
        break;

    case GST_STATE_PAUSED:
        // Goes from GST_STATE_PLAYING to GST_STATE_PAUSED state?
        if (old_state == GST_STATE_PLAYING) {

            // Recording has paused. Inform the GUI.
            rec_manager_update_gui();

        }
        break;

    case GST_STATE_NULL:
        // Recording has stopped. Inform the GUI.
        rec_manager_update_gui();
        break;

    default:
        break;
    }
}

static void rec_pipeline_error_cb(GstBus *bus, GstMessage *msg, void *userdata) {
    if (!GST_IS_MESSAGE(msg)) return;

    GError *error = NULL;
    gchar *dbg = NULL;

    gst_message_parse_error(msg, &error, &dbg);
    g_return_if_fail(error != NULL);

    LOG_DEBUG("\nGot pipeline error: %s.\n", error->message);

    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ELEMENT) {
        ;
    }

    if (dbg) {
        g_free(dbg);
    }

    if (error->code == GST_RESOURCE_ERROR_BUSY) {
        g_error_free(error);
        return;
    }

    if (error) {
        g_error_free(error);
    }
}

static void rec_eos_msg_cb(GstBus *bus, GstMessage *msg, void *userdata) {
    LOG_DEBUG("Got EOS message. Finishing recording.\n");

    // We've seen EOS. Set g_got_EOS_message to TRUE.
    g_got_EOS_message = TRUE;
}

GstElement *rec_create_pipeline(PipelineParms *parms, GError **error) {
    // Create a GStreamer pipeline for audio recording.
    gchar *err_msg = NULL;

    // Print debug info
    LOG_DEBUG("----------------------------\n");
    LOG_DEBUG("rec_create_pipeline, The parameters are:\n");
    LOG_DEBUG("audio source=%s\n", parms->source);
    LOG_DEBUG("device list is:\n");

    GList *n = g_list_first(parms->dev_list);
    while (n) {
        gchar *device = (gchar*)n->data;
        (void)device; // Avoid unused var message

        LOG_DEBUG("\t%s\n", device);
        n = g_list_next(n);
    }

    LOG_DEBUG("profile from GSettings=%s\n",parms->profile_str);
    LOG_DEBUG("filename=%s\n", parms->filename);
    LOG_DEBUG("append to file=%s\n", (parms->append ? "TRUE" : "FALSE"));

    // Create pipeline from the parms
    GstElement *pipeline = pipeline_create(parms, &err_msg);

    // Errors?
    if (!GST_IS_PIPELINE(pipeline) || err_msg) {
        goto LBL_1;
    }

    // Get "filesink" and set location (file name) and append mode
    GstElement *filesink = gst_bin_get_by_name(GST_BIN(pipeline), "filesink");

    if (!GST_IS_ELEMENT(filesink)) {
        err_msg = g_strdup_printf(_("Cannot find audio element %s.\n"), "filesink");
        goto LBL_1;
    }

    g_object_set(G_OBJECT(filesink), "location", parms->filename, NULL);
    g_object_set(G_OBJECT(filesink), "append", parms->append, NULL);
    g_object_unref(filesink);

    // Add a message handler
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

    // gst_bus_add_watch (bus, bus_info_cb, NULL);
    gst_bus_add_signal_watch(bus);

    // Detect state changes
    g_signal_connect(bus, "message::state-changed", G_CALLBACK(rec_state_changed_cb), NULL);

    // Monitor sound level/amplitude
    g_signal_connect(bus, "message::element", G_CALLBACK(rec_level_message_cb), NULL);

    // Catch error messages
    g_signal_connect(bus, "message::error", G_CALLBACK(rec_pipeline_error_cb), NULL);

    // EOS
    g_signal_connect(bus, "message::eos", G_CALLBACK(rec_eos_msg_cb), NULL);

    gst_object_unref(bus);

    // Assume all is OK
    gboolean ret = TRUE;

    // Set the pipeline to "playing" state
    gst_element_set_state(pipeline, GST_STATE_PAUSED);

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        err_msg = g_strdup(_("Cannot start reading from the stream/pipeline.\n"));
        ret = FALSE;
    } else {
        LOG_DEBUG("Pipeline is OK. Starting recording to %s.\n", parms->filename);
    }

    // Are we finally ok?
    if (ret) {
        return pipeline;
    }

    // Error occured
LBL_1:
    // Set the error object
    if (!err_msg)
        err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), "");

    LOG_ERROR(err_msg);

    // Destroy pipeline
    if (G_IS_OBJECT(pipeline)) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(pipeline));
    }

    // Set error
    g_set_error(error,
                AUDIO_RECORDER,      /* Error domain */
                AUDIO_RECORDER_ERR1, /* Error code */
                err_msg, "");

    g_free(err_msg);

    // Return NULL
    return NULL;
}

gchar *rec_get_output_filename() {
    // Return current output filename

    // Valid pipeline?
    if (!GST_IS_ELEMENT(g_pipeline)) return NULL;

    // Get the "filesink" element (by name)
    GstElement *filesink = gst_bin_get_by_name(GST_BIN(g_pipeline), "filesink");

    // Got filesink?
    if (!GST_IS_ELEMENT(filesink)) return NULL;

    gchar *filename = NULL;
    g_object_get(G_OBJECT(filesink), "location", &filename, NULL);
    g_object_unref(filesink);

    // The caller should g_free() the value
    return filename;
}

// ------------------------------------------------------------
// Support functions
// ------------------------------------------------------------

static gchar *rec_get_profile_id() {
    // Return saved media profile id.
    gchar *id = NULL;
    conf_get_string_value("media-format", &id);

    // Is this id real?
    gboolean is_OK = profiles_check_id(id);

    if (!is_OK) {
        g_free(id);
        ProfileRec *rec = profiles_find_for_ext("ogg");
        if (rec) {
            id = g_strdup(rec->id);
        }
    }

    // The caller should g_free() this value
    return id;
}

static gchar *check_audio_folder(gchar *audio_folder) {
    // Create the folder, this may fail.
    g_mkdir(audio_folder, 0755);

    // Directory exists?
    if (!g_file_test(audio_folder, G_FILE_TEST_IS_DIR)) {
        // Replace the old audio_folder
        g_free(audio_folder);

        // Set it to $HOME
        audio_folder = get_home_dir();
    }

    return audio_folder;
}

static gchar *rec_generate_unique_filename() {
    // Generate a new unique filename from date-time pattern.

    // Get saved media profile id
    gchar *profile_id = rec_get_profile_id();

    // And take its file extension (.ogg, .mp3, .flac, etc)
    gchar *file_ext = profiles_get_extension(profile_id);

    // Audio folder
    gchar *audio_folder = get_audio_folder();

    // Check if it is writable
    audio_folder = check_audio_folder(audio_folder);

    // Pattern to generate a unique filename
    gchar *filename_pattern = get_filename_pattern();

    LOG_DEBUG("audio_folder=%s file_ext=%s pattern=%s\n", audio_folder, file_ext, filename_pattern);

    gchar *basename = substitute_time_and_date_pattern(filename_pattern);

    // Invalid pattern?
    if (str_length(basename, 4000) < 1) {
        g_free(basename);

        // File name not given, set it to "Some filename"
        basename= g_strdup(_("Some filename"));
    }

    // Filename + file extension
    gchar *file_name = g_strdup_printf("%s.%s", basename, (file_ext ? file_ext : "xxx"));

    // Final filename with path
    gchar *final_name = g_build_filename(audio_folder, file_name, NULL);

    // Free the values
    g_free(profile_id);
    g_free(file_ext);
    g_free(audio_folder);
    g_free(filename_pattern);
    g_free(basename);

    LOG_DEBUG("Generated filename is:%s.\n", final_name);

    // The caller should g_free() this value.
    return final_name;
}

static gchar *rec_create_filename(gchar *track, gchar *artist, gchar *album) {
    // Note: we are not using album name here. Maybe in the next version.

    gchar *file_name = NULL;

    // Do we have a track name?
    if (str_length(track, NAME_MAX) < 2) {
        // No.
        // Auto-generate a new file name from the date+time pattern
        return rec_generate_unique_filename();
    }
    // Length of track name is > 64 bytes?
    // For example youtube video ids are very long.
    else if (str_length(track, NAME_MAX) > 64) {
        // Auto-generate a new file name from the date+time pattern
        return rec_generate_unique_filename();
    }

    // Get Audio/ folder
    gchar *audio_folder = get_audio_folder();
    audio_folder = check_audio_folder(audio_folder);

    // Get the saved media profile id (aac, mp3, cdlossless, cdlossy, etc).
    gchar *profile_id = rec_get_profile_id();

    // Take the file extension from media-profile_id; .ogg, .mp3, etc.
    gchar *file_ext = profiles_get_extension(profile_id);

    // Make track + file extension
    gchar *fname = g_strdup_printf("%s.%s", track, file_ext);
    gchar *path = NULL;

    // Do we have artist name?
    if (str_length(artist, PATH_MAX) > 0) {
        // Build path: audio_folder / artist
        // Eg. "/home/moma/Audio/Salomon Burke/" or from Skype "/home/moma/Audio/Skype recordings/"
        path = g_build_filename(audio_folder, artist, NULL);

        // Create directory
        if (g_mkdir_with_parents(path, 0755) == -1) {
            // Ref: https://developer.gnome.org/glib/unstable/glib-File-Utilities.html#g-mkdir-with-parents
            LOG_ERROR("Cannot create path \"%s\"\n", path);
            g_free(path);
            path = NULL;
        }
    }

    if (path) {
        // audio_folder / artist + track.ext
        file_name = g_build_filename(path, fname, NULL);
    } else {
        // audio_folder + track.ext
        file_name = g_build_filename(audio_folder, fname, NULL);
    }

    // TODO: Should we delete existing audio files?
    // Delete existing file
    g_remove(file_name);

    // Filename was write-protected?
    if (g_file_test(file_name, G_FILE_TEST_IS_REGULAR)) {
        // Yes.
        // Generate a new, unique name by using the pattern

        g_free(file_name);

        // Pattern to generate a unique filename (date + time pattern)
        gchar *filename_pattern = get_filename_pattern();

        // Re-build the file_name
        gchar *t = g_strdup_printf("%s/%s-%s.%s", (path ? path : audio_folder), track, filename_pattern, file_ext);

        // Substitute date+time values
        file_name = substitute_time_and_date_pattern(t);

        g_free(t);
        g_free(filename_pattern);
    }

    g_free(fname);
    g_free(path);

    g_free(profile_id);
    g_free(file_ext);
    g_free(audio_folder);

    // The caller should g_free() this value
    return file_name;
}

