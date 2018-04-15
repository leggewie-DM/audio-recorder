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
#include "rec-window.h"
#include "rec-manager.h"
#include "rec-manager-struct.h"
#include "log.h"
#include "dconf.h"
#include "utility.h"
#include "support.h"
#include "gst-recorder.h"
#include "dbus-player.h"

// Command queue
static GAsyncQueue *g_cmd_queue = NULL;

// Last used/saved command
static RecorderCommand *g_last_rec_cmd = NULL;

// Message thread
static guint g_thread_id = 0;

static void rec_manager_free_command(RecorderCommand *cmd);

static gboolean rec_manager_command_thread(gpointer user_data);

void rec_manager_init() {
    LOG_DEBUG("Init rec-manager.c.\n");

    // Create a message queue
    // Ref: https://www.gtk.org/api/2.6/glib/glib-Asynchronous-Queues.html
    g_cmd_queue = g_async_queue_new();

    // Message thread within GTK's main loop
    // Ref: https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html
    g_thread_id =  g_timeout_add_full(G_PRIORITY_DEFAULT, 200, rec_manager_command_thread, NULL, NULL);

    // Init recorder.c
    rec_module_init();
}

void rec_manager_exit() {
    LOG_DEBUG("Clean up rec-manager.c.\n");

    // Clean up recorder.c
    rec_module_exit();

    // Remove command thread
    g_source_remove(g_thread_id);
    g_thread_id = 0;

    // Unref message queue
    if (g_cmd_queue) {
        g_async_queue_unref(g_cmd_queue);
    }
    g_cmd_queue = NULL;

    // Free g_last_rec_cmd
    rec_manager_free_command(g_last_rec_cmd);
    g_last_rec_cmd = NULL;
}

void rec_manager_print_command(RecorderCommand *cmd) {
    if (!cmd) return;

    const gchar *type_str = NULL;

    switch (cmd->type) {
    case RECORDING_STOP:
        type_str = "RECORDING_STOP";
        break;

    case RECORDING_CONTINUE:
        type_str = "RECORDING_CONTINUE";
        break;

    case RECORDING_START:
        type_str = "RECORDING_START";
        break;

    case RECORDING_PAUSE:
        type_str = "RECORDING_PAUSE";
        break;

    case RECORDING_NOTIFY_MSG:
        type_str = "RECORDING_NOTIFY_MSG";
        break;

    case RECORDING_DEVICE_CHANGED:
        type_str = "RECORDING_DEVICE_CHANGED";
        break;

    case RECORDING_PROFILE_CHANGED:
        type_str = "RECORDING_PROFILE_CHANGED";
        break;

    case RECORDING_SHOW_WINDOW:
        type_str = "RECORDING_SHOW_WINDOW";
        break;

    case RECORDING_HIDE_WINDOW:
        type_str = "RECORDING_HIDE_WINDOW";
        break;

    case RECORDING_QUIT_LOOP:
        type_str = "RECORDING_QUIT_LOOP";
        break;

    case RECORDING_QUIT_APP:
        type_str = "RECORDING_QUIT_APP";
        break;

    default:
        type_str = "Unknown recording command";
    }

    // Suppress "not used" message
    if (type_str) {
        ;
    }

    if (cmd->type != RECORDING_NOTIFY_MSG) {
        LOG_DEBUG("%s: %s, %s, %s, time=%d/%d flags=%d\n", type_str, cmd->track, cmd->artist, cmd->album, cmd->track_pos, cmd->track_len, cmd->flags);
    } else {
        LOG_DEBUG("%s: %s\n", type_str, cmd->track);
    }
}

gint64 rec_manager_get_stream_time() {
    // Get and return current recording time (stream time) in seconds.
    return rec_get_stream_time();
}

void rec_manager_update_gui() {
    // Update GUI to reflect the status of recording
    win_update_gui();
}

void rec_manager_update_level_bar(gdouble norm_rms, gdouble norm_peak) {
    // Update gtklevelbar
    win_update_level_bar(norm_rms, norm_peak);
}

