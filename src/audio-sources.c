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
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gst/gst.h>

#include "audio-sources.h"
#include "gst-devices.h"
#include "support.h"  // _(x)
#include "dconf.h"
#include "utility.h"
#include "log.h"
#include "dbus-player.h"

// A List of audio devices and media players
G_LOCK_DEFINE_STATIC(g_device_list);
static GList *g_device_list = NULL;
// Contains:
//  * Real audio hardware, such as audio cards, microphones, webcams.
//  * Media-players like RhythmBox, Totem, Amarok and Banshee.
//  * Add also Skype if it's installed. Media-players and Skype can START/PAUSE/STOP recording via DBus.

static GList *get_audio_devices();

static gchar *audio_sources_get_GNOME_default();

static GList *audio_sources_get_device_for_players();
static gchar *audio_sources_get_first_microphone(gboolean find_webcam);
static GList *audio_sources_get_device_for_comm_programs();
static GList *audio_sources_get_device_from_settings(gint type);

static gchar *FIXME_get_default_sink_dev();
static gchar *FIXME_find_sink_file();

void audio_sources_init() {
    LOG_DEBUG("Init audio-sources.c.\n");

    g_device_list = NULL;

    // Init Puleaudio module
    gstdev_module_init();

    // Init DBus/Media Player modules
    dbus_player_init();
}

void audio_sources_exit() {
    LOG_DEBUG("Clean up audio-sources.c.\n");

    // Clean up pulseaudio molule
    gstdev_module_exit();

    // Clean up media players/DBus
    dbus_player_exit();

    audio_sources_free_list(g_device_list);
    g_device_list = NULL;
}

// ---------------------------------------------------
// DeviceItem functions

DeviceItem *device_item_create(gchar *id, gchar *description) {
    DeviceItem *item = g_malloc0(sizeof(DeviceItem));
    if (id) item->id = g_strdup(id);
    if (description) item->description = g_strdup(description);
    item->icon_name = NULL;
    return item;
}

DeviceItem *device_item_copy(DeviceItem *item) {
    // Make a copy of DeviceItem
    DeviceItem *copy = (DeviceItem*)g_memdup(item, sizeof(DeviceItem));
    copy->id = g_strdup(item->id);
    copy->description = g_strdup(item->description);
    copy->icon_name = g_strdup(item->icon_name);
    return copy;
}

void device_item_free(DeviceItem *item) {
    if (!item) return;
    // Free values
    if (item->id) g_free(item->id);
    if (item->description) g_free(item->description);
    if (item->icon_name) g_free(item->icon_name);
    // Free the node
    g_free(item);
}

const gchar *device_item_get_type_name(guint type) {
    switch (type) {
    case NOT_DEFINED:
        return "NOT_DEFINED";

    case DEFAULT_DEVICE:
        return "DEFAULT_DEVICE";

    case AUDIO_SINK:
        return "AUDIO_SINK";

    case AUDIO_SINK_MONITOR:
        return "AUDIO_SINK_MONITOR";

    case AUDIO_INPUT:
        return "AUDIO_INPUT";

    case MEDIA_PLAYER:
        return "MEDIA_PLAYER";

    case COMM_PROGRAM:
        return "COMM_PROGRAM";

    case USER_DEFINED:
        return "USER_DEFINED";
    }
    return "UNKNOWN TYPE";
}

// ---------------------------------------------------

DeviceItem *audio_sources_find_in_list(GList *lst, gchar *device_id) {
    // Return DeviceItem for the given device_id.
    GList *l = g_list_first(lst);
    while (l) {
        DeviceItem *item = (DeviceItem*)l->data;
        // Compare ids
        // if (!g_strcmp0(item->id, device_id) || (device_id == NULL && item->id == NULL)) {
        if (!g_strcmp0(item->id, device_id)) {
            return item;
        }
        l = g_list_next(l);
    }
    return NULL;
}

