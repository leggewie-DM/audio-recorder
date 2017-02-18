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
#include <glib.h>
#include <math.h>
#include "timer.h"

#include "utility.h"
#include "support.h"
#include "dconf.h"
#include "log.h"
#include "gst-vad.h"
#include "audio-sources.h"
#include "rec-manager.h"

/*
  Sample commands:

  Clock time:

  start at 09:30 pm
  start at 21:30
  start at 21:30:00

  -- During runtime:
       * Compare clock time to given timer value (clock time hh:mm:ss).
       * Start recorder (pipeline) if condition is TRUE.
       * User must stop recording manually (or by other command).
  -----------------------------------------------------

  Clock time or duration of recording:

  stop at 11:30 am
  stop after 1h 20m
  stop after 1 hour 20 min
  pause after 20 min

  -- During runtime:
       * Compare clock time to the given timer value.
       * Compare duration to the given timer limit.
       * Stop or pause recorder (pipeline) if condition is TRUE.
  -----------------------------------------------------

  Managing "silence".

  stop if silence
  stop if silence 5s
  stop if silence 5s 0.4
  stop if silence 5s 40%

  -- During runtime:
       * Make sure VAD is running.
       * Send threshold signals to this module (timer.c)
       * Recorder will stop if threshold < limit.
  ------------------------------------------------------

  pause if silence 5s 0.3
  pause if silence 5s 30%
  pause if silence 5s -24dB
  pause if silence

  -- During runtime:
       * Make sure VAD is running.
       * Send threshold signals to this module (timer.c)
       * Recorder will PAUSE if threshold < limit, and PLAY if threshold >= limit.
  ---------------------------------------------------------

  Using "sound", "voice" and "audio" commands.

  start if sound
  start if sound 0.3
  start if voice 30%
  start if voice 0.3
  start if audio -20dB

  NOTICE: Unfortunately these commands cannot handle time delay very well!

  These commands fire immediately after the volume exceeds/or is beneath the given limit (default limit is 0.3, or 30%).

  -- During runtime:
       * Make sure VAD is running.
       * Send threshold signals to gst-recorder.c.
       * Recorder will START if threshold >= limit, and PAUSE if threshold < limit.
  ---------------------------------------------------------

  File size:

  stop after 10MB
  pause if 2MB

  -- During runtime:
       * Take recorded file size and compare it to value in the timer command (in timer.c).
       * Start recorder (pipeline) if condition is TRUE.
  ---------------------------------------------------------

  stop after 2 GB | 12 pm | silence 4s
  start at 10:20 pm | voice

  Multiple conditions on one line, separated by "|" or "or".
  ---------------------------------------------------------

  Notice: The words "voice", "audio" and "sound" have all *same meaning*. Ok!

  The word "silence" is relative to the given volume level/threshold.
  Silence has both duration (in seconds) and volume limit.

  Volume limit can be given as:
  -Decimal value between [0, 1.0].
  -% value between [0%, 100%]. So 1.0 = 100%.
  -Or decibel value.

*/

// Timer function call frequency in seconds
#define TIMER_CALL_FREQ 1

// Default silence duration (in seconds)
#define DEF_SILENCE_DURATION 3

// Timer function id
static guint g_timer_func_id = 0;

// A GList of TimerRec nodes from the timer-parser.c
G_LOCK_DEFINE_STATIC(g_t_list);
static GList *g_t_list = NULL;

// Timer's start time
static struct tm g_timer_start_time;

void timer_func_stop();
gboolean timer_func_cb(gpointer user_data);
void timer_func_exit_cb(gpointer user_data);

gchar timer_func_eval_command(TimerRec *tr);

static void timer_set_start_time();
static struct tm timer_get_start_time();
static void timer_update_records_1();

static gchar timer_test_filesize(TimerRec *tr);
static gchar timer_test_clock_time_S(TimerRec *tr);
static gchar timer_test_clock_time_T(TimerRec *tr);
static gchar timer_test_clock_time_P(TimerRec *tr);
static gchar timer_test_time_duration(TimerRec *tr);

static void test_silence(TimerRec *tr, GstClockTimeDiff time_diff, gdouble rms);
static void test_sound(TimerRec *tr, GstClockTimeDiff time_diff, gdouble rms);
static void execute_action(TimerRec *tr, gchar action);

void timer_module_init() {
    LOG_DEBUG("Init timer.c.\n");

    // Init gst-vad.c
    vad_module_init();

    g_timer_func_id = 0;

    // Init parser module
    parser_module_init();

    // Start the timer function
    timer_func_start();
}

