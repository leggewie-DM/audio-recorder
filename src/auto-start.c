/*
 * Copyright (c) Linux community.
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
#include <stdlib.h>
#include "auto-start.h"
#include "support.h"
#include "utility.h"
#include "dconf.h"
#include "log.h"
#include <glib.h>
#include <glib/gstdio.h>

#define AUTO_START_PATH ".config/autostart/"
#define AUTO_START_FILENAME "audio-recorder.desktop"

static gchar *get_autostart_filename();
static gchar *get_desktop_filename();
static void create_autostart_directory();
static gchar *autostart_get_default_content();
static void autostart_remove_file(gchar *autostart_file);

void autostart_set(gboolean _on) {
    // Re-create $HOME/.config/autostart/audio-recorder.desktop file.
    // Take a copy of /usr/share/applications/audio-recorder.desktop.
    // if _on == TRUE: Set the field "X-GNOME-Autostart-enabled" to "true".
    // if _on == FALSE: Remove autostart file from the system.

    GKeyFile *key_file = NULL;

    // autostart_file, normally: $HOME/.config/autostart/audio-recorder.desktop
    gchar *autostart_file = get_autostart_filename();

    // desktop_file: normally /usr/share/applications/audio-recorder.desktop (shown in menus, toolbars and desktop surface)
    gchar *desktop_file = get_desktop_filename();

    // Ref. bug #1312524. Some users have reported problems with setting the autostart option to OFF/NO.
    if (_on == FALSE) {
        // Delete autostart file and disable it entirely.
        autostart_remove_file(autostart_file);
        goto LBL_1;
    }

    // Activate autostart (_on == TRUE).

    // Ref: https://developer.gnome.org/glib/unstable/glib-Key-value-file-parser.html
    key_file = g_key_file_new();
    GError *error = NULL;
    g_key_file_load_from_file(key_file, desktop_file, G_KEY_FILE_KEEP_TRANSLATIONS, &error);
    if (error) {
        LOG_ERROR("Cannot read file %s. %s\n", desktop_file, error->message);

        g_error_free(error);

        // Get default content
        gchar *text = autostart_get_default_content();
        error = NULL;
        g_key_file_load_from_data(key_file, text, -1, G_KEY_FILE_KEEP_TRANSLATIONS, &error);
        g_free(text);
    }

    if (error) {
        LOG_ERROR("Cannot read file %s. %s\n", desktop_file, error->message);
        g_error_free(error);
        // Cannot continue
        goto LBL_1;
    }

    // Enable/disable auto start.
    // Set the value of "X-GNOME-Autostart-enabled" to TRUE.
    g_key_file_set_boolean(key_file, G_KEY_FILE_DESKTOP_GROUP, "X-GNOME-Autostart-enabled", _on);

    // Show either trayicon or window. Not both.
    gboolean show_icon = FALSE;
    conf_get_boolean_value("show-systray-icon", &show_icon);

    // Create exec line
    gchar *cmd = NULL;
    if (show_icon) {

        // Hide window, show trayicon (icon is switched on in the settings)
        cmd = g_strdup_printf("%s --show-window=0", PACKAGE);

    } else {

        // Show window, hide trayicon (icon is switched off in the settings)
        cmd = g_strdup_printf("%s --show-window=1", PACKAGE);
    }

    g_key_file_set_string(key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, cmd);
    g_free(cmd);

    // Get key_file as text
    gchar *text = g_key_file_to_data(key_file, NULL, NULL);

    // Make sure we have $HOME/.config/autostart/ directory
    create_autostart_directory();

    // Save to autostart_file
    error = NULL;
    save_file_content(autostart_file, text, &error);
    if (error) {
        LOG_ERROR("Cannot write to file %s. %s\n", autostart_file, error->message);
        g_error_free(error);
    }

    // Free the text
    g_free(text);

LBL_1:
    // Free values
    if (key_file) {
        g_key_file_free(key_file);
    }

    g_free(autostart_file);
    g_free(desktop_file);
}

static void autostart_remove_file(gchar *autostart_file) {
    // Delete autostart file from the system

    // Safety test
    if (g_file_test(autostart_file, G_FILE_TEST_IS_REGULAR)) {
        LOG_DEBUG("Removing autostart file:%s\n", autostart_file);

        // Delete
        g_remove(autostart_file);

        if (g_file_test(autostart_file, G_FILE_TEST_IS_REGULAR)) {
            LOG_ERROR("Cannot delete autostart file:%s. Is it write protected?", autostart_file);
        }
    }

}

gboolean autostart_get() {
    // Get current auto-start value.
    // Read $HOME/.config/autostart/audio-recorder.desktop file and return value of "X-GNOME-Autostart-enabled".

    // auto_start_file, normally: $HOME/.config/autostart/audio-recorder.desktop
    gchar *auto_start_file = get_autostart_filename();

    gboolean ret = TRUE;

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    g_key_file_load_from_file(key_file, auto_start_file, G_KEY_FILE_KEEP_TRANSLATIONS, &error);
    if (error) {
        g_error_free(error);
        ret = FALSE;
        goto LBL_1;
    }

    error = NULL;
    ret = g_key_file_get_boolean(key_file, G_KEY_FILE_DESKTOP_GROUP, "X-GNOME-Autostart-enabled", &error);

    if (error) {
        g_error_free(error);
        ret = FALSE;
    }

LBL_1:
    // Free values
    g_key_file_free(key_file);
    g_free(auto_start_file);

    return ret;
}

static gchar *get_desktop_filename() {
    // Return path+name for the .desktop file.
    // Normally: /usr/share/applications/audio-recorder.desktop
    gchar *data_dir = get_data_directory();
    gchar *desktop_file = g_strdup_printf("%s/applications/%s", data_dir, AUTO_START_FILENAME);
    g_free(data_dir);

    // Caller should g_free() this value
    return desktop_file;
}

static gchar *get_autostart_filename() {
    // Return path+name for the auto start file.
    // Get $HOME
    gchar *home = get_home_dir();
    gchar *filename = g_build_filename(home, AUTO_START_PATH, AUTO_START_FILENAME, NULL);
    g_free(home);

    // Caller should g_free() this value
    return filename;
}

static void create_autostart_directory() {
    // Create "$HOME/.config/autostart/" directory.
    // It is normally created by "Autostart Applications" dialog, but it's initially missing in new GNOME-installations.
    gchar *home = get_home_dir();
    gchar *path = g_build_filename(home, AUTO_START_PATH, NULL);

    // Create autostart directory
    if (g_mkdir_with_parents(path, 0700) == -1) {
        LOG_ERROR("Cannot create path \"%s\"\n", path);
    }
    g_free(home);
    g_free(path);
}

static gchar *autostart_get_default_content() {
    // Default audio-recorder.desktop content

    return g_strdup("\n"
                    "[Desktop Entry]\n"
                    "GenericName=Audio Recorder\n"
                    "Type=Application\n"
                    "Exec=audio-recorder --show-window=1\n"
                    "Hidden=false\n"
                    "NoDisplay=false\n"
                    "Categories=GNOME;AudioVideo;Recorder\n"
                    "X-GNOME-Autostart-enabled=false\n"
                    "Name=Audio Recorder\n"
                    "Name[en_US]=Audio Recorder\n"
                    "Comment=Audio recorder application\n"
                    "Comment[en_US]=Easy-to-use audio recording tool\n");
}


