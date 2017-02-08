#ifndef __DBUS_PLAYER_H__
#define __DBUS_PLAYER_H__

#include <string.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <utility.h>

// Uncomment this to show debug messages from dbus-mpris2 and dbus-player modules.
//#define DEBUG_PLAYER

#if defined(DEBUG_PLAYER) || defined(DEBUG_ALL)
#define LOG_PLAYER LOG_MSG
#else
#define LOG_PLAYER(x, ...)
#endif

#define DBUS_MPRIS_TIMEOUT 400 // Milliseconds

#define PLAYER_STATUS_CLOSED -1
#define PLAYER_STATUS_STOPPED 0
#define PLAYER_STATUS_PLAYING 2
#define PLAYER_STATUS_PAUSED  3
#define PLAYER_STATUS_NOTIFY_MSG 7  // Send notification message to the GUI

#define MPRIS_STRLEN NAME_MAX - 4 // Set to max filename (NAME_MAX) in Linux

// Function pointers
typedef void (*GetTrackInfo)(gpointer player_rec); // player_rec = (MediaPlayerRec*)
typedef void (*SignalFunction)(gpointer player_rec, gboolean connect); // player_rec = (MediaPlayerRec*)
typedef gboolean (*AppIsRunning)(gpointer player_rec); // player_rec = (MediaPlayerRec*)
typedef void (*StartPlayer)(gpointer player_rec); // player_rec = (MediaPlayerRec*)

typedef struct {
    gchar track[MPRIS_STRLEN+1];  // Track name (or GUI message if status==PLAYER_STATUS_NOTIFY_MSG)
    gchar artist[MPRIS_STRLEN+1]; // Artist
    gchar album[MPRIS_STRLEN+1];  // Album
    gint status;  // One of the PLAYER_STATUS_* values
    gint64 track_len; // Total track length in microseconds
    gint64 track_pos;  // Current stream position in microseconds
    guint flags;  // Possible RECORDING_DELETE_FILE flag
} TrackInfo;

typedef struct {
    gint type; // MEDIA_PLAYER or COMM_PROGRAM

    GDBusProxy *proxy; // Proxy for the "org.mpris.MediaPlayer2.Player" interface

    GDBusProxy *prop_proxy; // Proxy for the "org.freedesktop.DBus.Properties" interface

    gchar *service_name; // Eg. "org.mpris.MediaPlayer2.banshee" or "com.Skype.API"

    gchar *desktop_file; // Name of .desktop file (normally without .desktop extension)
    gchar *exec_cmd;     // Executable command and args from a .desktop file
    gchar *app_name;     // Application title, eg. "Amarok 2.3.3", "Banshee 2.1" or "Skype"
    gchar *icon_name;	 // Icon name

    TrackInfo track;     // See TrackInfo

    GetTrackInfo func_get_info;  // Function to get track data; title/artist/album/music genre/time etc.
    SignalFunction func_set_signals;   // Function to connect/disconnect DBus signals
    AppIsRunning func_check_is_running;// Function to check if the app is running
    StartPlayer func_start_app;  // Function to run/start the player
} MediaPlayerRec;

void dbus_player_init();
void dbus_player_exit();

GHashTable *dbus_player_get_list_ref();

GHashTable *dbus_player_get_player_list();

MediaPlayerRec *dbus_player_lookup_app_name(const gchar *app_name);
MediaPlayerRec *dbus_player_lookup_service_name(const gchar *service_name);

void dbus_player_delete_item(gpointer data);
void dbus_player_debug_print(MediaPlayerRec *p);

void dbus_player_player_changed(gchar *service_name);

// Called by media players when they have new data
void dbus_player_process_data(gpointer player);

gchar *get_base_name(gchar *service_name);

void get_details_from_desktop_file(MediaPlayerRec *pl, const gchar *desktop_file);

#endif



