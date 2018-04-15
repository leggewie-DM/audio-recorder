/*
 * Copyright (c) 2011-2017 Osmo Antero.
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
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <dbus/dbus.h>

#include "dbus-skype.h"

#include "rec-manager-struct.h"

#include "utility.h"
#include "dconf.h"
#include "log.h"
#include "support.h"

// Interface to communicate with Skype over GDBus.
// Skype API: https://www.skype.com/en/developer/
// GDBus API: https://developer.gnome.org/gio/2.30/ch29.html

// Skype data
static struct {
    gint call_no;
    gint64 call_duration;
    gchar *filename;
    gboolean paused;
    gboolean record_ringing_sound;
} g_skype;

#define SKYPE_DBUS_API "com.Skype.API"
#define SKYPE_DBUS_TIMEOUT 400 // Milliseconds

static gint g_connect_count = 0;
static GMutex g_skype_mutex;

static GDBusConnection *g_dbus_conn = NULL;
static GDBusProxy *g_proxy_send = NULL;
static guint g_registration_id = 0;

static const gchar *g_introspection_xml = ""
        "<node>"
        "<interface name=\"com.Skype.API.Client\">"
        "<method name=\"Notify\">"
        "<annotation name=\"org.freedesktop.DBus.GLib.CSymbol\" value=\"skype_callback\"/>"
        "<arg type=\"s\" name=\"message\" direction=\"in\"/>"
        "</method>"
        "</interface>"
        "</node>";

static void skype_notify_callback(GDBusConnection *connection,
                                  const gchar           *sender,
                                  const gchar           *object_path,
                                  const gchar           *interface_name,
                                  const gchar           *method_name,
                                  GVariant              *parameters,
                                  GDBusMethodInvocation *invocation,
                                  gpointer               user_data);

static const GDBusInterfaceVTable g_interface_vtable = {
    skype_notify_callback, /* this is invoked by Skype */
    NULL,
    NULL,
};

void skype_connect();
void skype_disconnect();
GPid skype_start_application();
gboolean skype_is_running(gchar *service_name);

void skype_handle_message(const gchar *message);

void skype_setup_notify_methods(gboolean do_register/*register or unregister*/);

void skype_start_thread(GThreadFunc func, gpointer user_data);
gpointer skype_grant_thread(gpointer user_data);
gpointer skype_poll_thread(gpointer user_data);

gboolean skype_settle_protocol();
gboolean skype_bring_to_front();
gboolean skype_set_window_state(gchar *state);
gchar *skype_send_message(gchar *command);
gchar *skype_send_message_with_timeout(gchar *command, gint timeout);

gchar *skype_get_GET_value(gchar *command);

void skype_stop_recording();

gpointer skype_monitor_thread(gpointer user_data);

static void skype_GUI_message(gchar *msg);

void skype_module_init() {
    LOG_DEBUG("Init dbus-skype.c.\n");

    g_connect_count = 0;
    memset(&g_skype, '\0', sizeof(g_skype));

    g_dbus_conn = NULL;
    g_proxy_send = NULL;
    g_registration_id = 0;

    // Init mutex
    g_mutex_init(&g_skype_mutex);

    // Record ringing sound for incoming calls?
    conf_get_boolean_value("skype/record-ringing-sound", &g_skype.record_ringing_sound);
}

void skype_module_exit() {
    LOG_DEBUG("Clean up dbus-skype.c.\n");

    // Disconnect from Skype (if connected)
    skype_disconnect();

    if (G_IS_OBJECT(g_proxy_send)) {
        g_object_unref(g_proxy_send);
    }
    g_proxy_send = NULL;

    g_mutex_clear(&g_skype_mutex);
}

GDBusConnection *skype_connect_to_dbus() {
    // Connect to GDBus
    GError *error = NULL;

    if (!G_IS_DBUS_CONNECTION(g_dbus_conn)) {
        g_dbus_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

        if (!G_IS_DBUS_CONNECTION(g_dbus_conn)) {
            LOG_ERROR("skype_connect_to_dbus: Cannot connect to DBus: %s\n",
                      error ? error->message : "");

            if (error)
                g_error_free(error);

            return NULL;
        }
    }
    return g_dbus_conn;
}

void skype_disconnect_from_dbus() {
    // Disconnect from GDBus
    if (G_IS_DBUS_CONNECTION(g_dbus_conn)) {
        g_object_unref(g_dbus_conn);
    }
    g_dbus_conn = NULL;
}

void skype_set_record_ringing_sound(gboolean yes_no) {
    conf_save_boolean_value("skype/record-ringing-sound", yes_no);
    g_skype.record_ringing_sound = yes_no;
}

void skype_setup(gpointer player_rec, gboolean connect) {

    if (connect) {
        // Start and connect to Skype,

        skype_connect();

        // Register notification methods
        skype_setup_notify_methods(TRUE/*do register*/);

        // Disconnect from Skype
    } else {

        // Unregister notification methods
        skype_setup_notify_methods(FALSE/*unregister*/);

        skype_disconnect();
    }
}

