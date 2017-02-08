#ifndef _AUDIO_SOURCES_H__
#define _AUDIO_SOURCES_H__

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#define DEFAULT_AUDIO_SOURCE "pulsesrc"

// Types of audio devices (audio sources)
enum DeviceType {
    NOT_DEFINED = 0x0,

    // GNOME-desktop's default device. Defined as "gconfaudiosrc" or "autoaudiosrc".
    DEFAULT_DEVICE = 0x1,

    // Real audio card with sound output, audio sink. This is an audio-card with loudspeakers. Useless for recording.
    AUDIO_SINK = 0x2,

    // This is the monitor device for AUDIO_SINK. We use this to record from an audio-card (we record what comes out from loudspeakers).
    AUDIO_SINK_MONITOR = 0x4,

    // Standalone microphone or webcam with microphone. Many audio-cards have also microphone input.
    AUDIO_INPUT = 0x8,

    // DBus entity. Media-players like RhythmBox, Amarok, Totem and Banshee. These can control the recording via DBus. Enable DBus-plugin in media-players.
    MEDIA_PLAYER = 0x10,

    // DBus entity. Communication program like Skype. Skype can control the recording via DBus.
    COMM_PROGRAM = 0x20,

    // User-defined group of devices. User can record from several inputs (eg. record from several microphones when singing karaoke).
    USER_DEFINED = 0x40
};

typedef struct {
    // Device type. See above.
    enum DeviceType type;

    // Internal device id (audio device id). In Pulseaudio this field is called "name".
    gchar *id;

    // Human readable device description shown in listboxes.
    gchar *description;

    // Icon name for this device or source type
    gchar *icon_name;
} DeviceItem;

DeviceItem *device_item_create(gchar *id, gchar *description);
DeviceItem *device_item_copy_node(DeviceItem *item);
void device_item_free(DeviceItem *item);
const gchar *device_item_get_type_name(guint type);

// ComboBox columns
enum {
    // DeviceType (see above), hidden column
    COL_DEVICE_TYPE,

    // Device id or DBus id for media players/skype, hidden column
    COL_DEVICE_ID,

    // Icon for device or media players/skype, visible
    COL_DEVICE_ICON,

    // Description device or media players/skype, visible column
    COL_DEVICE_DESCR,

    // Number of columns in the combobox
    N_DEVICE_COLUMNS
};

gboolean filter_for_sink_dev(DeviceItem *item);

void audio_sources_init();
void audio_sources_exit();

void audio_sources_reload_device_list();

void audio_sources_free_list(GList *lst);
void audio_sources_print_list(GList *list, gchar *tag);
void audio_sources_print_list_ex();

GList *audio_sources_get_for_type(gint type);

DeviceItem *audio_sources_find_id(gchar *device_id);
DeviceItem *audio_sources_find_in_list(GList *lst, gchar *device_id);

void audio_sources_device_changed(gchar *device_id);
GList *audio_sources_wash_device_list(GList *dev_list);
GList *audio_sources_get_device_NEW(gchar **audio_source);

GtkWidget *audio_sources_create_combo();

void audio_source_fill_combo(GtkWidget *combo);

void audio_sources_combo_set_id(GtkWidget *combo, gchar *device_id);
gint audio_sources_get_combo_index(GtkWidget *combo);
gboolean audio_sources_combo_get_values(GtkWidget *combo, gchar **device_name, gchar **device_id, gint *device_type);

gboolean audio_sources_device_is_webcam(gchar *dev_name);

void get_devices_n();

gchar *get_default_sink_device();

#endif