DeviceItem *audio_sources_find_id(gchar *device_id) {
    // Return DeviceItem for the given device_id.

    // Lock
    G_LOCK(g_device_list);

    DeviceItem *found_item = audio_sources_find_in_list(g_device_list, device_id);

    // Unlock
    G_UNLOCK(g_device_list);

    return found_item;
}

// ---------------------------------------------------
// Functions related to audio sources

void audio_sources_free_list(GList *lst) {
    // Free the list and all its DeviceItems
    g_list_foreach(lst, (GFunc)device_item_free, NULL);
    g_list_free(lst);
    lst = NULL;
}

void audio_sources_print_list_ex() {
    // Print device list
    audio_sources_print_list(g_device_list, "Device list");
}

void audio_sources_print_list(GList *list, gchar *tag) {
    // Print device list
    LOG_MSG("\n%s:\n", tag);

    GList *l = g_list_first(list);
    gint i = 0;
    while (l) {
        DeviceItem *item = (DeviceItem*)l->data;

        const gchar *type_name = device_item_get_type_name(item->type);

        LOG_MSG("#%d:type=%s(%d) id=%s, descr=%s\n", i++, type_name, item->type, item->id, item->description);
        LOG_MSG("\ticon_name=%s\n",item->icon_name);
        l = g_list_next(l);
    }
    LOG_MSG("-------------------------------------------\n");
}

GList *audio_sources_get_for_type(gint type) {
    // Return a GList of DeviceItems that match the type. The type can be a OR'ed value.
    // Eg. GList *lst = audio_sources_get_for_type(AUDIO_INPUT | AUDIO_SINK_MONITOR | DEFAULT_DEVICE);
    //     ...
    //     audio_sources_free_list(lst);
    //     lst = NULL;

    // Set lock
    G_LOCK(g_device_list);

    GList *lst = NULL;

    GList *n = g_list_first(g_device_list);
    while (n) {
        DeviceItem *item = (DeviceItem*)n->data;

        // Test the type
        if (item->type & type) {
            DeviceItem *copy = device_item_copy(item);
            lst = g_list_append(lst, copy);
        }

        // Next item
        n = g_list_next(n);
    }

    // Free the lock
    G_UNLOCK(g_device_list);

    // Notice:
    // The returned list is a deep copy. You must free it with
    // audio_sources_free_list(lst);
    // lst = NULL;
    return lst;
}

void audio_sources_device_changed(gchar *device_id) {
    // Device selection in the main window has changed.

    // Disconnect/re-connect DBus signals (if the device_id is Media Player or Skype)
    dbus_player_player_changed(device_id/*=player->service_name*/);
}

GList *audio_sources_wash_device_list(GList *dev_list) {
    // Wash the given device list.
    // User may have unplugged webcams and microphones, etc.

    // This function gets a fresh device list from GStreamer and removes invalid/unplugged items from the list.
    // Invalid devices may crash the GStreamer pipeline.

    // Create a new list for valid devs
    GList *new_list = NULL;

    // Get fresh device list from pulseaudio (or gstreamer)
    GList *fresh_dev_list = get_audio_devices();

    GList *n = g_list_first(dev_list);
    while (n) {
        gchar *dev_id = (gchar*)n->data;
        // dev_id is in fresh_dev_list?
        if (audio_sources_find_in_list(fresh_dev_list, dev_id)) {
            // Yes
            new_list = g_list_append(new_list, g_strdup(dev_id));
        }
        n = g_list_next(n);
    }

    // Free fresh_dev_list
    audio_sources_free_list(fresh_dev_list);

    // Return new_list.
    // Caller should free this with free_str_list(new_list).
    return new_list;
}

