#ifndef _REC_MANAGER_STRUCT_H_
#define _REC_MANAGER_STRUCT_H_

#include <glib.h>
#include <gdk/gdk.h>

// Types of messages in the queue
enum CommandType {RECORDING_STOP = 0,
                  RECORDING_START = 2,
                  RECORDING_PAUSE = 3,
                  RECORDING_CONTINUE = 4,
                  RECORDING_NOTIFY_MSG = 7,
                  RECORDING_DEVICE_CHANGED,  /* Changes in the device list */
                  RECORDING_PROFILE_CHANGED, /* Changes in media profiles; MP3, OGG, etc. GStreamer pipeline has been modified */
                  RECORDING_SHOW_WINDOW,
                  RECORDING_HIDE_WINDOW,
                  RECORDING_QUIT_LOOP,
                  RECORDING_QUIT_APP
                 };

// Flags
enum CommandFlags {RECORDING_NO_FLAGS = 0, RECORDING_DELETE_FILE = 4};

typedef struct {
    enum CommandType type;
    gchar *track;
    gchar *artist;
    gchar *album;
    gint64 track_len; // in microseconds
    gint64 track_pos; // in microseconds
    enum CommandFlags flags;
} RecorderCommand;

// Send message to the queue
void rec_manager_send_command(RecorderCommand *cmd);

#endif