void timer_module_exit() {
    LOG_DEBUG("Clean up timer.c.\n");

    // Stop timer function
    timer_func_stop();

    // Clean up parser module
    parser_module_exit();

    // Clean up gst-vad.c
    vad_module_exit();

    g_t_list = NULL;
}

void timer_set_debug_flag(gboolean on) {
    // Set debug flag. Please see application options:
    // $ audio-recorder --help
    vad_set_debug_flag(on);
}

void timer_module_reset(gint for_state) {
    // Reset timer before we move to the given state

    switch (for_state) {
    case GST_STATE_PLAYING:
    case GST_STATE_NULL:
        timer_update_records_1();
        break;

    case GST_STATE_PAUSED:
        //timer_update_records_2();
        break;

    default:
        ;
    }
}

void timer_module_rec_start() {
    // Called when recording stops.

    // Reset timer
    timer_update_records_1();
}

// --------------------------------------------------
// The actual timer function
// --------------------------------------------------

void timer_func_start() {
    // Already running?
    if (g_timer_func_id > 0) {
        return;
    }

    // Start the timer function
    g_timer_func_id = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, TIMER_CALL_FREQ, (GSourceFunc)timer_func_cb,
                      (gpointer)1/*!= 0*/, (GDestroyNotify)timer_func_exit_cb);
}

void timer_func_stop() {
    // Stop the timer funcion
    if (g_timer_func_id > 0) {
        g_source_remove(g_timer_func_id);
    }
    g_timer_func_id = 0;
}

void timer_func_exit_cb(gpointer user_data) {
    // Nothing to cleanup
    ;
}

void timer_settings_changed() {
    // Increment the counter so various modules know that the timer-settings have been altered
    gint count = 0;
    conf_get_int_value("timer-setting-counter", &count);
    conf_save_int_value("timer-setting-counter", count+1);
}

static void timer_update_records_1() {
    // Reset timer nodes
    G_LOCK(g_t_list);

    GList *item = g_list_first(g_t_list);
    while (item) {
        TimerRec *tr = (TimerRec*)item->data;

        // Start to count from 0
        tr->time_below = 0.0;
        tr->time_above = 0.0;

        // Next item
        item = g_list_next(item);
    }

    G_UNLOCK(g_t_list);
}

#if 0
static void timer_update_records_2() {
    // Reset timer nodes

    G_LOCK(g_t_list);

    GList *item = g_list_first(g_t_list);
    while (item) {
        //TimerRec *tr = (TimerRec*)item->data;

        // Start to count from 0
        //tr->seconds = 0;
        //tr->seconds_x = 0;

        // Next item
        item = g_list_next(item);
    }
    G_UNLOCK(g_t_list);
}
#endif

static void timer_clear_list() {
    // Reset the timer list

    // Lock g_timer_list
    G_LOCK(g_t_list);

    parser_free_list();
    g_t_list = NULL;

    // Unlock
    G_UNLOCK(g_t_list);
}

static void timer_set_start_time() {
    // Set timer's start time
    time_t t = time(NULL);
    localtime_r(&t, &g_timer_start_time);
}

static struct tm timer_get_start_time() {
    return g_timer_start_time;
}

gdouble normalize_threshold(gdouble threshold, gchar *threshold_unit) {
    gdouble val = threshold;
    if (!threshold_unit) return val;

    // dB?
    if (threshold_unit[0] == 'd')  {
        // rms_dB:
        // RMS, https://en.wikipedia.org/wiki/Root_mean_square
        // From dB to a normalized 0 - 1.0 value.
        val = pow(10, threshold / 20);
    }
    // [0 - 100]% ?
    else if (threshold_unit[0] == '%')  {
        val = val / 100.0;

        // Already in [0 - 1.0]
    } else {
        // val = threshold;
    }

    return val;
}

gboolean check_need_VAD() {
    GList *item = g_list_first(g_t_list);
    while (item) {
        TimerRec *tr = (TimerRec*)item->data;

        if (!g_strcmp0(tr->label, "silence") ||
                !g_strcmp0(tr->label, "voice") ||
                !g_strcmp0(tr->label, "sound") ||
                !g_strcmp0(tr->label, "audio")) {

            return TRUE;
        }

        // Next item
        item = g_list_next(item);
    }
    return FALSE;
}

