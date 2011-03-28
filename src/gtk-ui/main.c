/*
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "config.h"
#include "sync-ui.h"

static char *settings_id = NULL;

static GOptionEntry entries[] =
{
  { "show-settings", 0, 0, G_OPTION_ARG_STRING, &settings_id, "Open sync settings for given sync url or configuration name", "url or config name" },
  { NULL }
};


static void
set_app_name_and_icon ()
{
    /* TRANSLATORS: this is the application name that may be used by e.g.
       the windowmanager */
    g_set_application_name (_("Sync"));
    gtk_window_set_default_icon_name ("sync");
}

static void
init (int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;

    gtk_init (&argc, &argv);
    bindtextdomain (GETTEXT_PACKAGE, SYNCEVOLUTION_LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    context = g_option_context_new ("- synchronise PIM data with Syncevolution");
    g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
    g_option_context_add_group (context, gtk_get_option_group (TRUE));
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_warning ("option parsing failed: %s\n", error->message);
    }
}


#ifdef ENABLE_UNIQUE
#include <unique/unique.h>

enum
{
    COMMAND_0, 

    COMMAND_SHOW_CONFIGURATION
    /* no sync-ui specific commands */
};

static UniqueResponse
message_received_cb (UniqueApp         *app,
                     gint               command,
                     UniqueMessageData *message,
                     guint              time_,
                     app_data          *data)
{
    char *arg;
    GtkWindow *main_win;

    main_win = sync_ui_get_main_window (data);
    switch (command) {
    case UNIQUE_ACTIVATE:
        if (GTK_IS_WINDOW (main_win)) {
            /* move the main window to the screen that sent us the command */
            gtk_window_set_screen (GTK_WINDOW (main_win), 
                                   unique_message_data_get_screen (message));
            gtk_window_present (GTK_WINDOW (main_win));
        }
        break;
    case COMMAND_SHOW_CONFIGURATION:
        arg = unique_message_data_get_text (message);
        if (GTK_IS_WINDOW (main_win) && arg) {
            /* move the main window to the screen that sent us the command */
            gtk_window_set_screen (GTK_WINDOW (main_win), 
                                   unique_message_data_get_screen (message));
            sync_ui_show_settings (data, arg);
        }
        break;
    default:
        break;
    }

    return UNIQUE_RESPONSE_OK;
}

int
main (int argc, char *argv[])
{
    UniqueApp *app;

    init (argc, argv);

    app = unique_app_new_with_commands ("org.Moblin.Sync", NULL,
                                        "show-configuration", COMMAND_SHOW_CONFIGURATION,
                                        NULL);

    if (unique_app_is_running (app)) {
        UniqueMessageData *message = NULL;
        UniqueCommand command = UNIQUE_ACTIVATE;

        if (settings_id) {
            command = COMMAND_SHOW_CONFIGURATION;
            message = unique_message_data_new ();
            unique_message_data_set_text (message, settings_id, -1);
        }
        unique_app_send_message (app, command, message);
        unique_message_data_free (message);
    } else {
        app_data *data;

        set_app_name_and_icon ();

        data = sync_ui_create ();
        if (data) {
            /* UniqueApp watches the main window so it can terminate 
             * the startup notification sequence for us */
            unique_app_watch_window (app, sync_ui_get_main_window (data));

            /* handle notifications from new app launches */     
            g_signal_connect (app, "message-received", 
                              G_CALLBACK (message_received_cb), data);
            if (settings_id) {
                sync_ui_show_settings (data, settings_id);
            }
            gtk_main ();
        }
    }

    g_object_unref (app);
    return 0;
}

#else

int
main (int argc, char *argv[])
{
    app_data *data;

    init (argc, argv);

    set_app_name_and_icon ();
    data = sync_ui_create ();

    if (settings_id) {
        sync_ui_show_settings (data, settings_id);
    }

    gtk_main ();
    return 0;
}

#endif /* ENABLE_UNIQUE */

