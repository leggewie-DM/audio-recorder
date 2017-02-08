#ifndef _G_ST_VAD_H__
#define _G_ST_VAD_H__

#include <string.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <math.h>
#include <gst/gst.h>
#include "timer.h"

void vad_module_init();
void vad_module_exit();

void vad_start_VAD();
void vad_stop_VAD();

void vad_set_debug_flag(gboolean on);

void vad_clear_trigger_list();
void vad_add_trigger(TimerRec *tr);

gboolean vad_get_debug_flag();

// Uncomment this to show debug messages from gst-vad.c
//#define DEBUG_VAD

#if defined(DEBUG_VAD) || defined(DEBUG_ALL)
#define LOG_VAD LOG_MSG
#else
#define LOG_VAD(x, ...)
#endif


#endif

