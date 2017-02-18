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
#include <glib.h>
#include <glib/gstdio.h>
#include <gdk/gdk.h>
#include <string.h>
#include <math.h> // round()
#include <locale.h>

#include <gst/pbutils/pbutils.h>

#include "levelbar.h" // Level bar widget
#include "support.h"
#include "audio-sources.h"
#include "rec-window.h"
#include "dbus-server.h"
#include "audio-sources.h"
#include "media-profiles.h"
#include "timer.h"
#include "rec-manager.h"
#include "utility.h"
#include "dconf.h"
#include "log.h"
#include "settings.h"
#include "auto-start.h"
#include "about.h"
#include "help.h"

// Main window and all its widgets.
MainWindow g_win;

// Command line options, arguments.
static gint g_version_info = -1;    // Print version info then exit
static gint g_show_window = -1;     // Show/hide window
static gint g_show_tray_icon = -1;  // Show/hide tray icon
static gint g_reset_settings = -1;  // Reset all settings then restart
static gint g_debug_threshold = -1; // Output RMS threshold value?
static gchar *g_command_arg = NULL; // Argument of --command (-c)

// Default timer text
static const gchar *g_def_timer_text = ""
                                       "#start at 02:20 pm\n"
                                       "#stop at 15:00\n"
                                       "#stop after 1h 30min\n"
                                       "stop if silence 4s 20%\n"
                                       "#stop if silence | 100MB\n"
                                       "#start if voice 0.3\n"
                                       "#start if voice 30%";

static GOptionEntry option_entries[] = {
    // Translators: This is a command line option.
    {
        "version", 'v', 0, G_OPTION_ARG_NONE, &g_version_info,
        N_("Print program name and version."), NULL
    },

    // Translators: This is a command line option.
    {
        "show-window", 'w', 0, G_OPTION_ARG_INT, &g_show_window,
        N_("Show application window at startup (0=hide main window, 1=force display of main window)."), NULL
    },

    // Translators: This is a command line option.
    {
        "show-icon", 'i', 0, G_OPTION_ARG_INT, &g_show_tray_icon,
        N_("Show icon on the system tray (0=hide icon, 1=force display of icon)."), NULL
    },

    // Translators: This is a command line option.
    {
        "reset", 'r', 0, G_OPTION_ARG_NONE, &g_reset_settings,
        N_("Reset all settings and restart audio-recorder."), NULL
    },

    // Translators: This is a command line option.
    // Output audio level values in a terminal window.
    // This makes it easier to set correct threshold (dB or %) value in the Timer.
    {
        "debug-signal", 'd', 0, G_OPTION_ARG_NONE, &g_debug_threshold,
        N_("List signal level values in a terminal window."), NULL
    },

    // Translators: This is a command line option. Notice:
    // Do not translate the "status,start,stop,pause,show and quit" words.
    // Control the recorder from command line with the --command option.
    // --command=status returns one of: "not running" | "on" | "off" | "paused".
    {
        "command", 'c', 0, G_OPTION_ARG_STRING, &g_command_arg,
        N_("Send a command to the recorder. Valid commands are; status,start,stop,pause,show,hide and quit. "
        "The status argument returns; 'not running','on','off' or 'paused'."), NULL
    },
    { NULL },
};

static gboolean win_delete_cb(GtkWidget *widget, GdkEvent *event, gpointer data);

static gpointer send_client_request(gchar *argv[], gpointer data);
static void contact_existing_instance(gchar *argv[]);
static gboolean ar_is_running();

static void combo_select_string(GtkComboBox *combo, gchar *str, gint sel_row);

void win_update_gui() {
    // Get recording state
    gint state = -1;
    gint pending = -1;

    rec_manager_get_state(&state, &pending);

    if (state == GST_STATE_PAUSED && pending == GST_STATE_NULL) {
        state = GST_STATE_NULL;
    }

    gchar *image_file = NULL;
    gchar *label = NULL;
    gboolean active = FALSE;

    switch (state) {
    case GST_STATE_PLAYING:
        // Recording is on
        image_file = (gchar*)get_image_path("audio-recorder-on-dot.svg");

        // Translators: This is a button label, also used in the menu.
        label = _("Stop recording");
        active = TRUE;
        break;

    case GST_STATE_PAUSED:
        // Paused
        image_file = (gchar*)get_image_path("audio-recorder-paused-dot.svg");

        // Translators: This is a button label, also used in the menu.
        label = _("Continue recording");
        active = TRUE;
        break;

    case GST_STATE_READY:
    default:
        // Stopped/off
        image_file = (gchar*)get_image_path("audio-recorder-off-dot.svg");

        // Translators: This is a button label, also used in the menu.
        label = _("Start recording");
        active = FALSE;
    }

    // Set label
    if (GTK_IS_BUTTON(g_win.recorder_button)) {
        gtk_button_set_label(GTK_BUTTON(g_win.recorder_button), label);

        // Set image (a small color dot on the button)
        GtkWidget *image = gtk_image_new_from_file(image_file);
        gtk_button_set_image(GTK_BUTTON(g_win.recorder_button), image);
    }

    g_free(image_file);

    // Reset amplitude/level bar
    if (!active) {
        win_update_level_bar(0.0, 0.0);
    }

    // Update systray icon and its menu selections
    systray_set_menu_items1(state);
}

#if 0
void win_update_gui() {
    // Get recording state
    gint state = -1;
    gint pending = -1;

    rec_manager_get_state(&state, &pending);

    if (state == GST_STATE_PAUSED && pending == GST_STATE_NULL) {
        state = GST_STATE_NULL;
    }

    const gchar *icon_name = NULL;
    gchar *label = NULL;
    gboolean active = FALSE;

    switch (state) {
    case GST_STATE_PLAYING:
        // Recording is on
        icon_name = "audio-recorder-on";

        // Translators: This is a button label, also used in the menu.
        label = _("Stop recording");
        active = TRUE;
        break;

    case GST_STATE_PAUSED:
        // Paused
        icon_name = "audio-recorder-paused";

        // Translators: This is a button label, also used in the menu.
        label = _("Continue recording");
        active = TRUE;
        break;

    case GST_STATE_READY:
    default:
        // Stopped/off
        icon_name = "audio-recorder-off";

        // Translators: This is a button label, also used in the menu.
        label = _("Start recording");
        active = FALSE;
    }

    // Set label
    if (GTK_IS_BUTTON(g_win.recorder_button)) {
        gtk_button_set_label(GTK_BUTTON(g_win.recorder_button), label);

        GdkPixbuf *icon_pixbuf = load_icon_pixbuf((gchar*)icon_name);
        GtkWidget *image = NULL;

        if (GDK_IS_PIXBUF(icon_pixbuf)) {
            GdkPixbuf *tmp = gdk_pixbuf_scale_simple(icon_pixbuf, 14, 14, GDK_INTERP_NEAREST);
            g_object_unref(icon_pixbuf);
            icon_pixbuf = tmp;

            image = gtk_image_new_from_pixbuf(icon_pixbuf);
        }

        if (GTK_IS_WIDGET(image)) {
            gtk_button_set_image(GTK_BUTTON(g_win.recorder_button), image);
            g_object_unref(icon_pixbuf);
        }

    }

    // Reset amplitude/level bar
    if (!active) {
        win_update_level_bar(0.0, 0.0);
    }

    // Update systray icon and its menu selections
    systray_set_menu_items1(state);
}

#endif