gboolean skype_is_running(gchar *service_name) {
    // Check if Skype is running.

    GDBusConnection *dbus_conn = skype_connect_to_dbus();

    GError *error = NULL;

    gboolean is_running = FALSE;

    // Set lock (avoid overheating the DBus)
    g_mutex_lock(&g_skype_mutex);

    GDBusProxy *proxy = g_dbus_proxy_new_sync(dbus_conn,
                        G_DBUS_PROXY_FLAGS_NONE,
                        NULL,
                        "org.freedesktop.DBus",
                        "/org/freedesktop/DBus",
                        "org.freedesktop.DBus",
                        NULL,
                        &error);

    if (error) {
        LOG_ERROR("DBus error: Cannot create proxy.\n");
        g_error_free(error);
        goto LBL_1;
    }

    // Set timeout
    g_dbus_proxy_set_default_timeout(proxy, SKYPE_DBUS_TIMEOUT);

    error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(proxy, "NameHasOwner",
                       g_variant_new("(s)", service_name),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       &error);

    if (error) {
        g_printerr("Skype: Cannot execute NameHasOwner for %s. %s\n", service_name, error->message);
        g_error_free(error);
        goto LBL_1;
    }

    // Take a boolean value
    g_variant_get_child(result, 0, "b", &is_running);
    g_variant_unref(result);

LBL_1:
    if (G_IS_OBJECT(proxy)) {
        g_object_unref(proxy);
    }

    // Free the lock
    g_mutex_unlock(&g_skype_mutex);

    return is_running;
}

void skype_connect() {
    // Increment connection counter.
    // This will terminate all previous attempts and threads.
    g_connect_count += 1;

    // Note: We do not use pid here. It's just for debugging.
    GPid pid = -1;

    // Skype is already running?
    gboolean is_running = skype_is_running(SKYPE_DBUS_API);

    if (!is_running) {
        // Start Skype
        pid = skype_start_application();
        if (pid < 1) {
            LOG_SKYPE("Error: Cannot start Skype. Check if Skype is installed and in the $PATH.\n");
            return;
        }

        // Wait 1 second
        g_usleep(G_USEC_PER_SEC * 1.0);
    }

    // Re-test
    is_running = skype_is_running(SKYPE_DBUS_API);

    if (pid < 1) {
        pid = get_PID("skype");
        // Pid not used here.
    }

    // Check status
    gchar *str_val = skype_send_message_with_timeout("GET USERSTATUS", 400);

    // Bring to front
    skype_bring_to_front();

    // Got NULL or "ERROR 68" (68 = Access denied)
    if (!str_val || g_str_has_prefix(str_val, "ERROR")) {
        // Ask Skype show the "grant access" dialog
        skype_start_thread(skype_grant_thread, GINT_TO_POINTER(g_connect_count));
    }

    // Start to poll Skype
    skype_start_thread(skype_poll_thread, GINT_TO_POINTER(g_connect_count));
}

void skype_disconnect() {
    // Disconnect this app from the Skype

    g_connect_count = -1;
    g_skype.call_no = 0;

    // Unregister notify methods
    skype_setup_notify_methods(FALSE/*unregister*/);

    // Disconnct from DBus
    skype_disconnect_from_dbus();
}

void skype_start_app(gpointer player_rec) {
    ;
    // Do nothing
}

GPid skype_start_application() {
    // Start the Skype application

    // Get command path, normally "/usr/bin/skype-wrapper"
    gchar *cmd_path = find_command_path("skype-wrapper");

    // Try "/usr/bin/skype"
    if (!cmd_path) {
        cmd_path = find_command_path("skype");
    }

    if (!cmd_path) {
        cmd_path = g_strdup("skype");
    }

    // Build argv[] list
    gchar **argv = g_new(gchar*, 2);
    argv[0] = g_strdup(cmd_path);
    argv[1] = NULL;

    // Now create a process and run the command. It will return immediately because it's asynchronous.
    GError *error = NULL;
    GPid pid = exec_command_async(argv, &error);

    // Free the values
    if (error)
        g_error_free(error);

    g_strfreev(argv);
    g_free(cmd_path);

    return pid;
}

void skype_start_thread(GThreadFunc func, gpointer user_data) {
    GError *error = NULL;
    g_thread_try_new("Skype service thread", func, user_data, &error);
    if (error) {
        LOG_ERROR("Skype error: Cannot start thread. %s\n", error->message);
        g_error_free(error);
    }
}

