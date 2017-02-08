#ifndef _GST_DEVICES_H
#define _GST_DEVICES_H

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gst/gst.h>

#include "audio-sources.h"

void gstdev_module_init();
void gstdev_module_exit();

// List of audio input (source) devices
GList *gstdev_get_source_list();

#endif

