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
#include <stdlib.h>
#include "dconf.h"
#include "support.h"
#include "utility.h"
#include "log.h"
#include <gio/gio.h>


// This module writes and reads values from/to GNOME's configuration registry (GSettings).
// DConf is most likely the backend.
// You can check these values in dconf-editor.
// Start dconf-editor and browse to /apps/audio-recorder/.

// Notice:
// All valid configuration keys must be defined in the schema file "org.gnome.audio-recorder.gschema.xml".
// It's in the data/ folder.
// You must also compile and install this file to /usr/share/glib-2.0/schemas/ directory.
//
// 0) Test the schema file first. I assume here that the schema is in the current (.) directory.
//    $ glib-compile-schemas --dry-run .
//
// 1) Copy the schema file to /usr/share/glib-2.0/schemas/.
//    $ sudo cp org.gnome.audio-recorder.gschema.xml /usr/share/glib-2.0/schemas/
//
// 2) Then re-compile all schemas.
//    $ sudo glib-compile-schemas /usr/share/glib-2.0/schemas/
//
// The Makefile.am in the data/ folder should do this installation automatically.
//
// You can also study various schema values with gsettings tool.
// $ gsettings
//
// This command will list all keys and values for org.gnome.audio-recorder 
// $ gsettings  list-recursively  org.gnome.audio-recorder
// -----------------------------------------------------------------
//
// The main schema name is "org.gnome.audio-recorder", and its path is /apps/audio-recorder/.
// Notice that there are also child schemas (sub paths) for
//  track/ (/apps/audio-recorder/track/).
//  skype/ (/apps/audio-recorder/skype/).
//  players/ (/apps/audio-recorder/players/).

// The main configuration schema for this application
#define APPLICATION_SETTINGS_SCHEMA "org.gnome.audio-recorder"

static GSettings *conf_get_base_settings();

void conf_flush_settings() {
    // Flush and write settings cache to disk
    g_settings_sync();

    GSettings *settings = conf_get_base_settings();
    if (!settings) {
        return;
    }

    guint i = 0;
    while (i++ < 4 && g_settings_get_has_unapplied(settings)) {
        g_settings_sync();
        g_usleep(G_USEC_PER_SEC * 0.1);
    }
}

static gboolean conf_is_valid_key(GSettings *settings, gchar *key) {
    // Check if the key is valid key within settings
    if (!G_IS_SETTINGS(settings)) {
        return FALSE;
    }

    // Get list of keys
    gchar **keys = NULL;

// Check:
// $ pkg-config --modversion glib-2.0 
//
#if GLIB_CHECK_VERSION(2, 45, 6)
    GSettingsSchema *schema = NULL;
    g_object_get(settings, "settings-schema", &schema, NULL);
    keys = g_settings_schema_list_keys(schema);
#else
    keys = g_settings_list_keys(settings);
#endif

    gint i = 0;
    gboolean found = FALSE;
    while (keys && keys[i]) {

        //Debug:
        //GVariant *value = NULL;
        //gchar *str = NULL;
        //value = g_settings_get_value (settings, keys[i]);
        //str = g_variant_print(value, TRUE);
        //g_print ("%s %s %s\n", g_settings_schema_get_id (schema), keys[i], str);
        //g_variant_unref(value);
        //g_free (str);

        if (!g_strcmp0(key, keys[i])) {
            found = TRUE;
            break;
        }
        i++;
    }

    g_strfreev(keys);

#if GLIB_CHECK_VERSION(2, 45, 6)
    g_settings_schema_unref(schema);
#endif

    return found;
}


#if 0
void conf_list_keys(GSettings *settings) {
    // List keys for the given settings (and schema)
    gchar *str = NULL;
    g_object_get(settings, "schema", &str, NULL);
    if (!str) {
        g_print("Bad schema:%s\n", str);
    }

    gchar **keys = NULL;
    gint i = 0;
    keys = g_settings_list_keys(settings);
    while (keys && keys[i]) {
        g_print("List key for schema:%s key:%s\n",  str, keys[i]);
        i++;
    }

    g_strfreev(keys);
    g_free(str);
}

void conf_list_children(GSettings *settings) {
    // List children of the given settings (and schema)
    gchar *str = NULL;
    g_object_get(settings, "schema", &str, NULL);
    if (!str) {
        g_print("Bad schema:%s\n", str);
    }

    gchar **children = NULL;
    gint i = 0;
    children = g_settings_list_children(settings);
    while (children && children[i]) {
        g_print("List child for schema:%s child:%s\n",  str, children[i]);
        i++;
    }

    g_strfreev(children);
    g_free(str);
}
#endif

static void conf_get_child_path(gchar *key, gchar **child_path, gchar **child_key) {
    // Split the key to child_path and child_key.
    // Eg. "track/track-name" has child_path "track/" and child_key "track-name".
    *child_path = NULL;
    *child_key = NULL;

    static gchar buf[MAX_PATH_LEN];

    // Find "/" in key
    gchar *p = g_strrstr(key, "/");
    if (p) {
        memset(buf, '\0', MAX_PATH_LEN);
        g_utf8_strncpy(buf, key, p - key);
        *child_path = g_strdup(buf);

        *child_key = g_strdup(p+1);
    }
}