gpointer skype_grant_thread(gpointer user_data) {
    // Try to get access to Skype.
    // Skype will show a "Grant access" dialog, and user must choose Yes or No.
    // Note: User can disable/enable this in the Skype's Public API menu.
    gboolean done = FALSE;

    gint my_counter = GPOINTER_TO_INT(user_data);

#define MAX_SECS (60*60*4.0) // 4.0 hours

    guint secs_count = 0;

    gint gui_msg1_sent = 0;
    gint gui_msg2_sent = 0;

    while (!done) {

        // Keep running? We have to quit if g_connect_count changes.
        if (g_connect_count < 0 || g_connect_count != my_counter) return 0;

        gboolean is_running = skype_is_running(SKYPE_DBUS_API);
        if (!is_running) {
            LOG_SKYPE("Grant thread #%d: Skype is not running.\n", my_counter);
        }

        // Ask Skype to grant access. Skype will now show its "Grant access" dialog.
        gchar *this_prog_name = get_program_name();

        // Replace spaces with '-' otherwise Skype will cut the name
        g_strdelimit(this_prog_name, " ", '-');

        // The command becomes "NAME Audio-Recorder"
        gchar *cmd = g_strdup_printf("NAME %s", this_prog_name);

        // Note: This call will block.
        gchar *str_val = skype_send_message_with_timeout(cmd, 20*1000/*wait upto 20 seconds*/);
        // Returns:
        //  "OK"  (Access granted)
        //  "ERROR 68" (Access denied).
        //  "CONNSTATUS OFFLINE"  (Offline, in the login dialog)
        //  NULL (Timed out)

        LOG_SKYPE("skype_grant_thread #%d. Try to get access to Skype. str_val:<%s>\n", my_counter, str_val);

        g_free(cmd);
        g_free(this_prog_name);

        if (!str_val) {
            // Message timed out.

        } else if (!g_strcmp0(str_val, "OK")) {
            // User has logged in and access was granted. Good.
            done = TRUE;
        }
        // ERROR 68, Access denied.
        else if (!g_strcmp0(str_val, "ERROR 68")) {

            if (gui_msg1_sent < 2) {
                // Send message to the GUI (normally a red message label)
                skype_GUI_message(
                    // Translators: The "Public API" setting refers to Skype's Options -> Public API menu.
                    _("Access to Skype denied.\nAnswer YES to grant access to Skype.\nYou can enable/disable this in the Skype's \"Public API\" settings."));

                // Send only once
                gui_msg1_sent += 1;
            }

        }
        // CONNSTATUS OFFLINE?
        else if (g_str_has_suffix(str_val, " OFFLINE")) {

            if (gui_msg2_sent < 2) {
                // Send message to the GUI (normally a red message label)
                // Translators: This message is shown in the GUI. A red label.
                skype_GUI_message(_("Skype is offline. Cannot connect to Skype unless you login."));

                // Send only once
                gui_msg2_sent += 1;
            }
        }
        // CONNSTATUS XXX?
        else if (g_strstr_len(str_val, -1, "CONNSTATUS")) {
            // Skype has stopped in the login-dialog. User must login first.
            ;
        }

        g_free(str_val);

        if (!done) {
            // Sleep 1.2 seconds
            g_usleep(G_USEC_PER_SEC * 1.2);
        }

        // Do not wait forever
        secs_count += 1;
        if (secs_count > MAX_SECS) return 0;
    }

    return 0;
}

gpointer skype_poll_thread(gpointer user_data) {
    // Check if user has done logon (to Skype) and this app has access to Skype.
    // If yes, settle the PROTOCOL and register notification methods.
    gint my_counter = GPOINTER_TO_INT(user_data);

    gboolean done = FALSE;
    while (!done) {

        // Keep running? We have to quit if g_connect_count changes.
        if (g_connect_count < 0 || g_connect_count != my_counter) return 0;

        // Do a simple test. Test if Skype gives a sensible answer.
        gchar *str_val = skype_send_message_with_timeout("PROTOCOL 2", 400);

        LOG_SKYPE("skype_poll_thread #%d. Settle protocol. str_val:<%s>\n", my_counter, str_val);

        if (!str_val) {
            ;
        } else if (g_str_has_prefix(str_val, "PROTOCOL")) {
            // Got a sensible answer.

            // Reset the GUI/error label
            skype_GUI_message(NULL);

            // Settle PROTOCOL. Strictly, we should test the return value from skype_settle_protocol.
            skype_settle_protocol();

            // Register notification methods
            skype_setup_notify_methods(TRUE/*register*/);

            // We're done here
            done = TRUE;
        }

        g_free(str_val);

        if (!done) {
            g_usleep(G_USEC_PER_SEC * 1);
        }
    }
    return 0;
}

gchar *skype_get_app_name() {
    // Return application name.

    // Name of the Skype application.
    // Translators: English "Skype" name is OK:
    return g_strdup(_("Skype"));
}

void skype_get_info(gpointer player_rec) {
    ;
}

static gchar *get_string_val(GVariant *v) {
    // Read and return a string value. The v can be either "s" or "(s)".
    // If container then return the first string.
    gchar *s = NULL;

    // Is it a container type "(s)"?
    if (g_variant_is_container(v)) {
        g_variant_get_child(v, 0, "s", &s);

        // Is it a string type "s"?
    }
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) {
        g_variant_get(v, "s", &s);
    }

    // Caller should g_free() this value
    return s;
}

gchar *skype_send_message(gchar *command) {
    return skype_send_message_with_timeout(command, 0);
}

gchar *skype_send_message_with_timeout(gchar *command, gint timeout) {
    // Send message to Skype

    gchar *str = NULL;

    // Set lock (avoid overheating the DBus)
    g_mutex_lock(&g_skype_mutex);

    // Connection
    GDBusConnection *dbus_conn = skype_connect_to_dbus();

    // Create proxy
    GError *error = NULL;

    if (!g_proxy_send) {
        g_proxy_send = g_dbus_proxy_new_sync(dbus_conn,
                                             G_DBUS_PROXY_FLAGS_NONE,
                                             NULL,
                                             SKYPE_DBUS_API, // "com.Skype.API"
                                             "/com/Skype",
                                             SKYPE_DBUS_API, // "com.Skype.API"
                                             NULL,
                                             &error);
    }

    if (error) {
        g_printerr("Skype: Cannot create proxy for %s. %s.\n", DBUS_INTERFACE_DBUS, error->message);
        g_error_free(error);
        goto LBL_1;
    }

    // Set timeout. Maybe 0.
    g_dbus_proxy_set_default_timeout(g_proxy_send, timeout);

    error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(g_proxy_send, "Invoke",
                       g_variant_new("(s)", command),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       &error);

    if (error) {
        g_printerr("Skype: Cannot execute Invoke() for %s. %s\n", command, error->message);
        g_error_free(error);
        goto LBL_1;
    }

    // Debug:
    // gchar *s = g_variant_print(result, TRUE);
    // g_print("Result variant:%s\n", s);
    // g_free(s);

    str = get_string_val(result);
    g_variant_unref(result);

LBL_1:

    // Free the lock
    g_mutex_unlock(&g_skype_mutex);

    // The caller should g_free() this value
    return str;
}

