#ifndef _UTILITY_H
#define _UTILITY_H

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gio/gio.h> /* GFile */

#define MAX_PATH_LEN 2048
#define DEF_STR_LEN 511

gboolean run_tool_for_file(gchar *file, gchar *alternative_tool, GError **error);

gboolean is_file_writable(gchar *filename);

gint messagebox_error(const gchar *message, GtkWidget *window);

void split_filename2(gchar *path, gchar **filepath,  gchar **filebase);
void split_filename3(gchar *path, gchar **filepath,  gchar **filebase, gchar **fileext);

guint str_length0(const gchar *s);
guint str_length(const gchar *s, guint maxlen);
void str_copy(gchar *dest, gchar *src, guint len);

void str_trim(gchar *s);

gchar *check_null(gchar *s);

gchar *replace_str(gchar *what, gchar *width);

gchar *format_file_size(guint64 fsize);

gboolean paths_are_equal(gchar *path1, gchar *path2);

guint64 get_file_size(gchar *filename);

gchar *substitute_time_and_date_pattern(gchar *pattern);

void seconds_to_h_m_s(guint seconds, guint *hh, guint *mm, guint *ss);

GPid exec_command_async(gchar **argv, GError **error);

gboolean exec_command_sync(char *command, gint *status, gchar **stderr, gchar **stdout);
gboolean exec_shell_command(gchar *command, gchar **stderr, gchar **stdout);

gchar *find_command_path(gchar *command);

gboolean run_simple_command(gchar *cmd, gchar *args);

gchar *get_nth_arg(gchar *str, guint n, gboolean ret_rest);
gchar *get_last_arg(gchar *str);

GPid get_PID(gchar *app_name);
gboolean check_PID(GPid pid);

void purify_filename(gchar *filename, gboolean purify_all);

gchar *read_file_content(gchar *filename, GError **error);
gboolean save_file_content(gchar *filename, gchar *text, GError **error);

gchar *get_home_dir();
GList *get_directory_listing(gchar *path, gchar *file_pattern);

gchar *get_filename_pattern();
gchar *get_audio_folder();

GdkPixbuf *get_pixbuf_from_file(gchar *filename, gint width, gint height);

void str_cut_nicely(gchar *s, glong to_len, glong min_len);

GList *str_list_copy(GList *list);
void str_list_free(GList *list);
void str_list_print(gchar *prefix, GList *lst);
gboolean str_lists_equal(GList *l1, GList *l2);

gchar *read_value_from_keyfile(gchar *key_file, gchar *group_name, gchar *key_name);
GdkPixbuf *load_icon_pixbuf(gchar *icon_name, guint _size);

void kill_frozen_instances(gchar *program_path, GPid preserve_pid);
void kill_program_by_name(gchar *app_name, GPid preserve_pid);

#ifndef g_strrstr0
gchar *g_strrstr0(gchar *haystack, gchar *needle);
#endif

#endif