void normalize_values() {
    GList *item = g_list_first(g_t_list);
    while (item) {
        TimerRec *tr = (TimerRec*)item->data;

        // Convert hh:mm:ss to seconds
        tr->norm_secs = (gdouble)(tr->val[0]*3600 + tr->val[1]*60 + tr->val[2]);

        // Convert tr->threshold to [0 - 1.0] from tr->threshold_unit
        tr->norm_threshold = normalize_threshold(tr->threshold, tr->threshold_unit);

        // Next item
        item = g_list_next(item);
    }
}

static gchar highest_priority(gchar c1, gchar c2) {
    // sTop ha higher priority than Start, Continue or Pause
    // Start has higher priority than Pause or Continue

    if (c1 == 0 && c2 == 0) { // No action
        return 0;
    }

    if (c1 == 'T' || c2 == 'T') { // sTop
        return 'T';
    }

    if (c1 == 'S' || c2 == 'S') { // Start
        return 'S';
    }

    if (c1 == 'C' || c2 == 'C') { // Continue
        return 'C';
    }

    // Lowest priority Pause
    return 'P';
}

gboolean timer_func_cb(gpointer user_data) {
    // The actual timer function

    // Timer is ON/OFF?
    static gboolean timer_active = FALSE;

    // Counter to detect if GConf settings have been altered
    static gint setting_counter = -1;

    // Do we need VAD-pipeline?
    static gboolean need_VAD = FALSE;

    // Timer (GConf) settings changed?
    gint val = 0;
    conf_get_int_value("timer-setting-counter", &val);

    if (val == setting_counter) {
        // No changes in parameters.
        // Evaluate timer values.
        goto EVAL_0;
    }

    // Save settings counter
    setting_counter = val;

    // Get new values from GConf and parse values
    conf_get_boolean_value("timer-active", &timer_active);

    LOG_TIMER("Timer settings changed:<%s>\n", (timer_active ? "timer ON" : "timer OFF"));

    // Timer is ON/OFF?
    if (!timer_active) {
        // It's OFF

        timer_clear_list();

        // Stop the listener
        vad_stop_VAD();

        goto LBL_1;
    }

    // Set timer's start time
    timer_set_start_time();

    // Free the old g_t_list
    timer_clear_list();

    // Set lock
    G_LOCK(g_t_list);

    // Get timer text
    gchar *timer_text = NULL;
    conf_get_string_value("timer-text", &timer_text);

    LOG_TIMER("----------------\nTimer text is:\n<%s>\n--------------\n", timer_text);

    // Parse timer conditions.
    // This will return pointer to the g_timer_list (GList) in timer-parser.c.
    g_t_list = parser_parse_actions(timer_text);

    g_free(timer_text);

    if (g_list_length(g_t_list) < 1) {
        LOG_TIMER("The timer has no conditions.\n");

    } else {
        LOG_TIMER("The timer conditions are:\n");

#if defined(DEBUG_TIMER)
        // Debug print the command list
        parser_print_list(g_t_list);
#endif
    }

    // Important: Start VAD-pipeline only when we needed.
    // Only "silence", "voice", "audio" and "sound" commands/conditions need VAD (Voice Activity Detection).
    need_VAD = check_need_VAD();

    // Normalize values
    normalize_values();

    G_UNLOCK(g_t_list);

EVAL_0:

    // Check if recorder was started with --debug-signal (or -d) argument
    need_VAD = need_VAD || vad_get_debug_flag();

    // Timer is ON?
    if (!timer_active) {
        // No.
        // Make sure the VAD has stopped (do not waste CPU cycles)
        vad_stop_VAD();
        goto LBL_1;
    }

    // Yes. Timer is ON.

    // Do we need data from gst-vad.c?
    if (!need_VAD) {
        // No.
        // Make sure the VAD has stopped (do not waste CPU cycles)
        vad_stop_VAD();

    } else {
        // Yes.
        // Start VAD (if not already running).
        vad_start_VAD();
    }

    // ------------------------
    // Evaluate timer commands
    // ------------------------

    gchar saved_action = 0;
    TimerRec *saved_tr = NULL;

    // For all TimerRec structures in GList...
    GList *item = g_list_first(g_t_list);
    while (item) {
        TimerRec *tr = (TimerRec*)item->data;

        // Set lock
        G_LOCK(g_t_list);

        // Check the timer condition
        gchar c = timer_func_eval_command(tr);

        // Notice: The Timer list may have several commands like:
        //  start at 21:00
        //  stop at 21:30
        //  stop after 20MB | 09:20 pm | silence 4 sec 20%
        //
        // sTop ha higher priority than Start, Continue or Pause
        // Start has higher priority than Pause or Continue
        gchar highest = highest_priority(saved_action, c);

        if (highest == c || saved_action == 0) {
            // Save this action
            saved_action = c;
            saved_tr = tr;
        }

        // Unlock
        G_UNLOCK(g_t_list);

        // Next item
        item = g_list_next(item);
    }

    // Execute timer command (if saved_action != 0)
    execute_action(saved_tr, saved_action);

LBL_1:
    // Continue calling this function
    return TRUE;
}

