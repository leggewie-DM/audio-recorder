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
#include "audio-sources.h"
#include "support.h"
#include "utility.h"
#include "timer.h"
#include "dconf.h"

#include "rec-window.h"
#include "auto-start.h"
#include "help.h"
#include "dbus-skype.h"
#include "rec-manager-struct.h"

extern GtkWidget *page_to_edit_pipelines();

static GtkWidget *g_dialog = NULL;

// Currently selected device/player type
static gint g_current_type = 0;

// All changed device/player types
static gint g_changed_types = 0;

// List of some known players; RhythmBox, Banshee, VLC, Amarok (this is just a label text)
// static const gchar *g_player_list = NULL;

// Listbox (GtkTreeView) columns.
enum {
    COL_CHECKBOX,// Checkbox (hidden in the upper listbox)
    COL_TYPE,    // DeviceType  (hidden)
    COL_ID,      // Device ID (hidden)
    COL_ICON,    // Device icon (visible)
    COL_DESCR,   // Device description (visible)
    COL_HELP,    // Help text (help text shown under the listbox)
    N_COLUMNS    // Number of columns in this listbox (treeview)
};

extern void win_keep_on_top(gboolean on_top);

static void device_list_reload(gint type, gchar *type_descr, gchar *help_text);
static void device_list_fill();
static void device_list_save();

GtkWindow *win_settings_get_window() {
    return GTK_WINDOW(g_dialog);
}

void update_main_GUI() {
    // Send "changed" message to the rec-manager and GUI.
    RecorderCommand *cmd = g_malloc0(sizeof(RecorderCommand));
    cmd->type = RECORDING_PROFILE_CHANGED;

    // Send command to rec-manager.c and GUI.
    // It will free the cmd structure after processing.
    rec_manager_send_command(cmd);
}

void win_settings_destroy_dialog() {
    if (GTK_IS_WIDGET(g_dialog)) {
        gtk_widget_destroy(GTK_WIDGET(g_dialog));
    }
    g_dialog = NULL;
}

static void keep_on_top_switch_cb(GtkWidget *widget, gpointer data) {
    // Is ON/OFF?
    gboolean active = gtk_switch_get_active(GTK_SWITCH(widget));

    // Save in Dconf
    conf_save_boolean_value("keep-on-top", active);

    win_keep_on_top(active);

}

static void show_icon_switch_cb(GtkWidget *widget, gpointer data) {
    // Is ON/OFF?
    gboolean active = gtk_switch_get_active(GTK_SWITCH(widget));

    // Save in gconf
    conf_save_boolean_value("show-systray-icon", active);

    // Install or remove systray icon
    systray_icon_setup(active);

    // Update also autostart option (mode of autostart depends on the systray icon)
    active = autostart_get();
    autostart_set(active);
}

static void autostart_switch_cb(GtkWidget *widget, gpointer data) {
    // Is ON/OFF?
    gboolean active = gtk_switch_get_active(GTK_SWITCH(widget));

    // Write autostart values
    autostart_set(active);
}

static void win_settings_get_folder_name(GtkWidget *widget, gpointer data) {
    // Get folder where to save all recordings.
    GtkWidget *entry = (GtkWidget*)data;

    // Translators: This is a title in a directory chooser dialog.
    GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Select Directory"),
                        NULL,
                        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                        "gtk-cancel", GTK_RESPONSE_CANCEL,
                        "gtk-open", GTK_RESPONSE_ACCEPT,
                        NULL);

    // Set current directory
    gchar *path = g_strdup((gchar*)gtk_entry_get_text(GTK_ENTRY(entry)));
    if (str_length(path, 2048) < 1) {
        g_free(path);
        path = get_home_dir();
    }

    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), path);

    guint response = gtk_dialog_run(GTK_DIALOG(dialog));

    g_free(path);
    path = NULL;

    switch (response) {
    case GTK_RESPONSE_ACCEPT: {
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
    }
    }

    if (GTK_IS_WIDGET(dialog)) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }

    if (path) {
        gtk_entry_set_text(GTK_ENTRY(entry), path);
        g_free(path);
    }
}

static void win_settings_show_filename_help() {
    // Show help file
    help_show_page("filename-format.html");
}

// ----------------------------------------------

