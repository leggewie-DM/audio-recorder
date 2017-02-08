#ifndef _REC_WINDOW_H_
#define _REC_WINDOW_H_

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "levelbar.h"

// Width of the settings window
#define PREF_WINDOW_WIDTH 300

// PULSE_TYPE: Type of pulse on the levelbar.
// Notice: This cannot be changed from the GUI.
// Use Gsettings/dconf-editor and find "level-bar-pulse-type" in apps -> audio-recorder.
typedef enum PULSE_TYPE {PULSE_PEAK, PULSE_RMS} PULSE_TYPE;

// Settings for main window
typedef struct {
#ifdef APP_HAS_MENU
    GtkUIManager *ui_manager;
#endif
    GtkWidget *window; // Main window
    GdkWindowState state; // Window's state

    GtkWidget *recorder_button; // Button to start/stop/pause/continue recording

    GtkWidget *time_label;  // Labels to show recording time and filesize
    GtkWidget *size_label;
    GtkWidget *level_bar; // Gtklevelbar widget

    GtkWidget *filename; // Current filename
    GtkWidget *add_to_file; // Add to current file?

    GtkWidget *timer_active;  // Timer settings
    GtkWidget *timer_text;
    GtkWidget *timer_save_button;

    GtkWidget *audio_device; // List of audio devices, media players/skype. Combobox.
    GtkWidget *media_format; // List of media formats; ogg, flac, mp3, avi, etc. Combobox.

    GtkWidget *error_box; // Show error messages here

    GtkWidget *settings_button; // Additional settings button

    PULSE_TYPE pulse_type;  // Value on the level-bar widget

} MainWindow;

void win_create_window();
void win_show_window(gboolean show);
gboolean win_window_is_visible();
void win_close_button_cb(GtkButton *button, gpointer user_data);
void win_timer_save_text_cb(GtkWidget *widget, gpointer user_data);
void win_quit_application();
void win_hide_window();
void win_update_gui();
void win_set_error_text(gchar *error_txt);
void win_show_settings_dialog();

void win_refresh_device_list();
void win_refresh_profile_list();

void win_update_level_bar(gdouble norm_rms, gdouble norm_peak);
void win_set_filename(gchar *filename);
void win_set_time_label(gchar *time_txt);
void win_set_size_label(gchar *size_txt);

void win_show_right_click_menu();

GdkWindowState win_get_window_state();

void systray_module_init();
void systray_module_exit();
void systray_icon_setup(gboolean show);
gboolean systray_icon_is_installed();

GtkWidget *systray_create_menu(gboolean for_tray_icon);
void systray_set_menu_items1(gint state);
void systray_set_menu_items2(gboolean show);

#endif

