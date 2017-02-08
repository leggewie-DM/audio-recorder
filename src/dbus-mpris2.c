/*
 * Copyright (c) Linux community.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either"OK
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
#include <dbus/dbus.h>
#include "log.h"
#include "utility.h"
#include "dbus-player.h"
#include "dbus-mpris2.h"

// This is a MPRIS2 (org.mpris.MediaPlayer2) compliant media-player interface.
// Client side implementation.
// Please see: https://specifications.freedesktop.org/mpris-spec/latest/

// Glib/DBus connection
static GDBusConnection *g_dbus_conn = NULL;

static GDBusConnection *mpris2_connect_to_dbus();
static void mpris2_disconnect_from_dbus();

static gboolean mpris2_check_proxy(MediaPlayerRec *player);
static GVariant *mpris2_get_property(MediaPlayerRec *player, gchar *prop_name);

void mpris2_module_init() {
    LOG_DEBUG("Init dbus_mpris2.c.\n");

    g_dbus_conn = NULL;
}

void mpris2_module_exit() {
    LOG_DEBUG("Clean up dbus_mpris2.c.\n");

    mpris2_disconnect_from_dbus();
}

GDBusConnection *mpris2_connect_to_dbus() {
    // Connect to glib/DBus
    GError *error = NULL;

    if (!G_IS_DBUS_CONNECTION(g_dbus_conn)) {
        g_dbus_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

        if (!G_IS_DBUS_CONNECTION(g_dbus_conn)) {
            LOG_ERROR("mpris2_connect_to_dbus: Cannot connect to DBus: %s\n",
                      error ? error->message : "");

            if (error)
                g_error_free(error);

            return NULL;
        }
    }
    return g_dbus_conn;
}

void mpris2_disconnect_from_dbus() {
    // Disconnect from glib/DBus
    if (G_IS_DBUS_CONNECTION(g_dbus_conn)) {
        g_object_unref(g_dbus_conn);
    }
    g_dbus_conn = NULL;
}

static void mpris2_player_track_changed_TOTEM(gpointer player_rec) {
    // Spesific treatment of Totem movie player
    // Sound/audio track changed.

    static gchar *g_totem_track = NULL;
    static gboolean g_totem_stopped = TRUE;

    MediaPlayerRec *player = (MediaPlayerRec*)player_rec;
    if (!player) return;

    // Re-read all track-data
    mpris2_get_metadata(player_rec);

    TrackInfo *tr = &player->track;

    if (tr->status != PLAYER_STATUS_PLAYING) {
        return;
    }

    gchar *tmp = g_strdup_printf("%s/%s/%s", check_null(tr->album), check_null(tr->artist), check_null(tr->track));

    g_totem_stopped = TRUE;
    if (g_totem_track && g_strcmp0(tmp, g_totem_track) != 0) {
        g_totem_stopped = FALSE;
    }

    if (g_totem_stopped == FALSE && tr->track_pos == 0L) {
        // Stop current recording
        tr->status = PLAYER_STATUS_STOPPED;
        dbus_player_process_data(player);

        g_totem_stopped = TRUE;
    }

    tr->status = PLAYER_STATUS_PLAYING;

    // Remember last track
    g_free(g_totem_track);
    g_totem_track = tmp;

    //Debug:
    //dbus_player_debug_print(player);

    // Send data to the queue (rec-manager.c)
    dbus_player_process_data(player);
}

static void mpris2_player_track_changed(gpointer player_rec) {
    // Sound/audio track changed.
    MediaPlayerRec *player = (MediaPlayerRec*)player_rec;
    if (!player) return;

    // Totem's DBus-plugin sends PLAYER_STATUS_PLAYING multiple times (eg. 20 times in a row).
    // This may occur because we re-read all 'Metadata' from the player for each
    // state and track-change signals.
    // ATM, we have to give Totem movie player a spesific treatment.
    if (g_str_has_suffix(player->service_name, ".totem")) {
        mpris2_player_track_changed_TOTEM(player_rec);
        return;
    }

    TrackInfo *tr = &player->track;

    // Check if track_pos == 0 (at the beginning of track)?
    if (tr->track_pos == 0L) {
        // Stop current recording first. Send PLAYER_STATUS_STOPPED to the rec-manager.c.
        // Notice: Some players send NEITHER "{'PlaybackStatus': 'Stopped'}" NOR
        // "{'PlaybackStatus': 'Playing'}" signals before/after changing a music track.
        // We have to stop/start recording ourselves.

        tr->status = PLAYER_STATUS_STOPPED;
        dbus_player_process_data(player);
    }

    // Re-read all track-data. Status should now be PLAYER_STATUS_PLAYING.
    mpris2_get_metadata(player_rec);

    //Debug:
    //dbus_player_debug_print(player);

    // Send data to the queue (rec-manager.c)
    dbus_player_process_data(player);
}

static void mpris2_player_state_changed(gpointer player_rec) {
    // Player's state changed; Playing | Paused | Stopped.
    MediaPlayerRec *player = (MediaPlayerRec*)player_rec;
    if (!player) return;

    // Re-read data.
    mpris2_get_metadata(player_rec);

    //Debug:
    //dbus_player_debug_print(player);

    // Send data to the queue (rec-manager.c)
    dbus_player_process_data(player);
}

void debug_hash_table(MediaPlayerRec *player, GHashTable *table) {
    LOG_PLAYER("------------------------\n");
}

MediaPlayerRec *mpris2_player_new(const gchar *service_name) {
    // New MediaPlayerRec record
    MediaPlayerRec *player = g_malloc0(sizeof(MediaPlayerRec));
    player->service_name = g_strdup(service_name);
    player->app_name = NULL;
    return player;
}

static gboolean mpris2_check_proxy(MediaPlayerRec *player) {
    // Create and return proxy for player->service_name so we can read it's Player properties.
    // Please see: https://specifications.freedesktop.org/mpris-spec/latest/

    if (!player) return FALSE;

    // Already created?
    if (G_IS_DBUS_PROXY(player->proxy)) {
        return TRUE;
    }

    GDBusConnection *dbus_conn = mpris2_connect_to_dbus();

    // Proxy that points to "org.mpris.MediaPlayer2.Player" object.
    GError *error = NULL;
    player->proxy = g_dbus_proxy_new_sync(dbus_conn,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          NULL,
                                          player->service_name,  /* name */
                                          "/org/mpris/MediaPlayer2",  /* object path */
                                          "org.mpris.MediaPlayer2.Player",  /* interface */
                                          NULL,
                                          &error);

    if (error) {
        g_printerr("Cannot create proxy for %s. %s.\n", player->service_name, error->message);
        g_error_free(error);
        player->proxy = NULL;
    }

    return (G_IS_DBUS_PROXY(player->proxy));
}