static void device_list_checkbox_toggled(GtkCellRendererToggle *toggle, gchar *path_str, gpointer data) {
    // User clicked on COL_CHECKBOX.
    GtkTreeIter iter;
    gboolean active;

    g_object_get(G_OBJECT(toggle), "active", &active, NULL );

    GtkTreeModel *model = GTK_TREE_MODEL(data);
    gtk_tree_model_get_iter_from_string(model, &iter, path_str);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, !active, -1);

    // Save device list in GConf (for g_current_type)
    device_list_save();
}

static GtkWidget *create_listbox() {
    // Create list store
    GtkListStore *store = gtk_list_store_new(N_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_INT, G_TYPE_STRING,
                          GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING);

    // List view
    GtkWidget *list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

    // Add some extra space
    gtk_widget_set_size_request (list_view, -1, 85);

    // Unref store
    g_object_unref(G_OBJECT(store));

    // No headers
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list_view), FALSE);

    // Checkbox (this invisible in the upper listbox (treeview list).
    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Row checkbox");
    GtkCellRenderer *cell = gtk_cell_renderer_toggle_new();
    g_signal_connect(G_OBJECT(cell), "toggled", G_CALLBACK(device_list_checkbox_toggled), GTK_TREE_MODEL(store));
    gtk_tree_view_column_pack_start(col, cell, FALSE );
    gtk_tree_view_column_add_attribute(col, cell, "active", COL_CHECKBOX);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list_view), col);

    // Device type column (invisible)
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Device type");
    cell = gtk_cell_renderer_text_new();
    gtk_tree_view_column_set_visible(col, FALSE);
    gtk_tree_view_column_pack_start(col, cell, FALSE);
    gtk_tree_view_column_set_attributes(col, cell, "text", COL_TYPE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list_view), col);

    // Device id column (invisible)
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Device id");
    cell = gtk_cell_renderer_text_new();
    gtk_tree_view_column_set_visible(col, FALSE);
    gtk_tree_view_column_pack_start(col, cell, FALSE);
    gtk_tree_view_column_set_attributes(col, cell, "text", COL_ID, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list_view), col);

    // Device icon (pixbuf) column (visible)
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Pixbuf");
    cell = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, cell, FALSE);
    gtk_tree_view_column_set_attributes(col, cell, "pixbuf", COL_ICON, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list_view), col);

    // Device description column (visible)
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Description");
    cell = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, cell, FALSE);
    gtk_tree_view_column_set_attributes(col, cell, "text", COL_DESCR, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list_view), col);

    // Help text (invisible, text is shown under the second listbox)
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Help text");
    gtk_tree_view_column_set_visible(col, FALSE);
    cell = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, cell, FALSE);
    gtk_tree_view_column_set_attributes(col, cell, "text", COL_HELP, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list_view), col);

    return list_view;
}

static void player_view_row_changed_cb(GtkTreeSelection *selection, gpointer user_data) {
    // Row changed in player view.
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;

    // Get row values
    gint type;
    gchar *descr = NULL;
    gchar *help_text = NULL;

    gtk_tree_model_get(model, &iter,
                       COL_TYPE, &type,
                       COL_DESCR, &descr,
                       COL_HELP, &help_text, -1);

    // From type to type name
    const gchar *type_name = device_item_get_type_name(type);
    (void)type_name; // Avoid unused var message

    LOG_DEBUG("Selected row is:%s (%d), %s\n", type_name, type, descr);

    // Reload the device list
    device_list_reload(type, descr, help_text);

    g_free(descr);
    g_free(help_text);
}

static void device_list_reload(gint type, gchar *type_descr, gchar *help_text) {
    // Save type name
    g_current_type = type;

    // Show help text
    GtkWidget *label = (GtkWidget*)g_object_get_data(G_OBJECT(g_dialog), "player-label-widget");
    gtk_label_set_text(GTK_LABEL(label), help_text);

    // Set GUI label
    gchar *txt = g_strdup_printf(_("Recording devices for %s:"), type_descr);
    label = (GtkWidget*)g_object_get_data(G_OBJECT(g_dialog), "device-label-widget");
    gtk_label_set_text(GTK_LABEL(label), txt);
    g_free(txt);

    // Fill the device list
    device_list_fill();
}

