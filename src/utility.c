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
#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>
#include <time.h>

#include "support.h" // _(x)
#include "utility.h"
#include "log.h"
#include "dconf.h"

// Some utility functions

gboolean run_tool_for_file(gchar *file, gchar *alternative_tool, GError **error) {
    // Run suitable program/tool for the given file.

    if (error) {
        *error = NULL;
    }

    // Create place for 2 arguments + NULL
    gchar **argv = g_new(gchar*, 3);

    // Try: xdg-open
    gchar *tool = find_command_path("xdg-open");

    if ((!tool) && alternative_tool) {
        // Try: alternative_tool
        tool = find_command_path(alternative_tool);
    }

    LOG_DEBUG("Running:%s \"%s\"\n", tool, file);

    // The tool command
    argv[0] = g_strdup(tool);
    // File or URL
    argv[1] = g_strdup(file);
    argv[2] = NULL;

    // Now create a process and run the command.
    // It will return immediately because it's asynchronous.
    exec_command_async(argv, error);

    gboolean ret = TRUE;

    if (error && *error) {
        // Error
        ret = FALSE;
    }

    g_free(tool);
    g_strfreev(argv);

    return ret;
}

gboolean is_file_writable(gchar *filename) {
    // Test if filename is writable
    GFileOutputStream *fstream;
    GError *error = NULL;
    GFile *file = g_file_new_for_path(filename);
    gboolean del = FALSE;

    if (g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        fstream = g_file_append_to(file, G_FILE_CREATE_NONE, NULL, &error);
    } else {
        fstream = g_file_create(file, G_FILE_CREATE_NONE, NULL, &error);
        del = (!error);
    }

    gboolean ret = TRUE;

    if (error || !fstream) {
        ret = FALSE;
    }

    if (error)
        g_error_free(error);

    g_object_unref(fstream);
    g_object_unref(file);

    if (del)
        g_remove(filename);

    return ret;
}

gchar *check_null(gchar *s) {
    // Return safe (not null) string
    static gchar *not_null = "\0";
    if (!s)
        return not_null;
    else
        return s;
}

guint str_length(const gchar *s, guint maxlen) {
    // Return length of s
    if (!s) return 0;
    return g_utf8_strlen(s, maxlen);
}

guint str_length0(const gchar *s) {
    return str_length(s, MAX_PATH_LEN);
}

void str_copy(gchar *dest, gchar *src, guint len) {
    // Strncpy src to dest
    if (!dest) return;

    if (!src) {
        *dest = '\0';
    } else {
        strncpy(dest, src, len);
    }
}

void str_trim(gchar *s) {
    // Remove leading and trailing whitespaces from a string. Edit in place.
    if (s) {
        g_strstrip(s);
    }
}

void str_cut_nicely(gchar *s, glong to_len, glong min_len) {
    // Cut string nicely to given to_len.
    glong l = g_utf8_strlen(s, -1);
    if (l <= to_len) return;

    glong i = to_len - 1;

    // Find space
    while (i >= 0) {
        if ( *(s + i) == ' ') break;
        i--;
    }

    if (i >= min_len) {
        *(s + i) = '\0';
    } else {
        *(s + to_len -1) = '\0';
    }
}

void split_filename2(gchar *path, gchar **filepath,  gchar **filebase) {
    // Split path to filepath + filename
    gchar *fileext;
    split_filename3(path, filepath,  filebase, &fileext);
    if (fileext) {
        /* TODO: Not very optimal code this... path is much longer than (filebase + '.' + fileext)
        */
        gchar *result = g_strdup(path);
        g_sprintf(result, "%s.%s", *filebase, fileext);
        g_free(fileext);
        g_free(*filebase);
        *filebase = result;
    }
}