GList *audio_sources_get_device_NEW(gchar **audio_source) {
    // Return audio_source and device list as set in the GUI and [Additional settings] dialog.
    *audio_source = NULL;

    gchar *dev_id = NULL;
    GList *dev_list = NULL;
    gchar *source = NULL;
    gint type = -1;

    // These are set in the main window (in device/player combo)
    conf_get_string_value("audio-device-id", &dev_id);
    conf_get_int_value("audio-device-type", &type);

    // Normally "pulsesrc"
    source = g_strdup(DEFAULT_AUDIO_SOURCE);

    // Type is Media Player?
    if (type == MEDIA_PLAYER) {

        dev_list = audio_sources_get_device_for_players();

        // Type is Skype or similar comm program?
    } else if (type == COMM_PROGRAM) {

        dev_list = audio_sources_get_device_for_comm_programs();

        // Type is "User defined audio source" ?
    }  else if (type == USER_DEFINED) {

        // Devices are assigned to media-players in the [Additional settings] dialog.
        // See also: dconf-editor, key: /apps/audio-recorder/players/
        dev_list = audio_sources_get_device_from_settings(type);

        // Type is "System's default device"?
    } else if (type == DEFAULT_DEVICE) {

        g_free(source);
        source = audio_sources_get_GNOME_default();

        // Let the audio-source settle the device
        dev_list = NULL;

    } else {
        // Type is AUDIO_SINK_MONITOR or AUDIO_INPUT device

        // Add dev_id to dev_list
        dev_list = g_list_append(dev_list, g_strdup(dev_id));
    }

    g_free(dev_id);

    // Return values
    *audio_source = source;

    // Debug print:
    // print_str_glist("Final device list", new_list);

    // Caller should g_free() the returned audio_source.
    // Caller should free dev_list with str_list_free(dev_list).
    return dev_list;
}

static gchar *audio_sources_get_GNOME_default() {
    // Try first "gconfaudiosrc"
    // Its real value is probably set in the DConf registry.
    // Start gconf-editor and browse to key /system/gstreamer/0.10/default (audiosrc).
    gchar *source = g_strdup("gconfaudiosrc");
    GstElement *elem = gst_element_factory_make(source, "test-audio-source");
    gboolean ret = (elem != NULL);
    g_object_unref(elem);

    if (!ret) {
        g_free(source);

        // Try "autoaudiosrc"
        source = g_strdup("autoaudiosrc");
        elem = gst_element_factory_make(source, "test-audio-source");
        ret = (elem != NULL);
        g_object_unref(elem);
    }

    if (!ret) {
        g_free(source);
        source = g_strdup(DEFAULT_AUDIO_SOURCE);
    }

    // Caller should g_free() this value
    return source;
}

#if 0
static gchar *audio_sources_get_first_audio_card() {
    // Return the first AUDIO_SINK_MONITOR (audiocard with loudspeakers that we can record from)
    gchar *device_id = NULL;
    GList *lst = audio_sources_get_for_type(AUDIO_SINK_MONITOR);

    GList *first = g_list_first(lst);
    if (first) {
        DeviceItem *item = (DeviceItem*)first->data;
        device_id = g_strdup(item->id);
    }
    // Free lst
    audio_sources_free_list(lst);

    // Caller should g_free() this value
    return device_id;
}
#endif

static gchar *audio_sources_get_last_audio_card() {
    // Return the last AUDIO_SINK_MONITOR (audiocard with loudspeakers that we can record from)
    gchar *device_id = NULL;
    GList *lst = audio_sources_get_for_type(AUDIO_SINK_MONITOR);

    GList *last = g_list_last(lst);
    if (last) {
        DeviceItem *item = (DeviceItem*)last->data;
        device_id = g_strdup(item->id);
    }
    // Free lst
    audio_sources_free_list(lst);

    // Caller should g_free() this value
    return device_id;
}


static gchar *audio_sources_get_default_monitor_dev() {
    // Return default .monitor device (audio card that we can record from).

    gchar *def_sink = get_default_sink_device();
    if (def_sink == NULL) {
        // Take last audio card. Try to record from it.
        return audio_sources_get_last_audio_card();
    }

    // Make source_dev from sink_dev by adding a ".monitor" suffix.
    gchar *def_source = g_strdup_printf("%s.monitor", def_sink);

    g_free(def_sink);

    // Is it valid id?
    DeviceItem *item = audio_sources_find_id(def_source);
    if (item) {
        return def_source;
    }

    g_free(def_source);
    return NULL;
}

