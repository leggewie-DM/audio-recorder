/*
 * Copyright (c) Team audio-recorder.
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
#include "support.h"

gchar *get_data_directory() {
    // Get data directory
    // Normally: /usr/share/

    // Caller should g_free() this value
    return g_strdup(DATA_DIR);
}

gchar *get_package_data_directory() {
    // Get package data location
    // Normally: /usr/share/audio-recorder/

    // Caller should g_free() this value
    return g_strdup(PACKAGE_DATA_DIR);
}

gchar *get_image_directory() {
    // Get pixmaps directory
    // Normally: /usr/share/pixmaps/audio-recorder/

    // Caller should g_free() this value
    return g_strdup(PIXMAPS_DIR);
}

gchar *get_image_path(const gchar *image_name) {
    // Return path/image_name

    gchar *path = get_image_directory();
    gchar *fname = g_build_filename(path, image_name, NULL);
    g_free(path);

    // Caller should g_free() this value
    return fname;
}

gchar *get_program_name() {
    // Program name.
    // Translators: This is the name of this Audio Recorder program.
    gchar *name = g_strdup(_("Audio Recorder"));

    // Caller should g_free() this value.
    return name;
}