void split_filename3(gchar *path, gchar **filepath,  gchar **filebase, gchar **fileext) {
    // Split path to filepath + filename + fileext
    static gchar buf[MAX_PATH_LEN];

    *filepath = NULL;
    *filebase = NULL;
    *fileext = NULL;

    if (!path || str_length(path, MAX_PATH_LEN) < 1) return;

    glong pos_ext = -1;
    glong pos_base = -1;

    glong path_len = str_length(path, MAX_PATH_LEN);

    gchar *pos = g_utf8_offset_to_pointer(path, path_len - 1);
    while (pos >= path) {
        gunichar unichar = g_utf8_get_char(pos);

        /* Find last '.' */
        if (unichar == '.' && pos_ext < 0)
            pos_ext = g_utf8_pointer_to_offset(path, pos);

        /* Find last '/' */
        else if (unichar == '/' && pos_base < 0) {
            pos_base = g_utf8_pointer_to_offset(path, pos);
            break;
        }
        pos = g_utf8_prev_char(pos);
    }

    glong len;

    // Take the file name (basename) before file extension
    if (pos_base > -1) {
        if (pos_ext > -1)
            len = pos_ext - pos_base -1;
        else
            len = path_len - pos_base -1;
    } else {
        if (pos_ext > -1)
            len = pos_ext;
        else
            len = path_len;
    }

    if (len > 0) {
        memset(buf, '\0', MAX_PATH_LEN);
        gchar *pos = g_utf8_offset_to_pointer(path, pos_base+1);
        g_utf8_strncpy(buf, pos, (gsize)len);
        *filebase = g_strdup(buf);
    }

    // Take the unused path before basename and file extension
    if (pos_base > -1) {
        memset(buf, '\0', MAX_PATH_LEN);
        g_utf8_strncpy(buf, path, (gsize)pos_base+1);
        *filepath = g_strdup(buf);
    }

    // Take file extension without "."
    if (pos_ext > -1) {
        len = path_len - pos_ext - 1;
        if (len > 0 ) {
            memset(buf, '\0', MAX_PATH_LEN);
            gchar *pos = g_utf8_offset_to_pointer(path, pos_ext+1);
            g_utf8_strncpy(buf, pos, (gsize)len);
            *fileext = g_strdup(buf);
        }
    }

    /* The caller should g_free() all returned values:
       g_free(filepath)
       g_free(filebase)
       g_free(fileext)
    */
}

gboolean paths_are_equal(gchar *path1, gchar *path2) {
    // Check if paths are equal.
    // TODO: It would be smarter to test inodes.

    // Add trailing /
    gchar *p1 = g_build_filename(path1, "/", NULL);
    gchar *p2 = g_build_filename(path2, "/", NULL);

    gboolean ret = !g_strcmp0(p1, p2);

    g_free(p1);
    g_free(p2);

    return ret;
}

gchar *format_file_size(guint64 fsize) {
    // Format file size string
    gint div = 1;
    gchar *label;

    if (fsize > 1E9) {
        div = 1E9;
        label = "GB";
    } else if (fsize > 1E6) {
        div = 1E6;
        label = "MB";
    } else if (fsize > 1E3) {
        div = 1E3;
        label = "KB";
    } else {
        div = 1;
        label = ""; // "bytes"
    }

    gchar *txt = g_strdup_printf("%02.1F %s", ((gdouble)fsize / (gdouble)div), label);

    // The caller should g_free() this value
    return txt;
}

gchar *substitute_time_and_date_pattern(gchar *pattern) {
    // Substitue time+date pattern

    // Typical pattern is: "%Y-%m-%d-%H:%M:%S"
    // See: https://linux.die.net/man/3/strftime
    time_t t;
    struct tm *tmp;
    t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL) {
        return NULL;
    }

    static gchar buf[128];

    strftime(buf, 128, pattern, tmp);

    // Caller should g_free() this value
    return g_strdup(buf);
}

void seconds_to_h_m_s(guint seconds, guint *hh, guint *mm, guint *ss) {
    // Split seconds to hours, minutes and seconds.
    *hh = seconds / 3600;
    seconds -= (*hh * 3600);

    *mm = seconds / 60;
    seconds -= (*mm * 60);

    *ss = seconds;
}

guint64 get_file_size(gchar *filename) {
    // Get file size.
    // Ref: https://developer.gnome.org/glib/2.34/glib-File-Utilities.html#g-stat
    GStatBuf fstat;
    if (!g_stat(filename, &fstat)) {
        return (guint64)fstat.st_size;
    }
    return 0L;
}