static gchar *audio_sources_get_first_microphone(gboolean find_webcam) {
    // Return first AUDIO_INPUT device (input device; microphone or webcam with microphone, etc.)
    GList *lst = audio_sources_get_for_type(AUDIO_INPUT);
    if (!lst) return NULL;

    gchar *device_id = NULL;

    if (find_webcam) {
        GList *n = g_list_first(lst);
        while (n) {
            DeviceItem *item = (DeviceItem*)n->data;
            // Stupid test to find a "webcam".
            if (audio_sources_device_is_webcam(item->description)) {
                device_id = g_strdup(item->id);
                break;
            }
            n = g_list_next(n);
        }
    }

    if (!device_id) {
        // Take the first one in the list
        DeviceItem *item = (DeviceItem*)lst->data;
        device_id = g_strdup(item->id);
    }

    // Free lst
    audio_sources_free_list(lst);

    // Caller should g_free() this value
    return device_id;
}

static GList *audio_sources_get_device_for_players() {
    // Return device list for Media Players (RhythmBox, Totem, Banshee, Amarok, etc.)
    // See [Additional settings] dialog.
    GList *dev_list = audio_sources_get_device_from_settings(MEDIA_PLAYER);

    // The list is empty?
    // Then find default .monitor device (=audio card with loudspeakers that we can record from).
    if (!dev_list) {
        gchar *dev_id = audio_sources_get_default_monitor_dev();
        if (dev_id) {
            dev_list = g_list_append(dev_list, dev_id/*steal the string*/);
        }
    }

    return dev_list;
}

static GList *audio_sources_get_device_for_comm_programs() {
    // Return device list for communication programs like Skype

    GList *dev_list = audio_sources_get_device_from_settings(COMM_PROGRAM);

    // The list is empty?
    // There were no devices in GSettings for COMM_PROGRAM.
    // See [Additional settings] dialog.
    if (!dev_list) {
        // For COMM_PROGRAMs, such as Skype, we need to record from two devices; audio-card and microphone.

        // The list is empty?
        // Then find default .monitor device (=audio card with loudspeakers that we can record from).
        gchar *dev_id = audio_sources_get_default_monitor_dev();
        if (dev_id) {
            dev_list = g_list_append(dev_list, dev_id/*steal the string*/);
        }

        // + add first microphone/webcam
        dev_id = audio_sources_get_first_microphone(TRUE/*find webcam if possible*/);
        if (dev_id) {
            dev_list = g_list_append(dev_list, dev_id/*steal the string*/);
        }
    }

    return dev_list;
}

static GList *audio_sources_get_device_from_settings(gint type) {
    // Get the device list from GSettings.
    // This is set in the [Additional settings] dialog.
    // See dconf-editor, key: /apps/audio-recorder/players/

    GList *dev_lst = NULL;

    gchar *conf_key = g_strdup_printf("players/device-type-%d", type);
    conf_get_string_list(conf_key, &dev_lst);
    g_free(conf_key);

    return dev_lst;
}

gboolean audio_sources_device_is_webcam(gchar *dev_name) {
    // A very primitive test to determine if dev_name is a web-camera

    // FIXME, TODO: Find a better way to check if the device is a webcam.

    // Some typical words we find in names of webcams
    // See: http://www.ideasonboard.org/uvc/
    gchar *cam_names[] = {"cam ", "amera", "amcorder", "web", "motion", "islim", "eface", "pix ", "pixel", NULL};

    // To lower case
    gchar *name = g_utf8_strdown(dev_name, -1);

    gint i = 0;
    for (i=0; cam_names[i]; i++) {
        gchar *p = g_strrstr(name, cam_names[i]);
        if (p) return TRUE;
    }

    return FALSE;
}