gchar timer_func_eval_command(TimerRec *tr) {
    // Return action code: 'S'=Start, 'T'=sTop, 'P'=Pause, 'C'=Continue, 0=No action.
    gchar action = 0;

    // Test filesize?
    if (tr->data_type == 'f') {
        // stop/pause if ### bytes/KB/MB/GB/TB
        // Example:
        // stop/pause if 250 MB

        action = timer_test_filesize(tr);

        if (action != 0) {
            LOG_TIMER("Filesize test is TRUE. Action is '%c' (%s).\n", action, parser_get_action_name(action));
        }

        // Test clock time ##:##:##?
    } else if (tr->data_type == 't') {
        // start/stop/pause at ##:##:## am/pm (where ##:##:## is a clock time in hh:mm:ss format)
        // Example:
        // start/stop/pause at 10:15:00 pm

        if (tr->action == 'S') { // 'S'tart

            action = timer_test_clock_time_S(tr);

        } else if (tr->action == 'T') { // s'T'op

            action = timer_test_clock_time_T(tr);

        } else if (tr->action == 'P') { // 'P'ause

            action = timer_test_clock_time_P(tr);
        }

        if (action != 0) {
            LOG_TIMER("Clock-time test is TRUE. Action is '%c' (%s).\n", action, parser_get_action_name(action));
        }

        // Test time duration?
    } else if (tr->data_type == 'd') {
        // start/stop/pause after # hour # min # seconds
        // Example:
        // start/stop/pause after 1 h 25 min

        action = timer_test_time_duration(tr);

        if (action != 0) {
            LOG_TIMER("Test for time period/duration is TRUE. Action is '%c' (%s).\n", action, parser_get_action_name(action));
        }
    }

    return action;
}

static gchar timer_test_filesize(TimerRec *tr) {
    // Test filesize.
    // stop/pause if/after/on ### bytes/KB/MB/GB/TB
    // Examples:
    //  stop after 250MB
    //  pause if 1.2 GB

    gchar action = 0;

    // Get output filename
    gchar *filename = rec_manager_get_output_filename();

    if (!filename) {
        return action;
    }

    // Get file size
    gdouble filesize = 1.0 * get_file_size(filename);

    g_free(filename);

    // Filesize limit exceeded?
    if (filesize >= tr->val[0]) {
        // Execute
        action = tr->action;
    }

    LOG_TIMER("Testing filesize: trigger filesize=%3.1f bytes, unit=%s, current filesize=%3.1f bytes, filename=<%s>, -->%s\n",
              tr->val[0], tr->label, filesize, filename, (action == 0 ? "FALSE" : "TRUE"));

    return action;
}

#if 0
static TimerRec *find_action(gchar action) {
    // Find latest TimeRec record for action.
    TimerRec *found_tr = NULL;

    // Try to set lock
    gboolean locked = G_TRYLOCK(g_t_list);

    // For all TimerRec structures in GList...
    GList *item = g_list_first(g_t_list);
    while (item) {
        TimerRec *tr = (TimerRec*)item->data;

        if (action == tr->action) {

            switch (tr->data_type) {

            case 't':
                // Take the one with latest timestamp of found_tr and tr.

                if (!found_tr) {
                    // First match
                    found_tr = tr;
                } else {

                    // Compare timestamps of found_tr and tr.
                    gint64 timer_secs = tr->val[0]*3600 +  tr->val[1]*60 +  tr->val[2];
                    if (timer_secs > found_tr->norm_secs) {
                        found_tr = tr;
                    }
                }

                break;

            case 'd':
            case 'f':
                if (!found_tr) {
                    found_tr = tr;
                }
                break;

            }
        }

        // Next item
        item = g_list_next(item);
    }

    if (locked) {
        // Unlock
        G_UNLOCK(g_t_list);
    }

    return found_tr;
}
#endif