void skype_setup_notify_methods(gboolean do_register/*register or unregister*/) {
    // Register/unregister Skype signals

    // Connect to DBus
    GDBusConnection *dbus_conn = skype_connect_to_dbus();

    /* Some debug info from the dbus_monitor.
    $ dbus_monitor

     method call sender=:1.121 -> dest=:1.124 serial=20 path=/com/Skype/Client; interface=com.Skype.API.Client; member=Notify
       string "CALL 64 XXX"

     method call sender=:1.121 -> dest=:1.124 serial=21 path=/com/Skype/Client; interface=com.Skype.API.Client; member=Notify
       string "CALL 64 STATUS XXX"

    See: https://lists.freedesktop.org/archives/dbus/2010-December/013910.html
    */

    // Do register, connect?
    if (do_register) {

        // Already connected?
        if (g_registration_id > 0) {
            return;

        }
        GDBusNodeInfo *introspection_data = g_dbus_node_info_new_for_xml(g_introspection_xml, NULL);
        g_assert(introspection_data != NULL);

        g_registration_id = g_dbus_connection_register_object(dbus_conn,
                            SKYPE_SERVICE_PATH,
                            introspection_data->interfaces[0],
                            &g_interface_vtable,
                            NULL,  /* user_data */
                            NULL,  /* user_data_free_func */
                            NULL); /* GError** */

        g_dbus_node_info_unref(introspection_data);

        if (g_registration_id > 0) {
            LOG_SKYPE("Skype: Notification methods connected. g_registration_id=%d\n", g_registration_id);
        } else {
            LOG_SKYPE("Skype: Error, cannot connect notification methods. g_registration_id=%d\n", g_registration_id);
        }


        // Unregister, disconnect
    } else {

        // Already disconnected?
        if (g_registration_id < 1) {
            return;
        }

        if (!g_dbus_connection_unregister_object(dbus_conn, g_registration_id)) {
            LOG_SKYPE("Skype: Error, cannot disconnect notification methods.\n");

        } else {
            LOG_SKYPE("Skype: Notification methods disconnected. OK.\n");
        }

        g_registration_id = 0;
    }

}

gboolean skype_settle_protocol() {
    // Send "PROTOCOL #" message to Skype and check if it replies OK.

    guint i = 0;
    for (i=7; i > 1; i--) {
        // Try "PROTOCOL 7", "PROTOCOL 6" ... "PROTOCOL 2"
        gchar *protocol = g_strdup_printf("PROTOCOL %d", i);

        gchar *str_ret = skype_send_message_with_timeout(protocol, SKYPE_DBUS_TIMEOUT);

        // Got the same string back?
        gboolean ok = !g_strcmp0(str_ret, protocol);

        g_free(str_ret);
        str_ret = NULL;

        g_free(protocol);
        protocol = NULL;

        // Protocol accepted
        if (ok) return TRUE;
    }
    return FALSE;
}

gboolean skype_set_window_state(gchar *state) {
    // Set the window state.
    // WINDOWSTATE NORMAL|MINIMIZED|MAXIMIZED|HIDDEN
    gchar *cmd = g_strdup_printf("WINDOWSTATE %s", state);
    gchar *str_ret = skype_send_message_with_timeout(cmd, SKYPE_DBUS_TIMEOUT);
    g_free(cmd);

    // Got "OK"?
    gboolean ok = !g_strcmp0(str_ret, "OK");
    g_free(str_ret);

    return ok;
}

gboolean skype_bring_to_front() {
    // Bring Skype window to the front. Set focus on the window.
    gchar *str_ret = skype_send_message_with_timeout("FOCUS", SKYPE_DBUS_TIMEOUT);

    // Got "OK"?
    gboolean ok = !g_strcmp0(str_ret, "OK");
    g_free(str_ret);

    return ok;
}

gboolean is_error_str(gchar *str) {
    // Check if str begins with "ERROR "
    if (!str) return FALSE;

    return g_str_has_prefix(str, "ERROR ");
}

gchar *skype_get_version() {
    // GET SKYPEVERSION
    // <- SKYPEVERSION 2.1.0
    gchar *str = skype_get_GET_value("GET SKYPEVERSION");

    // The caller should g_free() this value
    return str;
}

gchar *skype_get_program_name() {
    // Return program name.
    // Eg. "Skype 2.1.0"
    gchar *ver_str = skype_get_version();
    gchar *s = NULL;
    if (ver_str)
        // Skype name + version.
        // Translators: English "Skype %s" is OK:
        s = g_strdup_printf(_("Skype %s"), ver_str);
    else
        // Skype name.
        // Translators: English "Skype" is OK:
        s = g_strdup(_("Skype"));

    g_free(ver_str);

    // The caller should g_free() this value
    return s;
}

