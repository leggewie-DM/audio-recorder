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
#include "settings.h"
#include "media-profiles.h"
#include "support.h"
#include "log.h"
#include "utility.h"
#include "dconf.h"

#include "audio-sources.h"
#include "gst-pipeline.h"

static GtkWidget *g_profiles = NULL;
static GtkWidget *g_saved_id = NULL; // Hidden field to hold currect selection of g_profiles

static GtkWidget *g_file_ext = NULL;
static GtkWidget *g_pipe_text = NULL;

static void get_profiles() {
    GtkTreeModel *store = gtk_combo_box_get_model(GTK_COMBO_BOX(g_profiles));
    gtk_list_store_clear(GTK_LIST_STORE(store));

    media_profiles_load();

    GList *list = profiles_get_list();

    GList *item = g_list_first(list);
    while (item) {
        ProfileRec *rec = (ProfileRec*)item->data;

        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(g_profiles), rec->id, rec->id);

        LOG_DEBUG("Loading media profile: %s\n", rec->id);

        item = g_list_next(item);
    }
}

static void populate_fields(gchar *profile_name) {

    gtk_entry_set_text(GTK_ENTRY(g_saved_id), "");
    gtk_entry_set_text(GTK_ENTRY(g_file_ext), "");

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_pipe_text));
    gtk_text_buffer_set_text(buf, "", -1);

    const ProfileRec *rec = profiles_find_rec(profile_name);
    if (!rec) {
        return;
    }

    // Save current selection in g_saved_id
    gtk_entry_set_text(GTK_ENTRY(g_saved_id), rec->id);

    gtk_entry_set_text(GTK_ENTRY(g_file_ext), rec->ext);

    gtk_text_buffer_set_text(buf, rec->pipe, -1);
}

static void read_fields(gchar **old_name, gchar **name, gchar **file_ext, gchar **pipe_text) {

    *old_name = g_strdup(gtk_entry_get_text(GTK_ENTRY(g_saved_id)));

    *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(g_profiles));

    *file_ext = g_strdup(gtk_entry_get_text(GTK_ENTRY(g_file_ext)));

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_pipe_text));

    GtkTextIter start_t, end_t;
    gtk_text_buffer_get_start_iter(buf, &start_t);
    gtk_text_buffer_get_end_iter(buf, &end_t);

    *pipe_text = gtk_text_buffer_get_text(buf, &start_t, &end_t, FALSE);
}

static gboolean check_fields() {

    gboolean ret = FALSE;

    gchar *old_name = NULL;
    gchar *name = NULL;
    gchar *file_ext =  NULL;
    gchar *pipe_text = NULL;
    read_fields(&old_name, &name, &file_ext, &pipe_text);

    if (str_length0(name) < 1) {
        goto LBL_1;
    }

    if (str_length0(file_ext) < 1) {
        goto LBL_1;
    }

    if (str_length0(pipe_text) < 1) {
        goto LBL_1;
    }

    // Ok
    ret = TRUE;

LBL_1:
    g_free(old_name);
    g_free(name);
    g_free(file_ext);
    g_free(pipe_text);

    LOG_DEBUG("check_fields() function returns: %s\n", ret ? "TRUE" : "FALSE");

    return ret;
}

static gboolean find_row(gchar *find_name, GtkTreeIter *iter) {
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(g_profiles));

    gboolean ret = gtk_tree_model_get_iter_first(model, iter);
    while (ret) {

        gchar *name = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(model), iter, 0, &name, -1);

        if (!g_strcmp0(find_name, name)) {
            g_free(name);
            return TRUE;
        }

        g_free(name);

        ret = gtk_tree_model_iter_next(model, iter);
    }

    return FALSE;
}

static void new_profile() {
    populate_fields("");

    GtkWidget *entry = gtk_bin_get_child(GTK_BIN(g_profiles));
    gtk_entry_set_text(GTK_ENTRY(entry), "");

    LOG_DEBUG("Create a new profile.\n");

    gtk_widget_grab_focus(g_profiles);
}

static void delete_profile() {
    gchar *old_name = NULL;
    gchar *name = NULL;
    gchar *file_ext =  NULL;
    gchar *pipe_text = NULL;
    read_fields(&old_name, &name, &file_ext, &pipe_text);

    const gchar *str = NULL;

    if (str_length0(old_name) > 0) {
        str = old_name;
    } else {
        str = name;
    }

    profiles_delete(str);

    populate_fields("");

    get_profiles();

    gtk_combo_box_set_active_iter(GTK_COMBO_BOX(g_profiles), NULL);

    GtkWidget *entry = gtk_bin_get_child(GTK_BIN(g_profiles));
    gtk_entry_set_text(GTK_ENTRY(entry), "");

    g_free(old_name);
    g_free(name);
    g_free(file_ext);
    g_free(pipe_text);
}