static gchar timer_test_clock_time_S(TimerRec *tr) {
    // Test clock time for 'S'tart recording.
    // start at ##:##:## am/pm (where ##:##:## is a clock time in hh:mm:ss format)
    // Examples:
    //  start at 10:15:00 pm
    //  start after 1 hour

    gchar action = 0;

    // Get date & time
    time_t t = time(NULL);
    struct tm *tmp;
    tmp = localtime(&t);

    // Timer command already executed today?
    if (tr->day_of_year == tmp->tm_yday) {
        // Fire only once a day

        LOG_TIMER("Timer command 'S'tart already executed today. Current time:%02d:%02d:%02d timer value:%02.0f:%02.0f:%02.0f (day_of_year:%d/%d).\n",
                  tmp->tm_hour, tmp->tm_min, tmp->tm_sec,  tr->val[0], tr->val[1], tr->val[2], tmp->tm_yday, tr->day_of_year);

        return 0;
    }

    // Clock time in secs
    gint64 clock_secs = tmp->tm_hour*3600 + tmp->tm_min*60 + tmp->tm_sec;

    // TimeRec's value in secs
    gint64 timer_secs = tr->norm_secs;

    // Note:
    // Do NOT fire if current clock time is 60 minutes or more over the timer value.
    // Eg. Assume the timer value is set to 14:00h:
    //     We start recording if clock time is between 14:00:01h and 15:00h. (1 hour over timer value).
    //     After 15:00h the timer must wait until next day.

    gint64 diff_secs = (clock_secs - timer_secs);

    action = 0;
    if (clock_secs > timer_secs && diff_secs < (60*60L)/*1 HOUR HARD-CODED*/) {
        // Start-time is over current clock time.

        // Already fired today? (fire once a day)
        if (tr->day_of_year != tmp->tm_yday) {
            action = 'S';
        }
    }

    if (action) {
        // Save day_of_year so we know when the clock turns around (to the next day).
        // Then this timer command will become valid and fire again.
        tr->day_of_year = tmp->tm_yday;
    }

    LOG_TIMER("Test clock time for 'S'tart: current time:%02d:%02d:%02d timer value:%02.0f:%02.0f:%02.0f (day_of_year:%d/%d) diff in secs:%ld, -->%s\n",
              tmp->tm_hour, tmp->tm_min, tmp->tm_sec,  tr->val[0], tr->val[1], tr->val[2], tmp->tm_yday,
              tr->day_of_year, (long)(timer_secs - clock_secs), (action == 0 ? "FALSE" : "TRUE"));

    return action;
}

static gchar timer_test_clock_time_T(TimerRec *tr) {
    // Test clock time for sTop recording.
    // stop at ##:##:## am/pm (where ##:##:## is a clock time in hh:mm:ss format)
    // Examples:
    //  stop at 23:00
    //  stop at 09:10 am

    gchar action = 0;

    // Get date & time
    time_t t = time(NULL);
    struct tm *tmp;
    tmp = localtime(&t);

    // Timer command already executed today?
    if (tr->day_of_year == tmp->tm_yday) {
        // Fire only once a day

        LOG_TIMER("Timer command s'T'op already executed today. Current time:%02d:%02d:%02d timer value:%02.0f:%02.0f:%02.0f (day_of_year:%d/%d).\n",
                  tmp->tm_hour, tmp->tm_min, tmp->tm_sec,  tr->val[0], tr->val[1], tr->val[2], tmp->tm_yday, tr->day_of_year);

        return 0;
    }

    // Clock time in secs
    gint64 clock_secs = tmp->tm_hour*3600 + tmp->tm_min*60 + tmp->tm_sec;

    // TimeRec's value in seconds
    gint64 timer_secs = tr->norm_secs;

    // Check if clock-time is over stop-time
    action = 0;

    if (clock_secs > timer_secs) {
        // FIXME: Should we check tr->day_of_year != tmp->tm_yday here?
        // if (timer_secs > clock_secs && tr->day_of_year != tmp->tm_yday) {

        // Already fired today? (fire once a day)
        if (tr->day_of_year != tmp->tm_yday) {
            action = 'T';
            tr->day_of_year = tmp->tm_yday;
        }
    }

    LOG_TIMER("Test clock time for s'T'op: current time:%02d:%02d:%02d timer value:%02.0f:%02.0f:%02.0f (day_of_year:%d/%d) diff in secs:%ld, -->%s\n",
              tmp->tm_hour, tmp->tm_min, tmp->tm_sec,  tr->val[0], tr->val[1], tr->val[2], tmp->tm_yday,
              tr->day_of_year, (long)(timer_secs - clock_secs), (action == 0 ? "FALSE" : "TRUE"));

    return action;
}