gint messagebox_error(const gchar *message, GtkWidget *window) {
    // Show dialog box with error message and wait for user's response.

    GtkWidget *dialog;
    GtkResponseType answer;

    dialog = gtk_message_dialog_new((window ? GTK_WINDOW(window) : NULL),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_CLOSE,
                                    message, NULL);

    gchar *prog_name = get_program_name();
    gtk_window_set_title(GTK_WINDOW(dialog), prog_name);
    g_free(prog_name);

    answer = gtk_dialog_run(GTK_DIALOG(dialog));
    if (answer == -1) {}
    // answer not used

    gtk_widget_destroy(dialog);

    return TRUE;
}

gboolean exec_command_sync(char *command, gint *status, gchar **serr, gchar **sout) {
    // Start, spawn a child process and run the given command.
    // The started process is synchronous thefore this function will wait until the child terminates.
    // This functions returns TRUE if the child completed without errors, otherwise FALSE.

    *sout = NULL;
    *serr = NULL;
    *status = 0; /* ok */

    GError *error = NULL;
    gint ret = TRUE; /* ok */

    if (!g_spawn_command_line_sync(command, sout, serr, status, &error)) {
        // Got an error

        // Add error->message to stderr
        if (error) {
            if (*serr) {
                gchar *tmp = g_strdup_printf("%s\n%s", *serr, error->message);
                g_free(*serr);
                *serr = tmp;
            } else {
                *serr = g_strdup(error->message);
            }
        }

        ret = FALSE;
    }

    if (*sout)
        str_trim(*sout);/* Modify in place */

    if (str_length(*sout, 1024) < 1) {
        g_free(*sout);
        *sout = NULL;
    }

    if (*serr)
        str_trim(*serr);/* Modify in place */

    if (str_length(*serr, 1024) < 1) {
        g_free(*serr);
        *serr = NULL;
    }

    if (error)
        g_error_free(error);

    return ret;
}

gboolean exec_shell_command(gchar *command, gchar **serr, gchar **sout) {
    // Execute shell command. Return result in serr and sout.

    gint status = -1;

    *serr = *sout = NULL;

    gboolean ret = exec_command_sync(command, &status, serr, sout);

    if ((!ret) || *serr) {
        gchar *msg = g_strdup_printf("exec_shell_command (%s) failed. %s\n", command, *serr);
        LOG_ERROR(msg);
        g_free(msg);
        ret = FALSE;
    }

    return ret;
}

gchar *find_command_path(gchar *command) {
    // Use "which" to locate absolute path of the given program.
    // An example:
    // find_command_path("gimp") will return "/usr/bin/gimp".

    gchar *line = g_strdup_printf("which %s", command);

    gint status;
    gchar *serr = NULL;
    gchar *sout = NULL;

    if (!exec_command_sync(line, &status, &serr, &sout)) {
        gchar *msg = g_strdup_printf("Cannot execute command '%s'.\n%s.\n", line, serr);
        LOG_ERROR("%s", msg);
        g_free(msg);
    }

    g_free(line);
    g_free(serr);

    // The caller should g_free() this value after usage
    return sout;
}

GPid exec_command_async(gchar **argv, GError **error) {
    // Start, spawn a child process and run the command given by argv list.
    // The process is asynchronous and returns immediately back to the caller.
    // This function returns PID of the child process. Returns GError.
    GPid child_pid;

    if (error) *error = NULL;

    if (!g_spawn_async(NULL/*inherit parent*/, argv, NULL/*inherit parent*/,
                       G_SPAWN_LEAVE_DESCRIPTORS_OPEN | G_SPAWN_SEARCH_PATH,
                       NULL/*no setup function*/, NULL/*no user data*/, &child_pid, error)) {

        // Translators: This is an error message.
        LOG_ERROR(_("Exec error. Cannot start process %s.\n%s.\n"), argv[0], (*error)->message);
        child_pid = -1;
    }

    // Return pid
    return child_pid;
}