void win_set_filename(gchar *filename) {
    // Remove path from filename
    gchar *path = NULL;
    gchar *fname = NULL;
    split_filename2(filename, &path, &fname);

    // Show the filename
    if (GTK_IS_WIDGET(g_win.filename)) {
        gtk_entry_set_text(GTK_ENTRY(g_win.filename), (fname ? fname : ""));
    }

    g_free(path);
    g_free(fname);
}

void win_update_level_bar(gdouble norm_rms, gdouble norm_peak) {
    // Set pulse on the levelbar

    if (!IS_LEVEL_BAR(g_win.level_bar)) return;

    // Show either RMS or peak-value on the levelbar.
    // Notice: This value has no GUI-setting. User must change it in the dconf-editor.
    level_bar_set_fraction(LEVEL_BAR(g_win.level_bar),
                               (g_win.pulse_type == PULSE_RMS ? norm_rms : norm_peak));
}

void win_set_time_label(gchar *time_txt) {
    // Set time label ##:##:##
    if (GTK_IS_WIDGET(g_win.time_label)) {
        gtk_label_set_text(GTK_LABEL(g_win.time_label), time_txt);
    }
}

void win_set_size_label(gchar *size_txt) {
    // Set label for filesize (recorded filesize)
    if (GTK_IS_WIDGET(g_win.time_label)) {
        gtk_label_set_text(GTK_LABEL(g_win.size_label), size_txt);
    }
}

void win_set_error_text(gchar *error_txt) {
    // Display error message (in GtkLabel)

    // Cut long messages
    if (str_length(error_txt, 1024) > 256) {
        *(error_txt + 256) = '\0';
    }

    // Remove last "\n" (it adds an empty line to the label)
    if (error_txt) {
        gchar *p = g_strrstr(error_txt, "\n");
        if (p) {
            *p = '\0';
        }
    }

    // Get the label widget
    GtkWidget *label = NULL;
    if (GTK_IS_WIDGET(g_win.error_box)) {
        label = (GtkWidget*)g_object_get_data(G_OBJECT(g_win.error_box), "label-widget");
    }

    if (!GTK_IS_WIDGET(label)) return;

    if (str_length(error_txt, 1024) < 1) {
        // Hide the error label
        gtk_label_set_text(GTK_LABEL(label), "");
        gtk_widget_hide(g_win.error_box);

    } else {
        // Set and show the error label
        gtk_label_set_text(GTK_LABEL(label), error_txt);
        gtk_widget_show(g_win.error_box);
    }
}

void win_flip_recording_cb(GtkButton *button, gpointer user_data) {
    // Start, continue or stop recording
    rec_manager_flip_recording();
}

gboolean win_recording_button_cb(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    // Click on g_win.recorder_button

    if (event->button == 3) {
        // Show right click menu
        win_show_right_click_menu();
    }
    return FALSE;
}

void win_show_right_click_menu() {
    // Show a popup menu. Borrow menu from the systray module.
    GtkWidget *popup_menu = systray_create_menu(FALSE/*not for systray*/);

    // GTK+ version is >= 3.22
    #if GTK_CHECK_VERSION(3, 22, 0)
        // Since: 3.22
        gtk_menu_popup_at_pointer(GTK_MENU(popup_menu), NULL);
    #else
        gtk_menu_popup(GTK_MENU(popup_menu), NULL, NULL,  NULL, NULL, -1, gtk_get_current_event_time());
    #endif


}

static void win_expander_click_cb(GObject *object, GParamSpec *param_spec, gpointer userdata) {
    // Handle click on GUI Exapnder>
    GtkExpander *expander = GTK_EXPANDER(object);
    gboolean expanded = gtk_expander_get_expanded(expander);
    gchar *expander_name = (gchar*)userdata;

    // Save the expanded status
    conf_save_boolean_value(expander_name, expanded);

    if (!g_strcmp0(expander_name, "timer-expanded")) {

        // Save the timer text
        win_timer_save_text_cb(g_win.timer_save_button, NULL);
    } else if (!g_strcmp0(expander_name, "settings-expanded")) {

        // Show/hide [Additional settings] button
        if (expanded)
            gtk_widget_show(g_win.settings_button);
        else
            gtk_widget_hide(g_win.settings_button);
    }
}

void win_add_to_changed_cb(GtkToggleButton *togglebutton, gpointer user_data) {
    // Click on g_win.add_to_file checkbox

    // It is active?
    gboolean active = gtk_toggle_button_get_active(togglebutton);

    // Save in GConf registry
    conf_save_boolean_value("append-to-file", active);

    // No reason to let the timer know about this.
    // Commented out:
    // timer_settings_changed();
}

void win_timer_active_cb(GtkToggleButton *togglebutton, gpointer user_data) {
    // Click on g_win.timer_active checkbox

    // Timer is active?
    gboolean active = gtk_toggle_button_get_active(togglebutton);

    if (GTK_IS_WIDGET(g_win.timer_text)) {
        // Always keep the edit field enabled.
        // Commented out:
        // gtk_widget_set_sensitive(g_win.timer_text, active);
    }

    // Save in GConf registry
    conf_save_boolean_value("timer-active", active);

    // Let the timer know that the settings have been altered
    timer_settings_changed();
}


void win_refresh_profile_list() {
    // Save media-profile (id)

    gchar *id = profiles_get_selected_id(g_win.media_format);

    profiles_get_data(g_win.media_format);

    combo_select_string(GTK_COMBO_BOX(g_win.media_format), id, 0);

    g_free(id);
}

static void combo_select_string(GtkComboBox *combo, gchar *str, gint sel_row) {
    // Find str in the GtkComboBox (combo).
    // If str is not found then select sel_row line.
    GtkTreeModel *model = gtk_combo_box_get_model(combo);
    GtkTreeIter iter;

    // Loop for all rows
    gint ret = gtk_tree_model_get_iter_first(model, &iter);
    while (ret && str) {
        gchar *val = NULL;
        gtk_tree_model_get(model, &iter, 0, &val, -1);

        // Strings match?
        if (g_strcmp0(val, str) == 0) {
            // Select this item
            gtk_combo_box_set_active_iter(combo, &iter);
            g_free(val);
            // And quit
            return;
        }
        g_free(val);
        ret = gtk_tree_model_iter_next(model, &iter);
    }

    // Item was not found. Select sel_row.
    gtk_combo_box_set_active(combo, sel_row);
}

void window_show_timer_help() {
    // Show help file
    help_show_page("timer-syntax.html");
}

gboolean win_window_is_visible() {
    // Window is visible?
    return gtk_widget_get_visible(g_win.window);
}

GdkWindowState win_get_window_state() {
    // Return current window state
    return g_win.state;
}

void win_keep_on_top(gboolean on_top) {

    // Keep or unkeep on top of the desktop.
    // This needs quit and restart to work properly!.
    gtk_window_set_keep_above(GTK_WINDOW(g_win.window), on_top);

    if (on_top && gtk_widget_get_realized(g_win.window)) {
        win_show_window(TRUE);
    }
}

