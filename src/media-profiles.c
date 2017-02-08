/*
 * Copyright (c) Linux community.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License (GPL3), or any later version.
 *
 * This library is distributed in the hope that it will be useful,-
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Library General Public License 3 for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License 3 along with this program; if not, see /usr/share/common-licenses/GPL file
 * or <http://www.gnu.org/licenses/>.
*/
#include "media-profiles.h"
#include "log.h"
#include "dconf.h"
#include "utility.h"
#include "support.h"

// Automatic plugin installation has not been implemented yet.
#include <gst/pbutils/missing-plugins.h>
#include <gst/pbutils/install-plugins.h>

// DefaultProfiles contains hard-coded values for media profiles and GStreamer pipelines.
// User can modify these values in [Additiona settings] dialog. Modified values are saved in the GSettings/DConf registry.

// Notice: You should delete the "media-profiles" values from GSettings if you change these hard-coded lines (and plan to re-compile this app).
// Start dconf-editor and browse to apps -> audio-recorder (key: "media-profiles").
// $ dconf-editor

// Or run:
// $ audio-recorder --reset
//
ProfileRec DefaultProfiles[] = {
    {"CD Quality, AAC 44KHz",       "m4a",  "", "audio/x-raw,rate=44100,channels=2 ! avenc_aac compliance=-2 ! avmux_mp4"},
    {"CD Quality, Lossless 44KHz",  "flac", "", "audio/x-raw,rate=44100,channels=2 ! flacenc name=enc"},
    {"CD Quality, Lossy 44KHz",     "ogg",  "", "audio/x-raw,rate=44100,channels=2 ! vorbisenc name=enc quality=0.5 ! oggmux"},
    {"CD Quality, MP3 Lossy 44KHz", "mp3",  "", "audio/x-raw,rate=44100,channels=2 ! lamemp3enc name=enc target=0 quality=2 ! xingmux ! id3mux"},
    {"Lossless WAV 22KHz",          "wav",  "", "audio/x-raw,rate=22050,channels=1 ! wavenc name=enc"},
    {"Lossless WAV 44KHz",          "wav",  "", "audio/x-raw,rate=44100,channels=2 ! wavenc name=enc"},
    {"Lossy Speex 32KHz",           "spx",  "", "audio/x-raw,rate=32000,channels=2 ! speexenc name=enc ! oggmux"},
};

// EDIT 02.jan.2017: Removed MP2 from DefaultPofiles[].
//                   Changed channels=1 on pipeline for WAV 22050hz. 
// {"CD Quality, MP2 Lossy 44KHz", "mp2",  "", "audio/x-raw,rate=44100,channels=2 ! twolame name=enc mode=0 bitrate=192 ! id3mux"},
//
// EDIT 03.feb.2015: The 3.rd field "" is not used any more. We keep it for future.
//
// EDIT 09.oct.2014: Consider removing MP2/twolame from the list.
//
// EDIT 01.aug.2014: "faac" plugin was removed from "gstreamer1.0-plugins-bad" in Ubuntu 14.10 beta (was it removed temporarily?)
// Ref: https://bugs.launchpad.net/ubuntu/+source/gst-plugins-bad1.0/+bug/1299376
// {"aac",   '"CD Quality, AAC 44KHz",     "m4a", "faac",      "audio/x-raw,rate=44100,channels=2 ! faac profile=2 ! ffmux_mp4"},
//
// This is now replaced by avenc_aac element. It comes from the gstreamer1.0-libav package.
// avenc_aac compliance=-2 ! avmux_mp4.
//
// Other notes:
// ! audio/x-raw,rate=44100 ! vorbisenc ! queue ! mux.audio_0
// https://delog.wordpress.com/2011/09/19/capture-audio-with-gstreamer-on-pandaboard/


// List of media profiles
static GList *g_profile_list = NULL;

void free_func(gpointer data);

void media_profiles_init() {
    LOG_DEBUG("Init media-profiles.c. \n");
}

void media_profiles_exit() {
    LOG_DEBUG("Clean up media-profiles.c. \n");

    // Clean up. Free the list
    media_profiles_clear();
}

GList *profiles_get_list() {
    media_profiles_load();

    return g_profile_list;
}

void profiles_reset() {
    // Save empty [] to GSettings

    // Array type
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ssss)"));

    GVariant *variant = g_variant_new("a(ssss)", builder);
    conf_save_variant("saved-profiles", variant);

    g_variant_builder_unref(builder);

    // Clear lists
    media_profiles_clear();

    // Reload
    media_profiles_load();
}