static void player_view_fill() {
    GtkWidget *view = g_object_get_data(G_OBJECT(g_dialog), "player-list-widget");

    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    GtkTreeIter  iter;

    if (!GTK_IS_TREE_MODEL(model)) {
        return;
    }

    // Clear view
    gtk_list_store_clear(GTK_LIST_STORE(model));

    // Get available players, comm progs.
    GList *dev_list = audio_sources_get_for_type(MEDIA_PLAYER | COMM_PROGRAM | USER_DEFINED);
    // Debug print:
    // audio_sources_print_list(dev_list, "Players:");

    // Collect types
    gint item_types = 0;

    // Add items to the listbox
    GList *n = g_list_first(dev_list);
    while (n) {
        DeviceItem *item = (DeviceItem*)n->data;

        // Already in the list?
        if (item_types & item->type) {
            // Next item thanks
            n = g_list_next(n);
            continue;
        }

        // Collect types
        item_types = item_types | item->type;

        // Add new row
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);

        gchar *descr = NULL;
        gchar *help_text = NULL;
        GdkPixbuf *pixbuf = NULL;

        switch (item->type) {
        case MEDIA_PLAYER:
            // Translators: This is a label in the [Additional settings] dialog
            descr = g_strdup(_("Media players (RhythmBox, Banshee, etc.)"));

            pixbuf = NULL;

            // Translators: This is a label/help text in the [Additional settings] dialog
            help_text = g_strdup(_("Select output device (speakers) for recording."));
            break;

        case COMM_PROGRAM:
            descr = g_strdup(item->description);

            pixbuf = load_icon_pixbuf(item->icon_name, 22);

            // Translators: This is a label/help text in the [Additional settings] dialog
            help_text = g_strdup(_("Select both output device (speakers) and webcam/microphone."));
            break;

        //case USER_DEFINED:
        default:
            descr = g_strdup(item->description);

            gchar *path = get_image_path(item->icon_name);
            pixbuf = get_pixbuf_from_file(path, 22, 22);
            g_free(path);

            // Translators: This is a label/help text in the [Additional settings] dialog
            help_text = g_strdup(_("Select one or more devices for recording."));
            break;
        }

        if (!GDK_IS_PIXBUF(pixbuf)) {
            // No. Load a default icon.
            gchar *path = get_image_path("mediaplayer.png");
            pixbuf = get_pixbuf_from_file(path, 22, 22);
            g_free(path);
        }

        // Set column data
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           COL_CHECKBOX, 0, /* not used */
                           COL_TYPE, item->type, /* type */
                           COL_ID, "", /* not used */
                           COL_ICON, pixbuf, /* icon pixbuf */
                           COL_DESCR, descr /* text */,
                           COL_HELP, help_text /* help text */, -1);

        // Pixbuf has a reference count of 2 now, as the list store has added its own
        if (GDK_IS_PIXBUF(pixbuf)) {
            g_object_unref(pixbuf);
        }

        g_free(descr);
        g_free(help_text);

        // Next item
        n = g_list_next(n);
    }

    // Free dev_list
    audio_sources_free_list(dev_list);
    dev_list = NULL;
}

// ----------------------------------------------

static void device_list_save() {
    // Save device list in GConf registry (for g_current_type)
    if (!g_current_type) return;

    // Remember all changed types (save bit)
    g_changed_types = g_changed_types | g_current_type;

    GtkWidget *list_view = g_object_get_data(G_OBJECT(g_dialog), "device-list-widget");

    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(list_view));
    if (!GTK_IS_TREE_MODEL(model)) {
        return;
    }

    // Collect selected devices for g_current_type
    GList  *selected_devs = NULL;

    // For each row
    GtkTreeIter iter;
    gint ret = gtk_tree_model_get_iter_first(model, &iter);
    while (ret) {
        // Read checkbox value and device_id value
        gint active = 0;
        gchar *device_id = NULL;

        gtk_tree_model_get(model, &iter,
                           COL_CHECKBOX, &active,
                           COL_ID, &device_id, -1);

        // Checkbox is checked?
        if (active) {
            selected_devs = g_list_append(selected_devs, g_strdup(device_id));
        }

        g_free(device_id);

        // Take next row
        ret = gtk_tree_model_iter_next(model, &iter);
    }

    // Save the list in GSettings (dconf) registry
    gchar *conf_key = g_strdup_printf("players/device-type-%d", g_current_type);

    conf_save_string_list(conf_key, selected_devs);
    // Check this in dconf-editor, key: /apps/audio-recorder/players/

    g_free(conf_key);