gchar *skype_get_status(gint call_no) {
    // GET CALL 196 STATUS
    // <- CALL 196 STATUS INPROGRESS|FINISHED|FAILED
    if (call_no < 1) return NULL;

    gchar *cmd = g_strdup_printf("GET CALL %d STATUS", call_no);
    gchar *status = skype_get_GET_value(cmd);
    g_free(cmd);

    // Caller should g_free() this value
    return status;
}

gchar *skype_get_user_id() {
    // GET CURRENTUSERHANDLE
    // <-- CURRENTUSERHANDLE Alexander
    gchar *name = skype_get_GET_value("GET CURRENTUSERHANDLE");

    // Caller should g_free() this value
    return name;
}

gchar *skype_get_user_name(gchar *skype_id) {
    // GET USER skype-id FULLNAME
    // <- USER skype-id FULLNAME Alexander Luis Botero
    gchar *cmd = g_strdup_printf("GET USER %s FULLNAME", skype_id);
    gchar *name = skype_get_GET_value(cmd);
    g_free(cmd);

    // Caller should g_free() this value
    return name;
}

gchar *skype_get_partner_id() {
    // Get partner (eg. the caller's) id
    // GET CALL 204 PARTNER_HANDLE
    // <- CALL 204 PARTNER_HANDLE Anna-Katarina
    gchar *cmd = g_strdup_printf("GET CALL %d PARTNER_HANDLE", g_skype.call_no);
    gchar *caller_name = skype_get_GET_value(cmd);
    g_free(cmd);

    // Caller should g_free() this value
    return caller_name;
}

gchar *skype_get_partner_name() {
    // GET CALL 204 PARTNER_DISPNAME
    // <- CALL 204 PARTNER_DISPNAME Anna Katarina Hall
    gchar *cmd = g_strdup_printf("GET CALL %d PARTNER_DISPNAME", g_skype.call_no);
    gchar *caller_name = skype_get_GET_value(cmd);
    g_free(cmd);

    // Caller should g_free() this value
    return caller_name;
}

gchar *skype_get_call_type() {
    // GET CALL 204 TYPE
    // <- CALL 204 TYPE INCOMING_PSTN - incoming call from PSTN
    // <- CALL 204 TYPE OUTGOING_PSTN - outgoing call to PSTN
    // <- CALL 204 TYPE INCOMING_P2P - incoming call from P2P
    // <- CALL 204 TYPE OUTGOING_P2P - outgoing call to P2P
    gchar *cmd = g_strdup_printf("GET CALL %d TYPE", g_skype.call_no);
    gchar *type = skype_get_GET_value(cmd);
    g_free(cmd);

    gchar *s = NULL;
    // Is incoming call?
    if (!g_strstr_len(type,-1, "INCOMIN"))
        s = g_strdup("INCOMING");
    else if (type)
        s = g_strdup("OUTGOING");

    g_free(type);

    // Caller should g_free() this value
    return s;
}

gchar *get_skype_target_phone_number() {
    // CALL 204 TARGET_IDENTITY
    // <- CALL 204 TARGET_IDENTITY 87665522
    gchar *cmd = g_strdup_printf("GET CALL %d TARGET_IDENTITY", g_skype.call_no);
    gchar *target = skype_get_GET_value(cmd);
    g_free(cmd);

    // Caller should g_free() this value
    return target;
}

gchar *skype_get_rec_folder() {
    // Translators: This is a folder/directory where we put recordings from the Skype
    return g_strdup(_("Skype calls"));
}

gchar *skype_create_filename(gchar *my_id, gchar *partner_id, gchar *call_type, gchar *target_phone) {
    // Get filename pattern
    gchar *filename_pattern = get_filename_pattern();
    gchar *pattern = NULL;

    // INCOMING, OUTCOMING?
    if (!g_strcmp0(call_type, "INCOMING")) {
        // Incoming Skype call.
        // Translators: This is used like "Call from Alexander to Anna-Katarina 2010-12-26 10:30:20"
        pattern = g_strdup_printf(_("Call from %s to %s %s"), my_id, partner_id, filename_pattern);
    } else {
        // Outgoing Skype call.
        // Translators: This is used like "Call from Anna-Katarina to Alexander 2010-12-26 10:30:20"
        pattern = g_strdup_printf(_("Call from %s to %s %s"), partner_id, my_id, filename_pattern);
    }

    gchar *fname = substitute_time_and_date_pattern(pattern);
    purify_filename(fname, TRUE);

    g_free(filename_pattern);
    g_free(pattern);

    // The caller should g_free() this value
    return fname;
}