gchar *mpris2_get_property_str(MediaPlayerRec *player, gchar *prop_name) {
    // Read a string property from the player's "org.mpris.MediaPlayer2" interface.
    GVariant *result = mpris2_get_property(player, prop_name);

    if (!result) {
        return NULL;
    }

    GVariant *peek = result;
    // Is "(v)"?
    if (g_variant_is_container(result)) {
        // Peek to "v"
        g_variant_get(result, "(v)", &peek);
    }

    gchar *s = NULL;
    g_variant_get(peek, "s", &s);

    g_variant_unref(result);
    result = NULL;

    g_variant_unref(peek);
    peek = NULL;

    // Return a string.
    // Caller should g_free() this value.
    return s;
}

static GVariant *mpris2_get_property(MediaPlayerRec *player, gchar *prop_name) {
    // Read a property from the player's "org.mpris.MediaPlayer2" interface.
    if (!player) return FALSE;

    GDBusConnection *dbus_conn = mpris2_connect_to_dbus();

    // Proxy that points to "org.mpris.MediaPlayer2" object.
    GError *error = NULL;
    GDBusProxy *proxy = g_dbus_proxy_new_sync(dbus_conn,
                        G_DBUS_PROXY_FLAGS_NONE,
                        NULL,
                        player->service_name, /* service name */
                        "/org/mpris/MediaPlayer2", /* object path */
                        "org.mpris.MediaPlayer2", /* base interface */
                        NULL,
                        &error);

    if (error) {
        g_printerr("Cannot create proxy for %s. %s.\n", player->service_name, error->message);
        g_error_free(error);
        return NULL;
    }

    // List of valid properties:
    // https://specifications.freedesktop.org/mpris-spec/latest/

    // Read value for prop_name.

    // I have earlier used the g_dbus_proxy_get_cached_property() function, but it returns NULL
    // values for many media-players.
    // GVariant *result = g_dbus_proxy_get_cached_property(proxy, prop_name);

    // This should work right.
    error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(proxy,
                       "org.freedesktop.DBus.Properties.Get",
                       g_variant_new("(ss)", "org.mpris.MediaPlayer2", prop_name),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       &error);

    if (error) {
        g_error_free(error);
        result = NULL;
    }

    // Unref proxy
    g_object_unref(proxy);

    // Caller should unref this value.
    return result;
}