static void save_profile() {

    if (!check_fields()) {
        return;
    }

    gchar *old_name = NULL;
    gchar *name = NULL;
    gchar *file_ext =  NULL;
    gchar *pipe_text = NULL;
    read_fields(&old_name, &name, &file_ext, &pipe_text);

    LOG_DEBUG("Save profile: old name:%s, new name:%s, file ext:%s, pipe text:%s\n", old_name, name, file_ext, pipe_text);

    profiles_update(old_name, name, file_ext, pipe_text);

    get_profiles();

    GtkTreeIter iter;
    gboolean found = find_row(name, &iter);

    if (found) {
        gtk_combo_box_set_active_iter(GTK_COMBO_BOX(g_profiles), &iter);
    }

    g_free(old_name);
    g_free(name);
    g_free(file_ext);
    g_free(pipe_text);
}

static void load_defaults() {

    profiles_reset();

    populate_fields("");

    GtkWidget *entry = gtk_bin_get_child(GTK_BIN(g_profiles));
    gtk_entry_set_text(GTK_ENTRY(entry), "");

    get_profiles();
}

static void title_changed(GtkComboBox *widget, gpointer data) {

    gchar *name =	gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(g_profiles));
    const gchar *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(g_profiles));

    LOG_DEBUG("Selected profile:%s\n", name);

    // Strings are equal?
    if (g_strcmp0(name, id) == 0) {
        // Combo: selection by using popup/down menu

        populate_fields(name);
    }

    g_free(name);
}

static void show_cmd_dialog(gchar *cmd) {
    // Show dialog with the GStreamer command

    GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Recording command"),
                        NULL,
                        GTK_DIALOG_DESTROY_WITH_PARENT,
                        _("_OK"),
                        GTK_RESPONSE_ACCEPT,
                        NULL);

    gtk_window_set_transient_for(GTK_WINDOW(dialog), win_settings_get_window());

    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 300);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *vbox0 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(vbox0);

    // Get content area
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(content), vbox0, TRUE, TRUE, 0);

    GtkWidget *text_field = gtk_text_view_new();
    gtk_widget_show(text_field);

    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(text_field), GTK_WRAP_WORD_CHAR);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_field));

    gtk_text_buffer_set_text(buffer, cmd,  -1);
    gtk_box_pack_start(GTK_BOX(vbox0), text_field, TRUE, TRUE, 0);

    gtk_dialog_run(GTK_DIALOG(dialog));
    if (GTK_IS_WIDGET(dialog)) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

static void show_recording_command() {
    // Parms to construct a pipeline
    PipelineParms *parms = g_malloc0(sizeof(PipelineParms));

    gchar *old_name = NULL;
    gchar *name = NULL;
    gchar *ext = NULL;
    gchar *pipe_text = NULL;

    read_fields(&old_name, &name, &ext, &pipe_text);

    // Translators: This is a filename "test.xxx".
    parms->filename = g_strdup_printf(_("test.%s"), ext);

    // Get partial pipline for this profile_id
    parms->profile_str = g_strdup(pipe_text);
    parms->file_ext = g_strdup(ext);

    // Get audio source and device list
    gchar *audio_source = NULL;
    parms->dev_list = audio_sources_get_device_NEW(&audio_source);
    parms->source = audio_source;

    GString *str = pipeline_create_command_str(parms);

    // Translators: This is shown in Additional settings -> Recording commands -> [Show Cmd].
    const gchar *s1 = _("# Copy and paste the following command to a terminal window.");

    // Translators: This is shown in Additional settings -> Recording commands -> [Show Cmd].
    const gchar *s2 = _("# The devices are taken from the GUI (main window).");

    // Translators: This is shown in Additional settings -> Recording commands -> [Show Cmd].
    const gchar *s3 = _("# Use the pactl tool to list all available audio (input) devices in your system.");

    // pactl commands to list available input devices.
    const gchar *s4a = "# pactl list | grep -A3 'Source #'";
    const gchar *s4b = "# pactl list short sources | cut -f2";

    // Translators: This is shown in Additional settings -> Recording commands -> [Show Cmd].
    gchar *s5 = g_strdup_printf(_("# This command will record to %s file."), parms->filename);

    // Translators: This is shown in Additional settings -> Recording commands -> [Show Cmd].
    const gchar *s6 = _("# Press Control-C to terminate the recording.");

    gchar *tmp_str = g_strdup_printf("%s\n%s\n%s\n%s\n%s\n%s\n%s\n\n", s1, s2, s3, s4a, s4b, s5, s6);

    str = g_string_prepend(str, tmp_str);

    g_free(s5);
    g_free(tmp_str);

    gchar *cmd = g_string_free(str, FALSE);

    // Show gst_launch command in a dialog
    show_cmd_dialog(cmd);

    g_free(old_name);
    g_free(name);
    g_free(ext);
    g_free(pipe_text);

    g_free(cmd);

    pipeline_free_parms(parms);
    parms = NULL;
}