void win_show_window(gboolean show) {
    // Close about-dialog if open
    about_destroy_dialog();

    // Show or hide the main window
    if (!GTK_IS_WINDOW(g_win.window)) return;

    if (show) {
        // Show window. Also bring up if window is minimized/iconified.

        gboolean on_top = FALSE;
        conf_get_boolean_value("keep-on-top", &on_top); // just a small trick!

        //gtk_window_set_keep_above(GTK_WINDOW(g_win.window), TRUE);

        gtk_widget_show(g_win.window);
        gtk_window_deiconify(GTK_WINDOW(g_win.window));
        gtk_window_present(GTK_WINDOW(g_win.window));

        //gtk_window_set_keep_above(GTK_WINDOW(g_win.window), on_top);

    } else {
        // Close settings-dialog if open
        win_settings_destroy_dialog();

        // Hide window
        gtk_widget_hide(g_win.window);
    }

    // Update menu on the systray
    systray_set_menu_items2(show);
}

void test_0_cb(GtkWidget *widget, gpointer data) {

}

void win_settings_cb(GtkWidget *widget, gpointer data) {
    // Show the [Additional settings] dialog
    win_settings_show_dialog(GTK_WINDOW(g_win.window));
}

void win_refresh_device_list() {
    // Refresh/reload the list of audio devices and Media Players, Skype

    // Reset error message
    win_set_error_text(NULL);

    // Get current selection
    gchar *dev_name;
    gchar *dev_id;
    gint dev_type;
    audio_sources_combo_get_values(g_win.audio_device, &dev_name, &dev_id, &dev_type);

    // Load audio sources (device ids and names) from Gstreamer and fill the combobox.
    // Add also Media Players, Skype (if installed) to the list. These can control the recording via DBus.
    audio_source_fill_combo(g_win.audio_device);

    // Stop recording if dev_id removed from the system
    DeviceItem *rec = audio_sources_find_id(dev_id);
    if (dev_id && rec == NULL) {

        gint state = -1;
        gint pending = -1;

        rec_manager_get_state(&state, &pending);

        if (state != GST_STATE_NULL) {
            // Stop recording
            rec_manager_stop_recording();
        }
    }

    // Set/show the current value
    audio_sources_combo_set_id(g_win.audio_device, dev_id);

    // Free the values
    g_free(dev_name);
    g_free(dev_id);
}

void win_set_device_id() {
    // Set the value of g_win.audio_device combo
    gchar *str_value = NULL;
    conf_get_string_value("audio-device-id", &str_value);
    audio_sources_combo_set_id(g_win.audio_device, str_value);
    g_free(str_value);
}

void win_device_list_changed_cb(GtkWidget *widget, gpointer data) {
    // Selection in the device combo

    // Reset error message (message label)
    win_set_error_text(NULL);

    // Get values
    gchar *dev_name;
    gchar *dev_id;
    gint dev_type;
    audio_sources_combo_get_values(g_win.audio_device, &dev_name, &dev_id, &dev_type);

    LOG_DEBUG("-----------------------\n");
    LOG_DEBUG("Selected device or media player, etc. (g_win.audio_device):%s\n", dev_id);
    LOG_DEBUG("name:%s\n", dev_name);
    LOG_DEBUG("type:%d\n", dev_type);
    LOG_DEBUG("-----------------------\n");

    // Save id
    conf_save_string_value("audio-device-id", check_null(dev_id));

    // Save name
    conf_save_string_value("audio-device-name", dev_name);

    // Save type
    conf_save_int_value("audio-device-type", dev_type);

    // Tell audio_sources that the device has changed.
    // This will disconnect/re-connect all DBus signals to Media Players, Skype.
    audio_sources_device_changed(dev_id);

    // Let timer know that the settings have been altered.
    timer_settings_changed();

    // Free the values
    g_free(dev_name);
    g_free(dev_id);
}

void win_audio_format_changed_cb(GtkWidget *widget, gpointer data) {

    // Reset error message (message label)
    win_set_error_text(NULL);

    // Save media-profile (id)
    gchar *id = profiles_get_selected_id(widget/*=g_win.media_format*/);

    if (id == NULL) return;

    LOG_DEBUG("Selected audio format (g_win.media_format):%s\n", id);

    conf_save_string_value("media-format", id);

// Enable this if you want check the Gstreamer-plugin for selected media type.
// profiles_test_plugin(...) function will also install any missing plugin package.
#if 1
    // Test if the appropriate GStreamer plugin has been installed.
    gchar *err_msg = NULL;
    gboolean ok = profiles_test_plugin(id, &err_msg);

    if (!ok) {
        // Missing Gstreamer plugin.

        // Display error message in the GUI (red label)
        rec_manager_set_error_text(err_msg);

        LOG_ERROR(err_msg);

        g_free(err_msg);

    }
#endif

    g_free(id);
}

void win_timer_text_changed_cb(GtkTextBuffer *textbuffer, gpointer user_data) {
    // Timer text changed. Show [Save] button.
    gtk_widget_show(g_win.timer_save_button);
}

void win_timer_text_insert_cb(GtkTextBuffer *textbuffer, GtkTextIter *location, gchar *text, gint len, gpointer user_data) {
#define MAX_TEXT_LEN 3500

    // Get buffer start and end
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(textbuffer, &start, &end);

    // Get approx. character count
    gint text_len = gtk_text_iter_get_offset(&end);

    if (text_len > MAX_TEXT_LEN) {
        // Buffer becomes too large. Stop this insert!
        g_signal_stop_emission_by_name(textbuffer, "insert_text");
    }
}

void win_timer_save_text_cb(GtkWidget *widget, gpointer user_data) {
    // Save timer text
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_win.timer_text));

    // Read timer text
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);

    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    // Save it
    conf_save_string_value("timer-text", text);

    g_free(text);

    // Hide the [Save] button
    if (gtk_widget_get_visible(widget)) {
        gtk_widget_hide(widget);
    }

    // Let the timer know that the settings have been altered
    timer_settings_changed();
}

void win_close_button_cb(GtkButton *button, gpointer user_data) {
    // Click on [Close] button

    gboolean force_quit = (gboolean)GPOINTER_TO_INT(user_data);

    // Has icon on the systen tray?
    if (!force_quit && systray_icon_is_installed()) {
        // Hide this window
        win_show_window(FALSE);
        return;
    }

    // Quit application, destroy this window.
    about_destroy_dialog();

    win_delete_cb(g_win.window, NULL, NULL);

    gtk_main_quit();
    gtk_widget_destroy(g_win.window);

    g_win.window = NULL;
}

void win_quit_application() {
    // Quit this application
    win_close_button_cb(NULL, (gpointer)TRUE);
}

gboolean win_close_button_press_cb(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    // Right click on [Close] button?
    if (event->button == 3) {
        // Show a small "Quit" menu
        GtkWidget *menu = gtk_menu_new();
        // Translators: This is a small right-click-menu on the [Close] button.
        GtkWidget *menu_item = gtk_menu_item_new_with_label(_("Quit"));
        gtk_widget_show(menu_item);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

        g_signal_connect(menu_item, "activate", G_CALLBACK(win_close_button_cb), GINT_TO_POINTER(TRUE));

        // GTK+ version is >= 3.22
        #if GTK_CHECK_VERSION(3, 22, 0)
            // Since: 3.22
            gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
        #else
            gtk_menu_popup(GTK_MENU(menu), NULL, NULL,  NULL, NULL, -1, gtk_get_current_event_time());
        #endif

        return TRUE;
    }
    return FALSE;
}

gboolean win_delete_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
    // Called prior exit.

    LOG_DEBUG("win_delete_cb() called.\n");

    dbus_service_module_exit();

    systray_module_exit();

    rec_manager_exit();

    audio_sources_exit();

    media_profiles_exit();

    timer_module_exit();

    // Allow exit.
    return FALSE;
}

