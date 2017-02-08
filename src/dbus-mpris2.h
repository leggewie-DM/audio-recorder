#include <string.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <dbus/dbus.h>

void mpris2_module_init();
void mpris2_module_exit();

MediaPlayerRec *mpris2_player_new(const gchar *service_name);

gchar *mpris2_get_property_str(MediaPlayerRec *player, gchar *prop_name);

void mpris2_set_signals(gpointer player_rec, gboolean do_connect);

void mpris2_get_metadata(gpointer player_rec);

void mpris2_start_app(gpointer player_rec);

gboolean mpris2_service_is_running(gpointer player_rec);
gboolean mpris2_service_is_running_by_name(const char *service_name);

void mpris2_detect_players();