gboolean run_simple_command(gchar *cmd, gchar *args) {
    // Run a simple command with args

    // Find a path?
    gchar *path = find_command_path(cmd);
    gchar *c = NULL;
    if (path) {
        c = g_strdup_printf("%s %s", path, args);
    } else {
        c = g_strdup_printf("%s %s", cmd, args);
    }

    gchar *sout = NULL;
    gchar *serr = NULL;

    gboolean ret = exec_shell_command(c, &serr, &sout);

    g_free(path);
    g_free(c);
    g_free(sout);
    g_free(serr);

    return ret;
}

GPid get_PID(gchar *app_name) {
    // Return process PID for the given app_name
    gchar *serr = NULL;
    gchar *sout = NULL;

    gchar *cmd = g_strdup_printf("ps -o %%p --no-heading -C %s", app_name);

    exec_shell_command(cmd, &serr, &sout);
    g_free(cmd);

    GPid p = -1L;

    if (sout)
        p = atol(sout);

    g_free(sout);
    g_free(serr);

    return p;
}

gboolean check_PID(GPid pid) {
    // Check if process id (pid) is alive and running
    gchar *serr = NULL;
    gchar *sout = NULL;

    gchar *cmd = g_strdup_printf("ps --pid %d -o pid h", pid);
    exec_shell_command(cmd, &serr, &sout);
    g_free(cmd);

    gint64 p = -1L;

    if (sout)
        p = atol(sout);

    g_free(sout);
    g_free(serr);

    return p == pid && p > 0;
}

gchar *get_nth_arg(gchar *str, guint n, gboolean ret_rest) {
    // Return n'th token of the  str. The tokens must be separated by spaces " ".
    // Eg. get_nth_arg("AA BB CC DD EE", 3, FALSE) will return "CC".
    //     get_nth_arg("AA BB CC DD EE", 3, TRUE) will return "CC DD EE".
    // The first token # is 1, second is 2, etc.
    gchar *p1 = NULL;
    gchar *p2 = NULL;
    if (!str) return NULL;

    guint i = 0;
    p1 = str;
    while (1) {
        if (p1) {
            p2 = g_strstr_len(p1+1, -1, " ");
        }

        i = i + 1;
        if (i >= n) break;

        if (p2) {
            p1 = p2+1;
        } else {
            break;
        }
    }

    // n'th argument was seen?
    if (i < n) {
        // No.
        return NULL;
    }

    gchar *s = NULL;
    if (p1 && ret_rest) {
        s = g_strdup(p1);
    } else if (p1 && p2) {
        s = g_strndup(p1, p2 - p1);
    } else if (p1) {
        s = g_strdup(p1);
    } else {
        s = g_strdup(str);
    }

    str_trim(s);

    // The caller should g_free() this value
    return s;
}

gchar *get_last_arg(gchar *str) {
    // Find and return last token after space
    gchar *s = NULL;

    // Find last space
    gchar *p = g_strrstr(str, " ");
    if (p) {
        s = g_strdup(p+1);
    } else {
        s = g_strdup(str);
    }

    // The caller should g_free() this value
    return s;
}

void purify_filename(gchar *filename, gboolean purify_all) {
    // Purify filename, remove unwished characters.
    // Edit filename in place.

    gchar *delims = NULL;
    if (purify_all) {
        // Remove also '/' and '.'
        delims = "@&$^?()|~{}[]\\=+<>;\"'`,*./";
    } else {
        delims = "@&$^?()|~{}[]\\=+<>;\"'`,*";
    }

    // Replace illegals with space
    if (filename) {
        g_strdelimit(filename, delims, ' ');
    }
}

gchar *read_file_content(gchar *filename, GError **error) {
    // Read and return file content
    gchar *text = NULL;
    gsize len = 0;

    if (error) {
        *error = NULL;
    }

    // Ref: https://developer.gnome.org/glib/unstable/glib-File-Utilities.html#g-file-get-contents
    g_file_get_contents(filename, &text, &len, error);
    if (*error) {
        g_free(text);
        text = NULL;
    }

    // Caller should check and free the error value.
    // Caller should g_free() this value.
    return text;
}

gboolean save_file_content(gchar *filename, gchar *text, GError **error) {
    // Save file content
    gboolean ret = TRUE;

    if (error) {
        *error = NULL;
    }

    // Ref: https://developer.gnome.org/glib/unstable/glib-File-Utilities.html#g-file-set-contents
    ret = g_file_set_contents(filename, text, -1, error);

    // Caller should check and free the error value.
    return ret;
}