void profiles_save_configuration() {
    // Save g_profile_list to GSettings/DConf

    // Array type
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ssss)"));

    GList *item = g_list_first(g_profile_list);
    while (item) {
        ProfileRec *rec = (ProfileRec*)item->data;

        g_variant_builder_add(builder, "(ssss)", check_null(rec->id), check_null(rec->ext), "", check_null(rec->pipe));

        item = g_list_next(item);
    }

    GVariant *variant = g_variant_new("a(ssss)", builder);
    conf_save_variant("saved-profiles", variant);

    //g_variant_unref(variant);
    g_variant_builder_unref(builder);
}


void profiles_delete(const gchar *id) {
    // Delete entry from g_profile_list

    media_profiles_load();

    ProfileRec *rec = profiles_find_rec(id);

    if (!rec) {
        return;
    }

    GList *ptr = g_list_find(g_profile_list, rec);
    g_profile_list = g_list_remove_link(g_profile_list, ptr);
    g_list_free_full(ptr, free_func);

    profiles_save_configuration();
}


void profiles_update(gchar *old_id, gchar *id, gchar *file_ext, gchar *pipe_text) {

    LOG_DEBUG("Update or insert: old name=%s, new name=%s, file ext=%s, pipe=%s\n", old_id, id, file_ext, pipe_text);

    media_profiles_load();

    gboolean new_rec = FALSE;

    ProfileRec *rec = profiles_find_rec(old_id);

    if (!rec) {
        rec = profiles_find_rec(id);
    }

    if (!rec) {
        new_rec = TRUE;
        rec = g_malloc0(sizeof(ProfileRec));
    }

    g_free(rec->id);
    g_free(rec->ext);
    g_free(rec->pipe);

    rec->id = g_strdup(id);
    rec->ext = g_strdup(file_ext);
    // rec->not_used =
    rec->pipe = g_strdup(pipe_text);

    if (new_rec) {
        g_profile_list = g_list_append(g_profile_list, rec);
    }

    profiles_save_configuration();

}

ProfileRec *profiles_find_rec(const gchar *id) {

    media_profiles_load();

    GList *item = g_list_first(g_profile_list);
    while (item) {
        ProfileRec *rec = (ProfileRec*)item->data;
        if (!g_strcmp0(rec->id, id)) {
            return rec;
        }

        item = g_list_next(item);
    }
    return NULL;
}

ProfileRec *profiles_find_for_ext(const gchar *ext) {

    media_profiles_load();

    GList *item = g_list_first(g_profile_list);
    while (item) {
        ProfileRec *rec = (ProfileRec*)item->data;
        if (!g_strcmp0(rec->ext, ext)) {
            return rec;
        }

        item = g_list_next(item);
    }
    return NULL;
}

void free_func(gpointer data) {
    ProfileRec *rec = (ProfileRec*)data;
    if (!rec) return;

    LOG_DEBUG("Free data for: %s\n", rec->id);

    g_free(rec->id);
    g_free(rec->ext);
    // g_free(rec->not_used);
    g_free(rec->pipe);

    g_free(rec);
}

void media_profiles_clear() {
    g_list_free_full(g_profile_list, free_func);
    g_profile_list = NULL;
}

static void load_modified_values() {
    GVariant *var = NULL;
    conf_get_variant_value("saved-profiles", &var);

    gsize n = g_variant_n_children(var);

    gsize i = 0;
    for (i=0; i<n; i++) {

        ProfileRec *rec = g_malloc0(sizeof(ProfileRec));

        GVariant *tmp = g_variant_get_child_value(var, i);

        gchar *id = NULL;
        gchar *ext = NULL;
        gchar *not_used = NULL;
        gchar *pipe = NULL;

        g_variant_get(tmp, "(&s&s&s&s)", &id, &ext, &not_used, &pipe, NULL);

        LOG_DEBUG("Loading user-saved media profile from GSettings/DConf:%s\n", id);

        rec->id = g_strdup(id);
        rec->ext = g_strdup(ext);
        // rec->not_used =
        rec->pipe = g_strdup(pipe);

        g_variant_unref(tmp);

        g_profile_list = g_list_append(g_profile_list, rec);

        // Should we free these? No!
        //g_free(id);
        //g_free(ext);
        //g_free(not_used)
        //g_free(pipe);
    }
}