static void mpris2_prop_signal(GDBusProxy *proxy, gchar *sender_name, gchar *signal_name,
                               GVariant *parameters, gpointer user_data) {
    // Handle "PropertiesChanged" signal.

    MediaPlayerRec *player = (MediaPlayerRec*)user_data;
    if (!player) return;

    // Got "PropertiesChanged" signal?
    if (g_strcmp0(signal_name, "PropertiesChanged")) return;

    // Debug:
#if defined(DEBUG_PLAYER) || defined(DEBUG_ALL)
    gchar *str = g_variant_print(parameters, TRUE);
    LOG_PLAYER("Received %s signal from %s.\n", signal_name, player->service_name);
    LOG_PLAYER("Data is:%s\n\n", str);
    g_free(str);
#endif

    // The data must have format "(sa{sv}as)"
    if (g_strcmp0(g_variant_get_type_string(parameters), "(sa{sv}as)")) {
        return;
    }

    GVariantIter *iter = NULL;
    GVariantIter *invalidated_iter = NULL;
    gchar *iface = NULL;

    // Ref: https://developer.gnome.org/gio/2.28/ch29s05.html
    g_variant_get(parameters, "(sa{sv}as)", &iface, &iter, &invalidated_iter);

    // Debug:
    // LOG_PLAYER("The interface is %s.\n", iface);

    // Check if Metadata(sound track) or PlaybackStatus has changed.
    // We are NOT interested in the data itself, just which properties changed.
    gboolean track_changed = FALSE;
    gboolean player_state_changed = FALSE;

    gchar *key = NULL;
    GVariant *value = NULL;
    while (g_variant_iter_next(iter, "{sv}", &key, &value)) {

        // Sound/audio track changed?
        if (!g_ascii_strcasecmp(key, "Metadata")) {
            track_changed = TRUE;

            // Player's state changed; Playing/Stopped/Paused?
        } else if (!g_ascii_strcasecmp(key, "PlaybackStatus")) {
            player_state_changed = TRUE;
        }

        g_free(key);
        g_variant_unref(value);
    }

    if (track_changed) {
        // Track changed.
        // Stop current recording. Then re-read data and re-start recording from a new track.
        mpris2_player_track_changed(player);

    } else if (player_state_changed) {
        // Player's state changed.
        // Send status change to the recorder.
        mpris2_player_state_changed(player);
    }

    /* ***
        Sample datasets for the PropertiesChanged signal:

        Signal PropertiesChanged: ('org.mpris.MediaPlayer2.Player', {'PlaybackStatus': <'Playing'>}, @as [])

        Signal PropertiesChanged: ('org.mpris.MediaPlayer2.Player', {'Volume': <1.0>}, @as [])

        Signal PropertiesChanged: ('org.mpris.MediaPlayer2.Player', {'CanGoNext': <true>, 'Metadata': <
        {'mpris:trackid': <'/org/mpris/MediaPlayer2/Track/9'>, 'xesam:url': <'file:///home/moma/Music/Bruce%
        20Springsteen%20-%20%20Wrecking%20Ball%20%5Bmp3-256-2012%5D/03%20-%20Shackled%20And%20Drawn.mp3'>,
        'xesam:title': <'Shackled And Drawn'>, 'xesam:artist': <['Bruce Springsteen']>, 'xesam:album': <'Wrecking
        Ball'>, 'xesam:genre': <['Rock']>, 'xesam:albumArtist': <['Bruce Springsteen']>, 'xesam:audioBitrate'<262144>,
        'xesam:contentCreated': <'2012-01-01T00:00:00Z'>, 'mpris:length': <int64 226000000>, 'xesam:trackNumber':
        <3>, 'xesam:discNumber': <1>, 'xesam:useCount': <0>, 'xesam:userRating': <0.0>, 'mpris:artUrl': <
        'file:///home/moma/.cache/rhythmbox/album-art/00000098'>}>, 'CanSeek': <true>, 'CanGoPrevious': <true>,
        'PlaybackStatus': <'Playing'>}, @as [])

        Signal PropertiesChanged: ('org.mpris.MediaPlayer2.Player', {'PlaybackStatus': <'Paused'>}, @as [])

    *** */
}

static void mpris2_prop_changed(GDBusProxy *proxy, GVariant *changed_properties,
                                GStrv invalidated_properties, gpointer user_data) {
    ;
    //Ref: https://developer.gnome.org/gio/unstable/GDBusProxy.html#GDBusProxy-g-properties-changd

}