void audio_sources_load_device_list() {
    // Reload device list

    // Lock
    G_LOCK(g_device_list);

    // Free the old list
    audio_sources_free_list(g_device_list);
    g_device_list = NULL;

    // 1) Get list of real audio devices (fill to g_device_list)
    g_device_list = get_audio_devices();

    // 2) Add Media Players, Skype etc. to g_device_list (these can control the recording via DBus)
    // See the dbus_xxxx.c modules
    // ----------------------------

    // Get the player list (it's a GHashTable)
    GHashTable *player_list = dbus_player_get_player_list();
    GHashTableIter hash_iter;
    gpointer key, value;

    // Add to the g_device_list
    g_hash_table_iter_init(&hash_iter, player_list);
    while (g_hash_table_iter_next(&hash_iter, &key, &value)) {
        MediaPlayerRec *p = (MediaPlayerRec*)value;

        gchar *t = p->app_name;
        if (!t)
            t = p->service_name;

        // New DeviceItem
        DeviceItem *item = device_item_create(p->service_name, p->app_name);

        item->icon_name = g_strdup(p->icon_name);

        // Set type (COMM_PROGRAM or MEDIA_PLAYER)
        if (p->type)
            item->type = p->type;
        else
            item->type = MEDIA_PLAYER;

        g_device_list = g_list_append(g_device_list, item);
    }

    // 3) Add "User defined audio source" (user defined group of devices, selected for recording)
    // ----------------------------
    gchar *name = g_strdup(_("User defined audio source"));
    DeviceItem *item = device_item_create("user-defined", name);
    item->type = USER_DEFINED;
    item->icon_name = g_strdup("audio-card.png");

    g_device_list = g_list_append(g_device_list, item);
    g_free(name);


#if defined(ACTIVE_DEBUGGING) || defined(DEBUG_ALL)
    // Print device list
    audio_sources_print_list_ex();
#endif

    // The g_device_list is now loaded and ready

    // Unlock
    G_UNLOCK(g_device_list);
}

// --------------------------------------------
// Combobox related functions
// --------------------------------------------

GtkWidget *audio_sources_create_combo() {
    // Create a GtkComboBox with N_DEVICE_COLUMNS

    // Create list store
    GtkListStore *store = gtk_list_store_new(N_DEVICE_COLUMNS, G_TYPE_INT, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING);

    GtkWidget *combo = gtk_combo_box_new();
    gtk_combo_box_set_model(GTK_COMBO_BOX(combo),GTK_TREE_MODEL(store));

    // Unref store
    g_object_unref(G_OBJECT(store));

    // Device type (see the description of DeviceType enum)
    GtkCellRenderer *cell = gtk_cell_renderer_text_new();
    g_object_set(cell, "visible", 0, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, FALSE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", COL_DEVICE_TYPE, NULL);

    // Device id column, invisible
    cell = gtk_cell_renderer_text_new();
    g_object_set(cell, "visible", 0, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, FALSE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", COL_DEVICE_ID, NULL);

    // Pixbuf (device icon)
    cell = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, FALSE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "pixbuf", COL_DEVICE_ICON, NULL);

    // Device description column, visible
    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, FALSE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", COL_DEVICE_DESCR, NULL);

    return combo;
}

void audio_source_fill_combo(GtkWidget *combo) {
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    GtkTreeIter  iter;

    if (!GTK_IS_LIST_STORE(GTK_LIST_STORE(model))) {
        return;
    }

    // Disable "changed" signal
    gulong signal_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(combo), "selection-changed-signal"));
    g_signal_handler_block(combo, signal_id);

    // Clear the old combo list
    gtk_list_store_clear(GTK_LIST_STORE(model));

    // Get a new list of:
    // 1) Audio devices, audio-card w/ loudspeakers, webcams, microphones, etc.
    // 2) + Add media players that we see on the DBus.
    audio_sources_load_device_list();

    // Set lock
    G_LOCK(g_device_list);

    // Then add items to the ComboBox
    GList *n = g_list_first(g_device_list);
    while (n) {
        DeviceItem *item = (DeviceItem*)n->data;

        // Exclude AUDIO_SINK devices
        if (item->type == AUDIO_SINK) {
            // Next item thanks
            n = g_list_next(n);
            continue;
        }

        // Add new row
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);

        // Load icon
        const gchar *p = item->icon_name;

        GdkPixbuf *pixbuf = NULL;
        if (item->type == MEDIA_PLAYER || item->type == COMM_PROGRAM) {

            pixbuf = load_icon_pixbuf((gchar*)p, 22);

            // Got icon??
            if (!GDK_IS_PIXBUF(pixbuf)) {
                // No. Display default icon.
                p = "mediaplayer.png";
            }
        }

        if (!p) {
            p = "loudspeaker.png";
        }

        if (!GDK_IS_PIXBUF(pixbuf)) {
            // No. Load a default icon.
            gchar *path = get_image_path(p);
            pixbuf = get_pixbuf_from_file(path, 22, 22);
            g_free(path);
        }

        // Set column data
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           COL_DEVICE_TYPE, item->type, /* type */
                           COL_DEVICE_ID, item->id, /* internal device id or media player id */
                           COL_DEVICE_ICON, pixbuf, /* icon pixbuf */
                           COL_DEVICE_DESCR, item->description /* visible text */, -1);

        // Pixbuf has a reference count of 2 now, as the list store has added its own
        if (GDK_IS_PIXBUF(pixbuf)) {
            g_object_unref(pixbuf);
        }

        // Next item
        n = g_list_next(n);
    }

    // Enable changed-signal
    g_signal_handler_unblock(combo, signal_id);

    // Free the lock
    G_UNLOCK(g_device_list);
}

