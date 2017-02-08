/*
 * Copyright (c) Linux community.
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
#include "log.h"
#include "support.h"
#include "about.h"
#include "rec-manager.h"
#include <gst/gst.h>

// This module creates a DBus-server for this program.
// Other programs can control the recorder by calling methods via this DBus interface.
//
// Audio-recorder itself can send messages to the running instance of this program.
// See audio-recorder --help for more information.
//
//  Exported DBus methods are:
//  get_state(), returns current recording state: "not running" | "on" | off" | "paused".
//
//  set_state(state), set new state. The state can be one of: "start"|"stop"|"pause"|"hide"|"show"|"quit".
//                    returns "OK" if success. NULL if error.
//
// Ref: https://developer.gnome.org/gio/stable/GDBusServer.html
//
// Notice: This module has nothing to do with dbus-player.[ch], dbus-mpris2.[ch] modules.
//

#define R_DBUS_SERVER_ADDRESS "unix:abstract=audiorecorder"

#define R_DBUS_OBJECT_PATH "/org/gnome/API/AudioRecorder"
#define R_DBUS_INTERFACE_NAME "org.gnome.API.AudioRecorderInterface"

static GDBusServer *g_dbus_server = NULL;
static GDBusNodeInfo *g_introspection_data = NULL;

// Signatures for the methods we are exporting.
// DBus clients can get information or control the recorder by calling these functions.
static const gchar g_introspection_xml[] =
    "<node>"
    "  <interface name='org.gnome.API.AudioRecorderInterface'>"
    "    <method name='get_state'>"
    "      <arg type='s' name='response' direction='out'/>"  // Returns current recording state: "not running"|"on"|off"|"paused"
    "    </method>"
    "    <method name='set_state'>"
    "      <arg type='s' name='state' direction='in'/>"      // Set new state. Input argument:"start"|"stop"|"pause"|"hide"|"show"|"quit".
    "      <arg type='s' name='response' direction='out'/>"  // Returns "OK" or NULL if error.
    "    </method>"
    "  </interface>"
    "</node>";

static gboolean dbus_service_start();
static void dbus_service_set_state(gchar *new_state);

// -----------------------------------------------------------------------------------

void dbus_service_module_init() {
    LOG_DEBUG("Init dbus_service.c\n");

    g_dbus_server = NULL;

    // Start service
    dbus_service_start();
}

void dbus_service_module_exit() {
    LOG_DEBUG("Clean up dbus_service.c.\n");

    if (g_introspection_data) {
        g_dbus_node_info_unref(g_introspection_data);
    }
    g_introspection_data = NULL;

    if (g_dbus_server) {
        g_dbus_server_stop(g_dbus_server);
        g_object_unref(g_dbus_server);
    }
    g_dbus_server = NULL;
}

static void handle_method_call(GDBusConnection *connection,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data) {

    // DBus method call: get_state.
    // Returns "not running" | "on" | "off" | "paused"
    if (g_strcmp0(method_name, "get_state") == 0) {

        // Get recording state
        gint state = -1;
        gint pending = -1;
        rec_manager_get_state(&state, &pending);

        const gchar *state_str = NULL;

        switch (state) {
        case GST_STATE_PAUSED:
            state_str = "paused";
            break;

        case GST_STATE_PLAYING:
            state_str = "on";
            break;

        default:
            state_str = "off";
        }

        g_dbus_method_invocation_return_value(invocation, g_variant_new ("(s)", state_str));
        LOG_DEBUG("Audio recorder (DBus-server) executed method get_state().\n");
    }

    // DBus method call: set_state(new_state).
    // new_state can be: "start" | "stop" | "pause" | "show"  | "hide" | "quit".
    // Returns "OK" | NULL
    else if (g_strcmp0 (method_name, "set_state") == 0) {
        gchar *response = g_strdup("OK");

        gchar *new_state = NULL;
        g_variant_get(parameters, "(&s)", &new_state);

        g_dbus_method_invocation_return_value(invocation, g_variant_new ("(s)", response));
        g_free(response);
        LOG_DEBUG("Audio recorder (Dbus-server) executed method set_state(%s).\n", new_state);

        // Set recorder to new_state
        dbus_service_set_state(new_state);
    }
}

static void dbus_service_set_state(gchar *new_state) {
    // Set recorder to new_state

    // $ audio-recorder --command start
    if (!g_strcmp0(new_state, "start")) {
        rec_manager_start_recording();
    }

    // $ audio-recorder --command stop
    else if (!g_strcmp0(new_state, "stop")) {
        rec_manager_stop_recording();
    }

    // $ audio-recorder --command pause
    else if (!g_strcmp0(new_state, "pause")) {
        rec_manager_pause_recording();
    }

    // $ audio-recorder --command show
    else if (!g_strcmp0(new_state, "show")) {
        rec_manager_show_window(TRUE);
    }

    // $ audio-recorder --command hide
    else if (!g_strcmp0(new_state, "hide")) {
        rec_manager_show_window(FALSE);
    }

    // $ audio-recorder --command quit
    else if (!g_strcmp0(new_state, "quit")) {
        rec_manager_quit_application();
    }
}

static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call,
    NULL,
    NULL,
};

// ---------------------------------------------------------------------------------

static gboolean on_new_connection(GDBusServer *server, GDBusConnection *connection, gpointer user_data) {
    /*
    GCredentials *credentials = NULL;
    credentials = g_dbus_connection_get_peer_credentials(connection);

    gchar *s = NULL;
    if (credentials == NULL)
        s = g_strdup ("(no credentials received)");
    else
        s = g_credentials_to_string(credentials);


    LOG_DEBUG("Client connected.\n"
           "Peer credentials: %s\n"
           "Negotiated capabilities: unix-fd-passing=%d\n",
           s,
           g_dbus_connection_get_capabilities(connection) & G_DBUS_CAPABILITY_FLAGS_UNIX_FD_PASSING);
    */

    g_object_ref(connection);

    gint registration_id = g_dbus_connection_register_object(connection,
                           R_DBUS_OBJECT_PATH,
                           g_introspection_data->interfaces[0],
                           &interface_vtable,
                           NULL,  /* user_data */
                           NULL,  /* user_data_free_func */
                           NULL); /* GError */

    return (registration_id > 0);
}