gpointer skype_monitor_thread(gpointer user_data) {
    // This thread will monitor the Skype call.
    // If Skype has vanished/terminated then this thread will terminate the recording.
    // Make sure the recording stops if one side of Skype-parts quits inadvertently.
    gint call_no = GPOINTER_TO_INT(user_data);
    if (call_no < 0) return 0;

#define SLEEP_SECONDS 3
#define MAX_FAILURES 6

    // Terminate call after MAX_FAILURES
    guint failure_count = 0;

    while (1) {

        // User (in the GUI) has de-selected Skype?
        // g_connect_count has changed?
        if (g_connect_count < 0) return 0;

        // Sleep SLEEP_SECONDS
        g_usleep(G_USEC_PER_SEC * SLEEP_SECONDS);

        // Check call status

        // GET CALL 266 STATUS
        // <- CALL 266 STATUS INPROGRESS|FAILED|FINISHED|...
        gchar *status = skype_get_status(call_no);

        if (!g_strcmp0(status, "FINISHED") ||
                !g_strcmp0(status, "REFUSED") ||
                !g_strcmp0(status, "CANCELLED") ||
                !g_strcmp0(status, "FAILED") ||
                !g_strcmp0(status, "MISSED")) {

            // Call_no has been finished/refused/cancelled/failed/etc. Terminate this thread.

            // But first, make sure the recorder has stopped. Sometimes Skype fails to send
            // "FINISHED" or "CANCELLED" message when out-call is interrupted before answered.
            if (g_skype.call_no > 0) {
                skype_stop_recording();
            }

            g_free(status);
            return 0;
        }

        // The call is routing/ringing/on hold/in progress?
        gboolean in_progress = (!g_strcmp0(status, "INPROGRESS") ||
                                !g_strcmp0(status, "RINGING") ||
                                !g_strcmp0(status, "ONHOLD") ||
                                !g_strcmp0(status, "LOCALHOLD") ||
                                !g_strcmp0(status, "REMOTEHOLD") ||
                                !g_strcmp0(status, "ROUTING"));

        if (in_progress)
            LOG_SKYPE("Call %d in progress. Status:%s\n", call_no, status);
        else
            LOG_SKYPE("Call %d has terminated. Status:%s\n", call_no, status);

        g_free(status);

        if (!in_progress)  {
            // Count failures
            failure_count += 1;
            if (failure_count < MAX_FAILURES) continue;

            if (g_skype.call_no == call_no) {
                LOG_SKYPE("Something has gone wrong. Stopping recroding.");
                skype_stop_recording();
            }

            return 0;

        } else  {
            failure_count = 0;
        }

    } // while

    return 0;
}

void skype_start_recording() {
    LOG_SKYPE("Skype: start recording.\n");

    // Get data
    gchar *skype_program = skype_get_program_name();

    gchar *my_id = skype_get_user_id();
    gchar *my_name = skype_get_user_name(my_id);

    gchar *partner_id = skype_get_partner_id(); // Call partner's id
    gchar *partner_name = skype_get_partner_name(); // Call partner's name

    gchar *call_type = skype_get_call_type();

    gchar *target_phone = get_skype_target_phone_number();

    gchar *skype_folder = skype_get_rec_folder();

    LOG_SKYPE("Skype program:<%s>\n", skype_program);
    LOG_SKYPE("My Skype id and name:<%s> <%s>\n", my_id, my_name);
    LOG_SKYPE("Call partner's Skype id and name:<%s> <%s>\n", partner_id, partner_name);
    LOG_SKYPE("Call type:<%s>\n", call_type);
    LOG_SKYPE("Target phone num:<%s>\n", target_phone);

    // Start monitoring thread (do not start duplicate threads if the call was paused, on LOCALHOLD)
    if (!g_skype.paused) {

        if (g_skype.filename) {
            g_free(g_skype.filename);
        }

        g_skype.filename = skype_create_filename(my_id, partner_id, call_type, target_phone);

        LOG_SKYPE("Skype folder:<%s>\n",  skype_folder);
        LOG_SKYPE("Skype filename:<%s>\n", g_skype.filename);

        g_skype.call_duration = 0;

        // Start new monitoring thread
        skype_start_thread(skype_monitor_thread, GINT_TO_POINTER(g_skype.call_no));
    }

    g_skype.paused = FALSE;

    // Send message to rec-manager.c

    // Create new RecorderCommand
    RecorderCommand *cmd = g_malloc0(sizeof(RecorderCommand));
    cmd->type = RECORDING_START;

    // Set track name
    cmd->track = g_strndup(g_skype.filename, DEF_STR_LEN);

    // Set folder name as artist ;-)
    cmd->artist = g_strndup(skype_folder, DEF_STR_LEN);

    // Set (call) partner name as album ;-)
    cmd->album = g_strndup(partner_name, DEF_STR_LEN);

    // We do not set these:
    // cmd->track_pos = 0
    // cmd->track_len = 0;
    // cmd->flags = 0;

    // Send command to rec-manager.c.
    // It will free the cmd structure after processing.
    rec_manager_send_command(cmd);

    // Free values
    g_free(skype_program);
    g_free(my_id);
    g_free(my_name);
    g_free(partner_id);
    g_free(partner_name);
    g_free(call_type);
    g_free(target_phone);
    g_free(skype_folder);
}

