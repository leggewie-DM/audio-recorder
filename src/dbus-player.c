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
#include <string.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <unistd.h>

#include "dconf.h"
#include "log.h"
#include "utility.h"
#include "support.h"
#include "dbus-player.h"

#include "audio-sources.h"

// MPRIS2 compliant players.
#include "dbus-mpris2.h"

// Handcrafted DBus-interface for Skype.
#include "dbus-skype.h"

// Send commands to rec-manager.c
#include "rec-manager-struct.h"

// List of players
static GHashTable *g_player_list = NULL;

static void dbus_player_disconnect_signals();
static void dbus_player_clear_list();

static void dbus_player_get_saved();
static void dbus_player_save(MediaPlayerRec *pl);
static void dbus_player_delete_saved(const gchar *service_name);

gboolean add_player_to_list(gchar *desktop_file, gchar *service_name);
void add_skype();

void dbus_player_init() {
    LOG_DEBUG("Init dbus-player.c.\n");

    g_player_list = NULL;

    skype_module_init();

    mpris2_module_init();
}

void dbus_player_exit() {
    LOG_DEBUG("Clean up dbus-player.c.\n");

    // Disconnect DBus signals for all Media Players
    dbus_player_disconnect_signals();

    // Clear the player list
    dbus_player_clear_list();

    mpris2_module_exit();

    skype_module_exit();
}

static RecorderCommand *convert_data(MediaPlayerRec *pl) {
    // Convert MediaPlayerRec to RecorderCommand

    if (!pl) return NULL;
    TrackInfo *tr = &pl->track;

    RecorderCommand *cmd = g_malloc0(sizeof(RecorderCommand));
    cmd->track = g_strndup(tr->track, MPRIS_STRLEN);
    cmd->artist = g_strndup(tr->artist, MPRIS_STRLEN);
    cmd->album = g_strndup(tr->album, MPRIS_STRLEN);
    cmd->track_len = tr->track_len;
    cmd->track_pos = tr->track_pos;
    cmd->flags = tr->flags;

    if (tr->status == PLAYER_STATUS_PAUSED) {
        cmd->type = RECORDING_PAUSE;

    } else if (tr->status == PLAYER_STATUS_PLAYING) {
        cmd->type = RECORDING_START;

    } else if (tr->status == PLAYER_STATUS_NOTIFY_MSG) {
        cmd->type = RECORDING_NOTIFY_MSG;

    } else {
        // tr.status == PLAYER_STATUS_CLOSED ||
        // tr.status == PLAYER_STATUS_STOPPED
        cmd->type = RECORDING_STOP;
    }

    return cmd;
}

void dbus_player_process_data(gpointer player) {
    // Send message to the rec-manager.c
    if (!player) return;

    // Convert MediaPlayerRec to RecorderCommand
    RecorderCommand *cmd = convert_data(player);

    // Send the command. Rec-manager will free the cmd structure after processing.
    rec_manager_send_command(cmd);
}

void dbus_player_player_changed(gchar *service_name) {
    // Re-connect DBus signals/methods for the given service_name (Media Player or Skype, etc)

    // Disconnect all signals/object methods
    dbus_player_disconnect_signals();

    // Get MediaPlayerRec for this service_name
    MediaPlayerRec *player = dbus_player_lookup_service_name(service_name);

    if (player && player->func_set_signals) {

        LOG_PLAYER("Connect DBus signals for %s (%s).\n", player->app_name, player->service_name);

        // Start application (Media Player, Skype, etc)
        if (player->func_start_app) {
            player->func_start_app(player);
        }

        // Connect signals so we receive track-changed/start/stop messages from this app (over DBus)
        player->func_set_signals(player, TRUE); // TRUE=connect/register, FALSE=disconnect/unregister

        // Save this player in GSettings so user doesn't need to refresh the combo manually.
        dbus_player_save(player);
    }
}

static void dbus_player_disconnect_signals() {
    // Disconnect all signal-functions from the DBus
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, g_player_list);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        MediaPlayerRec *player = (MediaPlayerRec*)value;
        if (player && player->func_set_signals) {
            // Disconnect signals
            player->func_set_signals(player, FALSE); // FALSE=disconnect/unregister
        }
    }
}

