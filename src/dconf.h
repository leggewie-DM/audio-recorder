#ifndef _DCONF_H__
#define _DCONF_H__

#include <glib.h>
#include <gdk/gdk.h>

#if 0
#include <gconf/gconf-client.h>
#endif

// The main configuration schema for this application
#define APPLICATION_SETTINGS_SCHEMA "org.gnome.audio-recorder"

void conf_flush_settings();

void conf_get_boolean_value(gchar *key, gboolean *value);
void conf_get_int_value(gchar *key, gint *value);
void conf_get_string_value(gchar *key, gchar **value);
void conf_get_string_list(gchar *key, GList **list);
void conf_get_variant_value(gchar *key, GVariant **var);

void conf_save_boolean_value(gchar *key, gboolean value);
void conf_save_int_value(gchar *key, gint value);
void conf_save_string_value(gchar *key, gchar *value);
void conf_save_string_list(gchar *key, GList *list);
void conf_save_variant(gchar *key, GVariant *var);

#endif