static void show_cmd_clicked(GtkButton *button, gpointer user_data) {
    show_recording_command();
}

static void reset_clicked(GtkButton *button, gpointer user_data) {
    load_defaults();
}

static void new_clicked(GtkButton *button, gpointer user_data) {
    new_profile();
}

static void delete_clicked(GtkButton *button, gpointer user_data) {
    delete_profile();
}

static void save_clicked(GtkButton *button, gpointer user_data) {
    save_profile();
}

GtkWidget *page_to_edit_pipelines() {

    GtkWidget *vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);

    // Create a GtkGrid.
    // Ref: https://developer.gnome.org/gtk3/stable/GtkGrid.html
    GtkWidget *grid2 = gtk_grid_new();

    gtk_grid_set_column_homogeneous(GTK_GRID(grid2), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(grid2), 1);

    gtk_box_pack_start(GTK_BOX(vbox2), grid2, FALSE, TRUE, 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid2), 3);

    // Editable ComboBox with media-profiles

    // "Title:" label
    // Translators: This is a GUI label. Keep it short.
    GtkWidget *label0 = gtk_label_new(_("Title:"));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid2), label0, 0, 0, 1, 1);

    g_profiles = gtk_combo_box_text_new_with_entry();
    g_signal_connect(g_profiles, "changed", G_CALLBACK(title_changed), NULL);
    gtk_grid_attach_next_to(GTK_GRID(grid2), g_profiles, label0, GTK_POS_RIGHT, 3, 1);

    // Hidden field to hold selected g_profiles entry
    g_saved_id = gtk_entry_new();
    gtk_widget_hide(g_saved_id);

    // File extension.

    // Translators: This is a GUI label. Keep it short.
    label0 = gtk_label_new(_("File extension:"));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid2), label0, 0, 1, 1, 1);

    g_file_ext = gtk_entry_new();
    gtk_grid_attach_next_to(GTK_GRID(grid2), g_file_ext, label0, GTK_POS_RIGHT, 2, 1);

    // GStreamer pipeline. Press the [Show cmd] button to see the _entire_ Gstreamer pipeline.
    // Run & test the pipeline in a terminal window. Ok?
    // Translators: This is a GUI label (for GStreamer pipeline). Keep it short.
    label0 = gtk_label_new(_("Command:"));
    gtk_widget_set_halign(label0, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid2), label0, 0, 2, 1, 1);

    g_pipe_text = gtk_text_view_new();

    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_pipe_text), GTK_WRAP_WORD);

    gtk_box_pack_start(GTK_BOX(vbox2), g_pipe_text, TRUE, TRUE, 0);

    GtkWidget *vbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);

    GtkWidget *box0 = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_show(box0);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(box0), GTK_BUTTONBOX_START);

    // Translators: Button label in Additional settings -> Recording commands.
    GtkWidget *button0 = gtk_button_new_with_label(_("Show cmd"));
    g_signal_connect(button0, "clicked", G_CALLBACK(show_cmd_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box0), button0, TRUE, FALSE, 0);

    // Translators: Button label in Additional settings -> Recording commands.
    button0 = gtk_button_new_with_label(_("Reset"));
    g_signal_connect(button0, "clicked", G_CALLBACK(reset_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box0), button0, TRUE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox3), box0, FALSE, FALSE, 0);

    box0 = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_show(box0);

    gtk_button_box_set_layout(GTK_BUTTON_BOX(box0), GTK_BUTTONBOX_END);

    // Translators: Button label in Additional settings -> Recording commands.
    button0 = gtk_button_new_with_label(_("New"));
    g_signal_connect(button0, "clicked", G_CALLBACK(new_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box0), button0, TRUE, FALSE, 0);

    // Translators: Button label in Additional settings -> Recording commands.
    button0 = gtk_button_new_with_label(_("Delete"));
    g_signal_connect(button0, "clicked", G_CALLBACK(delete_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box0), button0, TRUE, FALSE, 0);

    // Translators: Button label in Additional settings -> Recording commands.
    button0 = gtk_button_new_with_label(_("Save"));
    g_signal_connect(button0, "clicked", G_CALLBACK(save_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box0), button0, TRUE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(vbox3), box0, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox2), vbox3, FALSE, TRUE, 0);

    // Fill text combo with profiles names/titles
    get_profiles();

    // Select same media-format row as in the main GUI.

    // Get saved selection (this is set in the main GUI)
    gchar *str_value = NULL;
    conf_get_string_value("media-format", &str_value);

    // Find this row
    GtkTreeIter iter;
    gboolean found =  find_row(str_value, &iter);

    // Select it
    if (found) {
        gtk_combo_box_set_active_iter(GTK_COMBO_BOX(g_profiles), &iter);

    } else {
        // Or simply select row at index 0
        gtk_combo_box_set_active(GTK_COMBO_BOX(g_profiles), 0);
    }

    g_free(str_value);

    return vbox2;

}