GHashTable *dbus_player_get_list_ref() {
    if (!g_player_list)
        g_player_list = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, dbus_player_delete_item);

    return g_player_list;
}

static gboolean dbus_palyer_remove_node(gpointer key, gpointer value, gpointer user_data) {
    // Return TRUE so this (key, value) pair gets removed and deleted.
    return TRUE;
}

static void dbus_player_clear_list() {
    // Delete the entire g_player_list
    if (g_player_list) {
        g_hash_table_foreach_remove(g_player_list, dbus_palyer_remove_node, NULL);
        g_hash_table_unref(g_player_list);
    }
    g_player_list = NULL;
}

void dbus_player_delete_item(gpointer data) {
    MediaPlayerRec *player = (MediaPlayerRec*)data;
    if (!player) return;

    LOG_PLAYER("dbus_player_delete_item: %s (%s).\n", player->app_name, player->service_name);

    if (player->func_set_signals) {
        // Disconnect signals
        player->func_set_signals(player, FALSE); // TRUE=connect, FALSE=disconnect
    }

    if (G_IS_DBUS_PROXY(player->proxy)) {
        g_object_unref(player->proxy);
    }
    player->proxy = NULL;

    if (G_IS_DBUS_PROXY(player->prop_proxy)) {
        g_object_unref(player->prop_proxy);
    }
    player->prop_proxy = NULL;

    g_free(player->service_name);
    g_free(player->desktop_file);
    g_free(player->exec_cmd);
    g_free(player->app_name);
    g_free(player->icon_name);
    g_free(player);
}

MediaPlayerRec *dbus_player_lookup_app_name(const gchar *app_name) {
    // Lookup player by its application name (p->app_name).
    // Typical app_names are "Amarok 2.3.2", "RhythmBox 2.3" and "Skype 4.1".
    GHashTableIter iter;
    gpointer key, value;

    if (str_length(app_name, 1024) < 1) return NULL;

    g_hash_table_iter_init(&iter, g_player_list);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        MediaPlayerRec *p = (MediaPlayerRec*)value;
        if (!g_strcmp0(p->app_name, app_name)) {
            return p;
        }
    }
    return NULL;
}

MediaPlayerRec *dbus_player_lookup_service_name(const gchar *service_name) {
    // Lookup player by its service name (p->service_name).
    // "org.mpris.MediaPlayer2.Player.banshee" is a typical service_name.
    GHashTableIter iter;
    gpointer key, value;

    if (str_length(service_name, 1024) < 1) return NULL;

    g_hash_table_iter_init(&iter, g_player_list);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        MediaPlayerRec *p = (MediaPlayerRec*)value;
        if (!g_strcmp0(p->service_name, service_name)) {
            return p;
        }
    }
    return NULL;
}

void dbus_player_debug_print(MediaPlayerRec *p) {
    if (!p) return;

    LOG_PLAYER("------------------------------\n");

    LOG_PLAYER("Player app name:%s\n", p->app_name);
    LOG_PLAYER("Service name:%s\n", p->service_name);
    LOG_PLAYER("Desktop file:%s.desktop\n", p->desktop_file);
    LOG_PLAYER("Executable command:%s\n", p->exec_cmd);

    TrackInfo *tr = &p->track;

    switch (tr->status) {
    case PLAYER_STATUS_CLOSED:
        LOG_PLAYER("Status:%d  PLAYER_STATUS_CLOSED (not running)\n", tr->status);
        break;

    case PLAYER_STATUS_STOPPED:
        LOG_PLAYER("Status:%d  PLAYER_STATUS_STOPPED\n", tr->status);
        break;

    case PLAYER_STATUS_PAUSED:
        LOG_PLAYER("Status:%d  PLAYER_STATUS_PAUSED\n", tr->status);
        break;

    case PLAYER_STATUS_PLAYING:
        LOG_PLAYER("Status:%d  PLAYER_STATUS_PLAYING\n", tr->status);
        break;

    case PLAYER_STATUS_NOTIFY_MSG:
        // Simply a msg to the GUI.
        LOG_PLAYER("Status:%d  PLAYER_STATUS_NOTIFY_MSG\n", tr->status);
        break;

    default:
        LOG_PLAYER("Unknown status:%d\n", tr->status);
    }

    if (tr->status != PLAYER_STATUS_NOTIFY_MSG) {
        LOG_PLAYER("Track:%s\n", tr->track);
        LOG_PLAYER("Artist:%s\n", tr->artist);
        LOG_PLAYER("Album:%s\n", tr->album);
        LOG_PLAYER("Track length in microsecs:%ld\n", tr->track_len);
        LOG_PLAYER("Track pos in microsecs:%ld\n", tr->track_pos);
        LOG_PLAYER("Flags:%d\n", tr->flags);
    } else {
        // Simply a msg to the GUI.
        LOG_PLAYER("Message:%s\n", tr->track);
    }

    LOG_PLAYER("------------------------------\n");
}

