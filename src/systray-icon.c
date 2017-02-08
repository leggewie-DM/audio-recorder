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
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib.h>

#include <gst/gst.h> // GST_STATE_* values

#include "log.h"
#include "support.h"
#include "utility.h"
#include "dconf.h"

#include "about.h"
#include "rec-window.h"
#include "rec-manager.h"

#include <libappindicator/app-indicator.h>

// Note: Uncomment this to show debug messages from this module
//#define DEBUG_SYSTRAY

#if defined(DEBUG_SYSTRAY) || defined(DEBUG_ALL)
#define LOG_SYSTRAY LOG_MSG
#else
#define LOG_SYSTRAY(x, ...)
#endif

// Macro to support GTK 3
#define gtk_menu_append(menu, menu_item) gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item)

// Systray icon
static GtkWidget *g_tray_icon = NULL;

// Menu on the icon
static GtkWidget *g_tray_menu = NULL;

static void systray_icon_create(gboolean show);
static void systray_icon_remove();
static void systray_set_icon(gchar *icon_name);
static void systray_set_menu_items(GtkWidget *menu, gint state);

void systray_module_init() {
    ;
}

void systray_module_exit() {
    systray_icon_remove();
}

void systray_icon_setup(gboolean show) {
    systray_icon_create(show);
}

gboolean systray_icon_is_installed() {
    return G_IS_OBJECT(g_tray_icon);
}

void systray_set_menu_items1(gint state) {
    systray_set_menu_items(g_tray_menu, state);
}

void systray_set_menu_items2(gboolean show) {
    if (!GTK_IS_MENU(g_tray_menu)) return;

    // Update "Show/Hide Window" menu item
    GtkWidget *menu_item = (GtkWidget*)g_object_get_data(G_OBJECT(g_tray_menu), "menu-item-show-window");
    if (!(GTK_IS_MENU(g_tray_menu) && GTK_IS_MENU_ITEM(menu_item))) return;

    LOG_SYSTRAY("systray_set_menu_items2, show:%d\n", show);

    if (!show) {
        // Tray menu.
        // Translators: This belongs to the tray menu (menu on the system tray).
        gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item), _("Show window"));
    } else {
        // Tray menu.
        // Translators: This belongs to the tray menu (menu on the system tray).
        gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item), _("Hide window"));
    }
}

static void systray_popup_menu_cb(GtkWidget * widget, gpointer data) {
    gchar *cmd = (gchar*)data;

    LOG_SYSTRAY("systray_popup_menu_cb: %s\n", cmd);

    if (!g_strcmp0(cmd, "start")) {
        rec_manager_flip_recording();
    } else if (!g_strcmp0(cmd, "continue")) {
        rec_manager_flip_recording();
    } else if (!g_strcmp0(cmd, "stop")) {
        rec_manager_stop_recording();
    } else if (!g_strcmp0(cmd, "pause")) {
        rec_manager_pause_recording();
    } else if (!g_strcmp0(cmd, "about")) {
        about_this_app();
    } else if (!g_strcmp0(cmd, "settings")) {
        // Not implemented
    } else if (!g_strcmp0(cmd, "quit")) {
        win_close_button_cb(NULL, GINT_TO_POINTER(TRUE)/*force quit*/);

    } else if (!g_strcmp0(cmd, "show-folder")) {

        // Open file browser and display content of Audio-folder.

        // Audio folder
        gchar *audio_folder = get_audio_folder();

        // Fire default file browser
        GError *error = NULL;
        run_tool_for_file(audio_folder, "nautilus", &error);

        if (error)  {
            // Translator: This error message is shown in a MessageBox.
            gchar *msg = g_strdup_printf(_("Cannot start file browser.\nPlease display %s manually."), audio_folder);
            messagebox_error(msg, NULL);
            g_free(msg);
            g_error_free(error);
        }
        g_free(audio_folder);


    } else if (!g_strcmp0(cmd, "show") || !g_strcmp0(cmd, "hide")) {

        // Main window is visible?
        gboolean visible = win_window_is_visible();

        // Main window is minimized/iconified?
        GdkWindowState state = win_get_window_state();

        win_show_window(!visible || state == GDK_WINDOW_STATE_ICONIFIED);
    }
}