static void load_default_values() {
    guint i = 0;

    for (i=0; i<sizeof(DefaultProfiles) / sizeof(DefaultProfiles[0]); i++) {

        ProfileRec *rec = g_malloc0(sizeof(ProfileRec));

        rec->id = g_strdup(DefaultProfiles[i].id);
        rec->ext = g_strdup(DefaultProfiles[i].ext);
        // rec->not_used =
        rec->pipe = g_strdup(DefaultProfiles[i].pipe);
        LOG_DEBUG("Taking hard-coded profile from media-profiles.c: %s\n", rec->id);

        g_profile_list = g_list_append(g_profile_list, rec);
    }
}

void media_profiles_load() {
    if (g_list_length(g_profile_list) > 0) return;

    // Check first user modified values (from Gsettings).
    // See: dconf-editor, key: apps -> audio-recorder -> saved-profiles.
    load_modified_values();

    if (g_list_length(g_profile_list) > 0) {
        return;
    }

    // Take hard-coded values from media-profiles.c.
    load_default_values();
}

gchar *profiles_get_extension(const gchar *id) {
    // Return file extension (such as .mp3, .ogg) for the given profile

    const ProfileRec *p = profiles_find_rec(id);
    if (!p) return NULL;

    LOG_DEBUG("Get file extension for: %s (%s)\n", id, p->ext);

    // Caller should g_free() this value
    return g_strdup(p->ext);
}

gchar *profiles_get_pipeline(const gchar *id) {
    // Return pipeline fragment for the given profile

    const ProfileRec *p = profiles_find_rec(id);
    if (!p) return NULL;

    LOG_DEBUG("Get pipeline for: %s (%s)\n", id, p->pipe);

    // Caller should g_free() this value
    return g_strdup(p->pipe);
}

gboolean profiles_check_id(const gchar *id) {
    // Check if the id is a valid
    const ProfileRec *p = profiles_find_rec(id);
    return (p != NULL);
}

void profiles_get_data(GtkWidget *widget) {
    // Fill GtkListStore with audio formats.

    LOG_DEBUG("profiles_get_data()\n");

    GtkComboBox *combo = GTK_COMBO_BOX(widget);

    // Set store
    GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(combo));

    gtk_list_store_clear(GTK_LIST_STORE(store));

    // Load profiles
    media_profiles_load();

    GList *item = g_list_first(g_profile_list);
    while (item) {
        ProfileRec *p = (ProfileRec*)item->data;

        // Typical audio format list is
        //  CD Quality, AAC (.m4a)
        //  CD Quality, Lossless (.flac)
        //  CD Quality, Lossy (.ogg)
        //  CD Quality, MP2 (.mp2)
        //  CD Quality, MP3 (.mp3)
        //  Voice, Lossless (.wav)
        //  Voice, Lossy (.spx)

        gchar *e = g_utf8_strup(p->ext, -1);
        gchar *n = g_utf8_strdown(p->id, -1);
        gchar *txt = g_strdup_printf(".%s  (%s)", e, n);

        GtkTreeIter iter;

        // Add OGG type "CD Quality, Lossy" as first (becomes default)
        if (!g_strcmp0(p->ext, "ogg") || !g_strcmp0(p->ext, "oga")) {
            gtk_list_store_prepend(GTK_LIST_STORE(store), &iter);
        } else {
            // Add last
            gtk_list_store_append(GTK_LIST_STORE(store), &iter);
        }

        gtk_list_store_set(GTK_LIST_STORE(store), &iter, 0, p->id, 1, txt, -1);

        g_free(txt);
        g_free(e);
        g_free(n);

        item = g_list_next(item);
    }
}

gchar *profiles_get_selected_id(GtkWidget *widget) {
    // Return selected profile id

    GtkComboBox *combo = GTK_COMBO_BOX(widget);
    g_return_val_if_fail(GTK_IS_COMBO_BOX(combo), NULL);

    GtkTreeIter iter;
    gchar *id = NULL;

    if (!gtk_combo_box_get_active_iter(combo, &iter)) return NULL;

    gtk_tree_model_get(gtk_combo_box_get_model(combo), &iter, COL_PROFILE_ID, &id, -1);

    // The caller should g_free() this value
    return id;
}

GtkWidget *profiles_create_combobox() {
    // Create a GtkComboBox with N_PROFILE_COLUMNS. Populate it with supported audio formats.

    // Create store
    GtkListStore *store = gtk_list_store_new(N_PROFILE_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);

    // Add data to the listbox
    GtkWidget *combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    profiles_get_data(combo);

    // Unref store
    g_object_unref(store);

    // "Id" column, invisible
    GtkCellRenderer *cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", COL_PROFILE_ID, NULL);
    gtk_cell_renderer_set_visible (cell, FALSE);

    // "Name" column, visible
    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", COL_PROFILE_TXT, NULL);

    return combo;
}