GHashTable *dbus_player_get_player_list() {
    // Clear the old list
    dbus_player_clear_list();

    // Populate the list.
    // Detect players that follow the org.mpris.MediaPlayer2.* standard.
    mpris2_detect_players();

    // Add lastly used and saved players to the list
    dbus_player_get_saved();

    // Make sure the most popular players are in the list (on our target distros).
    // Notice: Audio-recorder will automatically detect and remember the last used players.
    //         Start your media-player, then press [Refresh]-button at end of Source: listbox to detect it.
    //         You do not need to hard-code other, new players here.

    // Add Rhythmbox manually (check if installed)
    add_player_to_list("rhythmbox", "org.mpris.MediaPlayer2.rhythmbox");

    // Add Banshee manually (check if installed)
    add_player_to_list("banshee", "org.mpris.MediaPlayer2.banshee");

    // Add Skype manually (check if installed)
    add_skype();

    return g_player_list;
}

gboolean add_player_to_list(gchar *desktop_file, gchar *service_name) {
    // Add player manually to the list. Check if it's installed.

    // Already in the list?
    if (dbus_player_lookup_service_name(service_name)) return TRUE;

    // New MediaPlayer record
    MediaPlayerRec *player = mpris2_player_new(service_name);

    // Set desktop file
    player->desktop_file = g_strdup(desktop_file);

    // Read name and exec_cmd from player's .desktop file
    get_details_from_desktop_file(player, desktop_file);

    // Find executable /usr/bin/exec_cmd
    gchar *path = find_command_path(player->exec_cmd);

    if (!path) {
        // Not installed.
        dbus_player_delete_item(player);
        return FALSE;
    }
    g_free(path);


    // Set icon name
    // TODO: We should use g_app_info_get_icon(GAppInfo *appinfo) instead.
    // FIX ME.
    player->icon_name = g_strdup(desktop_file);

    // Function to connect/disconnect event signals for this player
    player->func_set_signals = mpris2_set_signals;

    // Function to get track-info (album, track/song name/title, genre, etc.)
    player->func_get_info = mpris2_get_metadata;

    // Function to start/run the media player
    player->func_start_app = mpris2_start_app;

    // Function to check if this player is running
    player->func_check_is_running = mpris2_service_is_running;

    if (!dbus_player_lookup_app_name(player->app_name)) {
        // Add to list
        GHashTable *player_list = dbus_player_get_list_ref();
        g_hash_table_insert(player_list, g_strdup(player->service_name), player);
    } else {
        // Duplicate or bad record. Free it.
        dbus_player_delete_item(player);
    }

    return TRUE;
}

void add_skype() {
    // Add Skype to the list (if installed)
    const gchar *service_name = "com.Skype.API";

    // Remove Skype from the list
    GHashTable *player_list = dbus_player_get_list_ref();
    g_hash_table_remove(player_list, service_name);

    // Find /usr/bin/skype
    gchar *path = find_command_path("skype");
    if (!path) {
        // Not installed.
        return;
    }
    g_free(path);

    // New MediaPlayer record (yep, we store Skype data in a MediaPlayer record)
    MediaPlayerRec *player = mpris2_player_new(service_name);

    player->type = COMM_PROGRAM;

    // Set application name, executable and desktop_file
    player->app_name = skype_get_app_name();
    player->exec_cmd = g_strdup("skype");
    player->desktop_file = g_strdup("skype");

    // Set icon name
    // TODO: We should use g_app_info_get_icon(GAppInfo *appinfo) instead.
    // FIX ME.
    player->icon_name = g_strdup("skype");

    // Function to register/unregister notification methods
    player->func_set_signals = skype_setup;

    // Function to get track-info (strictly, Skype does not have "track" data, though it has target filename)
    player->func_get_info = skype_get_info;

    // Function to start/run Skype
    player->func_start_app = skype_start_app;

    if (!dbus_player_lookup_app_name(player->app_name)) {
        // Add to list
        GHashTable *player_list = dbus_player_get_list_ref();
        g_hash_table_insert(player_list, g_strdup(player->service_name), player);
    } else {
        // Duplicate or bad record. Free it.
        dbus_player_delete_item(player);
    }
}