static void systray_set_menu_items(GtkWidget *menu, gint state) {
    // Update menu items to reflect state of recording
    if (!GTK_IS_MENU(menu)) return;

    LOG_SYSTRAY("systray_set_menu_items, state:%d\n", state);

    // Get menu items
    GtkWidget *item_start = (GtkWidget*)g_object_get_data(G_OBJECT(menu), "menu-item-start-recording");
    GtkWidget *item_continue = (GtkWidget*)g_object_get_data(G_OBJECT(menu), "menu-item-continue-recording");
    GtkWidget *item_stop = (GtkWidget*)g_object_get_data(G_OBJECT(menu), "menu-item-stop-recording");
    GtkWidget *item_pause = (GtkWidget*)g_object_get_data(G_OBJECT(menu), "menu-item-pause-recording");

    gchar *tray_icon = NULL;
    if (state == GST_STATE_PLAYING) {
        // Recording is on
        gtk_widget_hide(item_start);
        gtk_widget_hide(item_continue);
        gtk_widget_show(item_stop);
        gtk_widget_show(item_pause);
        gtk_widget_set_sensitive(item_pause, TRUE);
        tray_icon = "audio-recorder-on.png";

    } else if (state == GST_STATE_PAUSED) {
        // Paused
        gtk_widget_hide(item_start);
        gtk_widget_show(item_continue);
        gtk_widget_show(item_stop);

        gtk_widget_show(item_pause);
        gtk_widget_set_sensitive(item_pause, FALSE);
        tray_icon = "audio-recorder-paused.png";

    } else {
        // Stopped
        gtk_widget_show(item_start);
        gtk_widget_hide(item_continue);
        gtk_widget_hide(item_stop);
        gtk_widget_show(item_pause);
        gtk_widget_set_sensitive(item_pause, FALSE);
        tray_icon = "audio-recorder-off.png";

    }

    if (menu == g_tray_menu) {
        // Set tray icon
        systray_set_icon(tray_icon);
    }
}

GtkWidget *systray_create_menu(gboolean for_tray_icon) {
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *menu_item = NULL;

    if (for_tray_icon) {
        // Show main window

        // Window is visible?
        gboolean visible = win_window_is_visible();

        // Window is minimized/nomal?
        GdkWindowState state = win_get_window_state();

        if (visible && state != GDK_WINDOW_STATE_ICONIFIED ) {
            // Tray menu.
            menu_item = gtk_menu_item_new_with_label(_("Hide window"));
            g_signal_connect((gpointer)menu_item, "activate", G_CALLBACK(systray_popup_menu_cb), "hide");
        } else {
            // Tray menu.
            menu_item = gtk_menu_item_new_with_label(_("Show window"));
            g_signal_connect((gpointer)menu_item, "activate", G_CALLBACK(systray_popup_menu_cb), "show");
        }
        gtk_widget_show(menu_item);

        gtk_menu_append(GTK_MENU(menu), menu_item);
        // Save this item
        g_object_set_data(G_OBJECT(menu), "menu-item-show-window", menu_item);

        // Separator line
        menu_item = gtk_separator_menu_item_new();
        gtk_widget_show(menu_item);
        gtk_menu_append(GTK_MENU(menu), menu_item);
    }

    // Start recording.
    // Translators: This belongs to the tray menu (menu on the system tray).
    menu_item = gtk_menu_item_new_with_label(_("Start recording"));
    gtk_widget_show(menu_item);
    g_signal_connect((gpointer)menu_item, "activate", G_CALLBACK(systray_popup_menu_cb), "start");
    gtk_menu_append(GTK_MENU(menu), menu_item);
    // Save this item
    g_object_set_data(G_OBJECT(menu), "menu-item-start-recording", menu_item);

    // Continue recording.
    // Translators: This belongs to the tray menu (menu on the system tray).
    menu_item = gtk_menu_item_new_with_label(_("Continue recording"));
    // Hide this
    gtk_widget_hide(menu_item);
    g_signal_connect((gpointer)menu_item, "activate", G_CALLBACK(systray_popup_menu_cb), "continue");
    gtk_menu_append(GTK_MENU(menu), menu_item);
    // Save this item
    g_object_set_data(G_OBJECT(menu), "menu-item-continue-recording", menu_item);

    // Stop recording.
    // Translators: This belongs to the tray menu (menu on the system tray).
    menu_item = gtk_menu_item_new_with_label(_("Stop recording"));
    // Hide this
    gtk_widget_hide(menu_item);
    g_signal_connect((gpointer)menu_item, "activate", G_CALLBACK(systray_popup_menu_cb), "stop");
    gtk_menu_append(GTK_MENU(menu), menu_item);
    // Save this item
    g_object_set_data(G_OBJECT(menu), "menu-item-stop-recording", menu_item);

    // Pause recording.
    // Translators: This belongs to the tray menu (menu on the system tray).
    menu_item = gtk_menu_item_new_with_label(_("Pause recording"));
    // Hide this
    gtk_widget_hide(menu_item);
    g_signal_connect((gpointer)menu_item, "activate", G_CALLBACK(systray_popup_menu_cb), "pause");
    gtk_menu_append(GTK_MENU(menu), menu_item);
    // Save this item
    g_object_set_data(G_OBJECT(menu), "menu-item-pause-recording", menu_item);

    // Separator line
    menu_item = gtk_separator_menu_item_new();
    gtk_widget_show(menu_item);
    gtk_menu_append(GTK_MENU(menu), menu_item);

    // Menu item "Show saved recordings".
    // Open Audio-folder and show all saved recordings.
    // Translators: This belongs to the tray menu (menu on the system tray)
    menu_item = gtk_menu_item_new_with_label(_("Show saved recordings"));
    g_signal_connect(G_OBJECT (menu_item), "activate", G_CALLBACK(systray_popup_menu_cb), "show-folder");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);

    gboolean show_about_item = TRUE;
    if (show_about_item) {
        // Separator line
        menu_item = gtk_separator_menu_item_new();
        gtk_widget_show(menu_item);
        gtk_menu_append(GTK_MENU(menu), menu_item);

        // Deprecated in GTK 3.10
        // menu_item = gtk_image_menu_item_new_from_stock(GTK_STOCK_ABOUT, NULL);
        menu_item = gtk_menu_item_new_with_mnemonic (_("_About"));

        g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK(systray_popup_menu_cb), "about");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
        gtk_widget_show(menu_item);
    }

    // Separator line
    menu_item = gtk_separator_menu_item_new();
    gtk_widget_show(menu_item);
    gtk_menu_append(GTK_MENU(menu), menu_item);

    // Deprecated in GTK 3.10
    // menu_item = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
    menu_item = gtk_menu_item_new_with_mnemonic (_("_Quit"));

    g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK(systray_popup_menu_cb), "quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show (menu_item);
    gtk_widget_show(menu);

    // Get recording state
    gint state = -1;
    gint pending = -1;
    rec_manager_get_state(&state, &pending);

    // Settle menu items
    systray_set_menu_items(menu, state);

    return menu;
}

