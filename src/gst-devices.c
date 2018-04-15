/*
 * Copyright (c) 2011-2017 Osmo Antero.
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
#include "gst-devices.h"
#include "support.h" // _(x)
#include "log.h"
#include "utility.h"
#include "rec-manager-struct.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>

// Collect audio input devices and audio output devices w/ connected loudspeakers.
// This module is using gst_device_monitor_* functions that were introduced in GStreamer 1.4.

// You may also check the output of these pa (pulseaudio) commands.

// Get list of input devices:
// $ pactl list short sources | cut -f2
// $ pactl list | grep -A6  'Source #' | egrep "Name: |Description: "
//
// Get list of sink (output) devices:
// $ pactl list short sinks | cut -f2
// $ pactl list | grep -A6  'Sink #' | egrep "Name: |Description: "


// Audio sink devices
static GList *g_sink_list = NULL;

// Audio source devices
G_LOCK_DEFINE_STATIC(g_source_list);
static GList *g_source_list = NULL;

static GstDeviceMonitor *g_dev_monitor = NULL;

static void gstdev_get_devices();
static void gstdev_read_fields(GstDevice *dev, gchar **dev_id, gchar **dev_descr, gchar **dev_class, gchar **dev_caps_str);
static void gstdev_add_to_list(GstDevice *dev);
static void gstdev_remove_from_list(GstDevice *dev);
static void gstdev_clear_lists();

void gstdev_module_init() {
    LOG_DEBUG("Init gst-devices.c.\n");
    g_source_list = NULL;
    g_sink_list = NULL;
    g_dev_monitor = NULL;
}

void gstdev_module_exit() {
    LOG_DEBUG("Clean up gst-devices.c.\n");

    if (GST_IS_DEVICE_MONITOR(g_dev_monitor)) {
        gst_device_monitor_stop(g_dev_monitor);
        gst_object_unref(g_dev_monitor);
    }

    g_dev_monitor = NULL;

    // Clear lists
    gstdev_clear_lists();
}

GList *gstdev_get_source_list() {
    // Return g_source_list to the caller

    gstdev_get_devices();

    return g_source_list;
}

static void gstdev_clear_lists() {
    LOG_DEBUG("gstdev_clear_lists(). Clear g_sink_list and g_source_list.\n");

    G_LOCK(g_source_list);

    // Free g_sink_list
    audio_sources_free_list(g_sink_list);
    g_sink_list = NULL;

    // Free g_source_list
    audio_sources_free_list(g_source_list);
    g_source_list = NULL;

    G_UNLOCK(g_source_list);
}

void gstdev_update_GUI() {
    // Device list changed. Update GUI.

    RecorderCommand *cmd = g_malloc0(sizeof(RecorderCommand));
    cmd->type = RECORDING_DEVICE_CHANGED;

    // Send command to rec-manager.c and GUI.
    // It will free the cmd structure after processing.
    rec_manager_send_command(cmd);
}

static gboolean message_func(GstBus *bus, GstMessage *message, gpointer user_data) {
    GstDevice *device = NULL;
    gchar *name = NULL;

    LOG_DEBUG("message_func(): function to add or remove device called.\n");

    switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_DEVICE_ADDED:
        gst_message_parse_device_added(message, &device);

        name = gst_device_get_display_name(device);

        LOG_DEBUG("Audio device added: %s\n", name);
        g_free (name);

        gstdev_add_to_list(device);

        gstdev_update_GUI();

        break;

    case GST_MESSAGE_DEVICE_REMOVED:
        gst_message_parse_device_removed(message, &device);

        name = gst_device_get_display_name(device);

        LOG_DEBUG("Audio device removed: %s\n", name);
        g_free (name);

        gstdev_remove_from_list(device);

        gstdev_update_GUI();

        break;

    default:
        break;
    }

    return G_SOURCE_CONTINUE;
}

GstDeviceMonitor *setup_raw_audio_source_device_monitor() {

    LOG_DEBUG("Setup monitor to detect new and unplugged devices.\n");

    GstDeviceMonitor *monitor = gst_device_monitor_new ();

    GstBus *bus = gst_device_monitor_get_bus(monitor);
    gst_bus_add_watch(bus, message_func, NULL);
    gst_object_unref(bus);

    GstCaps *caps = gst_caps_new_empty_simple ("audio/x-raw");
    gst_device_monitor_add_filter(monitor, NULL, caps); // "Audio/Source", "Audio/Sink"
    gst_caps_unref (caps);

    gst_device_monitor_start(monitor);

    return monitor;
}

static void gstdev_read_fields(GstDevice *dev, gchar **dev_id, gchar **dev_descr, gchar **dev_class, gchar **dev_caps_str) {

    gchar *disp_name = gst_device_get_display_name(dev);

    // Cut it nicely
    *dev_descr = g_strdup(disp_name);
    str_cut_nicely(*dev_descr, 39/*to len*/, 25/*minimum len*/);

    g_free(disp_name);

    *dev_class = gst_device_get_device_class(dev);

    GstCaps *caps = gst_device_get_caps(dev);
    *dev_caps_str = gst_caps_to_string(caps);
    gst_caps_unref(caps);

    // Read device id (this should work for pulsesrc and alsasrc)
    GstElement *e = gst_device_create_element(dev, "element");

    GValue value = {0, };
    g_value_init (&value, G_TYPE_STRING);
    g_object_get_property(G_OBJECT(e), "device", &value);

    *dev_id = g_value_dup_string(&value);

    g_value_unset(&value);
    gst_object_unref(GST_OBJECT(e));
}