void dbus_player_send_notification(gchar *msg) {
    RecorderCommand *cmd = g_malloc0(sizeof(RecorderCommand));
    cmd->type = RECORDING_NOTIFY_MSG;
    cmd->track = g_strndup(msg, MPRIS_STRLEN);

    // Send command to rec-manager.c.
    // It will free the cmd structure after processing.
    rec_manager_send_command(cmd);
}

// --------------------------------------------------------------------
// Support functions to read/write values to "players/saved-player-list" in GSettings.
// --------------------------------------------------------------------

static void split_value(gchar *str, gchar **desktop_file, gchar **service_name) {
    // Split str on "\t" and return the parts.
    *desktop_file = NULL;
    *service_name = NULL;

    if (!str) return;

    // Split on '\t'
    gchar **args = g_strsplit(str, "\t", 3);

    // We cope only 2 arguments
    if  (g_strv_length(args) != 2)  {
        goto LBL_1;
    }

    if (args && args[0]) {
        *desktop_file = g_strdup(args[0]);

        if (args[1]) {
            *service_name = g_strdup(args[1]);
        }
    }

LBL_1:
    // Delete args
    g_strfreev(args);
}

gchar *get_base_name(gchar *service_name) {
    // Take last part of service_name and return it.
    // Eg. take "vlc" from "org.mpris.MediaPlayer2.vlc"

    // Find last "."
    gchar *pos = g_strrstr0(service_name, ".");
    if (pos) {
        return g_strdup(pos+1);
    }

    return NULL;
}

static void dbus_player_delete_saved(const gchar *service_name) {
    // Delete service_name from GSettings.
    // See dconf-editor, key: /apps/audio-recorder/players/players/saved-player-list
    GList *list = NULL;
    GList *new_list = NULL;

    if (!service_name) return;

    str_trim((gchar*)service_name);

    // Get saved-player-list from GSettings.
    const gchar *conf_key = "players/saved-player-list";
    conf_get_string_list((gchar*)conf_key, &list);

    GList *item = g_list_first(list);
    while (item) {
        // Take values. Eg: "amarok \t org.mpris.MediaPlayer2.amarok"
        gchar *str = (gchar*)item->data;
        gchar *desktop_file = NULL;
        gchar *service = NULL;

        // Split on '\t'
        split_value(str, &desktop_file, &service);

        // Service names match?
        if (!g_strcmp0(service_name, service)) {
            // Drop this node
            ;
        } else {
            // Keep this node
            new_list = g_list_append(new_list, g_strdup(str));
        }

        g_free(desktop_file);
        g_free(service);

        item = g_list_next(item);
    }

    // Save changes to GSettings
    conf_save_string_list((gchar*)conf_key, new_list);

    // Free delete_list
    str_list_free(new_list);
    new_list = NULL;

    // Free list
    str_list_free(list);
    list = NULL;
}

static void dbus_player_save(MediaPlayerRec *pl) {
    // Save pl->app_name/pl->service_name to GSettings.
    // See dconf-editor, key: /apps/audio-recorder/players/players/saved-player-list
    GList *list = NULL;
    if (!pl->service_name) return;

    // Delete old value
    dbus_player_delete_saved(pl->service_name);

    // str must have format "vlc \t org.mpris.MediaPlayer2.vlc"
    gchar *str = g_strdup_printf("%s\t%s", check_null(pl->desktop_file), pl->service_name);

    // Get saved-player-list from Gsettings.
    const gchar *conf_key = "players/saved-player-list";
    conf_get_string_list((gchar*)conf_key, &list);

    // Add new entry and save in GSettings/DConf
    list = g_list_prepend(list, g_strdup(str));
    conf_save_string_list((gchar*)conf_key, list);

#if defined(DEBUG_PLAYER) || defined(DEBUG_ALL)
    LOG_PLAYER("----------------------------\n");
    str_list_print("New, saved saved-player-list", list);
    LOG_PLAYER("----------------------------\n");
#endif

    // Free list
    str_list_free(list);
    list = NULL;
    g_free(str);
}

