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
#include <stdlib.h> // getenv()
#include "help.h"
#include "support.h"
#include "utility.h"
#include "log.h"

static gchar *help_find_translated_page(gchar *webpage);

void help_show_page(gchar *webpage) {
    // Find translated page if possible
    gchar *filename = help_find_translated_page(webpage);

    LOG_DEBUG("Showing webpage <%s>.\n", filename);

    help_start_tool(filename);

    g_free(filename);
}

static GList *help_get_language_codes() {
    GList *lst = NULL;

    // Load all languages supported by this Linux
    const gchar* const *lang_args = g_get_language_names();
    guint i = 0;
    while (lang_args && lang_args[i]) {
        // Typical language values are: "nb_NO", "en_US", "de", "en"
        lst = g_list_append(lst, g_strdup(lang_args[i]));
        i++;
    }

    /* The lang_args array is owned by Glib. Do not free it.
       g_strfreev(lang_args);
    */

    // Add also value from the LANG variable
    // $ echo $LANG
    // nb_NO.utf8
    //
    gchar *lang = g_strdup(getenv("LANG"));
    if (str_length(lang, 128) > 0) {
        // Like: "nb_NO.utf8"
        lst = g_list_prepend(lst, g_strdup(lang));

        // Remove the utf8 part (or whatever), and "nb_NO" remains
        gchar *p = g_strrstr(lang, ".");
        if (p) {
            // This removes the "."
            *p = '\0';
            lst = g_list_prepend(lst, g_strdup(lang));
        }
    }

    g_free(lang);

    return lst;
}

static gchar *help_find_translated_page(gchar *webpage) {
    // Get data directory. Normally /usr/shared/audio-recorder/
    gchar *data_dir = get_package_data_directory();

    // Get localized language codes
    GList *lst = help_get_language_codes();

    // Try to find a localized, translated HTML file (for this given webpage).
    gchar *filepath = NULL;
    gchar *filebase = NULL;
    gchar *fileext = NULL;
    split_filename3(webpage, &filepath,  &filebase, &fileext);

    gchar *filename = NULL;
    GList *item = g_list_first(lst);
    while (item) {
        // Typical language values are: "nb_NO", "en_US", "de", "en"
        // So for example, we try to find a "/usr/shared/rec-applet/webpage-nb_NO.html" (Norwegian Bokmål) file.

        // Build filename
        filename = g_strdup_printf("%s/%s-%s.%s", data_dir, filebase, (gchar*)item->data, fileext);

        // Does it exist?
        if (g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
            break;
        }

        g_free(filename);
        filename = NULL;

        item = g_list_next(item);
    }

    // Got valid file?
    if (!filename) {
        // Show the original path + webpage (in english)
        filename = g_build_filename(data_dir, webpage, NULL);
    }

    g_free(data_dir);
    g_free(filepath);
    g_free(filebase);
    g_free(fileext);

    // Free the lst
    g_list_foreach(lst, (GFunc)g_free, NULL);
    g_list_free(lst);
    lst = NULL;

    // The caller should g_free() this value.
    return filename;
}

void help_start_tool(gchar *webpage) {
    // Open web browser and display the webpage
    GError *error = NULL;
    run_tool_for_file(webpage, "sensible-browser", &error);

    if (error)  {
        // Translator: This error message is shown in a MessageBox. Very rare error.
        gchar *msg = g_strdup_printf(_("Cannot start the internet browser.\nPlease open the help file %s manually."), webpage);
        messagebox_error(msg, NULL);
        g_free(msg);
        g_error_free(error);
    }
}