static GSettings *conf_get_base_settings() {
    // Return GSettings (base) object. This points to /apps/audio-recorder/.

#if 0
    // This code failed on some Linux-distributions. Reverting to g_settings_new().

    // Ref: https://developer.gnome.org/gio/2.32/gio-GSettingsSchema-GSettingsSchemaSource.html
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = g_settings_schema_source_lookup(source, APPLICATION_SETTINGS_SCHEMA, TRUE);

    // Check if schema has been installed
    if (!schema) {
        g_printerr("Error: Cannot find settings for %s in GNOME's registry. "
                   "Please run \"make install\" as sudo or root user.\n", "/apps/audio-recorder/");
    }

    GSettings *settings = g_settings_new_full(schema, NULL, NULL);
    g_settings_schema_unref(schema);
#endif

    GSettings *settings = g_settings_new(APPLICATION_SETTINGS_SCHEMA);
    if (!G_IS_SETTINGS(settings)) {
        g_printerr("Error: Cannot find settings for %s in GNOME's registry. "
                   "Please run \"make install\" as sudo or root user.\n", "/apps/audio-recorder/");
    }

    return settings;
}

static GSettings *conf_get_settings_for_key(gchar *key, gchar **child_path, gchar **child_key) {
    // Return GSettings object for the given key
    *child_path = NULL;
    *child_key = NULL;

    // The key may contain a child path (it has a "/" in it).
    // For example the key "track/track-name" contains child_path ("track/") and child_key "track-name".
    // Take possible child_path and child_key.
    conf_get_child_path(key, child_path, child_key);

    // The main GSettings object
    GSettings *settings = conf_get_base_settings(); // Points to /apps/audio-recorder/.

    if (*child_path) {
        // Get GSettings object for child_path
        GSettings *child_settings = g_settings_get_child(settings, *child_path);
        if (G_IS_SETTINGS(child_settings)) {
            g_object_unref(settings);
            settings = child_settings; // Points to /apps/audio-recorder/some child_path/.
        }
    }

    return settings;
}

void conf_get_boolean_value(gchar *key, gboolean *value) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).

    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    // Read value
    *value = g_settings_get_boolean(settings, k);

LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);
}

void conf_get_int_value(gchar *key, gint *value) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).

    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    // Read value
    *value = g_settings_get_int(settings, k);

LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);
}

void conf_get_string_value(gchar *key, gchar **value) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).

    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    // Read value
    *value = g_settings_get_string(settings, k);

LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);

    // The caller should g_free() the value
}

void conf_get_string_list(gchar *key, GList **list) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).

    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    // Read string list
    gchar **argv = g_settings_get_strv(settings, k);

    // From gchar *argv[] to GList
    *list = NULL;
    guint i = 0;
    while (argv && argv[i]) {
        *list = g_list_append(*list, g_strdup(argv[i]));
        i++;
    }

    // Free argv[]
    g_strfreev(argv);


LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);

    // The caller should free the list
}

void conf_get_variant_value(gchar *key, GVariant **var) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    *var = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).

    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    *var = g_settings_get_value(settings, k);

LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);

    // The caller should free the value
}


void conf_save_boolean_value(gchar *key, gboolean value) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).


    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    // Save value
    if (!g_settings_set_boolean(settings, k, value)) {
        LOG_ERROR("Cannot save configuration key \"%s\" (%s).\n", key, (value ? "true" : "false"));
    }

LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);
}

void conf_save_int_value(gchar *key, gint value) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).

    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    // Save value
    if (!g_settings_set_int(settings, k, value)) {
        LOG_ERROR("Cannot save configuration key \"%s\" (%d).\n", key, value);
    }

LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);
}

void conf_save_string_value(gchar *key, gchar *value) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).

    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    // Save value
    if (!g_settings_set_string(settings, k, value)) {
        LOG_ERROR("Cannot save configuration key \"%s\" (%s).\n", key, value);
    }

    g_settings_apply(settings);

LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);
}

void conf_save_string_list(gchar *key, GList *list) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).

    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    // From GList to gchar *argv[].
    guint len = g_list_length(list);
    // Allocate argv[]
    gchar **argv = g_new(gchar*, len + 1 /*+1 for NULL*/);

    guint i = 0;
    GList *n = g_list_first(list);
    while (n) {
        argv[i++] = g_strdup(n->data);
        n = g_list_next(n);
    }
    argv[len] = NULL;

    // Save string list
    if (!g_settings_set_strv(settings, k, (const gchar* const*)argv)) {
        LOG_ERROR("Cannot save configuration key \"%s\" (value is a string list).\n", key);
    }

    // Free argv[]
    g_strfreev((gchar **)argv);

LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);
}


void conf_save_variant(gchar *key, GVariant *var) {
    gchar *child_path = NULL;
    gchar *child_key = NULL;

    // Get GSettings object. Handle also child_path and child_key (like "track/track-name")
    GSettings *settings = conf_get_settings_for_key(key, &child_path, &child_key);

    gchar *k = NULL;
    if (child_key)
        k = child_key; // settings object points to child_path (/apps/audio-recorder/some child_path/).
    else
        k = key; // settings object points to main path (/apps/audio-recorder/).

    // Check if the key is valid. Avoid crash.
    if (!conf_is_valid_key(settings, k)) {
        LOG_ERROR("Cannot find configuration key \"%s\". Run \"make install\" as sudo or root user.\n", key);
        goto LBL_1;
    }

    g_settings_set_value(settings, k, var);

LBL_1:
    // Free values
    g_free(child_path);
    g_free(child_key);
    g_object_unref(settings);
}