const gchar *rec_manager_get_state_name(gint state) {
    // Return name of the recording state, state of pipeline.

    switch (state) {
    case GST_STATE_PAUSED:
        return "PAUSED";

    case GST_STATE_PLAYING:
        return "RECORDING ON";

    case GST_STATE_READY:
        return "IN READY STATE";

    case GST_STATE_NULL:
        return "RECORDING OFF";

    default:
        return "UNKNOWN STATE";
    }
}

void rec_manager_flip_recording() {
    // Start, continue or stop recording

    // Get recording status
    gint old_status = -1;
    gint pending = -1;
    rec_manager_get_state(&old_status, &pending);

    switch (old_status) {
    case GST_STATE_PAUSED:
        // Continue recording
        rec_manager_continue_recording();
        break;

    case GST_STATE_PLAYING:
        // Stop recording
        rec_manager_stop_recording();
        break;

    default:
        // Start recording
        rec_manager_start_recording();
    }
}

void rec_manager_set_filename_label(gchar *filename) {
    // Set filename label
    win_set_filename(filename);
}

void rec_manager_set_time_label(gchar *label_txt) {
    // Set time label
    win_set_time_label(label_txt);
}

void rec_manager_set_size_label(gchar *label_txt) {
    // Set file size label
    win_set_size_label(label_txt);
}

void rec_manager_set_error_text(gchar *error_txt) {
    // Set error label
    win_set_error_text(error_txt);
}

void rec_manager_get_state(gint *status, gint *pending) {
    // Return recording status
    rec_get_state(status, pending);
}

gchar *rec_manager_get_output_filename() {
    // Return output filename
    return rec_get_output_filename();
}

void rec_manager_show_window(gboolean show) {
    // Show or hide application window

    if (show)
        // Send RECORDING_SHOW_WINDOW message to the queue
        rec_manager_send_command_ex(RECORDING_SHOW_WINDOW, NULL/*track*/, NULL/*artist*/, NULL/*album*/, 0/*track_pos*/, 0/*track_len*/, 0/*flags*/);
    else
        // Send RECORDING_HIDE_WINDOW message to the queue
        rec_manager_send_command_ex(RECORDING_HIDE_WINDOW, NULL/*track*/, NULL/*artist*/, NULL/*album*/, 0/*track_pos*/, 0/*track_len*/, 0/*flags*/);
}

void rec_manager_quit_application() {
    //  Quit application

    // Send RECORDING_QUIT_APP message to the queue
    rec_manager_send_command_ex(RECORDING_QUIT_APP, NULL/*track*/, NULL/*artist*/, NULL/*album*/, 0/*track_pos*/, 0/*track_len*/, 0/*flags*/);
}

void rec_manager_start_recording() {
    // Start recording

    // Send RECORDING_START message to the queue
    // The filename will be auto-generated because the track name is NULL.
    rec_manager_send_command_ex(RECORDING_START, NULL/*track*/, NULL/*artist*/, NULL/*album*/, 0/*track_pos*/, 0/*track_len*/, 0/*flags*/);
}

void rec_manager_stop_recording() {
    // Stop recording

    // Send RECORDING_STOP message to the queue
    rec_manager_send_command_ex(RECORDING_STOP, NULL/*track*/, NULL/*artist*/, NULL/*album*/, 0/*track_pos*/, 0/*track_len*/, 0/*flags*/);
}

void rec_manager_continue_recording() {
    // Send RECORDING_CONTINUE message to the queue (Continues paused recording. This does not start if recording is off)
    rec_manager_send_command_ex(RECORDING_CONTINUE, NULL/*track*/, NULL/*artist*/, NULL/*album*/, 0/*track_pos*/, 0/*track_len*/, 0/*flags*/);
}

void rec_manager_pause_recording() {
    // Pause recording

    // Send RECORDING_PAUSE message to the queue
    rec_manager_send_command_ex(RECORDING_PAUSE, NULL/*track*/, NULL/*artist*/, NULL/*album*/, 0/*track_pos*/, 0/*track_len*/, 0/*flags*/);
}

gboolean rec_manager_is_recording() {
    // Is recording?
    return rec_is_recording();
}