void win_destroy_cb(GtkWidget *widget, gpointer data) {
    // Quit/exit this application
    gtk_main_quit ();
}

static gboolean win_state_event_cb(GtkWidget *widget, GdkEventWindowState *event, gpointer data) {
    // Save window's state.
    // We are interested in GDK_WINDOW_STATE_ICONIFIED and 0 (normal state).
    g_win.state = event->new_window_state;

    if (g_win.state == 0) {
        // Enable/disbale menu items (on systray icon)
        systray_set_menu_items2(TRUE);

    } else if (g_win.state == GDK_WINDOW_STATE_ICONIFIED) {
        // Enable/disbale menu items (on systray icon)
        systray_set_menu_items2(FALSE);
    }

    return TRUE;
}

gboolean win_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    // Key press event on the main window

    if (event->state & GDK_CONTROL_MASK) {
        if (event->keyval == 's' || event->keyval == 'S') {
            // Control + S saves timer text
            win_timer_save_text_cb(g_win.timer_save_button, NULL);

        } else if (event->keyval == 'x' || event->keyval == 'X') {
            // Control + X stops recording
            rec_manager_stop_recording();

        } else if (event->keyval == 'p' || event->keyval == 'P') {
            // Control + P pauses recording
            rec_manager_pause_recording();

        } else if (event->keyval == 'r' || event->keyval == 'R') {
            // Control + R starts recording
            rec_manager_start_recording();
        }
    }

    // Pass this event further
    return FALSE;
}

void win_show_settings_dialog() {
    win_settings_show_dialog(GTK_WINDOW(g_win.window));
}

void win_level_bar_clicked(GtkWidget *widget, GdkEvent *event, gpointer data) {
    // User clicked on the level bar.
    // Set BAR_VALUE or BAR_SHAPE. See levelbar.h.
    GdkEventButton *ev = (GdkEventButton*)event;

    if (ev->button == 1) {
        // LEFT mouse button

        // Get from DConf
        gint bar_value = (gint)VALUE_NONE;
        conf_get_int_value("level-bar-value", &bar_value);

        // Calc next enum value
        bar_value += 1;
        if (bar_value < (gint)VALUE_NONE/*first enum*/ || bar_value > (gint)VALUE_PERCENT/*last enum*/) {
            bar_value = (gint)VALUE_NONE;
        }

        // Update GUI
        level_bar_set_value_type(LEVEL_BAR(g_win.level_bar), bar_value);

        // Save in DConf
        conf_save_int_value("level-bar-value", bar_value);

    } else if (ev->button == 3) {
        // RIGHT mouse button

        // Get from DConf
        gint bar_shape = (gint)SHAPE_CIRCLE;
        conf_get_int_value("level-bar-shape", &bar_shape);

        // Calc next enum value
        bar_shape += 1;
        if (bar_shape < (gint)SHAPE_LEVELBAR/*first enum*/ || bar_shape > (gint)SHAPE_CIRCLE/*last enum*/) {
            bar_shape = (gint)SHAPE_LEVELBAR;
        }

        // Update GUI
        level_bar_set_shape(LEVEL_BAR(g_win.level_bar), bar_shape);

        // Save in DConf
        conf_save_int_value("level-bar-shape", bar_shape);
    }

}

void reset_all_settings() {
    // Reset all settings

    // Run:
    // $ gsettings reset-recursively org.gnome.audio-recorder
    gchar *args = g_strdup_printf("reset-recursively %s", APPLICATION_SETTINGS_SCHEMA);
    run_simple_command("gsettings", args);
    g_free(args);

    // Just in case the gsettings command failed
    conf_save_boolean_value("started-first-time", TRUE);
    conf_save_string_value("track/last-file-name", "");
    conf_save_boolean_value("append-to-file", FALSE);
    conf_save_boolean_value("timer-expanded", FALSE);
    conf_save_boolean_value("timer-active", FALSE);
    conf_save_string_list("players/saved-player-list", NULL);

    // Clear profiles. Save empty array []
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ssss)"));
    GVariant *variant = g_variant_new("a(ssss)", builder);
    conf_save_variant("saved-profiles", variant);
    g_variant_builder_unref(builder);

    // Flush Gsettings (write changes to disk)
    conf_flush_settings();

}

void win_create_window() {
    // Create main window and all its widgets
    g_win.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_default_size(GTK_WINDOW(g_win.window), PREF_WINDOW_WIDTH, -1);
    gtk_window_set_position(GTK_WINDOW(g_win.window),  GTK_WIN_POS_MOUSE);

    // Show on all desktops
    gtk_window_stick(GTK_WINDOW(g_win.window));

    gtk_window_set_default_icon_name("audio-recorder");

    gchar *prog_name = get_program_name();
    gtk_window_set_title(GTK_WINDOW(g_win.window), prog_name);
    g_free(prog_name);

    // Set resizable to FALSE.
    // Note: This makes the window to shrink when GtkExpanders are shrunk/reduced/contracted.
    gtk_window_set_resizable(GTK_WINDOW(g_win.window), FALSE);

    g_signal_connect(g_win.window, "delete-event", G_CALLBACK(win_delete_cb), NULL);
    g_signal_connect(g_win.window, "destroy", G_CALLBACK(win_destroy_cb), NULL);
    g_signal_connect(g_win.window, "window-state-event", G_CALLBACK (win_state_event_cb), NULL);

    // Actions on Control + S/R/P/X keys
    g_signal_connect(g_win.window, "key-press-event", G_CALLBACK(win_key_press_cb), NULL);


    gboolean bool_value = FALSE;

    // Keep/unkeep window always on top?
    conf_get_boolean_value("keep-on-top", &bool_value);
    win_keep_on_top(bool_value);

    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_show(frame);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(g_win.window), frame);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 7);

    GtkWidget *vbox0 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_show(vbox0);
    gtk_container_add(GTK_CONTAINER(frame), vbox0);

#ifdef APP_HAS_MENU
    // Create menubar.
    // EDIT: I have removed the menubar.
    GtkWidget *menubar = win_create_menubar();
    gtk_widget_show(menubar);
    gtk_box_pack_start(GTK_BOX(vbox0), menubar, FALSE, FALSE, 0);