// MOMA
#if defined(ACTIVE_DEBUGGING) || defined(DEBUG_ALL)
    LOG_DEBUG("-------------------\n");
    const gchar *type_name= device_item_get_type_name(g_current_type);
    LOG_DEBUG("Selected devices for %s (%d):\n", type_name, g_current_type);
    str_list_print("Device", selected_devs);
#endif

    // Free GList
    str_list_free(selected_devs);
    selected_devs = NULL;
}

static gboolean is_in_selected_list(gchar *dev_id, GList *dev_list) {
    // Check if dev_id is in dev_list list
    GList *n = g_list_first(dev_list);
    while (n) {
        if (!g_strcmp0(dev_id, (gchar*)(n->data))) {
            return TRUE;
        }
        n = g_list_next(n);
    }
    return FALSE;
}

static void device_list_fill() {
    GtkWidget *list_view = g_object_get_data(G_OBJECT(g_dialog), "device-list-widget");

    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(list_view));
    GtkTreeIter  iter;

    if (!GTK_IS_TREE_MODEL(model)) {
        return;
    }

    // Get device list for g_current_type
    GList *selected_list = NULL;
    if (g_current_type) {
        // See dconf-editor, key: /apps/audio-recorder/players/

        gchar *conf_key = g_strdup_printf("players/device-type-%d", g_current_type);
        conf_get_string_list(conf_key, &selected_list);
        g_free(conf_key);
    }

    // Clear the old list
    gtk_list_store_clear(GTK_LIST_STORE(model));

    // Get list of microphones and audio cards
    GList *dev_list = audio_sources_get_for_type(AUDIO_INPUT/*microphones*/ | AUDIO_SINK_MONITOR/*audio cards*/);
    // Debug print:
    // audio_sources_print_list(dev_list, "Device list:");

    // Add items to the view
    GList *n = g_list_first(dev_list);
    while (n) {
        DeviceItem *item = (DeviceItem*)n->data;

        // Add new row
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);

        // Icon pixbuf
        gchar *p = item->icon_name;
        if (!p)
            p = "loudspeaker.png";

        gchar *path = get_image_path(p);

        GdkPixbuf *pixbuf = get_pixbuf_from_file(path, 24, 24);

        // Check if item->id is in selected_devs list
        gboolean is_active = is_in_selected_list(item->id, selected_list/*GList*/);

        // Set column data
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           COL_CHECKBOX, is_active, /* row checkbox */
                           COL_TYPE, 0, /* not used */
                           COL_ID, item->id, /* device id */
                           COL_ICON, pixbuf, /* icon pixbuf */
                           COL_DESCR, item->description, /* visible text */
                           COL_HELP, "", /* not used */
                           -1);

        // Pixbuf has a reference count of 2 now, as the list store has added its own
        g_object_unref(pixbuf);

        g_free(path);

        // Next item
        n = g_list_next(n);
    }

    // Free dev_list
    audio_sources_free_list(dev_list);

    // Free selected_list and its data
    str_list_free(selected_list);
    selected_list = NULL;
}

void player_view_set(const gchar *type_name) {
    GtkWidget *view = g_object_get_data(G_OBJECT(g_dialog), "player-list-widget");

    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    if (!GTK_IS_TREE_MODEL(model)) {
        return;
    }

    // For each row
    GtkTreeIter iter;
    gint ret = gtk_tree_model_get_iter_first(model, &iter);
    while (ret) {
        gint type = -1;
        gtk_tree_model_get(model, &iter,  COL_TYPE, &type, -1);
        const gchar *name = device_item_get_type_name(type);

        if (!g_strcmp0(type_name, (gchar*)name)) {
            GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
            gtk_tree_selection_select_iter(selection, &iter);
            return;

        }

        // Take next row
        ret = gtk_tree_model_iter_next(model, &iter);
    }

    // Select first
    gtk_tree_model_get_iter_from_string(model, &iter, "0");
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_select_iter(selection, &iter);
}