gchar *get_home_dir() {
    // Return user's $HOME directory

    // Read $HOME environment variable
    const char *home_dir = g_getenv("HOME");
    if (!home_dir) {
        // Get home from passwd file
        home_dir = g_get_home_dir();
    }

    // Caller should g_free() this value
    return g_strdup(home_dir);
}

GList *get_directory_listing(gchar *path, gchar *file_pattern) {
    // Get directory listing and return a GList of filenames that matches the given file_pattern.
    GError *error = NULL;

    // Ref: https://developer.gnome.org/glib/unstable/glib-File-Utilities.html
    GDir *dir = g_dir_open(path, 0, &error);
    if (error) {
        LOG_ERROR("Cannot read directory %s. %s\n", path, error->message);
        g_error_free(error);
        return NULL;
    }

    GList *list = NULL;

    // Create file pattern
    GPatternSpec *patt = g_pattern_spec_new(file_pattern);

    // Loop for all filenames
    const gchar *filename = g_dir_read_name(dir);
    while (filename) {
        // Filename matches the pattern?
        gboolean ret = g_pattern_match_string(patt, filename);
        if (ret) {
            // Make path + filename
            gchar *p_ = g_build_filename(path, filename, NULL);

            // Yes, add filename to the list.
            list = g_list_append(list, p_);

            // Note: p_ will be freed when the list is destroyed.
        }
        // Next filename
        filename = g_dir_read_name(dir);
    }

    g_dir_close(dir);
    g_pattern_spec_free(patt);

    // Caller should free this list with:
    //  g_list_free_full(list, g_free);
    //  list = NULL;
    return list;
}

// Some support functions

gchar *get_filename_pattern() {
    // Return pattern that will be base for a new, unique filename (includes filename + date+time pattern)

    gchar *filename_pattern = NULL;
    conf_get_string_value("filename-pattern", &filename_pattern);
    str_trim(filename_pattern);

    // Pattern cannot have "/" character (edit in place)
    if (filename_pattern) {
        g_strdelimit(filename_pattern, "/", '-');
    }

    // The pattern is ok?
    if (str_length(filename_pattern, 1024) < 2) {
        // Set default pattern
        g_free(filename_pattern);

        // Translators: This is a default filename pattern. You can keept this as it is.
        filename_pattern = g_strdup(_("%Y-%m-%d-%H:%M:%S"));
        conf_save_string_value("filename-pattern", filename_pattern);
    }

    // The caller should g_free() this value
    return filename_pattern;
}

gchar *get_audio_folder() {
    // Return directory where we store audio files.
    // Normally: /home/username/Audio

    gchar *folder_name = NULL;
    conf_get_string_value("folder-name", &folder_name);
    str_trim(folder_name);

    // Has folder name?
    if (str_length(folder_name, 1024) < 1) {
        g_free(folder_name);
        // Set default to "/home/username/Audio"

        // Get $HOME
        gchar *home = get_home_dir();

        // If user has a $HOME/Music folder, then use the English word.
        gchar *path = g_strdup_printf("%s/%s", home, "Music");

        if (g_file_test(path, G_FILE_TEST_IS_DIR))
            folder_name = g_strdup_printf("%s/%s", home, "Audio");
        else
            // Translators: This is a directory name like "/home/username/Audio".
            // We store all recordings in this directory.
            folder_name = g_strdup_printf("%s/%s", home, _("Audio"));

        // Save default
        conf_save_string_value("folder-name", folder_name);

        g_free(home);
        g_free(path);
        // g_mkdir(folder_name, 0755/* rwxr-xr-x */);
    }

    // The caller should g_free() this value
    return folder_name;
}

GdkPixbuf *get_pixbuf_from_file(gchar *filename, gint width, gint height) {
    if (!filename) return NULL;

    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    if (error) {
        g_warning("Could not load image from %s. %s\n", filename, error->message);
        g_error_free(error);
        pixbuf = NULL;
    }

    if (!pixbuf) return NULL;

    gint w = gdk_pixbuf_get_width(pixbuf);
    gint h = gdk_pixbuf_get_height(pixbuf);
    if ((w > width && width > 0) || (h > height && height > 0)) {
        GdkPixbuf *img = gdk_pixbuf_scale_simple(pixbuf, width, height, GDK_INTERP_HYPER);
        g_object_unref(pixbuf);
        pixbuf = img;
    }

    return pixbuf;
}

