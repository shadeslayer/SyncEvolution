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

static void
set_app_name_and_icon ()
{
    /* TRANSLATORS: this is the application name that may be used by e.g.
       the windowmanager */
    g_set_application_name (_("Sync"));
    gtk_window_set_default_icon_name ("sync");
}

#ifdef ENABLE_UNIQUE
#include <unique/unique.h>

enum
{
    COMMAND_0, 

    /* no sync-ui specific commands */
};

static UniqueResponse
message_received_cb (UniqueApp         *app,
                     UniqueCommand      command,
                     UniqueMessageData *message,
                     guint              time_,
                     GtkWindow         *main_win)
{
    UniqueResponse res;

    switch (command) {
    case UNIQUE_ACTIVATE:
        if (GTK_IS_WINDOW (main_win)) {
            /* move the main window to the screen that sent us the command */
            gtk_window_set_screen (GTK_WINDOW (main_win), 
                                   unique_message_data_get_screen (message));
            gtk_window_present (GTK_WINDOW (main_win));
        }
        res = UNIQUE_RESPONSE_OK;
        break;
    /* handle any sync-ui specific commands here */
    default:
        res = UNIQUE_RESPONSE_OK;
        break;
    }

    return res;
}

int
main (int argc, char *argv[])
{
    UniqueApp *app;

    gtk_init (&argc, &argv);
    bindtextdomain (GETTEXT_PACKAGE, SYNCEVOLUTION_LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    app = unique_app_new ("org.Moblin.Sync", NULL);

    if (unique_app_is_running (app)) {

        unique_app_send_message (app, UNIQUE_ACTIVATE, NULL);
        /* could handle the response here... */
    } else {
        GtkWidget *main_win;

        set_app_name_and_icon ();

        main_win = sync_ui_create_main_window ();
        if (main_win) {
            /* UniqueApp watches the main window so it can terminate 
             * the startup notification sequence for us */
            unique_app_watch_window (app, GTK_WINDOW (main_win));

            /* handle notifications from new app launches */     
            g_signal_connect (app, "message-received", 
                              G_CALLBACK (message_received_cb), main_win);

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
    gtk_init (&argc, &argv);
    bindtextdomain (GETTEXT_PACKAGE, SYNCEVOLUTION_LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    set_app_name_and_icon ();

    sync_ui_create_main_window ();
    gtk_main ();
    return 0;
}

#endif /* ENABLE_UNIQUE */

