#ifndef _REC_MANAGER__H_
#define _REC_MANAGER__H_

#include <glib.h>
#include <gdk/gdk.h>
#include <unistd.h>

#include "rec-manager-struct.h"

void rec_manager_init();
void rec_manager_exit();

void rec_manager_update_gui();

void rec_manager_set_time_label(gchar *label_txt);
void rec_manager_set_size_label(gchar *label_txt);
void rec_manager_set_error_text(gchar *error_txt);
void rec_manager_set_filename_label(gchar *filename);

gint64 rec_manager_get_stream_time();

void rec_manager_get_state(gint *status, gint *pending);
void rec_manager_continue_recording();
void rec_manager_start_recording();
void rec_manager_stop_recording();
void rec_manager_pause_recording();
gboolean rec_manager_is_recording();

void rec_manager_show_window(gboolean show);
void rec_manager_quit_application();

const gchar *rec_manager_get_state_name(gint state);

void rec_manager_send_gui_msg(gchar *msg);

gchar *rec_manager_get_output_filename();

void rec_manager_flip_recording();

void rec_manager_update_level_bar(gdouble norm_rms, gdouble norm_peak);

void rec_manager_print_command(RecorderCommand *cmd);
void rec_manager_send_command(RecorderCommand *cmd);
void rec_manager_send_command_ex(enum CommandType type, gchar *track, gchar *artist, gchar *album, gint track_pos, gint track_len, guint flags);

#endif


