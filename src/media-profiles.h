#ifndef _MEDIA_PROFILES_H__
#define _MEDIA_PROFILES_H__

#include <stdlib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>

// ComboBox columns
enum  {
    COL_PROFILE_ID,
    COL_PROFILE_TXT,  // profile name (displayed text)
    N_PROFILE_COLUMNS
};

typedef struct {
    gchar *id;
    gchar *ext;
    gchar *not_used; // kept for future
    gchar *pipe;
} ProfileRec;

void media_profiles_init();
void media_profiles_exit();


void profiles_get_data(GtkWidget *widget);

gchar *profiles_get_selected_id(GtkWidget *widget);

void profiles_save_configuration();
void profiles_update(gchar *old_id, gchar *id, gchar *file_ext, gchar *pipe_text);
void profiles_delete(const gchar *id);
void profiles_reset();

GList *profiles_get_list();
ProfileRec *profiles_find_rec(const gchar *id);

void media_profiles_load();
void media_profiles_clear();

const ProfileRec *profiles_find_profile(const gchar *id);
gchar *profiles_get_extension(const gchar *id);
gchar *profiles_get_pipeline(const gchar *id);

ProfileRec *profiles_find_for_ext(const gchar *ext);

gchar *profiles_get_selected_name(GtkWidget *widget);

GtkWidget *profiles_create_combobox();

gboolean profiles_check_id(const gchar *id);

gboolean profiles_test_plugin(gchar *id, gchar **err_msg);

#endif