void audio_sources_combo_set_id(GtkWidget *combo, gchar *device_id) {
    // Set combo selection by device id

    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    if (!GTK_IS_TREE_MODEL(model)) {
        return;
    }

    GtkTreeIter  iter;

    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    while (device_id && valid) {
        gchar *id = NULL;
        gtk_tree_model_get(model, &iter, COL_DEVICE_ID, &id, -1);

        // Compare ids
        if (!g_strcmp0(device_id, id)) {
            // Select it
            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(combo), &iter);
            return;
        }
        valid = gtk_tree_model_iter_next(model, &iter);
    }

    // str_value was not found. Select the first, 0'th row
    valid = gtk_tree_model_get_iter_first(model, &iter);
    if (valid) {
        gtk_combo_box_set_active_iter(GTK_COMBO_BOX(combo), &iter);
    }
}

gboolean audio_sources_combo_get_values(GtkWidget *combo, gchar **device_name,
                                        gchar **device_id, gint *device_type) {
    *device_name = NULL;
    *device_id = NULL;
    *device_type = -1;

    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    if (!GTK_IS_TREE_MODEL(model)) {
        return FALSE;
    }

    GtkTreeIter iter;
    if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter)) return FALSE;

    gtk_tree_model_get(model, &iter, COL_DEVICE_ID, device_id,
                       COL_DEVICE_DESCR, device_name,
                       COL_DEVICE_TYPE, device_type, -1);

    // The caller should g_free() both device_name and device_id.
    return TRUE;
}

// Choose one of these get_audio_devices() functions

static GList *get_audio_devices() {
    // Get list of audio input devices (ids and names).
    GList *pa_list = gstdev_get_source_list();

    GList *lst = NULL;

    GList *n = g_list_first(pa_list);
    while (n) {
        DeviceItem *item = (DeviceItem*)n->data;

        // Take a copy
        DeviceItem *copy = device_item_copy(item);

        // Add to our private list
        lst = g_list_append(lst, copy);

        n = g_list_next(n);
    }

    // Add "Default" device

    // Translators: This is system's default audio device.
    DeviceItem *item = device_item_create("default-device"/*id*/, _("System's default device")/*description*/);
    item->type = DEFAULT_DEVICE;
    lst = g_list_append(lst, item);

    // Return lst
    return lst;
}

gchar *get_default_sink_device() {
    // Find default sink-device.
    return FIXME_get_default_sink_dev();
}