/*
gchar *g_strlist_to_string(GList *lst, gchar *delim) {
	GString *str = g_string_new(NULL);

	GList *n = g_list_first(lst);
	while (n) {
		gchar *txt = (gchar*)n->data;
		g_string_append_printf(str, "%s%s", txt, delim);
		n = g_list_next(n);
	}

	gchar *s = g_string_free(str, FALSE);

	// The caller should g_free() this value
	return s;
}
*/

void str_list_free(GList *list) {
    // Free GList of strings.
    g_list_foreach(list, (GFunc)g_free, NULL);
    g_list_free(list);
    list = NULL;
}

GList *str_list_copy(GList *list) {
    GList *new_list = NULL;
    GList *item = g_list_first(list);
    while (item) {
        gchar *s = g_strdup((gchar*)item->data);
        new_list = g_list_append(new_list, s);
        item = g_list_next(item);
    }
    return new_list;
}

void str_list_print(gchar *prefix, GList *lst) {
    // Print GList of strings.

    if (g_list_length(lst) < 1) {
        LOG_MSG("%s: <the list is empty>\n", prefix);
    }

    GList *n = g_list_first(lst);
    while (n) {
        gchar *txt = (gchar*)n->data;
        LOG_MSG("%s: %s\n", prefix, txt);
        n = g_list_next(n);
    }
}

gboolean str_lists_equal(GList *l1, GList *l2) {
    // Compare 2 string lists (GLists).
    // Return TRUE if lists are equal.

    if (g_list_length(l1) != g_list_length(l2)) {
        return FALSE;
    }

    guint i = 0;
    for (i=0; i < g_list_length(l1); i++) {
        gchar *s1 = (gchar*)g_list_nth_data(l1, i);
        gchar *s2 = (gchar*)g_list_nth_data(l2, i);

        if (g_strcmp0(s1, s2)) {
            return FALSE;
        }
    }
    return TRUE;
}

gchar *g_strrstr0(gchar *haystack, gchar *needle) {
    // Same as g_strrstr() but this tests for NULL values (and avoids annoying warnings).
    if (!(haystack && needle)) return NULL;
    return g_strrstr(haystack, needle);
}

gchar *read_value_from_keyfile(gchar *key_file, gchar *group_name, gchar *key_name) {
    // Open key_file and return value for the given key.
    // Sample call:
    // gchar *key_file = "/usr/share/applications/banshee.desktop";
    // gchar *icon_name = read_value_from_keyfile(key_file, "Desktop Entry", "Icon");
    gchar *value = NULL;
    GError *error = NULL;

    if (!key_file) {
        return NULL;
    }

    // Load key_file
    GKeyFile *g_key = g_key_file_new();
    g_key_file_load_from_file(g_key, key_file, G_KEY_FILE_NONE, &error);

    if (error) {
        goto LBL_1;
    }

    // Read value
    value = g_key_file_get_value(g_key, group_name, key_name, &error);

LBL_1:
    if (error) {
        g_error_free(error);
    }

    g_key_file_free(g_key);

    // Caller should g_free() this value
    return value;
}

