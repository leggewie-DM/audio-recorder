#ifndef _SUPPORT_H
#define _SUPPORT_H

#include <glib.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

gchar *get_data_directory();
gchar *get_package_data_directory();
gchar *get_image_directory();
gchar *get_image_path(const gchar *image_name);
gchar *get_program_name();


#endif