static gboolean dbus_service_start() {
    // Start DBus-server for this application.
    // This will receive method calls from external programs.

    // Build introspection data from XML
    g_introspection_data = g_dbus_node_info_new_for_xml(g_introspection_xml, NULL);

    // Server flags
    GDBusServerFlags server_flags = G_DBUS_SERVER_FLAGS_NONE;
    server_flags |= G_DBUS_SERVER_FLAGS_AUTHENTICATION_ALLOW_ANONYMOUS;

    gchar *guid = g_dbus_generate_guid();

    // Create a new D-Bus server that listens on R_DBUS_SERVER_ADDRESS
    GError *error = NULL;
    g_dbus_server = g_dbus_server_new_sync(R_DBUS_SERVER_ADDRESS,
                                           server_flags,
                                           guid,
                                           NULL, /* GDBusAuthObserver */
                                           NULL, /* GCancellable */
                                           &error);

    g_dbus_server_start(g_dbus_server);
    g_free(guid);

    if (g_dbus_server == NULL) {
        LOG_ERROR("Cannot create server address %s for DBus. %s\n", R_DBUS_SERVER_ADDRESS, error->message);
        g_error_free(error);
        return FALSE;
    }

    LOG_DEBUG("This Audio Recorder is listening on DBus at: %s\n", g_dbus_server_get_client_address(g_dbus_server));

    // Set up callback method (for client calls)
    g_signal_connect(g_dbus_server, "new-connection", G_CALLBACK (on_new_connection), NULL);

    return (g_dbus_server != NULL);
}

gchar *dbus_service_client_request(gchar *method_name, gchar *arg) {
    // Execute method call on the server.
    // Audio-recorder itself can call methods on the server.
    // DBus-server will most likely return a value. We will pass it to the calling function.

    GError *error = NULL;

    GDBusConnection *connection = g_dbus_connection_new_for_address_sync(R_DBUS_SERVER_ADDRESS,
                                  G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                  NULL, // GDBusAuthObserver
                                  NULL, // GCancellable
                                  &error);

    if (!connection) {
        LOG_DEBUG("Cannot connect to DBus address %s. %s\n", R_DBUS_SERVER_ADDRESS, error->message);
        g_error_free(error);
        return NULL;
    }

    // Optional argument
    GVariant *argument = NULL;
    if (arg) {
        argument = g_variant_new("(s)", arg);
    }

    GVariant *value = NULL;
    value = g_dbus_connection_call_sync(connection,
                                        NULL, // bus_name
                                        R_DBUS_OBJECT_PATH,
                                        R_DBUS_INTERFACE_NAME,
                                        method_name, // method_name
                                        argument, // input parm
                                        G_VARIANT_TYPE ("(s)"), // return value (one string)
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);

    if (!value) {
        LOG_ERROR("Error invoking %s(%s). %s\n", method_name, arg, error->message);
        g_error_free(error);
        g_object_unref(connection);
        return NULL;
    }

    gchar *server_response = NULL;
    g_variant_get(value, "(&s)", &server_response);

    // Take copy of the value
    gchar *ret = g_strdup(server_response);

    if (value) {
        g_variant_unref(value);
    }

    //Notice: "argument" is floating and consumed by g_dbus_connection_call_sync().
    //if (argument)
    //  g_variant_unref(argument);
    //}

    g_object_unref(connection);

    // Return ret
    return ret;
}