GdkPixbuf *load_icon_pixbuf(gchar *icon_name, guint _size) {
    // Load icon pixbuf from current icon theme.
    GdkPixbuf *pixbuf = NULL;

    if (!icon_name) {
        return NULL;
    }

    // Current icon theme
    GtkIconTheme *theme = gtk_icon_theme_get_default();

    // Load icon from its theme
    pixbuf = gtk_icon_theme_load_icon(theme, icon_name, _size, 0, NULL);

    // Got it?
    if (GDK_IS_PIXBUF(pixbuf)) {
        goto LBL_1;
    }

    // Executable name != icon_name.

    // Try to get icon_name from xxxx.desktop file.

    gchar *desktop_file = g_strdup_printf("%s.desktop", icon_name);
    // Eg. "banshee.desktop"

    // Find its full path, eg. /usr/share/applications/banshee.desktop
    const gchar *filename = NULL;
    GDesktopAppInfo *app_info = g_desktop_app_info_new(desktop_file);
    if (app_info) {
        filename = g_desktop_app_info_get_filename(app_info);
    }

    // Example: /usr/share/applications/nettfy.desktop:
    // [Desktop Entry]
    // Name=Nettfy player
    // GenericName=Music Player
    // Comment=Listen to music using nettfy
    // Icon=nettfy-linux-64x64
    // Exec=nettfy-bin %U

    // Get real icon name from desktop file (Icon=)
    gchar *icon_n = read_value_from_keyfile((gchar*)filename, "Desktop Entry", "Icon");

    if (icon_n) {
        // Load icon
        pixbuf = gtk_icon_theme_load_icon(theme, icon_n, _size, 0, NULL);
    }

    g_free(icon_n);
    g_free(desktop_file);
    g_object_unref(app_info);

LBL_1:

    // Some icons are large. Force to _size.
    if (GDK_IS_PIXBUF(pixbuf)) {
        GdkPixbuf *img = gdk_pixbuf_scale_simple(pixbuf, _size, _size, GDK_INTERP_HYPER);
        g_object_unref(pixbuf);
        pixbuf = img;
    }

    // Caller should g_object_unref() this value
    return pixbuf;
}

void kill_program_by_name(gchar *app_name, GPid preserve_pid) {
    // Kill all app_name processes. But do not kill preserve_pid.
    // Use this to kill programs that do not respond to client requests (dbus request).
    gchar *serr = NULL;
    gchar *sout = NULL;

    // Get list of PIDs for app_name
    gchar *cmd = g_strdup_printf("ps -o %%p --no-heading -C %s", app_name);

    exec_shell_command(cmd, &serr, &sout);
    g_free(cmd);

    if (!sout) {
        goto LBL_1;
    }

    // For each PID in the list...
    gchar **args = g_strsplit(sout, "\n", -1);
    guint i=0;
    while (args && args[i]) {
        GPid pid = atol(args[i]);
        if (pid != preserve_pid && pid > 1) {
            // $ kill -9 PID
            kill(pid, SIGKILL);
        }
        i++;
    }

    // Free args
    g_strfreev(args);
    args=NULL;

LBL_1:
    g_free(sout);
    g_free(serr);
}

void kill_frozen_instances(gchar *program_path, GPid preserve_pid) {
    // Terminate possibly frozen instances of audio-recorder.
    gchar *app_path = NULL;
    gchar *app_base = NULL;
    split_filename2(program_path, &app_path, &app_base);

    kill_program_by_name(app_base, preserve_pid);

    g_free(app_path);
    g_free(app_base);
}

/*
gchar *get_command_and_name(gchar *desktop_file, gchar **command) {
    // Load program.desktop file and return application name and executable command (with args).
    gchar *app_name = NULL;
    *command = NULL;

    gchar *s = NULL;
    // Ends with ".desktop"?
    if (g_str_has_suffix(desktop_file, ".desktop")) {
        s = g_strdup(desktop_file);
    } else {
        // Add ".desktop"
        s = g_strdup_printf("%s.desktop", desktop_file);
    }

    // Load GDesktopAppInfo from propgram.desktop file
    GDesktopAppInfo *app_info = g_desktop_app_info_new(s);
    g_free(s);

    if (!app_info) {
        return NULL;
    }

    // Ref:https://developer.gnome.org/gio/2.26/GAppInfo.html
    // and its implementation:https://developer.gnome.org/gio/2.30/gio-Desktop-file-based-GAppInfo.html

    // Read application name
    app_name = (gchar*)g_app_info_get_name(G_APP_INFO(app_info));
    if (!app_name) {
        app_name = (gchar*)g_app_info_get_display_name(G_APP_INFO(app_info));
    }

    app_name = g_strdup(app_name);

    // Read command with arguments
    *command = g_strdup((gchar*)g_app_info_get_commandline(G_APP_INFO(app_info)));

    g_object_unref(app_info);

    // Caller should g_free() both returned app_name and command.
    return app_name;
}
*/