void mpris2_set_signals(gpointer player_rec, gboolean do_connect) {
    // Connect/disconnct signals for this player.

    MediaPlayerRec *player = (MediaPlayerRec*)player_rec;

    // Connect event signals
    if (do_connect) {

        GDBusConnection *dbus_conn = mpris2_connect_to_dbus();

        GError *error = NULL;
        // Proxy for org.freedesktop.DBus.Properties
        player->prop_proxy = g_dbus_proxy_new_sync(dbus_conn,
                             G_DBUS_PROXY_FLAGS_NONE,
                             NULL,
                             player->service_name,  /* name */
                             "/org/mpris/MediaPlayer2",  /* object path */
                             "org.freedesktop.DBus.Properties",  /* interface */
                             NULL,
                             &error);

        if (error) {
            g_printerr("Cannot create proxy for org.freedesktop.DBus.Properties. %s.\n", error->message);
            g_error_free(error);
            player->prop_proxy = NULL;
            return;
        }

        // Ref: https://developer.gnome.org/gio/2.28/GDBusProxy.html
        g_signal_connect(player->prop_proxy, "g-properties-changed",
                         G_CALLBACK(mpris2_prop_changed), (gpointer)player/*user data*/);

        g_signal_connect(player->prop_proxy, "g-signal",
                         G_CALLBACK(mpris2_prop_signal), (gpointer)player/*user data*/);

        // Disconnect signals
    } else {

        // Delete player->prop_proxy. This should unset the above signals.
        if (G_IS_DBUS_PROXY(player->prop_proxy)) {
            g_object_unref(player->prop_proxy);
        }
        player->prop_proxy = NULL;

        // Delete also player->proxy
        if (G_IS_DBUS_PROXY(player->proxy)) {
            g_object_unref(player->proxy);
        }
        player->proxy = NULL;

    }
}

gboolean mpris2_service_is_running(gpointer player_rec) {
    // Check if the application is running.
    MediaPlayerRec *player = (MediaPlayerRec*)player_rec;
    if (!player_rec) return FALSE;

    return mpris2_service_is_running_by_name(player->service_name);
}

gboolean mpris2_service_is_running_by_name(const char *service_name) {
    // Check if the service_name/application is running.
    // Return TRUE/FALSE.

    // Connect to glib/DBus
    GDBusConnection *dbus_conn = mpris2_connect_to_dbus();
    if (!dbus_conn) return FALSE;

    // Proxy for DBUS_SERVICE_DBUS
    GError *error = NULL;
    GDBusProxy *proxy = g_dbus_proxy_new_sync(dbus_conn,
                        G_DBUS_PROXY_FLAGS_NONE,
                        NULL,
                        DBUS_SERVICE_DBUS,
                        DBUS_PATH_DBUS,
                        DBUS_INTERFACE_DBUS,
                        NULL,
                        &error);

    if (error) {
        g_printerr("Cannot create proxy for %s. %s.\n", DBUS_INTERFACE_DBUS, error->message);
        g_error_free(error);
        return FALSE;
    }

    // Call "NameHasOwner" method
    error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(proxy, "NameHasOwner",
                       g_variant_new("(s)", service_name),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       &error);

    if (error) {
        g_printerr("Cannot get NameHasOwner for %s. %s\n", service_name, error->message);
        g_error_free(error);
        g_object_unref(proxy);
        return FALSE;
    }

    // The result has format "(boolean,)"

    // Debug:
    // gchar *str = g_variant_print(result, TRUE);
    // LOG_PLAYER("Received data for HasName:<%s>\n", str);
    // g_free(str);

    // Take the boolean value
    gboolean running = FALSE;
    g_variant_get_child(result, 0, "b", &running);

    g_variant_unref(result);
    g_object_unref(proxy);

    // Return TRUE/FALSE
    return running;
}

static gchar *get_string_val(GVariant *v) {
    // Read and return a string value. The v can be either "s" or "as".
    // If array then return the first string.
    gchar *s = NULL;

    // Is it a string array "as"?
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING_ARRAY)) {

        g_variant_get_child(v, 0, "s", &s);

        // Is it a string "s"?
    }
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) {
        g_variant_get(v, "s", &s);
    }

    return s;
}