GList *remove_item(GList *list, gchar *dev_id) {

    GList *item = g_list_first(list);
    while (item) {

        DeviceItem *rec = (DeviceItem*)item->data;
        if (rec && g_strcmp0(rec->id, dev_id) == 0) {

            list = g_list_delete_link(list, item);
            device_item_free(rec);

            return list;
        }

        item = g_list_next(item);
    }

    // Return unmodified list
    return list;
}

static void gstdev_remove_from_list(GstDevice *dev) {
    gchar *dev_id = NULL;
    gchar *dev_descr = NULL;
    gchar *dev_class = NULL;
    gchar *dev_caps_str = NULL;

    LOG_DEBUG("Remove (input or output) device from the list.\n");

    G_LOCK(g_source_list);

    gstdev_read_fields(dev, &dev_id, &dev_descr, &dev_class, &dev_caps_str);

    gchar *dev_class_l = g_ascii_strdown(dev_class, -1);

    // Audio/Source
    if (g_str_has_prefix(dev_class_l, "audio/source")) {

        LOG_DEBUG("Remove audio input device (from g_source_list):%s, decr:%s, class:%s\n", dev_id, dev_descr, dev_class);

        g_source_list = remove_item(g_source_list, dev_id);

    }
    // Audio/Sink
    else if (g_str_has_prefix(dev_class_l, "audio/sink")) {

        LOG_DEBUG("Remove audio output device (from g_sink_list):%s, decr:%s, class:%s\n", dev_id, dev_descr, dev_class);

        g_sink_list = remove_item(g_sink_list, dev_id);
    }

    g_free(dev_id);
    g_free(dev_descr);
    g_free(dev_class);
    g_free(dev_class_l);
    g_free(dev_caps_str);

    G_UNLOCK(g_source_list);
}

