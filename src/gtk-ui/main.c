#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <unique/unique.h>

#include "config.h"
#include "sync-ui.h"

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