static gboolean profiles_check_plugin(const gchar *id, gchar **missing_elems[], gchar **details[]) {
    // Check if appropriate Gstreamer plugins have been installed for the given pipeline.
    // Return TRUE is everything is OK.
    // Return FALSE if some Gstreamer plugins (and gstreamer1.0-plugins-* packages) are missing.
    // If FALSE, return also missing element names in missing_elems[] and installation details in details[] array.

    gboolean ok = TRUE;

    const ProfileRec *rec = profiles_find_rec(id);
    if (!rec) return FALSE;

    LOG_DEBUG("Check and test pipeline for: %s (%s)\n", id, rec->pipe);

    // Pipeline to test
    gchar *pipe_str = g_strdup_printf("fakesrc ! %s ! fakesink", rec->pipe);

    // Parse pipeline
    GError *error = NULL;
    GstParseContext *ctx = gst_parse_context_new();

    GstElement *p = gst_parse_launch_full(pipe_str, ctx, GST_PARSE_FLAG_FATAL_ERRORS, &error);

    guint count = 0;

    if (error) {
        ok = FALSE;

        // Create place for 20 arguments + ending NULL
        *missing_elems = g_new(gchar*, 20 + 1);
        (*missing_elems)[0] = NULL;

        *details = g_new(gchar*, 20 + 1);
        (*details)[0] = NULL;

        // Get all missing element names and ask installation details
        gchar **elems = gst_parse_context_get_missing_elements(ctx);

        GstElement *pipeline = gst_pipeline_new("pipeline");

        guint i = 0;
        while (elems && elems[i] && count < 20) {
            LOG_ERROR("Missing Gstreamer element: %s.\n", elems[i]);

            GstMessage *msg = gst_missing_element_message_new(pipeline, elems[i]);
            if (msg) {
                gchar *detail_str = gst_missing_plugin_message_get_installer_detail(msg);

                LOG_MSG("Installation string for %s: %s.\n", elems[i], detail_str);

                if (detail_str) {
                    // Collect element names and details to be returned
                    (*missing_elems)[count] = g_strdup(elems[i]);
                    (*details)[count] = g_strdup(detail_str);
                    count++;
                }

                g_free(detail_str);
            }

            gst_message_unref(msg);

            i++;
        }

        g_error_free(error);
        g_strfreev(elems);
        gst_object_unref(pipeline);
    }

    if (count > 0) {
        (*missing_elems)[count] = NULL;
        (*details)[count] = NULL;
    }

    gst_parse_context_free(ctx);

    g_free(pipe_str);

    gst_object_unref(p);

    return ok;
}


#if 0
static gboolean profiles_check_pipe(const gchar *pipe) {

    // Pipeline to test
    gchar *test = g_strdup_printf ("fakesrc ! %s ! fakesink", pipe);

    gboolean pipe_OK = TRUE;

    // Parse pipeline
    GError *error = NULL;
    GstElement *p = gst_parse_launch(test, &error);
    if (p == NULL || error) {
        g_error_free(error);
        pipe_OK = FALSE;
    }

    gst_object_unref(p);

    g_free(test);
    return pipe_OK;
}
#endif