static gchar timer_test_clock_time_P(TimerRec *tr) {
    // Test clock time for Pause recording.
    // pause at ##:##:## am/pm (where ##:##:## is a clock time in hh:mm:ss format)
    // Examples:
    //  pause at 23:00
    //  pause at 09:10 am

    gchar action = 0;

    // Get date & time
    time_t t = time(NULL);
    struct tm *tmp;
    tmp = localtime(&t);

    // Timer command already executed today?
    if (tr->day_of_year == tmp->tm_yday) {
        // Fire only once a day

        LOG_TIMER("Timer command 'P'ause already executed today. Current time:%02d:%02d:%02d timer value:%02.0f:%02.0f:%02.0f (day_of_year:%d/%d).\n",
                  tmp->tm_hour, tmp->tm_min, tmp->tm_sec,  tr->val[0], tr->val[1], tr->val[2], tmp->tm_yday, tr->day_of_year);

        return 0;
    }

    // Clock time in secs
    gint64 clock_secs = tmp->tm_hour*3600 + tmp->tm_min*60 + tmp->tm_sec;

    // TimerRec's time in secs
    gint64 timer_secs = tr->norm_secs;

    // Check if clock-time is over pause-time
    if (clock_secs > timer_secs) {
        // FIXME: Should we check tr->day_of_year != tmp->tm_yday here?
        // if (timer_secs > clock_secs && tr->day_of_year != tmp->tm_yday) {

        // Already fired today? (fire once a day)
        if (tr->day_of_year != tmp->tm_yday) {
            action = 'P';
            tr->day_of_year = tmp->tm_yday;
        }
    }

    LOG_TIMER("Test clock time for 'P'ause: current time:%02d:%02d:%02d timer value:%02.0f:%02.0f:%02.0f (day_of_year:%d/%d) diff in secs:%ld, -->%s\n",
              tmp->tm_hour, tmp->tm_min, tmp->tm_sec,  tr->val[0], tr->val[1], tr->val[2], tmp->tm_yday,
              tr->day_of_year, (long)(timer_secs - clock_secs), (action == 0 ? "FALSE" : "TRUE"));

    return action;
}

static gchar timer_test_time_duration(TimerRec *tr) {
    // Test time duration/time period.
    // start/stop/pause after # hour/h # minuntes/m/min # seconds/s/sec
    // Examples:
    //  start after 1 h 25 min
    //  stop after 30 minutes
    //  pause after 1 h 15 m 20 s

    gchar action = 0;

    // Get date & time
    time_t t = time(NULL);
    struct tm *tmp;
    tmp = localtime(&t);

    // Action is s'T'op or 'P'ause recording?
    if (tr->action == 'T'  || tr->action == 'P') {
        // Eg. stop/pause after 8 min 20 sec

        // Compare stream time to the given timer value.

        // Get actual recording time in seconds
        gint64 recording_time_secs = rec_manager_get_stream_time();

        // TimeRec's value in seconds
        gint64 timer_secs = tr->norm_secs;

        if (recording_time_secs >= timer_secs) {
            // Execute command
            action = tr->action;

            // tr->day_of_year not really used here
            tr->day_of_year = tmp->tm_yday;
        }

#if defined(DEBUG_TIMER) || defined(DEBUG_ALL)
        guint hh = -1;
        guint mm = -1;
        guint ss = -1;
        // Split value to hours, minutes and seconds
        seconds_to_h_m_s(recording_time_secs, &hh, &mm, &ss);

        gint64 diff = timer_secs - recording_time_secs;

        LOG_TIMER("Test time period for '%c' (%s): current rec.time:%02d:%02d:%02d timer value:%02.0f:%02.0f:%02.0f diff secs:%ld, -->%s.\n",
                  tr->action, parser_get_action_name(tr->action),
                  hh, mm, ss, tr->val[0], tr->val[1], tr->val[2], (long)diff, (action == 0 ? "FALSE" : "TRUE"));
#endif

    } else {

        action = 0;

        // Action is 'S'tart recording?
        // Eg. start after 1 min 20 sec (execute "start after..." command only once during timer's life time)

        // Get start time for this timer (when lines were parsed)
        struct tm start_time = timer_get_start_time();
        gint64 start_time_secs = start_time.tm_hour*3600.0 + start_time.tm_min*60.0 + start_time.tm_sec;

        // Command already executed? (we use -2 as "execute once" indicator)
        if (tr->day_of_year  == -2) {

            // Fire only once a day
            LOG_TIMER("Timer command 'S'tart already executed once!: clock time:%02d:%02d:%02d timer thread started:%02d:%02d:%02d"
                      " timer value (duration):%02.0f:%02.0f:%02.0f.\n",
                      tmp->tm_hour, tmp->tm_min, tmp->tm_sec, start_time.tm_hour, start_time.tm_min, start_time.tm_sec,
                      tr->val[0], tr->val[1], tr->val[2]);

            return 0;
        }

        // Clock time in secs
        gint64 curr_time_secs = tmp->tm_hour*3600.0 + tmp->tm_min*60.0 + tmp->tm_sec;

        // TimerRec's value in seconds
        gint64 timer_secs = tr->norm_secs;

        if ((curr_time_secs - start_time_secs) >= timer_secs ) {
            // Execute command
            action = tr->action;

            // Execute only once (we use -2 as "execute once" indicator)
            tr->day_of_year = -2;
            // tr->day_of_year = tmp->tm_yday;
        }

        gint64 diff = timer_secs - (curr_time_secs - start_time_secs);
        (void) diff; // Avoid unused var message

        LOG_TIMER("Test time period for 'S'tart: clock time:%02d:%02d:%02d timer thread started:%02d:%02d:%02d"
                  " timer value (duration):%02.0f:%02.0f:%02.0f  diff secs:%ld, -->%s\n",
                  tmp->tm_hour, tmp->tm_min, tmp->tm_sec, start_time.tm_hour, start_time.tm_min, start_time.tm_sec,
                  tr->val[0], tr->val[1], tr->val[2], (long)diff, (action == 0 ? "FALSE" : "TRUE"));

    }

    return action;
}