GVariant *mpris2_get_player_value(gpointer player_rec, gchar *variable) {
    // Read value (named by variable) from org.mpris.MediaPlayer2.Player object.
    // Ref: https://specifications.freedesktop.org/mpris-spec/latest/Player_Interface.html

    MediaPlayerRec *player = (MediaPlayerRec*)player_rec;
    if (!player) return NULL;

    // Connect to glib/DBus
    GDBusConnection *dbus_conn = mpris2_connect_to_dbus();
    if (!dbus_conn) return NULL;

    // Proxy that points to "org.mpris.MediaPlayer2.Player"
    // Ref: https://specifications.freedesktop.org/mpris-spec/latest/
    if (!mpris2_check_proxy(player)) {
        return NULL;
    }

    GError *error = NULL;
    GVariant *res = g_dbus_proxy_call_sync(player->proxy,
                                           "org.freedesktop.DBus.Properties.Get",
                                           g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", variable),
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           &error);

    if (error) {
        g_error_free(error);
        return NULL;
    }

    return res;
}

#if 0
void debug_variant(const gchar *tag, GVariant *v) {
    if (!v) {
        g_print("%s is NULL.\n", tag);
        return;
    }

    gchar *sval = g_variant_print(v, TRUE);
    const gchar *stype = g_variant_get_type_string(v);
    g_print("%s has type:%s and value:%s\n", tag, stype, sval);
    g_free(sval);
}
#endif