#endif

    GtkWidget *vbox1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_show(vbox1);
    gtk_box_pack_start(GTK_BOX(vbox0), vbox1, FALSE, TRUE, 0);

    GtkWidget *hbox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_show(hbox0);
    gtk_box_pack_start(GTK_BOX(vbox1), hbox0, TRUE, TRUE, 0);

    // Get saved values
    gchar *last_file_name = NULL; // Last saved filename
    conf_get_string_value("track/last-file-name", &last_file_name);

    gboolean append_to_file = FALSE;
    conf_get_boolean_value("append-to-file", &append_to_file);

    // [Start/Stop/Continue recording] button
    g_win.recorder_button = gtk_button_new_with_mnemonic("");
    gtk_widget_show(g_win.recorder_button);
    gtk_box_pack_start(GTK_BOX (hbox0), g_win.recorder_button, FALSE, FALSE, 0);
    g_signal_connect(g_win.recorder_button, "clicked", G_CALLBACK(win_flip_recording_cb), NULL);
    g_signal_connect(g_win.recorder_button, "button-press-event",G_CALLBACK(win_recording_button_cb), NULL);
    gtk_button_set_use_underline(GTK_BUTTON(g_win.recorder_button), TRUE);
    gtk_button_set_always_show_image(GTK_BUTTON(g_win.recorder_button), TRUE);

    // Time label
    g_win.time_label = gtk_label_new("00:00:00");
    gtk_widget_show(g_win.time_label);
    gtk_box_pack_start(GTK_BOX(hbox0), g_win.time_label, FALSE, FALSE, 2);
    gtk_widget_set_sensitive(g_win.time_label, FALSE);

    // Label for filesize
    g_win.size_label = gtk_label_new("0.0 KB");
    gtk_widget_show(g_win.size_label);
    gtk_box_pack_start(GTK_BOX(hbox0), g_win.size_label, FALSE, FALSE, 7);
    gtk_widget_set_sensitive(g_win.size_label, FALSE);

    // Show current file size
    if (g_file_test(last_file_name, G_FILE_TEST_EXISTS)) {
        // Ref: https://developer.gnome.org/glib/2.34/glib-File-Utilities.html#g-stat
        GStatBuf fstat;
        if (!g_stat(last_file_name, &fstat)) {
            gchar *size_txt = format_file_size(fstat.st_size);
            gtk_label_set_text(GTK_LABEL(g_win.size_label), size_txt);
            g_free(size_txt);
        }
    }

    // Levelbar/pulsebar: Indicator for sound amplitude.
    // Put it in a GtkEventBox so we can catch click events.

    GtkWidget *event_box = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX(hbox0), event_box, TRUE, TRUE, 0);
    gtk_widget_show(event_box);

    // Allow mouse click events
    gtk_widget_set_events(event_box, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(event_box, "button_press_event", G_CALLBACK(win_level_bar_clicked), NULL);

    // Create a LevelBar widget and put it in the GtkEventBox
    g_win.level_bar = level_bar_new();
    gtk_widget_show(g_win.level_bar);
    gtk_container_add(GTK_CONTAINER(event_box), g_win.level_bar);
    level_bar_set_fraction(LEVEL_BAR(g_win.level_bar), 0.0);

    // How to draw the level bar?
    // Get from DConf
    gint bar_shape = SHAPE_CIRCLE;
    conf_get_int_value("level-bar-shape", &bar_shape);
    level_bar_set_shape(LEVEL_BAR(g_win.level_bar), bar_shape);
    // Notice: User can change this by RIGHT-clicking on the level-bar

    // Type of value on the level bar?
    // Get from DConf
    gint bar_value = VALUE_NONE;
    conf_get_int_value("level-bar-value", &bar_value);
    level_bar_set_value_type(LEVEL_BAR(g_win.level_bar), bar_value);
    // Notice: User can change this by LEFT-clicking on the level-bar

    // Should we show RMS or peak-value on the levelbar?
    // Notice: This value has no GUI-setting in the audio-recorder. You can change it in dconf-editor.
    g_win.pulse_type = PULSE_RMS;
    // EDIT 03.jan.2017: Value disabled by Moma. Always show RMS value.
    // conf_get_int_value("level-bar-pulse-type", (gint*)&g_win.pulse_type);
    // Start a.r with -d or --debug-signal to study these values.

    GtkWidget *hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_show(hbox1);
    gtk_box_pack_start(GTK_BOX(vbox1), hbox1, TRUE, TRUE, 0);

    // Translators: This is a GUI label. Keep it short.
    GtkWidget *label0 = gtk_label_new(_("File:"));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_widget_show(label0);
    gtk_box_pack_start(GTK_BOX(hbox1), label0, FALSE, FALSE, 0);

    g_win.filename = gtk_entry_new();
    gtk_widget_show(g_win.filename);
    gtk_box_pack_start(GTK_BOX(hbox1), g_win.filename, TRUE, TRUE, 0);
    gtk_entry_set_invisible_char(GTK_ENTRY(g_win.filename), 9679);

    // Show lastly saved filename; basename.ext
    gchar *path = NULL;
    gchar *fname = NULL;
    split_filename2(last_file_name, &path, &fname);
    gtk_entry_set_text(GTK_ENTRY(g_win.filename), (fname ? fname : ""));
    g_free(path);
    g_free(fname);

    // Free last_file_name
    g_free(last_file_name);

    // "Add to file" label.
    // Translators: This is a GUI label. Keep it VERY short.
    g_win.add_to_file = gtk_check_button_new_with_mnemonic(_("Add."));
    gtk_widget_show(g_win.add_to_file);
    gtk_box_pack_start(GTK_BOX(hbox1), g_win.add_to_file, FALSE, FALSE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_win.add_to_file), append_to_file);
    g_signal_connect(g_win.add_to_file, "toggled", G_CALLBACK(win_add_to_changed_cb), NULL);

    // Timer interface
    gchar *str_value = NULL;

    // "Timer>" GUI expander.
    // Translators: This is a GUI label. Keep it short.
    GtkWidget *timer_expander = gtk_expander_new(_("Timer."));
    gtk_widget_show(timer_expander);
    gtk_box_pack_start(GTK_BOX(vbox1), timer_expander, TRUE, TRUE, 0);

    conf_get_boolean_value("timer-expanded", &bool_value);

    gtk_expander_set_expanded(GTK_EXPANDER(timer_expander), bool_value);
    g_signal_connect(timer_expander, "notify::expanded", G_CALLBACK(win_expander_click_cb), "timer-expanded");

    GtkWidget *hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_show(hbox2);
    gtk_container_add(GTK_CONTAINER(timer_expander), hbox2);

    GtkWidget *vbox22 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(vbox22);
    gtk_box_pack_start(GTK_BOX(hbox2), vbox22, FALSE, FALSE, 0);

    // Timer checkbox
    g_win.timer_active = gtk_check_button_new();
    gtk_widget_show(g_win.timer_active);
    gtk_box_pack_start(GTK_BOX(vbox22), g_win.timer_active, FALSE, FALSE, 0);
    g_signal_connect(g_win.timer_active, "toggled", G_CALLBACK(win_timer_active_cb), NULL);

    conf_get_boolean_value("timer-active", &bool_value);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_win.timer_active), bool_value);

    // Timer text/commands; GtkTextView, multiline text view.
    GtkWidget *vbox20 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(vbox20);
    gtk_box_pack_start(GTK_BOX(hbox2), vbox20, TRUE, TRUE, 0);

    // Put g_win.timer_text to a GtkFrame so it gets a visible border/frame
    GtkWidget *frame2 = gtk_frame_new(NULL);
    gtk_widget_show(frame2);
    gtk_box_pack_start(GTK_BOX(vbox20), frame2, TRUE, TRUE, 0);

    g_win.timer_text = gtk_text_view_new();
    gtk_widget_show(g_win.timer_text);
    gtk_container_add(GTK_CONTAINER(frame2), g_win.timer_text);

    // Timer [Save] button
    g_win.timer_save_button = gtk_button_new_from_icon_name("document-save", GTK_ICON_SIZE_BUTTON);
    // Hide it
    gtk_widget_hide(g_win.timer_save_button);
    g_signal_connect(g_win.timer_save_button, "clicked", G_CALLBACK(win_timer_save_text_cb), NULL);
    gtk_button_set_always_show_image(GTK_BUTTON(g_win.timer_save_button), TRUE);

    GtkWidget *hbox22 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_show(hbox22);
    gtk_container_add(GTK_CONTAINER(vbox20), hbox22);
    gtk_box_pack_end(GTK_BOX(hbox22), g_win.timer_save_button, FALSE, FALSE, 0);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_win.timer_text));
    g_signal_connect(buffer, "changed", G_CALLBACK(win_timer_text_changed_cb), NULL);
    g_signal_connect(buffer, "insert-text", G_CALLBACK(win_timer_text_insert_cb), NULL);

    // Get current timer text
    conf_get_string_value("timer-text", &str_value);

    if (str_length(str_value, 4096) > 0)  {
        gtk_text_buffer_set_text(buffer, str_value, -1);
    } else {
        // Show g_def_timer_text
        gtk_text_buffer_set_text(buffer, g_def_timer_text, -1);
        conf_save_string_value("timer-text", (gchar *)g_def_timer_text);
    }
    g_free(str_value);

    // The [Info] button
    GtkWidget *button0 = gtk_button_new();
    gtk_widget_show(button0);
    GtkWidget *image = gtk_image_new_from_icon_name("dialog-information", GTK_ICON_SIZE_BUTTON);
    gtk_widget_show(image);
    gtk_button_set_always_show_image(GTK_BUTTON(button0), TRUE);
    gtk_button_set_image(GTK_BUTTON(button0), image);
    g_signal_connect(button0, "clicked", G_CALLBACK(window_show_timer_help), NULL);

    vbox22 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(vbox22);
    gtk_box_pack_start(GTK_BOX(hbox2), vbox22, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox22), button0, FALSE, FALSE, 0);

    GtkWidget *hseparator0 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_show(hseparator0);
    gtk_box_pack_start(GTK_BOX(vbox0), hseparator0, FALSE, TRUE, 0);

    // "Audio settings>" GUI expander.
    // Translators: This is a GUI label.
    GtkWidget *setting_expander = gtk_expander_new(_("Audio settings."));
    gtk_widget_show(setting_expander);
    gtk_box_pack_start(GTK_BOX(vbox0), setting_expander, TRUE, FALSE, 2);

    conf_get_boolean_value("settings-expanded", &bool_value);

    gtk_expander_set_expanded(GTK_EXPANDER(setting_expander), bool_value);
    g_signal_connect(setting_expander, "notify::expanded", G_CALLBACK(win_expander_click_cb), "settings-expanded");

    // Grid layout
    GtkWidget *grid0 = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(grid0), FALSE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid0), FALSE);
    gtk_widget_show(grid0);

    // Add grid0 to the setting_expander
    gtk_container_add(GTK_CONTAINER(setting_expander), grid0);
    gtk_grid_set_row_spacing(GTK_GRID(grid0), 2);

    // Audio Source label (meaning Audio Source, the device or program that produces sound).
    // Translators: This is a GUI label. Keep it short.
    label0 = gtk_label_new(_("Source:"));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_widget_show(label0);
    gtk_grid_attach(GTK_GRID(grid0), label0, 0, 0, 1, 1);

    // Create combobox for audio devices and Media Players, Skype, etc.
    g_win.audio_device = audio_sources_create_combo();
    gtk_widget_show(g_win.audio_device);
    gtk_grid_attach(GTK_GRID(grid0), g_win.audio_device, 1, 0, 1, 1);

    // Combobox "changed" signal
    gulong signal_id = g_signal_connect(g_win.audio_device, "changed", G_CALLBACK(win_device_list_changed_cb), NULL);
    // Save signal handle so we can block/unblock it
    g_object_set_data(G_OBJECT(g_win.audio_device), "selection-changed-signal", GINT_TO_POINTER(signal_id));


    // Refresh/reload the list of audio devices and Media Players, Skype

    // Reset error message
    win_set_error_text(NULL);

    // Load audio sources (device ids and names) from GStreamer and fill the combobox.
    // Add also Media Players, Skype (if installed) to the list. These can control the recording via DBus.
    audio_source_fill_combo(g_win.audio_device);

    // Set/show the current value.
    conf_get_string_value("audio-device-id", &str_value);

    DeviceItem *rec = audio_sources_find_id(str_value);
    if (rec) {
        // Set current value
        audio_sources_combo_set_id(g_win.audio_device, rec->id);
    } else {
        // Select first (Audio Output) device as default.
        GList *lst = audio_sources_get_for_type(AUDIO_SINK_MONITOR);
        DeviceItem *item = g_list_nth_data(lst, 0);
        if (item) {
            // Save values
            conf_save_string_value("audio-device-name", check_null(item->description));
            conf_save_string_value("audio-device-id", item->id);
            conf_save_int_value("audio-device-type", item->type);
        }

        // Free the list and its data
        audio_sources_free_list(lst);
        lst = NULL;
    }

    g_free(str_value);

    // Add [Reload] button
    button0 = gtk_button_new();
    gtk_widget_show(button0);
    image = gtk_image_new_from_icon_name("view-refresh", GTK_ICON_SIZE_BUTTON);
    gtk_widget_show(image);
    gtk_button_set_always_show_image(GTK_BUTTON(button0), TRUE);

    gtk_button_set_image(GTK_BUTTON(button0), image);
    gtk_grid_attach(GTK_GRID(grid0), button0, 2, 0, 1, 1);
    g_signal_connect(button0, "clicked", G_CALLBACK(win_refresh_device_list), NULL);

    // Audio format; .OGG, .MP3, .WAV, etc. See media-profiles.c.
    // Translators: This is a GUI label. Keep it short.
    label0 = gtk_label_new (_("Format:"));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_widget_show(label0);
    gtk_grid_attach(GTK_GRID(grid0), label0, 0, 2, 1, 1);

    // Get the media-profiles combobox filled with values.
    // Start dconf/gconf-editor and browse to: system -> gstreamer -> 0.10 -> audio -> profiles.
    g_win.media_format = profiles_create_combobox();

    gtk_widget_show(g_win.media_format);
    gtk_grid_attach(GTK_GRID(grid0), g_win.media_format, 1, 2, 1, 1);
    // Combox "changed" signal
    g_signal_connect(g_win.media_format, "changed", G_CALLBACK(win_audio_format_changed_cb), NULL);

    // Show current value
    conf_get_string_value("media-format", &str_value);

    combo_select_string(GTK_COMBO_BOX(g_win.media_format), str_value, 0/*select 0'th row if str_value not found*/);
    g_free(str_value);

    // Separator
    hseparator0 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_show(hseparator0);
    gtk_box_pack_start(GTK_BOX(vbox0), hseparator0, FALSE, TRUE, 1);

    // Place for error messages
    {
        g_win.error_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        // Hide it
        gtk_widget_hide(g_win.error_box);
        gtk_box_pack_start(GTK_BOX(vbox0), g_win.error_box, TRUE, FALSE, 0);

        // Label
        label0 = gtk_label_new("");
        gtk_widget_set_halign(label0, GTK_ALIGN_START);
        gtk_widget_show(label0);
        gtk_box_pack_start(GTK_BOX(g_win.error_box), label0, TRUE, FALSE, 0);

        // Ref: https://developer.gnome.org/gtk3/stable/GtkWidget.html#gtk-widget-override-color
        // gtk_widget_override_color has been deprecated since version 3.16 and should not be used in newly-written code.
        // Use a custom style provider and style classes instead.

        // Set red text color
        //GdkRGBA color;
        //gdk_rgba_parse(&color, "red");
        //gtk_widget_override_color(label0, GTK_STATE_FLAG_NORMAL, &color);

        // Wrap lines
        gtk_label_set_line_wrap(GTK_LABEL(label0), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(label0), PANGO_WRAP_WORD);

        gtk_label_set_max_width_chars(GTK_LABEL(label0), 60);

        // Remember the label widget
        g_object_set_data(G_OBJECT(g_win.error_box), "label-widget", label0);

        // Separator
        hseparator0 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_show(hseparator0);
        gtk_box_pack_start(GTK_BOX(g_win.error_box), hseparator0, FALSE, TRUE, 0);
    }

    // Buttons
    GtkWidget *hbox4 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_show(hbox4);
    gtk_box_pack_start(GTK_BOX(vbox0), hbox4, FALSE, TRUE, 0);

    button0 = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_always_show_image(GTK_BUTTON(button0), TRUE);
    gtk_widget_show(button0);
    gtk_box_pack_end(GTK_BOX(hbox4), button0, FALSE, FALSE, 0);
    g_signal_connect(button0, "clicked", G_CALLBACK(win_close_button_cb), GINT_TO_POINTER(FALSE));
    // Show a small menu on right-mouse click
    g_signal_connect(button0, "button-press-event", G_CALLBACK(win_close_button_press_cb), NULL);

    // [Additional settings] button.
    // Translators: This is a label on the [Additional settings] button.
    g_win.settings_button = gtk_button_new_with_label(_("Additional settings"));

    // Settings> box is expanded?
    conf_get_boolean_value("settings-expanded", &bool_value);

    if (bool_value)
        gtk_widget_show(g_win.settings_button);
    else
        gtk_widget_hide(g_win.settings_button);

    g_signal_connect(g_win.settings_button, "clicked", G_CALLBACK(win_settings_cb), NULL);
    gtk_box_pack_end(GTK_BOX(hbox4), g_win.settings_button, FALSE, TRUE, 0);

    // This button is for testing and debugging
    button0 = gtk_button_new_with_label("Test button");
    // ATM hide it
    gtk_widget_hide(button0);
    gtk_box_pack_end(GTK_BOX(hbox4), button0, FALSE, FALSE, 0);
    g_signal_connect(button0, "clicked", G_CALLBACK(test_0_cb), NULL);