void timer_evaluate_triggers(GstClockTimeDiff time_diff, gdouble rms) {
    // This is called from gst-vad.c.
    // Evaluate VAD related timer commands.
    G_LOCK(g_t_list);

    GList *item = g_list_first(g_t_list);
    while (item) {
        TimerRec *tr = (TimerRec*)item->data;

        if (!g_strcmp0(tr->label, "silence")) {

            test_silence(tr, time_diff, rms);

        } else if (!g_strcmp0(tr->label, "voice") ||
                   !g_strcmp0(tr->label, "sound") ||
                   !g_strcmp0(tr->label, "audio")) {

            test_sound(tr, time_diff, rms);
        }

        // Next item
        item = g_list_next(item);
    }

    G_UNLOCK(g_t_list);
}

static void test_silence(TimerRec *tr, GstClockTimeDiff time_diff, gdouble rms) {
    // stop if silence
    // stop if silence 5s
    // stop if silence 5s 0.1
    // stop if silence 5s 10%

    // pause if silence
    // pause if silence 5s 0.3
    // pause if silence 5s 30%
    // pause if silence 5s -24dB
    gchar action = '0';

    gdouble seconds = tr->norm_secs;

#if defined(DEBUG_TIMER) || defined(DEBUG_ALL)
    // Get recording state (for debugging)
    gint state = -1;
    gint pending = -1;
    rec_manager_get_state(&state, &pending);

    // Name of state
    const gchar *state_name = rec_manager_get_state_name(state);

    LOG_TIMER("Silence test. timer value:%3.1f sec, count seconds:%3.1f sec, *RMS:%3.2f, threshold:%3.1f%s (%3.2f), state:%s\n",
              seconds, tr->time_below, rms, tr->threshold, (tr->threshold_unit ? tr->threshold_unit : ""), tr->norm_threshold, state_name);
#endif

    // RMS > tr->threshold?
    if (rms > tr->norm_threshold + 0.001) { // 0.001=noise
        // Reset time teller
        tr->time_below = 0.0;

        if (tr->action == 'P') { // Pause
            // Resume (continue) recording after pause
            action = 'C';
            goto LBL_1;
        }
        return;
    }

    // Add time_diff to tr->time_below, convert to seconds
    tr->time_below += ((gdouble)time_diff / GST_SECOND);

    // tr->time_below < seconds?
    if (tr->time_below < seconds) {
        // Wait more
        return;
    }

    // Avoid overflow (no reason to keep this growing)
    if (tr->time_below > seconds + 140000) {
        tr->time_below = seconds + 140000;
    }

    // If here: RMS has been <= tr->threshold in seconds time.
    // Execute timer command.

    if (tr->action == 'T') { // sTop
        // stop if silence
        // stop if silence 0.3 4s
        action = 'T';

    } else if (tr->action == 'P') { // Pause
        // pause if silence 3s 0.2
        action = 'P';
    }

    LOG_TIMER("Condition %3.2f <= %3.2f (%3.2f%s) is TRUE in %3.1f seconds time. Execute command:%s.\n",
              rms, tr->norm_threshold, tr->threshold, (tr->threshold_unit ? tr->threshold_unit : ""),
              seconds, parser_get_action_name(action));

LBL_1:
    // Exceute action
    execute_action(tr, action);

    // Reset counters
    tr->time_below = 0.0;
    tr->time_above = 0.0;
}