void mpris2_get_metadata(gpointer player_rec) {
    // Get track information (=metadata) and state for the given media player.
    // Ref: https://specifications.freedesktop.org/mpris-spec/2.1/Player_Interface.html#Property:Metadata

    MediaPlayerRec *player = (MediaPlayerRec*)player_rec;
    if (!player) return;

    // Reset track info
    TrackInfo *tr = &player->track;
    tr->status = PLAYER_STATUS_STOPPED;
    tr->flags = 0;
    tr->track[0] = tr->artist[0] = tr->album[0] = '\0';
    tr->track_len = -1L;
    tr->track_pos = -1L;

    // Proxy that points to "org.mpris.MediaPlayer2.Player"
    // Ref: https://specifications.freedesktop.org/mpris-spec/2.1/
    if (!mpris2_check_proxy(player)) {
        return;
    }

    // Read "PlaybackStatus" from player's "org.mpris.MediaPlayer2.Player" interface
    //
    // Test:
    // $ dbus-send --print-reply --session --dest=org.mpris.MediaPlayer2.rhythmbox /org/mpris/MediaPlayer2
    //   org.freedesktop.DBus.Properties.Get string:'org.mpris.MediaPlayer2.Player' string:'PlaybackStatus'
    //
    GVariant *result = mpris2_get_player_value(player, "PlaybackStatus");

    // DEBUG: debug_variant("PlaybackStatus", result);

    if (!result) {
        // Cannot contact player (it has quit)?
        tr->status = PLAYER_STATUS_CLOSED;
        return;
    }

    // So result != NULL.

    // Notice: The MPRIS2-standard defines result as "v" (variant that contains one string).
    // I think some players return a container type "(v)".

    GVariant *peek = result;
    // Is "(v)"?
    if (g_variant_is_container(result)) {
        // Peek to "v"
        g_variant_get(result, "(v)", &peek);
    }

    gchar *s = NULL;
    g_variant_get(peek, "s", &s);

    // Set tr->status
    if (!g_ascii_strcasecmp(s, "Playing"))
        tr->status = PLAYER_STATUS_PLAYING;

    else if (!g_ascii_strcasecmp(s, "Paused"))
        tr->status = PLAYER_STATUS_PAUSED;

    else if (!g_ascii_strcasecmp(s, "Stopped"))
        tr->status = PLAYER_STATUS_STOPPED;

    g_variant_unref(result);
    result = NULL;

    g_variant_unref(peek);
    peek = NULL;

    g_free(s);

    // Should we continue?
    if (tr->status != PLAYER_STATUS_PLAYING) {
        return;
    }

    // Here tr->status is PLAYER_STATUS_PLAYING.
    // Get track info (Metadata) from player's "org.mpris.MediaPlayer2.Player" interface.
    // The dict has type "a{sv}".
    // Ref: https://specifications.freedesktop.org/mpris-spec/latest/Player_Interface.html#Property:Metadata
    //
    // Test:
    // $ dbus-send --print-reply --session --dest=org.mpris.MediaPlayer2.rhythmbox /org/mpris/MediaPlayer2
    //    org.freedesktop.DBus.Properties.Get string:'org.mpris.MediaPlayer2.Player' string:'Metadata'
    //
    GVariant *dict = mpris2_get_player_value(player, "Metadata");

    // DEBUG: debug_variant("Metadata", dict);

    if (!dict) {
        // Cannot get Metadata (should we consider this as on error?)
        // 03.april.2015, commented out by MOma:  Ambient Noise Player does not support "Metadata" yet.  

        // tr->status = PLAYER_STATUS_CLOSED;

        return;
    }

    /* Some essential key names for the returned "a{sv}" dictionary:
      'mpris:trackid' has type 's'
      'xesam:url' has type 's'
      'xesam:title' has type 's'
      'xesam:artist' has type 'as'
      'xesam:genre' has type 'as'
      'rhythmbox:streamTitle' has type 's'
      'xesam:audioBitrate' has type 'i'
      'mpris:length' has type 'x'
      'xesam:trackNumber' has type 'i'
      'xesam:useCount' has type 'i'
      'xesam:userRating' has type 'd'

     Here is example data from Rhythmbox:
     ('org.mpris.MediaPlayer2.Player',
       {'CanSeek': <true>,
       'Metadata': <{
       'mpris:trackid': <'/org/mpris/MediaPlayer2/Track/2'>,
       'xesam:url': <'file:///home/moma/Music/Bruce Springsteen.mp3'>,
       'xesam:title': <'Land Of Hope'>,
       'xesam:artist': <['Bruce Springsteen']>,
       'xesam:album': <'Wrecking Ball'>,
       'xesam:genre': <['Rock']>,
       'xesam:albumArtist': <['Bruce Springsteen']>,
       'xesam:audioBitrate': <262144>,
       'xesam:contentCreated': <'2012-01-01T00:00:00Z'>,
       'mpris:length': <int64 418000000>,
       'xesam:trackNumber': <10>,
       'xesam:discNumber': <1>,
       'xesam:useCount': <0>,
       'xesam:userRating': <0.0>,
       'mpris:artUrl': <'file:///home/moma/.cache/rhythmbox/album-art/00000098'>}>,
       'Volume': <1.0>,
       'PlaybackStatus': <'Playing'>}, @as [])"

      Totem movie player sends this (when playing a local file:///):
       'mpris:length' (type:x)  value:int64 197172000
       'mpris:trackid' (type:s) value:'file:///home/moma/Music/Believe%20%20mike%20newman%20mix.mp3'

      Data from VLC (when playing a local file:///):
        ('org.mpris.MediaPlayer2.Player',
        {'Metadata': <{'mpris:trackid': <objectpath '/org/videolan/vlc/playlist/6'>,
         'xesam:url': <'file:///home/moma/Music/Believe%20%20mike%20newman%20mix.mp3'>,
         'vlc:time': <uint32 197>, 'mpris:length': <int64 197172868>,
         'vlc:length': <int64 197172>, 'vlc:publisher': <9>}>}, @as [])
    */

    peek = dict;
    // Is it "(v)" ?
    if (g_variant_is_container(dict)) {
        // Peek to "v"
        g_variant_get(dict, "(v)", &peek);
    }

    GVariantIter iter;
    GVariant *value = NULL;
    gchar *key = NULL;

    // Ref: https://developer.gnome.org/glib/2.30/glib-GVariant.html#g-variant-get-va
    g_variant_iter_init(&iter, peek);
    while (g_variant_iter_next(&iter, "{sv}", &key, &value)) {

        // Debug
#if defined(DEBUG_PLAYER) || defined(DEBUG_ALL)
        gchar *sval = g_variant_print(value, TRUE);
        const gchar *stype = g_variant_get_type_string(value);
        LOG_PLAYER("Metdata key \"%s\" has type:%s and value:%s\n", key, stype, sval);
        g_free(sval);
#endif

        gchar *str = NULL;
        GVariantIter *iter2 = NULL;

        // xesam:title?
        if (g_str_has_suffix(key, ":title")) {
            str = get_string_val(value);
            str_copy(tr->track, str, MPRIS_STRLEN-1);
        }

        // xesam::artist? Notice: This has data type "as" (VLC media player sets this to "s"?)
        else if (g_str_has_suffix(key, ":artist")) {
            str = get_string_val(value);
            str_copy(tr->artist, str, MPRIS_STRLEN-1);
        }

        // xesam:albumArtist? Notice: This has data type "as". (VLC media player sets this to "s"?)
        else if (g_str_has_suffix(key, ":albumArtist")) {
            // Already set by ":artist"?
            if (tr->artist[0] == '\0') {
                str = get_string_val(value);
                str_copy(tr->artist, str, MPRIS_STRLEN-1);
            }
        }

        // xesam:album?
        else if (g_str_has_suffix(key, ":album")) {
            str = get_string_val(value);
            str_copy(tr->album, str, MPRIS_STRLEN-1);
        }

        // mpris:trackid?
        // xesam:url?
        else if (g_str_has_suffix(key, ":trackid") ||
                 g_str_has_suffix(key, ":url")) {

            str = get_string_val(value);

            // From URI format
            gchar *from_uri = NULL;
            if (str) {
                from_uri = g_filename_from_uri(str, NULL, NULL);
            }

            if (!from_uri) {
                from_uri = g_strdup(str);
            }

            // Take the base (filename) value only
            gchar *path = NULL;
            gchar *base = NULL;
            gchar *ext = NULL;
            split_filename3(from_uri, &path,  &base, &ext);

            if (str_length0(tr->track) < 1) {
                str_copy(tr->track, base, MPRIS_STRLEN-1);
            }

            g_free(from_uri);
            g_free(path);
            g_free(base);
            g_free(ext);

        }
        // mpris:length? (total length of content/stream in microseconds)
        else if (g_str_has_suffix(key, ":length")) {
            tr->track_len = g_variant_get_int64(value);
        }

        if (iter2)
            g_variant_iter_free(iter2);

        g_free(str);

        g_variant_unref(value);
        g_free(key);

    }

    g_variant_unref(dict);
    g_variant_unref(peek);

    // Read current stream/file position from player's "org.mpris.MediaPlayer2.Player" interface.
    // Ref: https://specifications.freedesktop.org/mpris-spec/latest/Player_Interface.html
    //
    // Test:
    // $ dbus-send --print-reply --session --dest=org.mpris.MediaPlayer2.rhythmbox /org/mpris/MediaPlayer2
    //     org.freedesktop.DBus.Properties.Get string:'org.mpris.MediaPlayer2.Player' string:'Position'
    //
    //  method returns sender=:1.125 -> dest=:1.170 reply_serial=2
    //  variant int64 218000000 (in microseconds)

    result = mpris2_get_player_value(player, "Position");
    // (<int64 0>,)

    if (!result) {
        // track_pos = -1L;
        return;
    }

    peek = result;

    // Is it "(v)"?
    if (g_variant_is_container(result)) {
        // Peek to "v"
        g_variant_get(result, "(v)", &peek);
    }

    tr->track_pos = g_variant_get_int64(peek);

    g_variant_unref(result);
    g_variant_unref(peek);
}