static void gstdev_add_to_list(GstDevice *dev) {
    gchar *dev_descr = NULL;
    gchar *dev_id = NULL;
    gchar *dev_class = NULL;
    gchar *dev_caps_str = NULL;

    LOG_DEBUG("Add new (input or output) device to the list.\n");

    G_LOCK(g_source_list);

    gstdev_read_fields(dev, &dev_id, &dev_descr, &dev_class, &dev_caps_str);

    // Create new DeviceItem
    DeviceItem *item = device_item_create(dev_id, dev_descr);

    gchar *dev_class_l = g_ascii_strdown(dev_class, -1);

    // Audio/Source
    if (g_str_has_prefix(dev_class_l, "audio/source")) {

        LOG_DEBUG("Add audio input device (to g_source_list):%s, decr:%s, class:%s\n", dev_id, dev_descr, dev_class);

        if (g_str_has_suffix(dev_id, ".monitor")) {

            // Monitor device for a real sound-card (we can record from this)
            item->type = AUDIO_SINK_MONITOR;

            // Set icon (audio card/loudspeaker)
            item->icon_name = g_strdup("loudspeaker.png");

        } else {
            // Device with audio input (microphone)
            item->type = AUDIO_INPUT;

            // Add "Microphone" to the device description
            gchar *descr = g_strdup_printf("%s %s", item->description, _("(Microphone)"));
            g_free(item->description);
            item->description = descr;

            // Find a suitable icon.
            // TODO: Make this test better.
            if (audio_sources_device_is_webcam(item->description)) {
                // Most likely a webcam
                item->icon_name = g_strdup("webcam.png");
            } else {
                // Ordinary microphone
                item->icon_name = g_strdup("microphone.png");
            }
        }

        // Add to g_sources list
        g_source_list = g_list_append(g_source_list, item);
    }


    // Audio/Sink
    else if (g_str_has_prefix(dev_class_l, "audio/sink")) {

        LOG_DEBUG("Add audio output device (to g_sink_list):%s, decr:%s, class:%s\n", dev_id, dev_descr, dev_class);

        // This is a sound sink, normally real audio card with loudspeakers
        item->type = AUDIO_SINK;

        // Add to g_sinks list
        g_sink_list = g_list_append(g_sink_list, item);

        // Set icon (audio card)
        item->icon_name = g_strdup("audio-card.png");
    }

    g_free(dev_id);
    g_free(dev_descr);
    g_free(dev_class);
    g_free(dev_class_l);
    g_free(dev_caps_str);

    G_UNLOCK(g_source_list);
}

void gstdev_fix_description() {
    // Remove "Monitor of" from the description text, and add "(Audio ouput)" word to it. It means "loudspeakers".

    // For example: "Monitor of Audio Stereo Card" becomes "Audio Stereo Card (Audio ouput)"
    // This is easier to understand.

    // Note: Check listing of these commands:
    //
    // Input devices:
    // pactl list | grep -A6  'Source #' | egrep "Name: |Description: "
    // pactl list short sources | cut -f2
    //
    // And sink (output) devices:
    // pactl list | grep -A6  'Sink #' | egrep "Name: |Description: "
    // pactl list short sinks | cut -f2

    G_LOCK(g_source_list);

    GList *item = g_list_first(g_source_list);
    while (item) {

        DeviceItem *rec = (DeviceItem*)item->data;

        // Take device-id without ".monitor" suffix
        if (g_str_has_suffix(rec->id, ".monitor")) {

            gchar *tmp = g_strdup(rec->id);
            gchar *p = g_strrstr(tmp, ".monitor");
            if (p) {
                // NULL terminate
                *p = '\0';

                // Find equivalent sink device (real audio card) and steal its description + add "(Audio output)" to it.
                DeviceItem *sink_rec = audio_sources_find_in_list(g_sink_list, tmp);
                if (sink_rec) {
                    g_free(rec->description);
                    rec->description = g_strdup_printf("%s %s", sink_rec->description, _("(Audio output)"));
                }
            }

            g_free(tmp);
        }

        item = g_list_next(item);
    }

    G_UNLOCK(g_source_list);
}

static void gstdev_get_devices() {

    LOG_DEBUG("Get list of audio input/output devices from GStreamer.\n");

    gstdev_clear_lists();

    // Set up device monitor
    if (!GST_IS_DEVICE_MONITOR(g_dev_monitor)) {
        g_dev_monitor = setup_raw_audio_source_device_monitor();
    }

    GList *list = gst_device_monitor_get_devices(g_dev_monitor);

    // Ref: http://code.metager.de/source/xref/freedesktop/gstreamer/gstreamer/gst/gstdevice.c

    GList *item = g_list_first(list);
    while (item) {
        GstDevice *dev = (GstDevice*)item->data;

        gstdev_add_to_list(dev);

        item = g_list_next(item);
    }

    g_list_free_full(list, (GDestroyNotify)gst_object_unref);

    // Remove "Monitor of" from the description text.
    gstdev_fix_description();
}