static void test_sound(TimerRec *tr, GstClockTimeDiff time_diff, gdouble rms) {
    // start if sound
    // start if sound 0.3
    // start if voice 30%
    // start if voice 0.3
    // start if audio -20dB
    gchar action = '0';

    gdouble seconds = tr->norm_secs;

    gint state = -1;
    gint pending = -1;
    rec_manager_get_state(&state, &pending);

#if defined(DEBUG_TIMER) || defined(DEBUG_ALL)
    // Name of state
    const gchar *state_name = rec_manager_get_state_name(state);

    LOG_TIMER("Sound/Voice/Audio test. timer value:%3.1f sec, count seconds:%3.1f sec, *RMS:%3.2f, threshold:%3.1f%s (%3.2f), state:%s\n",
              seconds, tr->time_above, rms, tr->threshold, (tr->threshold_unit ? tr->threshold_unit : ""), tr->norm_threshold, state_name);
#endif

    // rms over threshold?
    if (rms > tr->norm_threshold + 0.001) { // 0.001=noise

        // Paused temporarily?
        if (state == GST_STATE_PAUSED) {
            // Resume/continue recording immediately
            action = 'C';
            tr->time_above = 0.0;
            tr->time_below = 0.0;
            goto LBL_1;
        }

        // Add time_diff to tr->time_above, convert to seconds
        tr->time_above += ((gdouble)time_diff / GST_SECOND);

        // tr->time_above < seconds?
        if (tr->time_above < seconds) {
            // Wait more
            return;
        }

        // Avoid overflow (no reason to keep this growing)
        if (tr->time_above > seconds + 140000) {
            tr->time_above = seconds + 140000;
        }

        tr->time_below = 0.0;

        // If here: RMS has been > tr->threshold in seconds time.
        // Execute timer command.
        action = tr->action;

        LOG_TIMER("Condition %3.2f > %3.2f (%3.2f%s) is TRUE in %3.1f seconds time. Execute command:%s.\n",
                  rms, tr->norm_threshold, tr->threshold, (tr->threshold_unit ? tr->threshold_unit : ""),
                  seconds, parser_get_action_name(action));

        goto LBL_1;
    }

    // Here: rms < tr->norm_threshold.

    // Count seconds to pause
    tr->time_below += ((gdouble)time_diff / GST_SECOND);

    if (tr->time_below < 4.0) {
        // Wait more
        return;
    }

    // Pause recording temporarily
    action = 'P';
    tr->time_above = 0.0;
    tr->time_below = 0.0;

LBL_1:
    // Excecute action
    execute_action(tr, action);

}

void execute_action(TimerRec *tr, gchar action) {
    // Execute timer command

    if (action == 0 || tr == NULL) return;

    LOG_TIMER("Execute timer command '%c' (%s).\n", action, parser_get_action_name(action));

    // Get state of pipeline
    gint state = -1;
    gint pending = -1;
    rec_manager_get_state(&state, &pending);

    switch (action) {
    case 'S': // Start recording
        if (state == GST_STATE_PLAYING) {
            return;
        }

        rec_manager_start_recording();
        break;

    case 'T': // Stop recording

        if (state == GST_STATE_NULL && pending == GST_STATE_NULL) {
            return;
        }

        rec_manager_stop_recording();
        break;

    case 'P': // Pause recording

        if (state == GST_STATE_PAUSED) {
            return;
        }

        rec_manager_pause_recording();
        break;

    case 'p': // Pause recording

        if (state == GST_STATE_PAUSED) {
            return;
        }

        rec_manager_pause_recording();
        break;

    case 'C': // Continue/resume (if was paused). Does not restart if recording is off.
        rec_manager_continue_recording();
        break;

    case 0: // Do nothing
        break;

    default:
        LOG_ERROR("Unknown timer action <%c>.\n", action);
    }
}