#ifdef APP_HAS_MENU
    // Show/hide main menu?
    gboolean bool_val = FALSE;
    conf_get_boolean_value("show-main-menu", &bool_val);
    win_set_main_menu(bool_val);
#endif

    // Hide the [Save] widget (if it's visible)
    gtk_widget_hide(g_win.timer_save_button);

    // Hide the error_box widget (if it's visible)
    gtk_widget_hide(g_win.error_box);

    // Set/reset button images and labels
    win_update_gui();
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    GError *error = NULL;
    GOptionContext *context = g_option_context_new("Command line arguments.");
    g_option_context_set_help_enabled(context, TRUE);
    g_option_context_add_main_entries(context, option_entries, GETTEXT_PACKAGE);

    GOptionGroup *opt_group = gtk_get_option_group(TRUE);
    g_option_context_add_group(context, opt_group);
    g_option_context_parse(context, &argc, &argv, &error);

    if (error) {
        LOG_ERROR("Invalid command line argument. %s\n", error->message);
        g_error_free(error);
    }

    g_option_context_free(context);
    context = NULL;

    LOG_DEBUG("Value of --version (-v)=%d\n", g_version_info);
    LOG_DEBUG("Value of --show-icon (-i)=%d\n", g_show_tray_icon);
    LOG_DEBUG("Value of --show-window (-w)=%d\n", g_show_window);
    LOG_DEBUG("Value of --reset (-r)=%d\n", g_reset_settings);
    LOG_DEBUG("Value of --debug-signal (-d)=%d\n", g_debug_threshold);
    LOG_DEBUG("Value of --command (-c)=%s\n", g_command_arg);

    if (g_version_info != -1) {
        // Print program name and version info
        // Eg. "Audio Recorder 1.0"
        gchar *name = about_program_name();
        g_print("%s\n", name);
        g_free(name);
        // Exit
        exit(0);
    }

    // Reset all settings and restart audio-recorder?
    if (g_reset_settings != -1) {

        // This will !reset! all settings after kill & restart
        conf_save_boolean_value("started-first-time", TRUE);

        // Flush the settings registry
        conf_flush_settings();

        // Kill all previous instances
        send_client_request(argv, "simple-kill");

        // Wait 0.2s
        g_usleep(G_USEC_PER_SEC * 0.2);

        // This will do a fork() + execvp() [*]
        g_command_arg = NULL;
        contact_existing_instance(argv);

        exit(0);
    }

    // Initialize the i18n stuff
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    setlocale(LC_ALL, "");

    // Initialize libraries
    gtk_init(&argc, &argv);
    gdk_init(&argc, &argv);
    gst_init(&argc, &argv);

    gst_pb_utils_init();

    // Started 1.st time or after --reset (-c) argument ?
    gboolean first_time = FALSE;
    conf_get_boolean_value("started-first-time", &first_time);
    if (first_time) {
        // Reset all settings
        reset_all_settings();
        conf_save_boolean_value("started-first-time", FALSE);
    }

    // Did we get any --command (or -c) arguments?
    // $ audio-recorder --command <argument>

    // Print recording status, then exit?
    // The status is one of: "not running", "on", "off", "paused"
    if (!g_strcmp0(g_command_arg, "status")) {
        send_client_request(argv, g_command_arg);
        // This will call exit()
    }

    // Simply quit the recorder?
    else if (!g_strcmp0(g_command_arg, "quit")) {
        send_client_request(argv, g_command_arg);
        // This will call exit()
    }

    // Started without --command (-c) argument?
    if (g_command_arg == NULL) {
        // Try to contact already existing/running instance of audio-recorder
        gboolean is_running = ar_is_running();

        if (is_running) {
            // We found it.

            // Notice: g_command_arg will now point to a constant string. Do not try to g_free() this.
            g_command_arg = "show";

            // Now "show" the existing instance
            send_client_request(argv, g_command_arg);

            // And exit this duplicate instance
            exit(0);
        }
    }

    if (g_command_arg != NULL) {
        // Try contacting existings instance of the program.
        // Fork() + ecexvp() a new instance if necessary.
        contact_existing_instance(argv);
    }

    // Initialize local modules
    media_profiles_init();

    rec_manager_init();

    audio_sources_init();

    timer_module_init();

    // Setup DBus server for this program
    dbus_service_module_init();

    systray_module_init();

    // Show button images (normally not shown in the GNOME)
    // See also gconf-editor, setting '/desktop/gnome/interface'
    //GtkSettings *settings = gtk_settings_get_default();
    //gtk_settings_set_string_property(settings, "gtk-button-images", "TRUE", NULL);
    //g_object_set(settings, "gtk-button-images", TRUE, NULL);

    // Create main window and all its widgets
    win_create_window();

    gboolean show_window = TRUE;

    // g_show_window == 0: if --show-window=0 or -w 0 option seen
    if (g_show_window == 0) {
        show_window = FALSE;
    }

    // Determine how to display the window and tray icon
    gboolean show_icon = FALSE;
    conf_get_boolean_value("show-systray-icon", &show_icon);

    // g_show_tray_icon != -1: if --show-icon or -i option was seen
    if (g_show_tray_icon !=-1) {
        show_icon = g_show_tray_icon;
    }

    if (!show_icon) {
        // Cannot hide both window and the tray icon
        show_window = TRUE;
    }

    // Show/hide tray icon
    systray_icon_setup(show_icon);

    // First time?
    if (first_time) {
        // Force show window
        show_window = TRUE;

        // Auto-start this application on login
        autostart_set(TRUE);
    }

    // Let "--command show" and "--command hide" override the window visibility
    if (g_strrstr0(g_command_arg, "show")) {

        show_window = TRUE;

    } else if (g_strrstr0(g_command_arg, "hide")) {

        show_window = FALSE;
    }

    if (show_window) {
        // Show
        win_show_window(TRUE);
    } else {
        // Hide
        win_show_window(FALSE);
    }

    // Debugging:
    // Get and print level-values from the gst-vad.c module.
    // User must start this application from a terminal window to see these values.
    if (g_debug_threshold == 1) {
        // Set flag via timer.c
        timer_set_debug_flag(TRUE);
    }

    // g_win.audio_device must be set here at the very end.
    // Otherwise Media Players and Skype may show up before this app's main window. Skype may even block!
    win_set_device_id();

    // Main loop
    gtk_main ();

    return 0;
}