static gchar *FIXME_find_sink_file() {
    // FIXME because this function pokes directly pulseaudio's config file.
    // We should not do this! Do not even mension you've seen this horrible piece of code ;-)
    //
    // The question is: How to find default (or running) sink-device by using GStreamer functions?
    // Please tell me if you know.
    //
    // $ pactl list | grep -A6 State
    //
    // State: RUNNING
    // Name: alsa_output.pci-0000_00_1b.0.analog-stereo
    // Description: Built-in Audio Analog Stereo

#define PULSEAUDIO_LOCAL_CONFIG ".config/pulse/"

    // $ cat  ~/.config/pulse/*-default-sink

    gchar *home = get_home_dir();
    gchar *path = g_build_path("/", home, PULSEAUDIO_LOCAL_CONFIG, NULL);
    gchar *ret = NULL;

    GError *error = NULL;
    GDir *dir = g_dir_open(path, 0, &error);
    if (error) {
        g_error_free(error);
        goto LBL_1;
    }

    const gchar *fname = g_dir_read_name(dir);
    while (fname) {

        if (g_str_has_suffix(fname, "-default-sink")) {
            ret = g_build_path("/", path, fname, NULL);
            goto LBL_1;
        }

        fname = g_dir_read_name(dir);
    }

LBL_1:
    g_free(home);
    g_free(path);

    if (dir) {
        g_dir_close(dir);
        dir = NULL;
    }

    return ret;
}

static gchar *FIXME_get_default_sink_dev() {
    // FIXME because this function pokes directly pulseaudio's config file.
    // We should not do this! Do not even mension you've seen this horrible piece of code ;-)
    //
    // Possible solutions:
    // 1) Create a pipeline with pulsesink element, make it rolling and read its device id (it may still be NULL !).
    //
    // 2) Better idea:
    // Create a pipeline with a LEVEL element for each audio-card (that are .monitor devices). And check if it produces sound (if we find pulse on the pipe).
    //

    gchar *fname = FIXME_find_sink_file();

    gchar *sink_dev = NULL;
    GError *error = NULL;
    gsize siz = 0L;
    g_file_get_contents(fname, &sink_dev, &siz, &error);

    g_free(fname);

    if (error) {
        g_error_free(error);
    }

    str_trim(sink_dev);
    return sink_dev;
}

#if 0

audiotestsrc wave=sine freq=512 ! audioconvert ! audioresample ! pulsesink

                            gst-launch audiotestsrc ! audioconvert ! pulsesink


#endif



#if 0
                            Ideas:
                            How to find DEFAULT sink-device by using GStreamer functions?

                            gchar *name = NULL;

GError *error = NULL;
GstElement *p = gst_parse_launch("fakesrc ! audioconvert ! audioresample ! pulsesink name=plssnk", &error);
gst_element_set_state(p, GST_STATE_PLAYING);

GstElement *sink = gst_bin_get_by_name(GST_BIN(p), "plssnk");
if (sink) {

    g_object_get(G_OBJECT(sink), "device", &dev_id, "name", &name, NULL);

    GValue value = {0, };
    g_value_init(&value, G_TYPE_STRING);
    g_object_get_property(G_OBJECT(sink), "device", &value);

    dev_id = g_value_dup_string(&value);

    g_value_unset(&value);

}

GstElement *e = gst_element_factory_make("pulsesink", "pulsesink");
if (GST_IS_ELEMENT(e)) {

    g_object_get(G_OBJECT(e), "device", &dev_id, NULL);


    dev_id = (gchar*)g_object_get(G_OBJECT(e), "device", NULL);

    GValue value = {0, };
    g_value_init(&value, G_TYPE_STRING);
    g_object_get_property(G_OBJECT(e), "device", &value);

    dev_id = g_value_dup_string(&value);

    g_value_unset(&value);

    gst_object_unref(GST_OBJECT(e));
}

g_print("SINK DEVICE=%s - %s\n", dev_id, name);

return dev_id;
#endif
