#include <stdlib.h> // getenv()
#include <glib.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <assert.h>

void win_settings_show_dialog(GtkWindow *parent);
void win_settings_destroy_dialog();
GtkWindow *win_settings_get_window();
