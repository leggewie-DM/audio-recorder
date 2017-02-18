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
#include "gst-vad.h"
#include "timer.h"
#include "audio-sources.h"
#include "dconf.h"
#include "log.h"
#include "utility.h"

#include "gst-pipeline.h"
#include "gst-recorder.h"

#include <string.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <math.h>
#include <gst/gst.h>

// Some information on how to implement VAD (Voice Activity Detector) using Gstreamer elements.
// Ref: https://lists.freedesktop.org/archives/gstreamer-devel/2012-September/037233.html
//
// We are still using two (2) pipelines; one dummy pipeline for VAD with a level element, and
// another pipeline for recording.

// GStreamer pipeline for VAD
static GstElement *g_vad_pipeline = NULL;

// Debug flag (--debug-signal or -d argument)
static gboolean g_debug_flag = FALSE;

// Parameters for VAD-pipeline
static PipelineParms *g_curr_parms = NULL;

// Delay between reading values
#define TRIGGER_TIME_MS 150  // in milliseconds

static GstElement *vad_create_pipeline(PipelineParms *parms);
static void vad_save_parms(PipelineParms *new_parms);
static gboolean vad_is_running();
static void vad_shutdown_pipeline();
static gboolean vad_message_handler(GstBus * bus, GstMessage * message, gpointer data);

void vad_module_init() {
    LOG_DEBUG("Init gst-vad.c.\n");

    // Initialize
    g_vad_pipeline = NULL;
    g_curr_parms = NULL;
    g_debug_flag = FALSE;
}

void vad_module_exit() {
    LOG_DEBUG("Clean up gst-vad.c.\n");

    vad_stop_VAD();
}

void vad_set_debug_flag(gboolean on) {
    // Set debug flag. Please see arguments:
    // $ audio-recorder --help
    // VAD process will now print level values in a terminal window.

    gboolean active = FALSE;
    conf_get_boolean_value("timer-active", &active);

    if (on && (!active)) {
        // Just remind user about the timer checkbox
        g_print("Please activate the timer (checkbox) to see the level values.\n");
    }

    g_debug_flag = on;
}

void vad_save_parms(PipelineParms *new_parms) {
    if (g_curr_parms) {
        pipeline_free_parms(g_curr_parms);
    }
    g_curr_parms = NULL;

    if (!new_parms) return;

    // Save the pointer
    g_curr_parms = new_parms;
}

void vad_start_VAD() {
    PipelineParms *parms = g_malloc0(sizeof(PipelineParms));

    // Get audio source and device list
    parms->source = NULL;
    parms->dev_list = audio_sources_get_device_NEW(&(parms->source));

    gboolean changed = TRUE;
    if (g_curr_parms) {
        changed = g_strcmp0(parms->source, g_curr_parms->source);
        changed = changed || (!str_lists_equal(parms->dev_list, g_curr_parms->dev_list));
    }

    // Changed?
    if (changed) {
        // Yes, stop VAD.
        vad_stop_VAD();
    }

    // Already running (or values were unchanged)?
    if (vad_is_running()) {

        // Values not used!
        pipeline_free_parms(parms);
        parms = NULL;
        return;
    }

    // Create a new GStreamer pipeline for VAD
    g_vad_pipeline = vad_create_pipeline(parms);

    // Save values
    vad_save_parms(parms);
}

void vad_stop_VAD() {
    // Shutdown pipeline
    vad_shutdown_pipeline();

    // Reset static variables
    vad_message_handler(NULL, NULL, NULL);

    // Clear PipelineParms
    vad_save_parms(NULL);
}

static gboolean vad_is_running() {
    // Valid pipeline?
    if (!GST_IS_OBJECT(g_vad_pipeline)) return FALSE;

    // The state is?
    // Ref: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstElement.html#gst-element-get-state
    GstState status, pending;
    gst_element_get_state(g_vad_pipeline, &status, &pending, 0);

    return (status != GST_STATE_NULL);
}

static void vad_check_triggers(GstClockTime timestamp, GstClockTimeDiff time_diff,
                               gdouble rms_dB_left, gdouble rms_dB_right, gdouble peak_dB) {
    // From dB (decibel) to normalized value between [0 - 1.0].
    //
    // Normalized value [0 - 1.0] = exp(dB / 20).
    // Decibel value = log10(normalized value) * 20.

    // Calc normalized values
    gdouble rms_left = exp(rms_dB_left/20.0);
    gdouble rms_right = exp(rms_dB_right/20.0);
    gdouble peak = exp(peak_dB/20.0);

    // Calculate average
    gdouble sum = 0.0;
    guint count = 0;

    // Left channel gave a value?
    if (rms_left > 0.001) {
        sum += rms_left;
        count++;
    }

    // Right channel gave a value?
    if (rms_right > 0.001) {
        sum += rms_right;
        count++;
    }

    // Calc avg
    gdouble rms_avg = 0.0;
    if (count > 0) {
        rms_avg = sum / count;
    }

    // Print values in a terminal window?
    // Got --debug-signal or -d argument?
    if (g_debug_flag) {
        gdouble rms_dB_avg = rms_dB_left > -120 ? ((rms_dB_left + rms_dB_right)/2.0) : rms_dB_right;
        g_print("Audio level. Time:%" GST_TIME_FORMAT ", RMS:%3.2f dB, normalized RMS:%3.2f, peak value:%3.2f.\n",
                GST_TIME_ARGS(timestamp), rms_dB_avg, rms_avg, peak);
    }

    // Evaluate triggers related to RMS value
    timer_evaluate_triggers(time_diff, rms_avg);
}