static void dbus_player_get_saved() {
    // Get saved-player-list from GSettings.
    // See dconf-editor, key: /apps/audio-recorder/players/players/saved-player-list
    const gchar *conf_key = "players/saved-player-list";

    // Get list
    GList *list = NULL;
    conf_get_string_list((gchar*)conf_key, &list);

#if defined(DEBUG_PLAYER) || defined(DEBUG_ALL)
    LOG_PLAYER("----------------------------\n");
    str_list_print("Get saved-player-list", list);
    LOG_PLAYER("----------------------------\n");
#endif

    // Add saved & still existing media-players to the list
    GList *item = g_list_first(list);
    while (item) {
        // Read values. Eg. "vlc \t org.mpris.MediaPlayer2.vlc"
        gchar *str = (gchar*)item->data;
        gchar *desktop_file = NULL;
        gchar *service_name = NULL;

        // Split on '\t'
        split_value(str, &desktop_file, &service_name);

        // We will not tolerate errors here.
        // Wipe out the entire list if one line is bad (eg. it has older format)!
        if (!(desktop_file && service_name)) {
            g_free(desktop_file);
            g_free(service_name);
            conf_save_string_list((gchar*)conf_key, NULL);
            goto LBL_1;
        }

        // Add media-player to the list (to be shown in the "Source:" listbox).
        if (!add_player_to_list(desktop_file, service_name)) {

            // It's probably uninstalled. Delete form GSettings too.
            LOG_PLAYER("Player %s, (%s) removed from the list. It's probably uninstalled.\n",
                       desktop_file, service_name);

            dbus_player_delete_saved(service_name);
        }

        g_free(desktop_file);
        g_free(service_name);

        item = g_list_next(item);
    }

LBL_1: {
        // Free list
        str_list_free(list);
        list = NULL;
    }

}

void get_details_from_desktop_file(MediaPlayerRec *pl, const gchar *desktop_file) {

    if (!desktop_file) {
        goto LBL_1;
    }

    gchar *s = NULL;

    // Ends with ".desktop"?
    if (g_str_has_suffix(desktop_file, ".desktop")) {
        s = g_strdup(desktop_file);
    } else {
        // Add ".desktop"
        s = g_strdup_printf("%s.desktop", desktop_file);
    }

    // Get GDesktopAppInfo from propgram.desktop file
    GDesktopAppInfo *app_info = g_desktop_app_info_new(s);
    g_free(s);

    if (!app_info) {
        goto LBL_1;
    }

    // Ref:https://developer.gnome.org/gio/2.26/GAppInfo.html
    // and its implementation:https://developer.gnome.org/gio/2.30/gio-Desktop-file-based-GAppInfo.html

    // Read application title
    const gchar *app_name = g_app_info_get_name(G_APP_INFO(app_info));
    if (!app_name) {
        app_name = g_app_info_get_display_name(G_APP_INFO(app_info));
    }

    pl->app_name = g_strdup(app_name);

    // Read executable command and its arguments
    pl->exec_cmd = g_strdup((gchar*)g_app_info_get_commandline(G_APP_INFO(app_info)));

    // Set icon name (notice: this is not a perfect solution but works in most cases!).
    // TODO: We should use g_app_info_get_icon(GAppInfo *appinfo) instead.
    // FIX ME.
    pl->icon_name = g_strdup(desktop_file);

    g_object_unref(app_info);


LBL_1: {
        // Take "vlc" from "org.mpris.MediaPlayer2.vlc"
        gchar *base_name = get_base_name(pl->service_name);

        // Make sure these values are set
        if (str_length0(pl->app_name) < 1) {
            pl->app_name = g_strdup(base_name);
        }

        if (str_length0(pl->desktop_file) < 1) {
            pl->desktop_file = g_strdup(base_name);
        }

        if (str_length0(pl->exec_cmd) < 1) {
            pl->exec_cmd = g_strdup(base_name);
        }

        g_free(base_name);

    }
}


