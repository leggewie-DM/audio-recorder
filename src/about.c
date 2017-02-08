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
#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <assert.h>

#include "rec-window.h"
#include "support.h"

extern MainWindow g_win;

static GtkAboutDialog *g_dialog = NULL;

void about_show_installation_info_cb(GtkWidget *widget, GdkEvent *event, gpointer data);
gchar *about_get_installation_details();

// Contributors should edit these variables
const gchar *AUTHORS[] = {"Team Audio Recorder", NULL};

const gchar *DOCUMENTERS[] = {"", NULL};

const gchar *ARTISTS[] = {"Please see the website.", NULL};

const gchar *TRANSLATORS = "Please see:\n"
                           "https://translations.launchpad.net/audio-recorder\n"
                           "Thanks to all translators.";

const gchar *WEBSITE_URL = "https://launchpad.net/~audio-recorder";

gchar *about_program_name() {
    gchar *name = g_strdup_printf("Audio Recorder %s", PACKAGE_VERSION);

    // The caller should g_free() this value
    return name;
}

static gchar *load_license_text(gchar *file) {
    gchar *contents = NULL;
    gsize length;
    GError *error = NULL;

    if (!file) return NULL;

    gint ret = g_file_get_contents(file, &contents, &length, &error);
    if (error)
        g_error_free(error);

    // Did we find the license text?
    if (!ret || length <= 0) {
        g_free(contents);
        contents = g_strdup_printf("%s%s",
                                   _("This product is released under terms of GPL, GNU GENERAL PUBLIC LICENSE v3.\n"),
                                   _("Please see http://www.gnu.org/licenses/gpl-3.0.txt for more details."));
    }
    return contents;
}

void about_destroy_dialog() {
    if (GTK_IS_WIDGET(g_dialog)) {
        gtk_widget_destroy(GTK_WIDGET(g_dialog));
    }
    g_dialog = NULL;
}

void about_this_app() {
    // Show about-dialog

    // Destroy previous dialog (if open)
    about_destroy_dialog();

    g_dialog = (GtkAboutDialog*)gtk_about_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(g_dialog), GTK_WINDOW(g_win.window));

    // Show the translated application name
    gchar *package_name = get_program_name();
    gtk_about_dialog_set_program_name(g_dialog, package_name);
    g_free(package_name);

    gtk_about_dialog_set_version(g_dialog, PACKAGE_VERSION);
    gtk_about_dialog_set_copyright(g_dialog, "Team Audio Recorder");

    // Show the English name ("Audio Recorder")
    gtk_about_dialog_set_comments(g_dialog, PACKAGE_NAME);

    gtk_about_dialog_set_logo_icon_name(g_dialog, "audio-recorder");

    // Show license text
    gchar *package_data_dir = get_package_data_directory();
    gchar *filename = g_build_filename(package_data_dir, "COPYING", NULL);

    gchar *text = load_license_text(filename);

    gtk_about_dialog_set_license(g_dialog, text);
    g_free(package_data_dir);
    g_free(filename);

    gtk_about_dialog_set_website(g_dialog, WEBSITE_URL);
    gtk_about_dialog_set_website_label(g_dialog, WEBSITE_URL);

    gtk_about_dialog_set_authors(g_dialog, AUTHORS);
    gtk_about_dialog_set_documenters(g_dialog, DOCUMENTERS);

    gtk_about_dialog_set_translator_credits(g_dialog, TRANSLATORS);
    gtk_about_dialog_set_artists(g_dialog, ARTISTS);

    // Add an extra button for [Installation details]
    GtkWidget *hbutton_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_show(hbutton_box);

    // Get content area
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(g_dialog));
    gtk_container_add(GTK_CONTAINER(content), hbutton_box);

    // Add the button
    GtkWidget *button = gtk_button_new_with_label(_("Installation details"));
    gtk_widget_show(button);
    g_signal_connect((gpointer)button, "clicked", G_CALLBACK(about_show_installation_info_cb), NULL);
    gtk_container_add(GTK_CONTAINER(hbutton_box), button);

    gtk_dialog_run(GTK_DIALOG(g_dialog));

    if (GTK_IS_WIDGET(g_dialog)) {
        gtk_widget_destroy(GTK_WIDGET(g_dialog));
    }
    g_dialog = NULL;

    g_free(text);
}

void about_show_installation_info_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
    // Show dialog with [Installation details]

    GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Installation details"),
                        NULL,
                        GTK_DIALOG_DESTROY_WITH_PARENT,
                        _("OK"),
                        GTK_RESPONSE_ACCEPT,
                        NULL);

    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 550, 480);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *vbox0 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(vbox0);

    // Get content area
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(content), vbox0, TRUE, TRUE, 0);

    // Add scrolled window with text
    GtkWidget *scrolledwindow = gtk_scrolled_window_new (NULL, NULL);
    gtk_widget_show(scrolledwindow);
    gtk_box_pack_start(GTK_BOX(vbox0), scrolledwindow, TRUE, TRUE, 0);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW (scrolledwindow), GTK_SHADOW_IN);

    GtkWidget *text_field = gtk_text_view_new();
    gtk_widget_show(text_field);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_field));

    gchar *text = about_get_installation_details();

    gtk_text_buffer_set_text (buffer, text,  -1);
    gtk_container_add(GTK_CONTAINER(scrolledwindow), text_field);

    gtk_dialog_run(GTK_DIALOG(dialog));
    if (GTK_IS_WIDGET(dialog)) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }

    g_free(text);
}

gchar *about_get_installation_details() {
    // Current language settings
    const gchar* const *lang_args = g_get_language_names();
    guint i = 0;
    GString *s = g_string_new(NULL);
    while (lang_args && lang_args[i]) {
        g_string_append_printf(s, "%s, ", lang_args[i]);
        i++;
    }

    /*The array is owned by Glib. Do not free it.
      g_strfreev(lang_args);
    */

    // TODO:
    // Should we translate these?
    gchar *text = g_strdup_printf(
                      "Package name: %s\n"
                      "Package version: %s\n"
                      "Installation prefix: %s\n"
                      "Executable name: %s/%s\n"
                      "Pixmap location: %s\n"
                      "Desktop file: %s/%s/%s.desktop\n"
                      "Icon location: %s/%s\n"
                      "Package data location: %s/\n"
                      "Language locale directory: %s\n"
                      "System languages are: %s\n"
                      "LANG=%s\n"
                      "\n"
                      "Website for translations: %s\n"
                      "Bug reports: %s\n"
                      "\n"
                      "You can find other values in the GNOME's registry.\nStart dconf-editor and browse to /apps/audio-recorder/\n\n"
                      "You can reset the settings to default values with --reset or -r options:\n"
                      "$ audio-recorder --reset\n\n"
                      "For more options, see:\n"
                      "$ audio-recorder --help",
                      PACKAGE,
                      PACKAGE_VERSION,
                      PREFIX,
                      PACKAGE_BIN_DIR, PACKAGE,
                      PIXMAPS_DIR,
                      DATA_DIR, "applications", PACKAGE,
                      PACKAGE_DATA_DIR, "icons/hicolor/48x48/apps/",
                      PACKAGE_DATA_DIR,
                      PACKAGE_LOCALE_DIR,
                      s->str,
                      getenv("LANG"),
                      WEBSITE_URL,
                      PACKAGE_BUGREPORT);

    g_string_free(s, TRUE);

    return text;
}


