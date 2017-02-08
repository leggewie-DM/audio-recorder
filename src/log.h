#ifndef _REC_LOG_H__
#define _REC_LOG_H__

#include <stdio.h>
#include <glib.h>
#include <gdk/gdk.h>

// Activate this to show debug messages from ALL modules
//#define DEBUG_ALL

// Activate this to show debug messages from the main modules
//#define ACTIVE_DEBUGGING

#if defined(ACTIVE_DEBUGGING) || defined(DEBUG_ALL)
#define LOG_DEBUG(x, ...) log_message(__FILE__, __LINE__, "Debug:", x, ## __VA_ARGS__)
#else
#define LOG_DEBUG(x, ...)
#endif

#define LOG_MSG(x, ...) log_message(NULL, 0, NULL, x, ## __VA_ARGS__)
#define LOG_WARNING(x, ...) log_message(NULL, 0, "Warning:", x, ## __VA_ARGS__)
#define LOG_ERROR(x, ...) log_message(NULL, 0, "Error:", x, ## __VA_ARGS__)

void log_message(const gchar *_file, const gint _line, const gchar *type, const gchar *msg_format, ...);

#endif