static void systray_icon_create(gboolean show) {
    // Show tray icon?
    if (!show) {
        systray_icon_remove();
        return;
    }

    LOG_SYSTRAY("systray_icon_create.\n");

    if (IS_APP_INDICATOR(g_tray_icon)) {
        app_indicator_set_status(APP_INDICATOR(g_tray_icon), APP_INDICATOR_STATUS_ACTIVE);
        return;
    }

    g_tray_icon = (GtkWidget*)app_indicator_new("audio-recorder application",
                  "audio-recorder-off", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

    g_assert(IS_APP_INDICATOR(g_tray_icon));

    app_indicator_set_status(APP_INDICATOR(g_tray_icon), APP_INDICATOR_STATUS_ACTIVE);

    // Attach a popup-menu
    g_tray_menu = systray_create_menu(TRUE/*for_tray_icon*/);

    // Get current recording state
    gint state = -1;
    gint pending = -1;
    rec_manager_get_state(&state, &pending);

    // Set initial icon color and menu state
    systray_set_menu_items1(state);

    app_indicator_set_menu(APP_INDICATOR(g_tray_icon), GTK_MENU(g_tray_menu));
}

static void systray_icon_remove() {
    if (G_IS_OBJECT(g_tray_menu)) {
        gtk_widget_destroy(GTK_WIDGET(g_tray_menu));
    }
    g_tray_menu = NULL;

    if (IS_APP_INDICATOR(g_tray_icon)) {

        LOG_SYSTRAY("systray_icon_remove.\n");

        g_object_unref(G_OBJECT(g_tray_icon));
    }
    g_tray_icon = NULL;
}

static void systray_set_icon(gchar *icon_name) {
    if (!IS_APP_INDICATOR(g_tray_icon)) return;

    gchar *s = g_strdup(icon_name);

    // Remove file extension ".png"
    // Simply find the last '.' and put '\0' on it. This should work fine.
    gchar *pos = g_strrstr(s, ".");
    if (pos) {
        *pos = '\0';
    }

    app_indicator_set_icon(APP_INDICATOR(g_tray_icon), s);
    g_free(s);
}