void skype_stop_recording() {
    LOG_SKYPE("Skype: stop recording.\n");

    // Call has terminated normally?
    gboolean finished_ok = FALSE;

    // GET CALL 266 STATUS
    // <- CALL 266 STATUS FINISHED
    // Status can be: FINISHED, FAILED, UNPLACED, ROUTING, RINGING, INPROGRESS,
    //                LOCALHOLD, REMOTEHOLD, MISSED, REFUSED, BUSY, CANCELLED
    gchar *status = skype_get_status(g_skype.call_no);
    finished_ok = status && (!g_strcmp0(status, "FINISHED"));
    g_free(status);

    // Create new RecorderCommand
    RecorderCommand *cmd = g_malloc0(sizeof(RecorderCommand));
    cmd->type = RECORDING_STOP;

    // Set track name
    cmd->track = g_strndup(g_skype.filename, DEF_STR_LEN);

    // Got DURATION messages and ok?
    if (g_skype.call_duration < 1  && !finished_ok) {
        // Delete this recording. Set "Delete file" flag.

        LOG_SKYPE("Skype determines that the file is empty. Delete the recorded %s file.\n", g_skype.filename);

        // Set the delete flag
        cmd->flags = RECORDING_DELETE_FILE;
    }

    // Send command to rec-manager.c.
    // It will free the cmd structure after processing.
    rec_manager_send_command(cmd);

    // Reset values
    g_skype.call_no = 0;
    g_skype.call_duration = 0;
    g_skype.paused = FALSE;

    if (g_skype.filename) {
        g_free(g_skype.filename);
    }

    g_skype.filename = NULL;

    // Note: The monitoring thread will terminate itself because g_skype.call_no=0.
}

void skype_pause_recording() {
    g_skype.paused = TRUE;
    LOG_SKYPE("Skype: pause recording.\n");

    // Create new RecorderCommand
    RecorderCommand *cmd = g_malloc0(sizeof(RecorderCommand));
    cmd->type = RECORDING_PAUSE;

    // Set track name
    cmd->track = g_strndup(g_skype.filename, DEF_STR_LEN);

    // Send command to rec-manager.c.
    // It will free the cmd structure after processing.
    rec_manager_send_command(cmd);
}

void skype_handle_CALL(gchar *msg) {
    /*
    Important CALL messages:
     CALL 202 STATUS RINGING   // Start recording here if g_skype.record_ringing_sound == TRUE.

       CALL 202 STATUS REFUSED  // If the call was refused
       CALL 202 STATUS INPROGRESS   // The call is accepted. Start recording here.

     CALL 202 VIDEO_SEND_STATUS AVAILABLE
     CALL 202 VIDEO_STATUS VIDEO_BOTH_ENABLED
     CALL 202 DURATION 1
     CALL 202 DURATION 2
     ...
     CALL 202 DURATION 6   // Check if the call is active each 6'th seconds
     ...
     CALL 202 DURATION 70
     CALL 202 STATUS FINISHED  // Stop recording here
     ----

     CALL 202 FAILUREREASON 14  // Stop recording on error

     CALL 266 FAILUREREASON 3 // No answer
     CALL 266 STATUS FAILED // Call failed
     */

    // Take call number. Eg. 202
    gchar *arg2 = get_nth_arg(msg, 2, FALSE);

    // Take 3.rd argument. Eg. STATUS, DURATION or FAILUREREASON
    gchar *arg3 = get_nth_arg(msg, 3, FALSE);

    // Take 4.th argument. Eg. RINGING, INPROGRESS or duration value
    gchar *arg4 = get_nth_arg(msg, 4, FALSE);

    if (!g_strcmp0(arg3, "STATUS")) {
        // CALL 202 STATUS RINGING|REFUSED|INPROGRESS|FAILED

        // The phone is ringing?
        if (!g_strcmp0(arg4, "RINGING")) {

            // Take call no
            g_skype.call_no = atol(arg2);

            // Start recording from ringing sound?
            if (g_skype.record_ringing_sound) {
                skype_start_recording();
            }

        }

        // The call is in progress?
        else if (!g_strcmp0(arg4, "INPROGRESS")) {
            // (!g_strcmp0(arg4, "JOIN_CONFERENCE"))
            // Take call no
            g_skype.call_no = atol(arg2);
            // Start recording this call
            skype_start_recording();
        }

        // The call has ended?
        else if (!g_strcmp0(arg4, "FINISHED") ||
                 !g_strcmp0(arg4, "FAILED") ||
                 !g_strcmp0(arg4, "CANCELLED") ||
                 !g_strcmp0(arg4, "REFUSED") ||
                 !g_strcmp0(arg4, "MISSED")) {

            // Stop recoding
            skype_stop_recording();

        } else if (!g_strcmp0(arg4, "ONHOLD") ||
                   !g_strcmp0(arg4, "LOCALHOLD")) {
            // We do not react to "REMOTEHOLD" command

            // Pause recording
            skype_pause_recording();

        } else {

            LOG_SKYPE("Got message: CALL %d STATUS %s\n", g_skype.call_no, arg4);
        }

        // INPROGRESS - answer or resume call
        // FINISHED - hang up call
        // SEEN - sets call as seen, so


        goto LBL_1;
    }

    if (!g_strcmp0(arg3, "FAILUREREASON")) {
        // CALL 202 FAILUREREASON 14
        // Some error happened. Wait for STATUS change and stop recording.
        goto LBL_1;
    }

    if (!g_strcmp0(arg3, "DURATION")) {
        // CALL 202 DURATION 17  (in seconds)

        // Are we dropped in the middle of an on-going Skype call? Handle this situation gracefully.
        if (g_skype.call_no <= 0 && g_skype.filename == NULL) {
            // Do not re-initiate the recording if g_skype.filename != NULL.
            // User may have stopped the recording intentionally.

            // Take call no
            g_skype.call_no = atol(arg2);

            LOG_SKYPE("Dropped in the middle of a Skype call/conversation. Initiate recording.\n");

            // Make sure we are recording this call
            skype_start_recording();
        }

        // Save duration value
        g_skype.call_duration = atol(arg4);

        goto LBL_1;
    }

LBL_1:
    // Clean up
    g_free(arg2);
    g_free(arg3);
    g_free(arg4);
}