void mpris2_start_app(gpointer player_rec) {
    // Start player application

    /*
    I have tried StartSeviceByName command, but it does not work:

    $ dbus-send --session --dest="org.freedesktop.DBus" \
               "/org/freedesktop/DBus" \
               "org.freedesktop.DBus.StartServiceByName" \
               "string:org.gnome.Rhythmbox3" \
               "int32:0"

    // The following code works fine for "org.gnome.Rhythmbox3" but there is
    // no service file for "org.mpris.MediaPlayer2.rhythmbox".
    gchar *service_name = "org.gnome.Rhythmbox3";

    GDBusProxy *proxy = g_dbus_proxy_new_sync(dbus_conn,
                        G_DBUS_PROXY_FLAGS_NONE,
                        NULL,
                        DBUS_SERVICE_DBUS,
                        DBUS_PATH_DBUS,
                        DBUS_INTERFACE_DBUS,
                        NULL,
                        &error);

    // Call StartServiceByName method.
    error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(proxy, "StartServiceByName",
                       g_variant_new("(su)", service_name, 0),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       &error);
    */


    // Let's do it the hard way.

    MediaPlayerRec *player = (MediaPlayerRec*)player_rec;
    if (!player_rec) return;

    // Already running?
    if (mpris2_service_is_running(player_rec)) {
        return;
    }

    if (!player->exec_cmd) {
        g_printerr("Executable name for %s is not set. Start the application manually.\n",
                   player->app_name);
        return;
    }

    // Find path for exec_cmd
    gchar *path = find_command_path(player->exec_cmd);
    if (!path) {
        g_printerr("Cannot run %s. Start the application %s manually.\n",
                   player->exec_cmd, player->app_name);
        return;
    }

    // Run the application.

    // Build argv[] list.
    gchar **argv = g_new(gchar*, 2);
    argv[0] = g_strdup(path);
    argv[1] = NULL;

    // Run the command. It will return immediately because it's asynchronous.
    GError *error = NULL;
    GPid __attribute__((unused)) pid = exec_command_async(argv, &error);

    // Free the values
    if (error) {
        g_error_free(error);
        error = NULL;
    }

    g_strfreev(argv);
    g_free(path);
}