void rec_manager_send_gui_msg(gchar *msg) {
    // Display a message in the GUI (normally a red GtkLabel in the GUI)

    // Send RECORDING_NOTIFY_MSG to the queue
    rec_manager_send_command_ex(RECORDING_NOTIFY_MSG, msg/*the msg*/, NULL/*artist*/, NULL/*album*/, 0/*track_pos*/, 0/*track_len*/, 0/*flags*/);
}

static void rec_manager_free_command(RecorderCommand *cmd) {
    // Free command
    if (!cmd) return;
    g_free(cmd->track);
    g_free(cmd->artist);
    g_free(cmd->album);
    g_free(cmd);
}

void rec_manager_send_command_ex(enum CommandType type, gchar *track, gchar *artist, gchar *album, gint track_pos, gint track_len, guint flags) {
    // Build the command
    RecorderCommand *cmd = g_malloc0(sizeof(RecorderCommand));
    cmd->type = type;
    cmd->track = g_strdup(track);
    cmd->artist = g_strdup(artist);
    cmd->album = g_strdup(album);
    cmd->track_pos = track_pos;
    cmd->track_len = track_len;
    cmd->flags = flags;

    // Send the command. The command que will free the cmd after usage.
    rec_manager_send_command(cmd);
}

void rec_manager_send_command(RecorderCommand *cmd) {

    // Push command to the queue
    g_async_queue_push(g_cmd_queue, (gpointer)cmd);
}

gboolean rec_manager_command_thread(gpointer user_data) {
    // Read next command from the queue
    RecorderCommand *cmd = (RecorderCommand*)g_async_queue_try_pop(g_cmd_queue);
    if (!cmd) {
        // TRUE: Continue calling this function
        return TRUE;
    }

    // Debug print
#if defined(ACTIVE_DEBUGGING) || defined(DEBUG_ALL)
    rec_manager_print_command(cmd);
#endif

    if (cmd->type == RECORDING_START) {
        // Save the values so gst-recorder.c can grab them
        conf_save_string_value("track/track-name", check_null(cmd->track));
        conf_save_int_value("track/track-pos", cmd->track_pos);
        conf_save_int_value("track/track-len", cmd->track_len);
        conf_save_string_value("track/artist-name", check_null(cmd->artist));
        conf_save_string_value("track/album-name", check_null(cmd->album));
    }

    // Verify the delete flag and filename
    gboolean del_flag = FALSE;
    if (cmd->flags == RECORDING_DELETE_FILE) {
        gchar *filename = NULL;
        conf_get_string_value("track/last-file-name", &filename);

        // Remove path and file extension
        gchar *path=NULL;
        gchar *base = NULL;
        gchar *ext = NULL;
        split_filename3(filename, &path, &base, &ext);

        // Filenames do match?
        del_flag = (filename && !g_strcmp0(base, cmd->track));

        g_free(path);
        g_free(base);
        g_free(ext);
        g_free(filename);
    }

    switch (cmd->type) {
    case RECORDING_STOP:
        rec_stop_recording(del_flag/*delete file?*/);
        break;

    case RECORDING_START:
        rec_start_recording();
        break;

    case RECORDING_PAUSE:
        rec_pause_recording();
        break;

    case RECORDING_CONTINUE:
        rec_continue_recording();
        break;

    case RECORDING_NOTIFY_MSG:
        win_set_error_text(cmd->track);
        break;

    case RECORDING_DEVICE_CHANGED:
        win_refresh_device_list();
        break;

    case RECORDING_PROFILE_CHANGED:
        win_refresh_profile_list();
        break;

    case RECORDING_SHOW_WINDOW:
        win_show_window(TRUE);
        break;

    case RECORDING_HIDE_WINDOW:
        win_show_window(FALSE);
        break;

    case RECORDING_QUIT_LOOP:
        rec_stop_recording(FALSE);
        break;

    case RECORDING_QUIT_APP:
        rec_stop_recording(FALSE);

        // Quit application
        win_quit_application();
        break;

    } // switch ...

    // Free the lastly saved command
    rec_manager_free_command(g_last_rec_cmd);

    // Save this command
    g_last_rec_cmd = cmd;

    // TRUE: Continue calling this function
    return TRUE;
}



