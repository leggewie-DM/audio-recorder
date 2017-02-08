#ifndef __G_ST_PIPELINE_H__
#define __G_ST_PIPELINE_H__
#include <glib.h>
#include <gdk/gdk.h>
#include <gst/gst.h>

#include "log.h"
#include "support.h"
#include "dconf.h"
#include "utility.h"

typedef struct {
    gchar *source;        // pulsesrc, autoaudiosrc, etc.

    GList *dev_list;      // String list of device names (internal ids of audio input devices).
    // See: pactl list | grep -A2 'Source #' | grep 'Name: ' | cut -d" " -f2

    gchar *profile_str;   // Capabilities and encoder pipeline. Gstreamer 1.0 style.
    // Eg. "audio/x-raw,rate=44100,channels=2 ! vorbisenc name=enc quality=0.5 ! oggmux"

    gchar *file_ext;      // File extension such as "ogg", "flac" or "mp3".

    gchar *filename;      // Record to this file.
    gboolean append;      // Append to file option?

} PipelineParms;

void pipeline_free_parms(PipelineParms *parms);

GstElement *pipeline_create(PipelineParms *parms, gchar **err_msg);
GstElement *pipeline_create_VAD(PipelineParms *parms, gchar **err_msg);

GString *pipeline_create_command_str(PipelineParms *parms);
#endif