void mpris2_detect_players() {
    // Get list of MPRIS2 (org.mpris.MediaPlayer2.*) compliant programs.

    // Connect to glib/DBus
    GDBusConnection *dbus_conn = mpris2_connect_to_dbus();
    if (!dbus_conn) return;

    // Get player list
    GHashTable *player_list = dbus_player_get_list_ref();

#define DBUS_MPRIS2_NAMESPACE "org.mpris.MediaPlayer2."

    GError *error = NULL;

    // Ref: https://dbus.freedesktop.org/doc/api/html/group__DBusShared.html
    // Create a proxy for org.freedesktop.DBus and execute "ListNames"
    GVariant *result = g_dbus_connection_call_sync (dbus_conn,
                       "org.freedesktop.DBus", /* name */
                       "/org/freedesktop/DBus", /* path */
                       "org.freedesktop.DBus", /* interface */
                       "ListNames",
                       NULL,
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       &error);

#if 0
    // This does the same thing.
    GDBusProxy *proxy = g_dbus_proxy_new_sync(dbus_conn,
                        G_DBUS_PROXY_FLAGS_NONE,
                        NULL,
                        DBUS_SERVICE_DBUS,
                        DBUS_PATH_DBUS,
                        DBUS_INTERFACE_DBUS,
                        NULL,
                        &error);

    if (error) {
        g_print("Cannot create proxy for ListNames. %s\n", error->message);
        g_error_free(error);
        return;
    }

    // Call ListNames method, wait for reply
    error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(proxy, "ListNames",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       &error);

    // Unref the proxy
    g_object_unref(proxy);

#endif

    if (error) {
        g_printerr("Cannot read service names from org.freedesktop.DBus. %s\n", error->message);
        g_error_free(error);
        return;
    }

    // The result is an array of strings, "(as)".

    GVariantIter *iter = NULL;
    gchar *service_name = NULL;

    // Get iter to a string array
    g_variant_get(result, "(as)", &iter);

    // Iter over all service_names
    service_name = NULL;
    while (g_variant_iter_next(iter, "s", &service_name)) {

        // Drop names that begin with ':'
        if (service_name && *service_name == ':') {
            g_free(service_name);
            continue;
        }

        // LOG_DEBUG("Available service: %s\n", service_name);

        MediaPlayerRec *player = NULL;

        // Belongs to org.mpris.MediaPlayer2.* namespace?
        if (!strncmp(DBUS_MPRIS2_NAMESPACE, service_name, strlen(DBUS_MPRIS2_NAMESPACE))) {

            LOG_PLAYER("Detected service name %s.\n", service_name);

            // New MediaPlayer record
            player = mpris2_player_new(service_name);

            // EDIT: "Identity" property is no longer needed or read.
            // We are reading application name from its desktop file.
            // Ref: https://specifications.freedesktop.org/mpris-spec/latest/Media_Player.html
            // player->app_name = mpris2_get_property_str(player, "Identity");

            // Get player's desktop file.
            // Ref: https://specifications.freedesktop.org/mpris-spec/latest/Media_Player.html
            player->desktop_file = mpris2_get_property_str(player, "DesktopEntry");

            if (str_length0(player->desktop_file) < 1) {
                g_printerr("Error: DBus-interface for %s should implement \"DesktopEntry\" property.\n", service_name);
                player->desktop_file = get_base_name(service_name);
            }

            // Load app name, executable and icon name from player's xxx.desktop file
            get_details_from_desktop_file(player, player->desktop_file);

            // Function to connect/disconnect event signals
            player->func_set_signals = mpris2_set_signals;

            // Function to check if this player is running
            player->func_check_is_running = mpris2_service_is_running;

            // Function to get track-info (track/song name/title/album/artist, length, etc.)
            player->func_get_info = mpris2_get_metadata;

            // Function to start/run the application
            player->func_start_app = mpris2_start_app;
        }

        // Has a valid player record?
        if (player && player->app_name) {

            // Add player to the g_player_list.

            // Lookup the record by its app_name (like: "Amarok 2.3.2")
            if (!dbus_player_lookup_app_name(player->app_name)) {
                // Add it to the list
                g_hash_table_insert(player_list, g_strdup(player->service_name), player);
            } else {
                // Duplicate or bad record. Free it.
                dbus_player_delete_item(player);
            }

        }

        g_free(service_name);
    }

    g_variant_iter_free(iter);
    g_variant_unref(result);

}