static gpointer send_client_request(gchar *argv[], gpointer data) {
    // Send command via DBus to the existing instance of audio-recorder

    // $ audio-recorder --command start
    // $ audio-recorder --command stop
    // $ audio-recorder --command pause

    // $ audio-recorder --command status  // Print status string; "not running" | "on" | "off" | "paused"

    // $ audio-recorder --command show
    // $ audio-recorder --command quit

    // You can also combine
    // $ audio-recorder --command start+show
    // $ audio-recorder --command stop+hide
    // $ audio-recorder --command stop+quit

    if (!data) return 0;

    // To lower case
    gchar *command = g_ascii_strdown((gchar*)data, -1);

    gboolean done = FALSE;
    gchar *ret = NULL;
    gint exit_val = 0;

    if (!g_strcmp0(command, "status")) {

        // Call get_state()
        ret = dbus_service_client_request("get_state", NULL/*no args*/);
        if (!ret) {
            // Audio-recorder is not running
            ret = g_strdup("not running");
            exit_val = -1;
        }
        g_print("%s\n", ret);
        g_free(ret);
        // Exit
        exit(exit_val);
    }

    // -------------------------------------------------

    if (g_strrstr(command, "start")) {

        // Call set_state("start")
        ret = dbus_service_client_request("set_state", "start");
        if (g_strcmp0(ret, "OK")) {
            LOG_ERROR("Cannot execute client/dbus request %s.\n", "set_state(\"start\")");
        }
        g_free(ret);
        done = TRUE;
    }

    if (g_strrstr(command, "stop")) {

        // Call set_state("stop")
        ret = dbus_service_client_request("set_state", "stop");
        if (g_strcmp0(ret, "OK")) {
            LOG_ERROR("Cannot execute client/dbus request %s.\n", "set_state(\"stop\")");
        }
        g_free(ret);
        done = TRUE;
    }

    if (g_strrstr(command, "pause")) {

        // Call set_state("pause")
        ret = dbus_service_client_request("set_state", "pause");
        if (g_strcmp0(ret, "OK")) {
            LOG_ERROR("Cannot execute client/dbus request %s.\n", "set_state(\"pause\")");
        }
        g_free(ret);
        done = TRUE;
    }

    if (g_strrstr(command, "show")) {

        // Call set_state("show")
        ret = dbus_service_client_request("set_state", "show");
        if (g_strcmp0(ret, "OK")) {
            LOG_ERROR("Cannot execute client/dbus request %s.\n", "set_state(\"show\")");
        }
        g_free(ret);
        done = TRUE;
    }

    if (g_strrstr(command, "hide")) {

        // Call set_state("hide")
        ret = dbus_service_client_request("set_state", "hide");
        if (g_strcmp0(ret, "OK")) {
            LOG_ERROR("Cannot execute client/dbus request %s.\n", "set_state(\"hide\")");
        }
        g_free(ret);
        done = TRUE;
    }

    if (g_strrstr(command, "status")) {

        // Call get_state()
        ret = dbus_service_client_request("get_state", NULL /*no args*/);
        if (!ret) {
            // Audio-recorder is not running
            ret = g_strdup("not running");
        }
        // Print state; not running|on|off|paused
        g_print("%s\n", ret);
        g_free(ret);
        done = TRUE;
    }

    if (g_strrstr(command, "quit")) {

        // Call set_state("quit"). Terminate running instance of this application.
        ret = dbus_service_client_request("set_state", "quit");
        g_free(ret);

        // Wait a while
        g_usleep(G_USEC_PER_SEC * 0.2);

        // Terminate possibly frozen instances of audio-recorder
        kill_frozen_instances(argv[0], -1/*all PIDs*/);

        // Exit also this instance
        exit(0);
    }

    if (g_strrstr(command, "simple-kill")) {

        // Call set_state("quit"). Terminate running instance of this application.
        ret = dbus_service_client_request("set_state", "quit");
        g_free(ret);

        // Wait
        g_usleep(G_USEC_PER_SEC * 0.2);

        // Terminate possibly frozen instances of audio-recorder
        kill_frozen_instances(argv[0], getpid());

        done = TRUE;
    }


    if (!done) {
        LOG_ERROR("Invalid argument in --command=%s. See --help for more information.\n", command);
    }

    g_free(command);

    // FALSE: Remove idle function
    return 0;
}