void skype_handle_message(const gchar *message) {
    if (!message) return;

    // Debug the message:
    LOG_SKYPE("skype_handle_message:<%s>\n", message);

    /*
    Typical call sequence is:
    -> NAME Audio-Recorder  // Skype shows a dialog box and grants access (this can be overriden in Skype's Options).
       OK
    -> PROTOCOL 7
       PROTOCOL 7
       CONNSTATUS ONLINE
       CURRENTUSERHANDLE Moma-Antero
       USERSTATUS ONLINE
       CALL 202 CONF_ID 0
       CALL 202 STATUS RINGING   // Start recording here if g_skype.record_ringing_sound == TRUE.
       CALL 202 STATUS INPROGRESS   // Else start recording here
       CALL 202 VIDEO_SEND_STATUS AVAILABLE
       CALL 202 VIDEO_STATUS VIDEO_BOTH_ENABLED
       CALL 202 DURATION 1
       CALL 202 DURATION 2
       ...
       CALL 202 DURATION 6   // Poll Skype. Check if the call is active each 6'th second
       ...
       CALL 202 DURATION 12
       ...
       CALL 202 DURATION 70
       CALL 202 STATUS FINISHED  // Stop recording here
       -----
       Can also receive:
       CALL 202 FAILUREREASON 14  // Stop recording on errors
       -----
        The status can be (some examples):
        CALL 202 STATUS ____
        UNPLACED - call was never placed
        ROUTING - call is currently being routed
        EARLYMEDIA - with pstn it is possible that before a call is established, early media is played.
                     For example it can be a calling tone or a waiting message such as all operators are busy.
        FAILED - call failed - try to get a FAILUREREASON for more information.
        RINGING - currently ringing
        INPROGRESS - call is in progress
        ONHOLD - call is placed on hold
        FINISHED - call is finished
        MISSED - call was missed
        REFUSED - call was refused
        BUSY - destination was busy
        CANCELLED (Protocol 2)

        Important ones:
        ONHOLD - hold call
        INPROGRESS - answer or resume call
        FINISHED - hang up call
        FAILED - call failed
        UNPLACED - call was never placed
    */


    // Create a local copy
    gchar *msg = g_strdup(message);

    // Get the first argument.
    gchar *cmd = get_nth_arg(msg, 1, FALSE);

    // It is a "CALL ..." message?
    if (!g_strcmp0(cmd, "CALL")) {
        skype_handle_CALL(msg);
    }

    // Free values
    g_free(cmd);
    g_free(msg);
}

gchar *skype_get_GET_value(gchar *command) {
    // Send a "GET ..." command and return a value.
    // Examples:
    //  skype_get_GET_value(GET CURRENTUSERHANDLE") might return "Anna-Katarina".
    //  In this case the str_ret would be "CURRENTUSERHANDLE Anna-Katarina".
    //  or
    //  skype_get_GET_value("GET USER Alexander FULLNAME") might return "Alexander Luis Botero".
    //  In this case the str_ret would be "Alexander FULLNAME Alexander Luis Botero".

    gchar *str_ret = skype_send_message_with_timeout(command, SKYPE_DBUS_TIMEOUT);

    if (!str_ret) return NULL;

    // Got "ERROR #"?
    if (is_error_str(str_ret)) {
        LOG_ERROR("Skype error:%s (%s).\n", str_ret, command);
        g_free(str_ret);
        return NULL;
    }

    // Take the GET argument (the last token of command)
    gchar *get_arg = get_last_arg(command);

    // Find get_arg in the str_ret string
    gchar *p = g_strstr_len(str_ret, -1, get_arg);
    gchar *val = NULL;
    if (p) {
        // Take the 2.nd argument and all the rest
        val = get_nth_arg(p, 2, TRUE);
    }

    if (!val) {
        val = g_strdup(str_ret);
    }

    str_trim(val);

    g_free(get_arg);
    g_free(str_ret);

    // The caller should g_free() this value
    return val;
}

static void skype_GUI_message(gchar *msg) {
    RecorderCommand *cmd = g_malloc0(sizeof(RecorderCommand));
    cmd->type = RECORDING_NOTIFY_MSG;
    cmd->track = g_strndup(msg, DEF_STR_LEN);

    // Send command to rec-manager.c.
    // It will free the cmd structure after processing.
    rec_manager_send_command(cmd);
}

static void skype_notify_callback (GDBusConnection *connection,
                                   const gchar           *sender,
                                   const gchar           *object_path,
                                   const gchar           *interface_name,
                                   const gchar           *method_name,
                                   GVariant              *parameters,
                                   GDBusMethodInvocation *invocation,
                                   gpointer               user_data) {

    // This method is called by Skype to send messages to this module.
    const gchar *message;

    // Ref: https://developer.gnome.org/glib/2.30/gvariant-format-strings.html#gvariant-format-strings-pointers
    g_variant_get(parameters, "(&s)", &message/*pointer only*/);

    LOG_SKYPE("skype_notify_callback: object:%s, method:%s, message:%s\n", object_path, method_name, message);

    // Handle this message
    skype_handle_message(message);
}

