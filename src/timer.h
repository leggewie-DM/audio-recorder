#ifndef _TIMER_H_
#define _TIMER_H_

#include <glib.h>
#include <gdk/gdk.h>
#include "log.h"
#include <gst/gst.h>

// Uncomment this to show debug messages from timer.c and timer-parser.c
//#define DEBUG_TIMER

#if defined(DEBUG_TIMER) || defined(DEBUG_ALL)
#define LOG_TIMER LOG_MSG
#else
#define LOG_TIMER(x, ...)
#endif

void parser_module_init();
void parser_module_exit();

void timer_module_init();
void timer_module_exit();

void timer_set_debug_flag(gboolean on);

void timer_func_start();
void timer_func_stop();

void timer_evaluate_triggers(GstClockTimeDiff time_diff, gdouble rms);

void timer_module_reset(gint for_state);

typedef struct {
    gchar action;      // recording action:   'S' = start | 'T' = stop | 'P' = pause
    gchar action_prep; // action preposition: 'a' = after
    gchar data_type;   // data type:          't' = clock time h:m:s | 'd' = time duration h,m,s | 'f' = file size | 'l' = label
    gdouble val[3];    // data:               hours,minutes,seconds | file size | silence/voice/audio/sound duration
    gchar label[12];   // label:              "silence" | ("sound" | "voice" | "audio") | ("bytes" | "kb" | "mb" | "gb" | "tb")

    gchar threshold_unit[10]; // level/threshold unit: "dB" (decibel) | "%" or empty
    gdouble threshold;        // level/threshold value in dB, % or plain value [0 - 1.0]

    gint day_of_year;  // internal flag. Used to check if the clock has gone around to the next day

    gint64 norm_secs; // = tr->val[0]*3600 + tr->val[1]*60 + tr->val[2] seconds (less recalculations)
    gdouble norm_threshold; // = threshold converted to [0 - 1.0] from threshold_unit (less recalculations)

    gdouble time_above;// internal value. Count seconds when value _above_ threshold
    gdouble time_below;// internal value. Count seconds when value _below_ threshold

} TimerRec;

GList *parser_parse_actions(gchar *txt);

void parser_print_rec(TimerRec *tr);
void parser_print_list(GList *list);
void parser_free_list();

void timer_settings_changed();

const gchar *parser_get_action_name(gchar action);

#endif