static void contact_existing_instance(gchar *argv[]) {

    // $ audio-recorder --command <argument>

    // Find existing instance of audio-recorder. Do not start this program twice.
    gboolean is_running = ar_is_running();

    if (is_running) {
        // Found existing instance.
        // Send client request (execute methode call over DBus).
        send_client_request(argv, g_command_arg);

        // And exit this duplicate instance
        exit(0);

    } else {

        // Terminate all POSSIBLY FROZEN audio-recorder instances that do not respond to our dbus-requests.
        kill_frozen_instances(argv[0], getpid()/*will not kill myself*/);

        // Fork and exec a new instance of audio-recorder
        pid_t pid = 0;
        if((pid = fork()) < 0) {
            LOG_ERROR("Cannot execute fork(). Terminating program.");
            exit(1);

        } else if(pid == 0) {
            //  Child process

            LOG_DEBUG("Fork() succeeded. Going to replace this child by execvp() call.\n");

            // Replace this child with a new audio-recorder process.
            // One important thing:
            // We could let this child simply run, but the child and parent processes (after fork())
            //  can't share the same file descriptors (eg. fd sockets).
            //  Parent will most likely block the child, and the latter couldn't be able to communicate
            //  with the X-window system.
            //  I've seen another solution where they call _exit() instead of exit().

            // Ref: https://linux.die.net/man/3/execvp
            execvp(argv[0], argv);

            // Should not come here
            LOG_ERROR("Child process failed to spawn. Should not come here. Terminating child process.\n");
            exit(0);

        } else {
            // Parent process

            // Sleep a while, let the child execvp()
            g_usleep(G_USEC_PER_SEC * 0.3);

            // Send request to the child (execute methode call over DBus)
            send_client_request(argv, g_command_arg);

            // Exit parent process.
            exit(0);
        }
    }
}

static gboolean ar_is_running() {
    // Try to contact existing instance of audio-recorder
    gchar *state = dbus_service_client_request("get_state", NULL);

    // Did we get an answer from existing instance?
    // The answers is "on", "off" or "paused" if a.r is already running.
    // Otherwise the reply is "not running" or NULL.

    gboolean ret = (state && (!g_strrstr(state, "not")));
    g_free(state);

    return ret;
}


