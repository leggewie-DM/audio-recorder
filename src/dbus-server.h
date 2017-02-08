#ifndef __DBUS_SERVICE_H__
#define __DBUS_SERVICE_H__

#include "log.h"
#include "support.h"

void dbus_service_module_init();

void dbus_service_module_exit();

// Start DBus-server for this audio recorder
gboolean dbus_service_start();

// Execute client request (method call) on DBus-server
gchar *dbus_service_client_request(gchar *method_name, gchar *arg);

#endif

