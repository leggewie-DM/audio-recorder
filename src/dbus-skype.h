#include <string.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <dbus/dbus.h>

#include "log.h"

void skype_module_init();
void skype_module_exit();

void skype_set_record_ringing_sound(gboolean yes_no);

gchar *skype_get_app_name();
void skype_setup(gpointer player_rec, gboolean connect);
void skype_get_info(gpointer player_rec);
void skype_start_app(gpointer player_rec);

// DBus path and interface
#define SKYPE_SERVICE_PATH       "/com/Skype/Client"
#define SKYPE_SERVICE_INTERFACE  "com.Skype.API.Client"

// Uncomment this to show debug messages from the Skype
//#define DEBUG_SKYPE

#if defined(DEBUG_SKYPE) || defined(DEBUG_ALL)
#define LOG_SKYPE LOG_MSG
#else
#define LOG_SKYPE(x, ...)
#endif