static gboolean vad_message_handler(GstBus * bus, GstMessage * message, gpointer data) {
    static GstClockTime g_last_timestamp = 0L;

    if (bus == NULL) {
        // Reset static variables
        g_last_timestamp = 0L;
        // And return
        return TRUE;
    }

    if (GST_MESSAGE_TYPE(message) != GST_MESSAGE_ELEMENT) {
        return TRUE;
    }

    const GstStructure *s = gst_message_get_structure(message);
    const gchar *name = gst_structure_get_name(s);

    GstClockTime timestamp;
    if (!gst_structure_get_clock_time(s, "timestamp", &timestamp)) {
        LOG_ERROR("vad_message_handler: Cannot read timestamp.\n");
    }

    // Wait at least TRIGGER_TIME_MS milliseconds
    GstClockTimeDiff diff = GST_CLOCK_DIFF(g_last_timestamp, timestamp);
    if (diff < TRIGGER_TIME_MS * GST_MSECOND) {
        goto LBL_1;
    }

    g_last_timestamp = timestamp;

    if (g_str_equal(name, "level")) {
        // Ref: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-level.html

        //Field names:
        //level field:endtime (GValueArray)
        //level field:timestamp (GValueArray)
        //level field:stream-time (GValueArray)
        //level field:running-time (GValueArray)
        //level field:duration (GValueArray)
        //level field:rms (GValueArray)
        //level field:peak (GValueArray)
        //level field:decay (GValueArray)

        const GValue *value = gst_structure_get_value (s, "rms");
        GValueArray *array = (GValueArray*)g_value_get_boxed(value);
        if (!array) goto LBL_1;

        guint channels = array->n_values;
        if (channels < 1) goto LBL_1;

        gdouble rms_dB_left = -G_MAXDOUBLE;  // left channel
        gdouble rms_dB_right = -G_MAXDOUBLE; // right channel

        if (channels > 0) {
            // Ref: https://lists.freedesktop.org/archives/gstreamer-devel/2012-October/037538.html
            rms_dB_left = g_value_get_double(array->values+0);
        }

        if (channels > 1) {
            rms_dB_right = g_value_get_double(array->values+1);
        }

        // peak_dB:
        value = gst_structure_get_value(s, "peak");
        array = (GValueArray*)g_value_get_boxed(value);
        gdouble peak_dB = g_value_get_double(array->values+0);

        // Evaluate timer triggers
        vad_check_triggers(timestamp, diff, rms_dB_left, rms_dB_right, peak_dB);
    }

LBL_1:
    // We handled the message we want, and ignored the ones we didn't want. so the core can unref the message for us
    return TRUE;
}

static void vad_shutdown_pipeline() {
    // Shutdown VAD-pipeline
    if (!GST_IS_PIPELINE(g_vad_pipeline)) {
        goto LBL_1;
    }

    LOG_VAD("Shutdown VAD pipeline.\n");

    // Switch to GST_STATE_NULL
    gst_element_set_state(g_vad_pipeline, GST_STATE_NULL);

    // Then destroy it
    gst_object_unref(GST_OBJECT(g_vad_pipeline));
    g_vad_pipeline = NULL;

LBL_1:
    ;
}

static GstElement *vad_create_pipeline(PipelineParms *parms) {
    if (!parms) return NULL;

#if defined(DEBUG_VAD) || defined(DEBUG_ALL)
    LOG_VAD("Start VAD for \"%s\"\n", parms->source);
    str_list_print("Monitor devices", parms->dev_list);
#endif

    gchar *err_msg = NULL;
    GstElement *pipeline = pipeline_create_VAD(parms, &err_msg);

    // Errors?
    if (!GST_IS_PIPELINE(pipeline) || err_msg) {
        goto LBL_1;
    }

    // Add message handler
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_signal_watch(bus);

    // Monitor "level" messages
    g_signal_connect(bus, "message::element", G_CALLBACK(vad_message_handler), NULL);

    gst_object_unref(bus);

    // Roll pipeline
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        err_msg = g_strdup(_("Cannot start reading from the stream/pipeline.\n"));
        goto LBL_1;
    } else {
        LOG_DEBUG("Pipeline for VAD (Voice Activity Detection) is running and OK.\n");
    }

    // Ok
    return pipeline;

LBL_1:
    if (!err_msg) {
        err_msg = g_strdup_printf(_("Cannot create audio pipeline. %s.\n"), "(VAD pipeline)");
    }
    LOG_ERROR(err_msg);
    g_free(err_msg);

    // Destroy pipeline
    if (G_IS_OBJECT(pipeline)) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(pipeline));
    }

    // Return NULL
    return NULL;
}

gboolean vad_get_debug_flag() {
    // --debug-signal or -d flag
    return g_debug_flag;
}