void report_plugin_return_code(GstInstallPluginsReturn ret) {
    // Just print GstInstallPluginsReturn code in clear text

    switch (ret) {

    case GST_INSTALL_PLUGINS_STARTED_OK:
        LOG_MSG("Installation of Gstreamer-plugins started with success (GST_INSTALL_PLUGINS_STARTED_OK).\n");
        break;

    case GST_INSTALL_PLUGINS_SUCCESS:
        LOG_MSG("Installation of Gstreamer-plugins completed with success (GST_INSTALL_PLUGINS_SUCCESS).\n");
        break;

    case GST_INSTALL_PLUGINS_NOT_FOUND:
        LOG_ERROR("Installation of Gstreamer-plugins failed (GST_INSTALL_PLUGINS_SUCCESS).\n");
        break;

    case GST_INSTALL_PLUGINS_ERROR:
        LOG_ERROR("Installation of Gstreamer-plugins failed (GST_INSTALL_PLUGINS_ERROR).\n");
        break;

    case GST_INSTALL_PLUGINS_PARTIAL_SUCCESS:
        LOG_ERROR("Installation of Gstreamer-plugins completed (GST_INSTALL_PLUGINS_PARTIAL_SUCCESS).\n");
        break;

    case GST_INSTALL_PLUGINS_USER_ABORT:
        LOG_ERROR("Installation of Gstreamer-plugins aborted by user (GST_INSTALL_PLUGINS_USER_ABORT).\n");
        break;

    case GST_INSTALL_PLUGINS_CRASHED:
        LOG_ERROR("Installation of Gstreamer-plugins failed (GST_INSTALL_PLUGINS_CRASHED).\n");
        break;

    case GST_INSTALL_PLUGINS_INVALID:
        LOG_ERROR("Installation of Gstreamer-plugins failed (GST_INSTALL_PLUGINS_INVALID).\n");
        break;

    case GST_INSTALL_PLUGINS_INTERNAL_FAILURE:
        LOG_ERROR("Installation of Gstreamer-plugins failed (GST_INSTALL_PLUGINS_INTERNAL_FAILURE).\n");
        break;

    case GST_INSTALL_PLUGINS_HELPER_MISSING:
        LOG_ERROR("Installation of Gstreamer-plugins failed (GST_INSTALL_PLUGINS_HELPER_MISSING).\n");
        break;

    case GST_INSTALL_PLUGINS_INSTALL_IN_PROGRESS:
        LOG_MSG("Installation of Gstreamer-plugins already in progress (GST_INSTALL_PLUGINS_INSTALL_IN_PROGRESS).\n");
        break;

    default:
        LOG_ERROR("Installation of Gstreamer-plugins failed (UNKNOWN ERROR CODE).\n");
    }
}

void plugin_inst_callback(GstInstallPluginsReturn ret, gpointer user_data) {
    // Callback function.
    // Called from gst_install_plugins_async(...)

    report_plugin_return_code(ret);

    switch (ret) {

    case GST_INSTALL_PLUGINS_SUCCESS:
        LOG_MSG("Automatic installation of Gstreamer-plugins completed with success.\n");

    // Notice: Fall through
    case GST_INSTALL_PLUGINS_PARTIAL_SUCCESS:

        // Update plugin-registry
        if (gst_update_registry()) {
            LOG_MSG("Update of Gstreamer's plugin-registry completed with success.\n");
        } else {
            LOG_ERROR("Update of Gstreamer's plugin-registry failed.\n");
        }

        break;

    default:
        ;
        // report_plugin_return_code() has already printed this error message.
        // LOG_ERROR("Automatic installation of Gstreamer-plugins failed!\n");
    }

    // Free details[] array
    gchar **details = (gchar **)user_data;
    g_strfreev(details);
}

gboolean profiles_test_plugin(gchar *id, gchar **err_msg) {
    // Check if an appropriate GStreamer plugin has been installed and the pipeline is valid.
    *err_msg = NULL;

    media_profiles_load();

    ProfileRec *rec = profiles_find_rec(id);

    if (!rec) {
        // Weird error. Will certainly fail later on.
        return TRUE;
    }

    gchar **missing_elems = NULL;
    gchar **details = NULL;
    gboolean ok = profiles_check_plugin(id, &missing_elems, &details);

    gchar *str = NULL;

    if (ok == FALSE && missing_elems && details) {

        // Convert missing_elems[] to string
        str = g_strjoinv(", ", missing_elems);
        LOG_ERROR("To support %s format you should install Gstreamer-plugins for %s.\n", rec->ext, str);

        GstInstallPluginsReturn ret = gst_install_plugins_async((const gchar * const*)details, NULL, plugin_inst_callback, (gpointer)details);

        report_plugin_return_code(ret);

        if (ret == GST_INSTALL_PLUGINS_STARTED_OK) {

            *err_msg = g_strdup_printf(_("Please install additional plugins (from gstreamer1.0-plugins-* package) to support the %s format.\n"), rec->ext);
            // details[] is freed by the callback function.

        } else if (ret == GST_INSTALL_PLUGINS_INSTALL_IN_PROGRESS) {

            *err_msg = g_strdup_printf(_("Please install additional plugins (from gstreamer1.0-plugins-* package) to support the %s format.\n"), rec->ext);
            g_strfreev(details);

        } else if (ret == GST_INSTALL_PLUGINS_ERROR) {

            *err_msg = g_strdup_printf(_("Please install additional plugins (from gstreamer1.0-plugins-* package) to support the %s format.\n"), rec->ext);
            g_strfreev(details);
        }

    }

    g_free(str);
    g_strfreev(missing_elems);

    return ok;
}