void win_settings_show_dialog(GtkWindow *parent) {

    // Translators: This is a title in the additional settings dialog
    g_dialog = gtk_dialog_new_with_buttons(_("Additional settings"),
                                           parent,
                                           GTK_DIALOG_DESTROY_WITH_PARENT,
                                           "gtk-cancel", GTK_RESPONSE_REJECT,
                                           "gtk-ok", GTK_RESPONSE_OK,
                                           NULL);

    gtk_window_set_transient_for(GTK_WINDOW(g_dialog), parent);

    //gtk_window_set_default_size(GTK_WINDOW(g_dialog), 500, 600);
    gtk_window_set_resizable(GTK_WINDOW(g_dialog), FALSE);

    gtk_dialog_set_default_response(GTK_DIALOG(g_dialog), GTK_RESPONSE_OK);
    // ---------------------------------------

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(g_dialog));

    // A notebook with 2 tabs
    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(content_area), notebook);

    // -----------------------------------
    // Tab page 0, General
    // -----------------------------------
    GtkWidget *vbox0 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    // Translators: This is a [Tab-page] in the [Additional settings] dialog.
    GtkWidget *page_label = gtk_label_new(_("General"));

    // Create a GtkGrid.
    // Ref: https://developer.gnome.org/gtk3/stable/GtkGrid.html
    GtkWidget *grid0 = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(grid0), FALSE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid0), FALSE);

    gtk_box_pack_start(GTK_BOX(vbox0), grid0, FALSE, TRUE, 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid0), 3);

    // "Folder name:" label
    // Translators: This is a GUI label. Keep it short.
    GtkWidget *label0 = gtk_label_new(_("Folder name:"));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);

    gtk_grid_attach(GTK_GRID(grid0), label0, 0, 0, 1, 1);

    GtkWidget *folder_name_field = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid0), folder_name_field, 1, 0, 5, 1);
    gtk_entry_set_invisible_char(GTK_ENTRY(folder_name_field), 9679);

    GtkWidget *button0 = gtk_button_new_with_mnemonic("...");
    gtk_grid_attach(GTK_GRID(grid0), button0, 6, 0, 1, 1);
    g_signal_connect(button0, "clicked", G_CALLBACK(win_settings_get_folder_name), folder_name_field);

    // Get and show current folder name (/home/username/Audio)
    gchar *folder_name = get_audio_folder();
    gtk_entry_set_text(GTK_ENTRY(folder_name_field), folder_name);
    g_free(folder_name);

    // "Filename format:" label
    // Translators: This is a GUI label. Keep it short.
    label0 = gtk_label_new(_("Filename format:"));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid0), label0, 0, 1, 1, 1);

    GtkWidget *file_name_pattern = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid0), file_name_pattern, 1, 1, 5, 1);
    gtk_entry_set_invisible_char(GTK_ENTRY(file_name_pattern), 9679);

    button0 = gtk_button_new();
    GtkWidget *image = gtk_image_new_from_icon_name("dialog-information", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_always_show_image(GTK_BUTTON(button0), TRUE);
    gtk_button_set_image(GTK_BUTTON(button0), image);
    g_signal_connect(button0, "clicked", G_CALLBACK(win_settings_show_filename_help), NULL);
    gtk_grid_attach(GTK_GRID(grid0), button0, 6, 1, 1, 1);

#if 0
FIXME:
Replace above gtk_image_new_from_icon_name() with:
    button0 = gtk_button_new();

    GtkIconTheme *icon_theme = gtk_icon_theme_get_for_screen(gdk_screen_get_default());

    GdkPixbuf *icon_pixbuf = gtk_icon_theme_load_icon (icon_theme,
                             "dialog-question",
                             16,
                             GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                             NULL);

    GtkWidget *image = gtk_image_new_from_pixbuf(icon_pixbuf);

    if (GTK_IS_WIDGET(image)) {
        gtk_button_set_image(GTK_BUTTON(button0), image);
    }

    g_object_unref(icon_pixbuf);

    g_signal_connect(button0, "clicked", G_CALLBACK(win_settings_show_filename_help), NULL);
    gtk_grid_attach(GTK_GRID(grid0), button0, 6, 1, 1, 1);
#endif

    // Get filename pattern
    gchar *str_value = get_filename_pattern();
    gtk_entry_set_text(GTK_ENTRY(file_name_pattern), str_value);
    g_free(str_value);

    gboolean bool_val = FALSE;

    // Create GtkGrid for some GtkSwitch'es
    GtkWidget *grid1 = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(grid1), FALSE);
    gtk_grid_set_row_spacing(GTK_GRID(grid1), 3);

    gtk_grid_attach(GTK_GRID(grid0), grid1, 1, 5, 5, 5);

    // Setting for "Keep window on top?"
    GtkWidget *keep_top_switch = gtk_switch_new();
    gtk_grid_attach(GTK_GRID(grid1), keep_top_switch, 0, 0, 1, 1);

    // Translators: This is a label for an ON/OFF switch.
    label0 =  gtk_label_new(_("Keep window on top."));
    gtk_label_set_justify(GTK_LABEL(label0), GTK_JUSTIFY_LEFT);
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label0, 3);
    gtk_grid_attach(GTK_GRID(grid1), label0, 1, 0, 4, 1);

    conf_get_boolean_value("keep-on-top", &bool_val);
    gtk_switch_set_active(GTK_SWITCH(keep_top_switch), bool_val);
    g_signal_connect(keep_top_switch, "notify::active", G_CALLBACK(keep_on_top_switch_cb), NULL);


    // Setting for "Show systray icon"
    GtkWidget *show_icon_switch = gtk_switch_new();
    gtk_grid_attach(GTK_GRID(grid1), show_icon_switch, 0, 1, 1, 1);

    // Translators: This is a label for an ON/OFF switch.
    label0 =  gtk_label_new(_("Show icon on the system tray."));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label0, 3);
    gtk_grid_attach(GTK_GRID(grid1), label0, 1, 1, 4, 1);

    conf_get_boolean_value("show-systray-icon", &bool_val);
    gtk_switch_set_active(GTK_SWITCH(show_icon_switch), bool_val);
    g_signal_connect(show_icon_switch, "notify::active", G_CALLBACK(show_icon_switch_cb), NULL);

    // Setting for "Auto-start application at login"
    GtkWidget *autostart_switch = gtk_switch_new();
    gtk_grid_attach(GTK_GRID(grid1), autostart_switch, 0, 2, 1, 1);

    bool_val = autostart_get();
    gtk_switch_set_active(GTK_SWITCH(autostart_switch), bool_val);
    g_signal_connect(autostart_switch, "notify::active", G_CALLBACK(autostart_switch_cb), NULL);

    // Translators: This is a label for an ON/OFF switch.
    label0 =  gtk_label_new(_("Auto-start this application at login."));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label0, 3);
    gtk_grid_attach(GTK_GRID(grid1), label0, 1, 2, 4, 1);


    // TODO:
    // This is a fix.
    // A hard-coded setting for Skype.
    // We should create a properties class for device items, so they can expose this type of settings without hardcoding.
    DeviceItem *item = audio_sources_find_id("com.Skype.API");
    GtkWidget *skype_switch = NULL;
    if (item) {
        // Separator line
        GtkWidget *hseparator0 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_grid_attach(GTK_GRID(grid1), hseparator0, 0, 3, 1, 1);

        hseparator0 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_grid_attach(GTK_GRID(grid1), hseparator0, 1, 3, 4, 1);

        // Setting for "Record ringing sound for Skype"
        skype_switch = gtk_switch_new();
        gtk_grid_attach(GTK_GRID(grid1), skype_switch, 0, 4, 1, 1);

        // Translators: This is a label for an ON/OFF switch.
        label0 =  gtk_label_new(_("Record ringing sound for Skype."));
        gtk_widget_set_halign(label0, GTK_ALIGN_START);
        gtk_widget_set_margin_start(label0, 3);
        gtk_grid_attach(GTK_GRID(grid1), label0, 1, 4, 4, 1);

        gboolean bool_val = FALSE;
        conf_get_boolean_value("skype/record-ringing-sound", &bool_val);
        gtk_switch_set_active(GTK_SWITCH(skype_switch), bool_val);
    }

    // Add new tab page
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox0, page_label);

    // -----------------------------------------------------
    // Tab page 1, Device settings for Media Players, Skype
    // -----------------------------------------------------
    GtkWidget *vbox1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    // Translators: This is a [Tab-page] in the [Additional settings] dialog.
    page_label = gtk_label_new(_("Device settings"));

    label0 = gtk_label_new(_("Installed items:"));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(vbox1), label0, FALSE, FALSE, 0);

    // Create a listbox (a GtkTreeView) for Media Player, Skype and one line for "User defined devices"
    GtkWidget *player_view = create_listbox();

    // COL_CHECKBOX is not used by this view. Hide it.
    GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(player_view), COL_CHECKBOX);
    gtk_tree_view_column_set_visible(col, FALSE);

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(player_view));
    g_signal_connect(sel, "changed", G_CALLBACK(player_view_row_changed_cb), NULL);

    // Reset current (selected) type
    g_current_type = 0;
    g_changed_types = 0;

    g_object_set_data(G_OBJECT(g_dialog), "player-list-widget", player_view);
    gtk_box_pack_start(GTK_BOX(vbox1), player_view, FALSE, FALSE, 0);

    // Fill the player view
    player_view_fill();

    // Separator line
    GtkWidget *hseparator0 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox1), hseparator0, TRUE, TRUE, 0);

    label0 = gtk_label_new("");
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(g_dialog), "device-label-widget", label0);

    gtk_box_pack_start(GTK_BOX(vbox1), label0, TRUE, TRUE, 0);

    // Listbox for devices
    GtkWidget *dev_view = create_listbox();
    g_object_set_data(G_OBJECT(g_dialog), "device-list-widget", dev_view);

    gtk_box_pack_start(GTK_BOX(vbox1), dev_view, FALSE, FALSE, 0);

    // Label for help text
    label0 = gtk_label_new("");
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(g_dialog), "player-label-widget", label0);
    gtk_box_pack_start(GTK_BOX(vbox1), label0, TRUE, TRUE, 0);

    // Get lastly selected device type (set in the main window)
    gint saved_dev_type = -1;
    conf_get_int_value("audio-device-type", &saved_dev_type);
    const gchar *type_name = device_item_get_type_name(saved_dev_type);
    // Select row in the view
    player_view_set(type_name);

    // Add new tab page
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox1, page_label);

    // -----------------------------------------------------
    // Tab page 2, Page to edit media profiles and Gstreamer pipelines
    // -----------------------------------------------------
    GtkWidget *vbox2 = page_to_edit_pipelines(GTK_WINDOW(g_dialog));

    // Translators: This is a [Tab-page] in the [Additional settings] dialog.
    page_label = gtk_label_new(_("Recording commands"));

    // Add new tab page
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox2, page_label);

    // Show dialog
    gtk_widget_show_all(g_dialog);

    gint res = gtk_dialog_run(GTK_DIALOG(g_dialog));

    switch (res) {
    case GTK_RESPONSE_ACCEPT:
    case GTK_RESPONSE_OK: {
        // Save folder name
        gchar *str_value = (gchar*)gtk_entry_get_text(GTK_ENTRY(folder_name_field));
        conf_save_string_value("folder-name", str_value);
        // Do not g_free() str_value
        // Ref: https://developer.gnome.org/gtk3/unstable/GtkEntry.html#gtk-entry-get-text

        // Save filename pattern
        str_value = (gchar*)gtk_entry_get_text(GTK_ENTRY(file_name_pattern));
        conf_save_string_value("filename-pattern", str_value);
        // Do not g_free() str_value

        // Device types changed (bitwise test)?
        if ((g_changed_types & saved_dev_type) != 0) {
            // Let timer know that the settings have been altered, so it
            // can re-load commands and re-start the VAD (in gst-vad.c).
            timer_settings_changed();
        }

        // Record ringing sound for Skype?
        if (GTK_IS_SWITCH(skype_switch)) {
            bool_val = gtk_switch_get_active(GTK_SWITCH(skype_switch));
            // Save value
            skype_set_record_ringing_sound(bool_val);
        }
    }
    break;


    default:
        break;
    }

    g_current_type = 0;
    g_changed_types = 0;

    // User may have modified media-profiles (on the "Recording commands" tab).
    // Update GUI.
    update_main_GUI();

    // Destroy this dialog
    if (GTK_IS_WIDGET(g_dialog)) {
        gtk_widget_destroy(GTK_WIDGET(g_dialog));
    }
    g_dialog = NULL;
}

