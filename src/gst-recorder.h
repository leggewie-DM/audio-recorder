#ifndef _GST_RECORDER_H
#define _GST_RECORDER_H

#include <glib.h>
#include <gdk/gdk.h>
#include <glib/gstdio.h>
#include <gst/gst.h>

void rec_start_stop_recording();

void rec_module_init();
void rec_module_exit();

void rec_set_data_lock();
void rec_free_data_lock();

void rec_get_state(gint *state, gint *pending);
gboolean rec_is_recording();

gboolean rec_start_recording();
void rec_stop_recording(gboolean delete_file);
void rec_pause_recording();
void rec_continue_recording();

void rec_start_stop_recording();
void rec_stop_and_reset();

gint64 rec_get_stream_time();

gchar *rec_get_output_filename();

void rec_test_func();

//void rec_treshold_message(GstClockTime timestamp, gboolean above, gdouble threshold);

#endif
