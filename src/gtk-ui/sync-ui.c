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


#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gnome-keyring.h>
#include <dbus/dbus-glib.h>

#include "syncevo-server.h"
#include "syncevo-session.h"

/* for return value definitions */
/* TODO: would be nice to have a non-synthesis-dependent API but for now it's like this... */
#include <synthesis/syerror.h>
#include <synthesis/engine_defs.h>

#include "config.h"
#include "sync-ui-config.h"
#include "sync-ui.h"
#include "sync-config-widget.h"

#ifdef USE_MOBLIN_UX
#include "mux-frame.h"
#include "mux-window.h"
#endif

static gboolean support_canceling = FALSE;

#define SYNC_UI_GCONF_DIR "/apps/sync-ui"
#define SYNC_UI_SERVER_KEY SYNC_UI_GCONF_DIR"/server"

#define SYNC_UI_ICON_SIZE 48

#define STRING_VARIANT_HASHTABLE (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))

typedef enum app_state {
    SYNC_UI_STATE_CURRENT_STATE,
    SYNC_UI_STATE_GETTING_SERVER,
    SYNC_UI_STATE_NO_SERVER,
    SYNC_UI_STATE_SERVER_OK,
    SYNC_UI_STATE_SERVER_FAILURE,
    SYNC_UI_STATE_SYNCING,
} app_state;


typedef struct app_data {
    GtkWidget *sync_win;
    GtkWidget *services_win; /* will be NULL when USE_MOBLIN_UX is set*/

    GtkWidget *server_box;
    GtkWidget *server_failure_box;
    GtkWidget *no_server_box;
    GtkWidget *error_box;
    GtkWidget *errors_box;
    GtkWidget *no_connection_box;
    GtkWidget *main_frame;
    GtkWidget *log_frame;
    GtkWidget *server_icon_box;

    GtkWidget *offline_label;
    GtkWidget *progress;
    GtkWidget *sync_status_label;
    GtkWidget *sync_btn;
    GtkWidget *edit_service_btn;
    GtkWidget *change_service_btn;

    GtkWidget *server_label;
    GtkWidget *last_synced_label;
    GtkWidget *sources_box;

    GtkWidget *new_service_btn;
    GtkWidget *services_box;
    GtkWidget *back_btn;

    gboolean online;

    gboolean syncing;
    gboolean synced_this_session;
    int last_sync;
    guint last_sync_src_id;

    SyncMode mode;

    server_config *current_service;
    app_state current_state;
    gboolean open_current;

    SyncevoServer *server;

    SyncevoSession *session; /* Session that we started */
    gboolean session_is_active; /* Can we issue commands to session ? */

    SyncevoSession *running_session; /* session that is currently active */
} app_data;

static void set_sync_progress (app_data *data, float progress, char *status);
static void set_app_state (app_data *data, app_state state);
static void show_main_view (app_data *data);
static void show_services_list (app_data *data);
static void update_services_list (app_data *data);
static void setup_new_service_clicked (GtkButton *btn, app_data *data);
static void get_reports_cb (SyncevoSession *session, SyncevoReports *reports, GError *error, app_data *data);
static char* get_error_string_for_code (int error_code);

static void
remove_child (GtkWidget *widget, GtkContainer *container)
{
    gtk_container_remove (container, widget);
}

static void 
change_service_clicked_cb (GtkButton *btn, app_data *data)
{
    show_services_list (data);
}

static void 
edit_service_clicked_cb (GtkButton *btn, app_data *data)
{
    g_assert (data);

    data->open_current = TRUE;
    show_services_list (data);
}

static void
show_error_dialog (GtkWindow *parent, const char* message)
{
    GtkWidget *w;
    w = gtk_message_dialog_new (parent,
                                GTK_DIALOG_MODAL,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_OK,
                                "%s",
                                message);
    gtk_dialog_run (GTK_DIALOG (w));
    gtk_widget_destroy (w);
}

static void
clear_error_info (app_data *data)
{
    gtk_container_foreach (GTK_CONTAINER(data->error_box),
                           (GtkCallback)remove_child,
                           data->error_box);

    gtk_widget_hide (data->errors_box);
}

const char*
get_pretty_source_name (const char *source_name)
{
    if (strcmp (source_name, "addressbook") == 0) {
        return g_strdup (_("Addressbook"));
    } else if (strcmp (source_name, "calendar") == 0) {
        return g_strdup (_("Calendar"));
    } else if (strcmp (source_name, "todo") == 0) {
        return g_strdup (_("Todo"));
    } else if (strcmp (source_name, "memo") == 0) {
        return g_strdup (_("Memo"));
    } else {
        char *tmp;
        tmp =  g_strdup (source_name);
        tmp[0] = g_ascii_toupper (tmp[0]);
        return tmp;
    }
}

static void
add_error_info (app_data *data, const char *message, const char *external_reason)
{
    GtkWidget *lbl;
    GList *l, *children;

    /* synthesis may emit same error several times, work around that: */
    children = gtk_container_get_children (GTK_CONTAINER (data->error_box));
    for (l = children; l; l = l->next) {
        GtkLabel *old_lbl = GTK_LABEL (l->data);

        if (strcmp (message, gtk_label_get_text (old_lbl)) == 0) {
            g_list_free (children);
            return;
        }
    }
    g_list_free (children);

    gtk_widget_show (data->errors_box);

    lbl = gtk_label_new (message);
    gtk_label_set_line_wrap (GTK_LABEL (lbl), TRUE);
    /* FIXME ugly hard coding*/
    gtk_widget_set_size_request (lbl, 160, -1);
    gtk_widget_show (lbl);
    gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
    gtk_box_pack_start (GTK_BOX (data->error_box), lbl, FALSE, FALSE, 0);

    if (external_reason) {
        g_warning ("%s: %s", message, external_reason);
    } else {
        g_warning ("%s", message);
    }
}

static void
save_gconf_settings (app_data *data, char *service_name)
{
    GConfClient* client;
    GError *err = NULL;

    client = gconf_client_get_default ();
    if (!gconf_client_set_string (client, SYNC_UI_SERVER_KEY, 
                                  service_name ? service_name : "", 
                                  &err)) {
        show_error_dialog (GTK_WINDOW (data->sync_win),
                           _("Failed to save current service in GConf configuration system"));
        g_warning ("Failed to save current service in GConf configuration system: %s", err->message);
        g_error_free (err);
    }
}

static void
abort_sync_cb (SyncevoSession *session,
               GError *error,
               app_data *data)
{
    if (error) {
        g_warning ("Session.Abort failed: %s", error->message);
        g_error_free (error);
    }
    /* status change handler takes care of updating UI */
}

static void
sync_cb (SyncevoSession *session,
         GError *error,
         app_data *data)
{
    if (error) {
        /* TODO: check for the case of sync already happening ? */
        add_error_info (data, _("Failed to start sync"), error->message);
        g_error_free (error);
        return;
    }

    set_sync_progress (data, 0.0, _("Starting sync"));
    /* stop updates of "last synced" */
    if (data->last_sync_src_id > 0)
        g_source_remove (data->last_sync_src_id);
    set_app_state (data, SYNC_UI_STATE_SYNCING);

}
static void 
sync_clicked_cb (GtkButton *btn, app_data *data)
{
    GHashTable *source_modes;
    GHashTableIter iter;
    source_config *source;
    GError *error = NULL;

    if (data->syncing) {
        syncevo_session_abort (data->running_session,
                               (SyncevoSessionGenericCb)abort_sync_cb,
                               data);
        set_sync_progress (data, -1.0, _("Trying to cancel sync"));
    } else {
        char *message = NULL;

        /* confirmation dialog for destructive sync options */
        switch (data->mode) {
        case SYNC_REFRESH_FROM_SERVER:
            message = g_strdup_printf (_("Do you want to delete all local data and replace it with "
                                         "data from %s? This is not usually advised."),
                                       data->current_service->name);
            break;
        case SYNC_REFRESH_FROM_CLIENT:
            message = g_strdup_printf (_("Do you want to delete all data in %s and replace it with "
                                         "your local data? This is not usually advised."),
                                       data->current_service->name);
            break;
        default:
            ;
        }
        if (message) {
            GtkWidget *w;
            int ret;
            w = gtk_message_dialog_new (GTK_WINDOW (data->sync_win),
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_QUESTION,
                                        GTK_BUTTONS_NONE,
                                        "%s",
                                        message);
            gtk_dialog_add_buttons (GTK_DIALOG (w), 
                                    _("No, cancel sync"), GTK_RESPONSE_NO,
                                    _("Yes, delete and replace"), GTK_RESPONSE_YES,
                                    NULL);
            ret = gtk_dialog_run (GTK_DIALOG (w));
            gtk_widget_destroy (w);
            g_free (message);
            if (ret != GTK_RESPONSE_YES) {
                return;
            }
        }

        /* TODO make sure source modes are correct:
         * override the sync mode in config with data->mode,
         * then override all non-supported sources with "none".  */
        source_modes = syncevo_source_modes_new ();

        g_hash_table_iter_init (&iter, data->current_service->source_configs);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer)&source)) {
            if (!source->supported_locally ||
                !source_config_is_enabled (source)) {
                syncevo_source_modes_add (source_modes,
                                          source->name,
                                          SYNC_NONE);
            }
        }

        syncevo_session_sync (data->session,
                              data->mode,
                              source_modes,
                              (SyncevoSessionGenericCb)sync_cb,
                              data);
        syncevo_source_modes_free (source_modes);
    }
}

static gboolean
refresh_last_synced_label (app_data *data)
{
    GTimeVal val;
    glong diff;
    char *msg;
    int delay;

    g_get_current_time (&val);
    diff = val.tv_sec - data->last_sync;

    if (data->last_sync <= 0) {
        msg = g_strdup (""); /* we don't know */
        delay = -1;
    } else if (diff < 30) {
        msg = g_strdup (_("Last synced just seconds ago"));
        delay = 30;
    } else if (diff < 90) {
        msg = g_strdup (_("Last synced a minute ago"));
        delay = 60;
    } else if (diff < 60 * 60) {
        msg = g_strdup_printf (_("Last synced %ld minutes ago"), (diff + 30) / 60);
        delay = 60;
    } else if (diff < 60 * 90) {
        msg = g_strdup (_("Last synced an hour ago"));
        delay = 60 * 60;
    } else if (diff < 60 * 60 * 24) {
        msg = g_strdup_printf (_("Last synced %ld hours ago"), (diff + 60 * 30) / (60 * 60));
        delay = 60 * 60;
    } else if (diff < 60 * 60 * 24 - (60 * 30)) {
        msg = g_strdup (_("Last synced a day ago"));
        delay = 60 * 60 * 24;
    } else {
        msg = g_strdup_printf (_("Last synced %ld days ago"), (diff + 24 * 60 * 30) / (60 * 60 * 24));
        delay = 60 * 60 * 24;
    }

    gtk_label_set_text (GTK_LABEL (data->last_synced_label), msg);
    g_free (msg);

    if (data->last_sync_src_id > 0)
        g_source_remove (data->last_sync_src_id);
    if (delay > 0)
        data->last_sync_src_id = g_timeout_add_seconds (delay, (GSourceFunc)refresh_last_synced_label, data);

    return FALSE;
}


static void
set_sync_progress (app_data *data, float progress, char *status)
{
    if (progress >= 0)
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (data->progress), progress);
    if (status)
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (data->progress), status);
}

static void
set_app_state (app_data *data, app_state state)
{

    if (data->current_state == state)
        return;

    if (state != SYNC_UI_STATE_CURRENT_STATE)
        data->current_state = state;

    switch (data->current_state) {
    case SYNC_UI_STATE_GETTING_SERVER:
        clear_error_info (data);
        gtk_widget_show (data->server_box);
        gtk_widget_hide (data->server_failure_box);
        gtk_widget_hide (data->no_server_box);
        gtk_label_set_text (GTK_LABEL (data->sync_status_label), "");

        gtk_widget_set_sensitive (data->main_frame, TRUE);
        gtk_widget_set_sensitive (data->sync_btn, FALSE);
        gtk_widget_set_sensitive (data->change_service_btn, TRUE);
        break;
    case SYNC_UI_STATE_NO_SERVER:
        clear_error_info (data);
        gtk_widget_hide (data->server_box);
        gtk_widget_hide (data->server_failure_box);
        gtk_widget_show (data->no_server_box);
        gtk_label_set_text (GTK_LABEL (data->sync_status_label), "");

        gtk_widget_set_sensitive (data->main_frame, TRUE);
        gtk_widget_set_sensitive (data->sync_btn, FALSE);
        gtk_widget_set_sensitive (data->change_service_btn, TRUE);
        gtk_window_set_focus (GTK_WINDOW (data->sync_win), data->change_service_btn);
        break;
    case SYNC_UI_STATE_SERVER_FAILURE:
        clear_error_info (data);
        gtk_widget_hide (data->server_box);
        gtk_widget_hide (data->no_server_box);
        gtk_widget_show (data->server_failure_box);
        gtk_label_set_text (GTK_LABEL (data->sync_status_label), "");

        gtk_widget_set_sensitive (data->main_frame, FALSE);
        gtk_widget_set_sensitive (data->sync_btn, FALSE);
        gtk_widget_set_sensitive (data->change_service_btn, FALSE);
        break;
    case SYNC_UI_STATE_SERVER_OK:
        /* we have a active, idle session */
        gtk_widget_show (data->server_box);
        gtk_widget_hide (data->server_failure_box);
        gtk_widget_hide (data->no_server_box);

        gtk_widget_set_sensitive (data->main_frame, TRUE);
        if (data->online) {
            gtk_widget_hide (data->no_connection_box);
        } else {
            gtk_widget_show (data->no_connection_box);
        }
        gtk_widget_set_sensitive (data->sync_btn, data->online);
        gtk_widget_set_sensitive (data->change_service_btn, TRUE);
        if (data->synced_this_session)
            gtk_button_set_label (GTK_BUTTON (data->sync_btn), _("Sync again"));
        else
            gtk_button_set_label (GTK_BUTTON (data->sync_btn), _("Sync now"));
        gtk_window_set_focus (GTK_WINDOW (data->sync_win), data->sync_btn);

        data->syncing = FALSE;
        break;
        
    case SYNC_UI_STATE_SYNCING:
        /* we have a active session, and a session is running
           (the running session may be ours) */
        clear_error_info (data);
        gtk_widget_show (data->progress);
        gtk_label_set_text (GTK_LABEL (data->sync_status_label), _("Syncing"));
        gtk_widget_set_sensitive (data->main_frame, FALSE);
        gtk_widget_set_sensitive (data->change_service_btn, FALSE);

        gtk_widget_set_sensitive (data->sync_btn, support_canceling);
        if (support_canceling) {
            gtk_button_set_label (GTK_BUTTON (data->sync_btn), _("Cancel sync"));
        }

        data->syncing = TRUE;
        break;
    default:
        g_assert_not_reached ();
    }
}

static void
sync_type_toggled_cb (GObject *radio, app_data *data)
{
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio))) {
        data->mode = GPOINTER_TO_INT (g_object_get_data (radio, "mode"));
    }
}

#ifdef USE_MOBLIN_UX
static void
settings_visibility_changed_cb (GtkWidget *window, app_data *data)
{
    if (mux_window_get_settings_visible (MUX_WINDOW (window))) {
        update_services_list (data);
    }
}

/* truly stupid, but glade doesn't allow custom containers.
   Now glade file has dummy containers that will be replaced here.
   The dummy should be a gtkbin and it's parent should be a box with just one child */ 

static GtkWidget*
switch_dummy_to_mux_frame (GtkWidget *dummy)
{
    GtkWidget *frame, *parent;
    const char *title;

    g_assert (GTK_IS_BIN (dummy));

    frame = mux_frame_new ();
    gtk_widget_set_name (frame, gtk_widget_get_name (dummy));
    title = gtk_frame_get_label (GTK_FRAME(dummy));
    if (title && strlen (title) > 0)
        gtk_frame_set_label (GTK_FRAME (frame), title);

    parent = gtk_widget_get_parent (dummy);
    g_assert (GTK_IS_BOX (parent));

    gtk_widget_reparent (gtk_bin_get_child (GTK_BIN (dummy)), frame);
    gtk_container_remove (GTK_CONTAINER (parent), dummy);

    /* make sure there are no other children in box */
    g_assert (gtk_container_get_children (GTK_CONTAINER (parent)) == NULL);

    gtk_box_pack_start (GTK_BOX (parent), frame, TRUE, TRUE, 0);
    gtk_widget_show (frame);
    return frame;
}

/* truly stupid, but glade doesn't allow custom containers.
   Now glade file has dummy containers that will be replaced here.
   The dummy should be a gtkwindow */ 
static GtkWidget*
switch_dummy_to_mux_window (GtkWidget *dummy)
{
    GtkWidget *window;
    const char *title;

    g_assert (GTK_IS_WINDOW (dummy));

    window = mux_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (window), 1024, 600);
    gtk_widget_set_name (window, gtk_widget_get_name (dummy));
    title = gtk_window_get_title (GTK_WINDOW (dummy));
    if (title && strlen (title) > 0)
        gtk_window_set_title (GTK_WINDOW (window), title);
    gtk_window_set_modal (GTK_WINDOW (window),
                          gtk_window_get_modal (GTK_WINDOW (dummy)));

    mux_window_set_decorations (MUX_WINDOW (window), MUX_DECOR_CLOSE);
    gtk_widget_reparent (gtk_bin_get_child (GTK_BIN (dummy)), window);

    return window;
}

static void
switch_main_and_settings_to_mux_window (app_data *data,
                                        GtkWidget *main, GtkWidget *settings)
{
    GtkWidget *mux_main;
    GtkWidget *tmp;

    mux_main = switch_dummy_to_mux_window (main);
    mux_window_set_decorations (MUX_WINDOW (mux_main), MUX_DECOR_SETTINGS|MUX_DECOR_CLOSE);
    g_signal_connect (mux_main, "settings-visibility-changed",
                      G_CALLBACK (settings_visibility_changed_cb), data);

    tmp = g_object_ref (gtk_bin_get_child (GTK_BIN (settings)));
    gtk_container_remove (GTK_CONTAINER (settings), tmp);
    mux_window_set_settings_widget (MUX_WINDOW (mux_main), tmp);
    mux_window_set_settings_title (MUX_WINDOW (mux_main), 
                                   gtk_window_get_title (GTK_WINDOW (settings)));
    g_object_unref (tmp);

    data->sync_win = mux_main;
    data->services_win = NULL;
}


#else

/* return the placeholders themselves when not using Moblin UX */
static GtkWidget*
switch_dummy_to_mux_frame (GtkWidget *dummy) {
    return dummy;
}
static GtkWidget*
switch_dummy_to_mux_window (GtkWidget *dummy)
{
    return dummy;
}
static void
switch_main_and_settings_to_mux_window (app_data *data,
                                        GtkWidget *main, GtkWidget *settings)
{
    data->sync_win = main;
    data->services_win = settings;
    gtk_window_set_transient_for (GTK_WINDOW (data->services_win),
                                  GTK_WINDOW (data->sync_win));
    g_signal_connect (data->services_win, "delete-event",
                      G_CALLBACK (gtk_widget_hide_on_delete), NULL);
}
#endif

static gboolean
init_ui (app_data *data)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GObject *radio;
    GtkWidget *frame, *setup_service_btn , *image, *scrolled_window;

    gtk_rc_parse (THEMEDIR "sync-ui.rc");

    builder = gtk_builder_new ();
    gtk_builder_add_from_file (builder, GLADEDIR "ui.xml", &error);
    if (error) {
        g_printerr ("Failed to load user interface from %s: %s\n",
                    GLADEDIR "ui.xml",
                    error->message);
        g_error_free (error);
        g_object_unref (builder);
        return FALSE;
    }

    data->server_box = GTK_WIDGET (gtk_builder_get_object (builder, "server_box"));
    data->no_server_box = GTK_WIDGET (gtk_builder_get_object (builder, "no_server_box"));
    data->server_failure_box = GTK_WIDGET (gtk_builder_get_object (builder, "server_failure_box"));
    data->errors_box = GTK_WIDGET (gtk_builder_get_object (builder, "errors_box"));
    data->no_connection_box = GTK_WIDGET (gtk_builder_get_object (builder, "no_connection_box"));
    data->error_box = GTK_WIDGET (gtk_builder_get_object (builder, "error_box"));
    data->server_icon_box = GTK_WIDGET (gtk_builder_get_object (builder, "server_icon_box"));

    image = GTK_WIDGET (gtk_builder_get_object (builder, "sync_failure_image"));
    gtk_image_set_from_file (GTK_IMAGE (image), THEMEDIR "sync-generic.png");
    image = GTK_WIDGET (gtk_builder_get_object (builder, "no_server_image"));
    gtk_image_set_from_file (GTK_IMAGE (image), THEMEDIR "sync-generic.png");
    setup_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "setup_sync_service_btn"));

    data->offline_label = GTK_WIDGET (gtk_builder_get_object (builder, "offline_label"));
    data->progress = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar"));
    data->edit_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "edit_service_btn"));
    data->change_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "change_service_btn"));
    data->sync_btn = GTK_WIDGET (gtk_builder_get_object (builder, "sync_btn"));
    data->sync_status_label = GTK_WIDGET (gtk_builder_get_object (builder, "sync_status_label"));

    data->server_label = GTK_WIDGET (gtk_builder_get_object (builder, "sync_service_label"));
    data->last_synced_label = GTK_WIDGET (gtk_builder_get_object (builder, "last_synced_label"));
    data->sources_box = GTK_WIDGET (gtk_builder_get_object (builder, "sources_box"));

    data->new_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "new_service_btn"));
    gtk_widget_set_size_request (data->new_service_btn, 
                                 SYNC_UI_LIST_BTN_WIDTH, SYNC_UI_LIST_ICON_SIZE);
    g_signal_connect (data->new_service_btn, "clicked",
                      G_CALLBACK (setup_new_service_clicked), data);

    scrolled_window = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow"));
    data->services_box = GTK_WIDGET (gtk_builder_get_object (builder, "services_box"));
    gtk_container_set_focus_vadjustment
            (GTK_CONTAINER (data->services_box),
             gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scrolled_window)));
    data->back_btn = GTK_WIDGET (gtk_builder_get_object (builder, "back_btn"));

    radio = gtk_builder_get_object (builder, "two_way_radio");
    g_object_set_data (radio, "mode", GINT_TO_POINTER (SYNC_TWO_WAY));
    g_signal_connect (radio, "toggled",
                      G_CALLBACK (sync_type_toggled_cb), data);
    radio = gtk_builder_get_object (builder, "one_way_from_remote_radio");
    g_object_set_data (radio, "mode", GINT_TO_POINTER (SYNC_REFRESH_FROM_SERVER));
    g_signal_connect (radio, "toggled",
                      G_CALLBACK (sync_type_toggled_cb), data);
    radio = gtk_builder_get_object (builder, "one_way_from_local_radio");
    g_object_set_data (radio, "mode", GINT_TO_POINTER (SYNC_REFRESH_FROM_CLIENT));
    g_signal_connect (radio, "toggled",
                      G_CALLBACK (sync_type_toggled_cb), data);

    /* No (documented) way to add own widgets to gtkbuilder it seems...
       swap the all dummy widgets with Muxwidgets */
    switch_main_and_settings_to_mux_window (data,
                                            GTK_WIDGET (gtk_builder_get_object (builder, "sync_win")),
                                            GTK_WIDGET (gtk_builder_get_object (builder, "services_win")));

    data->main_frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "main_frame")));
    data->log_frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "log_frame")));
    frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "services_list_frame")));

    g_signal_connect (data->sync_win, "destroy",
                      G_CALLBACK (gtk_main_quit), NULL);
    g_signal_connect_swapped (data->back_btn, "clicked",
                      G_CALLBACK (show_main_view), data);
    g_signal_connect (data->change_service_btn, "clicked",
                      G_CALLBACK (change_service_clicked_cb), data);
    g_signal_connect (setup_service_btn, "clicked",
                      G_CALLBACK (change_service_clicked_cb), data);
    g_signal_connect (data->edit_service_btn, "clicked",
                      G_CALLBACK (edit_service_clicked_cb), data);
    g_signal_connect (data->sync_btn, "clicked", 
                      G_CALLBACK (sync_clicked_cb), data);

    g_object_unref (builder);
    return TRUE;
}

static void
source_check_toggled_cb (GtkCheckButton *check, app_data *data)
{
    GPtrArray *options;
    gboolean *enabled;
    
/* TODO: save to syncevolution config
    NOTE the check has source name as data (g_object_set_data)
*/
}

static void
load_icon (const char *uri, GtkBox *icon_box, guint icon_size)
{
    GError *error = NULL;
    GdkPixbuf *pixbuf;
    GtkWidget *image;
    const char *filename;
    
    if (uri && strlen (uri) > 0) {
        if (g_str_has_prefix (uri, "file://")) {
            filename = uri+7;
        } else {
            g_warning ("only file:// icon uri is supported: %s", uri);
            filename = THEMEDIR "sync-generic.png";
        }
    } else {
        filename = THEMEDIR "sync-generic.png";
    }
    pixbuf = gdk_pixbuf_new_from_file_at_scale (filename,
                                                icon_size, icon_size,
                                                TRUE, &error);

    if (!pixbuf) {
        g_warning ("Failed to load service icon: %s", error->message);
        g_error_free (error);
        return;
    }

    image = gtk_image_new_from_pixbuf (pixbuf);
    gtk_widget_set_size_request (image, icon_size, icon_size);
    g_object_unref (pixbuf);
    gtk_box_pack_start (icon_box, image, FALSE, FALSE, 0);
    gtk_widget_show (image);
}

static void
update_service_source_ui (const char *name, source_config *conf, app_data *data)
{
    GtkWidget *check, *hbox, *box, *lbl;
    const char *pretty_name;
    const char *source_uri, *sync;
    gboolean enabled;

    pretty_name = get_pretty_source_name (name);
    source_uri = g_hash_table_lookup (conf->config, "uri");
    sync = g_hash_table_lookup (conf->config, "sync");
    if (!sync || 
        strcmp (sync, "disabled") == 0 ||
        strcmp (sync, "none") == 0) {
        // consider this source not available at all
        enabled = FALSE;
    } else {
        enabled = TRUE;
    }

    /* argh, GtkCheckButton won't layout nicely with several labels... 
       There is no way to align the check with the top row and 
       get the labels to align and not use way too much vertical space.
       In this hack the labels are not related to the checkbutton at all,
       this is definitely not nice but looks better */

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_box_pack_start_defaults (GTK_BOX (data->sources_box), hbox);
 
    box = gtk_vbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), box, FALSE, FALSE, 0);
    check = gtk_check_button_new ();
    gtk_box_pack_start (GTK_BOX (box), check, FALSE, FALSE, 0);

    box = gtk_vbox_new (FALSE, 0);
    gtk_box_pack_start_defaults (GTK_BOX (hbox), box);
    gtk_container_set_border_width (GTK_CONTAINER (box), 2);

    if (source_uri && strlen (source_uri) > 0) {
        lbl = gtk_label_new (pretty_name);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), enabled);
        gtk_widget_set_sensitive (check, TRUE);
    } else {
        char *text;
        /* TRANSLATORS: placeholder is a source name, shown with checkboxes in main window */
        text = g_strdup_printf (_("%s (not supported by this service)"), pretty_name);
        lbl = gtk_label_new (text);
        g_free (text);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), FALSE);
        gtk_widget_set_sensitive (check, FALSE);
    }
    g_free (pretty_name);
    gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
    gtk_box_pack_start_defaults (GTK_BOX (box), lbl);

    conf->label = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC (conf->label), 0.0, 0.5);
    gtk_box_pack_start_defaults (GTK_BOX (box), conf->label);

    source_config_update_label (conf);

    g_object_set_data (G_OBJECT (check), "source", (gpointer)name);
    g_signal_connect (check, "toggled",
                      G_CALLBACK (source_check_toggled_cb), data);


    gtk_widget_show_all (hbox); 
}

static void
check_source_cb (SyncevoSession *session,
                 GError *error,
                 source_config *source)
{
    if (error) {
        /* TODO make sure this is the right error */

        /* source is not supported locally */
        source->supported_locally = FALSE;
        g_error_free (error);
        return;
    }

    source->supported_locally = TRUE;
}

static void
update_service_ui (app_data *data)
{
    const char *icon_uri;
    GList *l;

    g_assert (data->current_service && data->current_service->config);

    gtk_container_foreach (GTK_CONTAINER (data->sources_box),
                           (GtkCallback)remove_child,
                           data->sources_box);

    syncevo_config_get_value (data->current_service->config,
                              NULL, "IconURI", &icon_uri);

    if (data->current_service->name){
        gtk_label_set_markup (GTK_LABEL (data->server_label), 
                              data->current_service->name);
    }
    if (icon_uri) {
        load_icon (icon_uri,
                   GTK_BOX (data->server_icon_box),
                   SYNC_UI_ICON_SIZE);
    }

    g_hash_table_foreach (data->current_service->source_configs,
                          (GHFunc)update_service_source_ui,
                          data);

/* TODO: mnake sure all default sources are visible
 * (iow add missing sources as insensitive) */

    gtk_widget_show_all (data->sources_box);
}

static void
find_password_cb (GnomeKeyringResult result, GList *list, app_data *data)
{
    switch (result) {
    case GNOME_KEYRING_RESULT_NO_MATCH:
        break;
    case GNOME_KEYRING_RESULT_OK:
        if (list && list->data) {
            GnomeKeyringNetworkPasswordData *key_data;
            key_data = (GnomeKeyringNetworkPasswordData*)list->data;
/*
            data->current_service->password = g_strdup (key_data->password);
*/
        }
        break;
    default:
        g_warning ("getting password from GNOME keyring failed: %s",
                   gnome_keyring_result_to_message (result));
        break;
    }
    return;
}

static void
unexpand_config_widget (GtkWidget *w, GtkWidget *exception)
{
    if (SYNC_IS_CONFIG_WIDGET (w) && exception && exception != w) {
        sync_config_widget_set_expanded (SYNC_CONFIG_WIDGET (w), FALSE);
    }
}

static void
config_widget_removed_cb (GtkWidget *widget, app_data *data)
{
    if (sync_config_widget_get_current (SYNC_CONFIG_WIDGET (widget))) {
        save_gconf_settings (data, NULL);
    }
    gtk_container_remove (GTK_CONTAINER (data->services_box), widget);
}

static void
config_widget_expanded_cb (GtkWidget *widget, app_data *data)
{
    gtk_container_foreach (GTK_CONTAINER (data->services_box),
                           (GtkCallback)unexpand_config_widget,
                           widget);
}

static GtkWidget*
add_server_to_box (GtkBox *box, SyncevoServer *server, app_data *data)
{
    GtkWidget *item = NULL;
    const char *name;
    gboolean current = FALSE;

/*
    syncevo_server_get (server, &name, NULL, NULL, NULL);
    if (data->current_service && data->current_service->name &&
        name && strcmp (name, data->current_service->name) == 0) {
        current = TRUE;
     }

    item = sync_config_widget_new (server, current, data->service);
    g_signal_connect (item, "removed",
                      G_CALLBACK (config_widget_removed_cb), data);
    g_signal_connect (item, "expanded",
                      G_CALLBACK (config_widget_expanded_cb), data);
    gtk_widget_show (item);
    gtk_box_pack_start (box, item, FALSE, FALSE, 0);

    if (current) {
        sync_config_widget_set_expanded (SYNC_CONFIG_WIDGET (item), data->op
        data->open_current = FALSE;
    }
*/
    return item;
}

static void
setup_new_service_clicked (GtkButton *btn, app_data *data)
{
/*
    GtkWidget *config_widget;
    SyncevoServer *server;

    gtk_container_foreach (GTK_CONTAINER (data->services_box),
                           (GtkCallback)unexpand_config_widget,
                           NULL);

    server = syncevo_server_new (NULL, NULL, NULL, TRUE);
    config_widget = add_server_to_box (GTK_BOX (data->services_box), server, data);

    sync_config_widget_set_expanded (SYNC_CONFIG_WIDGET (config_widget), TRUE);
*/
}

typedef struct templates_data {
    app_data *data;
    GPtrArray *templates;
}templates_data;

/*
static gboolean
server_array_contains (GPtrArray *array, SyncevoServer *server)
{
    int i;
    const char *name;

    syncevo_server_get (server, &name, NULL, NULL, NULL);

    for (i = 0; i < array->len; i++) {
        const char *n;
        SyncevoServer *s = (SyncevoServer*)g_ptr_array_index (array, i);
        syncevo_server_get (s, &n, NULL, NULL, NULL);
        if (g_ascii_strcasecmp (name, n) == 0)
            return TRUE;
    }
    return FALSE;
}
*/

static void
update_services_list (app_data *data)
{
/*
    syncevo_service_get_templates_async (data->service,
                                         (SyncevoGetTemplatesCb)get_templates_cb,
                                         data);
*/
}

static void
get_config_for_main_win_cb (SyncevoSession *session,
                            SyncevoConfig *config,
                            GError *error,
                            app_data *data)
{
    if (error) {
        g_warning ("Error in Session.GetConfig: %s", error->message);
        g_error_free (error);

        /* TODO: check for known errors */
        set_app_state (data, SYNC_UI_STATE_SERVER_FAILURE);
        return;
    }
    
    if (config) {
        GHashTableIter iter;
        char *name;
        source_config *source;

        server_config_init (data->current_service, config);

        /* get "locally supported" status for all sources */
        g_hash_table_iter_init (&iter, data->current_service->source_configs);
        while (g_hash_table_iter_next (&iter, (gpointer)&name, (gpointer)&source)) {
            syncevo_session_check_source (data->session,
                                          name,
                                          (SyncevoSessionGenericCb)check_source_cb,
                                          source);
        }

        syncevo_session_get_reports (data->session,
                                     0, 1,
                                     (SyncevoSessionGetReportsCb)get_reports_cb,
                                     data);

        update_service_ui (data);

    }
}

static void
set_running_session_status (app_data *data, SyncevoSessionStatus status)
{
    switch (status) {
    case SYNCEVO_STATUS_QUEUEING:
        g_warning ("Running session is queued, this shouldn't happen...");
        break;
    case SYNCEVO_STATUS_IDLE:
        set_app_state (data, SYNC_UI_STATE_SERVER_OK);
        break;
    case SYNCEVO_STATUS_RUNNING:
    case SYNCEVO_STATUS_SUSPENDING:
    case SYNCEVO_STATUS_ABORTING:
        set_app_state (data, SYNC_UI_STATE_SYNCING);
        break;
    case SYNCEVO_STATUS_DONE:
        gtk_label_set_text (GTK_LABEL (data->sync_status_label), 
                            _("Sync complete"));
        set_app_state (data, SYNC_UI_STATE_SERVER_OK);
        set_sync_progress (data, 1.0, "");
        
        break;
    default:
        g_warning ("unknown session status  %d used!", status);
    }
}

static void
update_source_status (char *name,
                      SyncevoSyncMode mode,
                      SyncevoSourceStatus status,
                      guint error_code,
                      app_data *data)
{
    char *error;
    
    error = get_error_string_for_code (error_code);
    if (error) {
        g_free (error);
        g_warning (" Source '%s' error: %s", name, error);
    }
}

static void
running_session_status_changed_cb (SyncevoSession *session,
                                   SyncevoSessionStatus status,
                                   guint error_code,
                                   SyncevoSourceStatuses *source_statuses,
                                   app_data *data)
{
    g_debug ("running session status changed -> %d", status);
    set_running_session_status (data, status);

    syncevo_source_statuses_foreach (source_statuses,
                                     (SourceStatusFunc)update_source_status,
                                     data);
}

static void
get_running_session_status_cb (SyncevoSession *session,
                               SyncevoSessionStatus status,
                               guint error_code,
                               SyncevoSourceStatuses *source_statuses,
                               GError *error,
                               app_data *data)
{
    if (error) {
        g_warning ("Error in Session.GetStatus: %s", error->message);
        g_error_free (error);
        return;
    }

    set_running_session_status (data, status);
}

static void
running_session_progress_changed_cb (SyncevoSession *session,
                                     int progress,
                                     SyncevoSourceProgresses *source_progresses,
                                     app_data *data)
{
    SyncevoSourceProgress *s_progress;
    char *name;
    char *msg = NULL;

    s_progress = syncevo_source_progresses_get_current (source_progresses);
    if (!s_progress) {
        return;
    }

    name = get_pretty_source_name (s_progress->name);

    switch (s_progress->phase) {
    case SYNCEVO_PHASE_PREPARING:
        msg = g_strdup_printf (_("Preparing '%s'"), name);
        break;
    case SYNCEVO_PHASE_RECEIVING:
        msg = g_strdup_printf (_("Receiving '%s'"), name);
        break;
    case SYNCEVO_PHASE_SENDING:
        msg = g_strdup_printf (_("Sending '%s'"), name);
        break;
    default:
        ;
    }
    g_free (name);

    if (msg) {
        set_sync_progress (data, ((float)progress) / 100, msg);
        g_free (msg);
    }

    syncevo_source_progress_free (s_progress);
}

typedef struct source_stats {
    long local_changes;
    long remote_changes;
    long local_rejections;
    long remote_rejections;
} source_stats;

static void
free_source_stats (source_stats *stats)
{
    g_slice_free (source_stats, stats);
}

static void
get_reports_cb (SyncevoSession *session,
                SyncevoReports *reports,
                GError *error,
                app_data *data)
{
    if (error) {
        g_warning ("Error in Session.GetReports: %s", error->message);
        g_error_free (error);
        return;
    }

    if (syncevo_reports_get_length (reports) > 0) {
        GHashTableIter iter;
        GHashTable *sources; /* key is source name, value is a source_stats */
        const char *key, *val;
        GHashTable *report = syncevo_reports_index (reports, 0);
        source_stats *stats;

        sources = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, (GDestroyNotify)free_source_stats);

        g_hash_table_iter_init (&iter, report);
        while (g_hash_table_iter_next (&iter, (gpointer)&key, (gpointer)&val)) {
            char **strs;

            strs = g_strsplit (key, "-", 6);
            if (g_strv_length (strs) != 6) {
                g_warning ("'%s' not parsable as a sync report item", key);
                g_strfreev (strs);
                continue;
            }

            stats = g_hash_table_lookup (sources, strs[1]);
            if (!stats) {
                stats = g_slice_new0 (source_stats);
                g_hash_table_insert (sources, g_strdup (strs[1]), stats);
            }

            if (strcmp (strs[3], "remote") == 0) {
                if (strcmp (strs[4], "added") == 0 ||
                    strcmp (strs[4], "updated") == 0 ||
                    strcmp (strs[4], "removed") == 0) {
                    stats->remote_changes += strtol (val, NULL, 10);
                } else if (strcmp (strs[5], "reject") == 0) {
                    stats->remote_rejections += strtol (val, NULL, 10);
                }

            }
            if (strcmp (strs[3], "local") == 0) {
                if (strcmp (strs[4], "added") == 0 ||
                    strcmp (strs[4], "updated") == 0 ||
                    strcmp (strs[4], "removed") == 0) {
                    stats->local_changes += strtol (val, NULL, 10);
                } else if (strcmp (strs[5], "reject") == 0) {
                    stats->local_rejections += strtol (val, NULL, 10);
                }
            }
            g_strfreev (strs);
            
        }

        /* sources now has all statistics we want */
        g_hash_table_iter_init (&iter, sources);
        while (g_hash_table_iter_next (&iter, (gpointer)&key, (gpointer)&stats)) {
            source_config *source_conf;

            /* store the statistics in source config source */
            source_conf = g_hash_table_lookup (data->current_service->source_configs,
                                               key);
            if (source_conf) {
                source_conf->local_changes = stats->local_changes;
                source_conf->remote_changes = stats->remote_changes;
                source_conf->local_rejections = stats->local_rejections;
                source_conf->remote_rejections = stats->remote_rejections;
            }

            /* if ui has been constructed already, we want to update the data */
            source_config_update_label (source_conf);
        }

        g_hash_table_destroy (sources);
    }
}

static void
set_active_session (app_data *data, SyncevoSession *session)
{
    data->session_is_active = TRUE;
    syncevo_session_get_config (data->session,
                                FALSE,
                                (SyncevoSessionGetConfigCb)get_config_for_main_win_cb,
                                data);
}

/* Our session status */
static void
status_changed_cb (SyncevoSession *session,
                   SyncevoSessionStatus status,
                   guint error_code,
                   SyncevoSourceStatuses *source_statuses,
                   app_data *data)
{
    GTimeVal val;

    g_debug ("active session status changed -> %d", status);
    switch (status) {
    case SYNCEVO_STATUS_IDLE:
        /* time for business */
        set_active_session (data, session);
        break;
    
    case SYNCEVO_STATUS_DONE:
        g_get_current_time (&val);
        data->last_sync = val.tv_sec;
        refresh_last_synced_label (data);
        
        data->synced_this_session = TRUE;

        /* refresh stats */
        syncevo_session_get_reports (data->session,
                                     0, 1,
                                     (SyncevoSessionGetReportsCb)get_reports_cb,
                                     data);
    default:
        ;
    }
}

/* Our session status */
static void
get_status_cb (SyncevoSession *session,
               SyncevoSessionStatus status,
               guint error_code,
               SyncevoSourceStatuses *source_statuses,
               GError *error,
               app_data *data)
{
    if (error) {
        g_warning ("Error in Session.GetStatus: %s", error->message);
        g_error_free (error);
        return;
    }

    g_debug ("active session status is %d", status);
    if (status == SYNCEVO_STATUS_IDLE) {
        /* time for business */
        set_active_session (data, session);
    }
}

static void
start_session_cb (SyncevoServer *server,
                  char *path,
                  GError *error,
                  app_data *data)
{

    if (error) {
        g_warning ("Error in Server.StartSession: %s", error->message);
        g_error_free (error);
        data->session = NULL;
        return;
    }

    if (data->running_session &&
        strcmp (path, syncevo_session_get_path (data->running_session)) == 0) {
        /* our session is already the active one */
        data->session = g_object_ref (data->running_session);
    } else {
        data->session = syncevo_session_new (path);
        
        gtk_label_set_markup (GTK_LABEL (data->server_label), 
                              _("Waiting for current sync operation to finish"));
        gtk_widget_show_all (data->sources_box);
    }

    /* we want to know about status changes to our session */
    g_signal_connect (data->session, "status-changed",
                      G_CALLBACK (status_changed_cb), data);
    syncevo_session_get_status (data->session,
                                (SyncevoSessionGetStatusCb)get_status_cb,
                                data);
}

static void
show_services_list (app_data *data)
{
#ifdef USE_MOBLIN_UX
    mux_window_set_settings_visible (MUX_WINDOW (data->sync_win), TRUE);
#else
    gtk_window_present (GTK_WINDOW (data->services_win));
    update_services_list (data);
#endif
}

static void
show_main_view (app_data *data)
{
#ifdef USE_MOBLIN_UX
    mux_window_set_settings_visible (MUX_WINDOW (data->sync_win), FALSE);
#else
    gtk_widget_hide (data->services_win);
#endif
    gtk_window_present (GTK_WINDOW (data->sync_win));
}

static void
gconf_change_cb (GConfClient *client, guint id, GConfEntry *entry, app_data *data)
{
    char *server = NULL;
    GError *error = NULL;

    server = gconf_client_get_string (client, SYNC_UI_SERVER_KEY, &error);
    if (error) {
        g_warning ("Could not read current server name from gconf: %s", error->message);
        g_error_free (error);
        error = NULL;
    }

    gtk_widget_hide (data->progress);

    server_config_free (data->current_service);
    data->current_service = NULL;
    
    if (!server || strlen (server) == 0) {
        set_app_state (data, SYNC_UI_STATE_NO_SERVER);
    } else {
        set_app_state (data, SYNC_UI_STATE_GETTING_SERVER);
        data->synced_this_session = FALSE;
        data->current_service = g_slice_new0 (server_config);
        data->current_service->name = server;

        if (data->session) {
            g_object_unref (data->session);
            data->session = NULL;
        }
        data->session_is_active = FALSE;
        
        syncevo_server_start_session (data->server,
                                      server,
                                      (SyncevoServerStartSessionCb)start_session_cb,
                                      data);
    }
}

static void
init_configuration (app_data *data)
{
    GConfClient* client;

    client = gconf_client_get_default ();
    gconf_client_add_dir (client, SYNC_UI_GCONF_DIR, GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);
    gconf_client_notify_add (client, SYNC_UI_GCONF_DIR, (GConfClientNotifyFunc)gconf_change_cb,
                             data, NULL, NULL);

    /* fake gconf change to init values */
    gconf_change_cb (client, 0, NULL, data);
}

static void
calc_and_update_progress (app_data *data, char *msg)
{
    
/*
    float progress;
    GList *list;
    int count = 0;
    
    //update progress from source conf
    
    set_sync_progress (data, sync_progress_sync_start + (progress / count), msg);
*/
}

static char*
get_error_string_for_code (int error_code)
{
    switch (error_code) {
    case -1:
        /* TODO: this is a hack... SyncEnd should be a signal of it's own,
           not just hacked on top of the syncevolution error codes */
        return g_strdup(_("Service configuration not found"));
    case 0:
    case LOCERR_USERABORT:
    case LOCERR_USERSUSPEND:
        return NULL;
    case DB_Unauthorized:
        return g_strdup(_("Not authorized"));
    case DB_Forbidden:
        return g_strdup(_("Forbidden"));
    case DB_NotFound:
        return g_strdup(_("Not found"));
    case DB_Fatal:
        return g_strdup(_("Fatal database error"));
    case DB_Error:
        return g_strdup(_("Database error"));
    case DB_Full:
        return g_strdup(_("No space left"));
    case LOCERR_PROCESSMSG:
        /* TODO identify problem item somehow ? */
        return g_strdup(_("Failed to process SyncML"));
    case LOCERR_AUTHFAIL:
        return g_strdup(_("Server authorization failed"));
    case LOCERR_CFGPARSE:
        return g_strdup(_("Failed to parse configuration file"));
    case LOCERR_CFGREAD:
        return g_strdup(_("Failed to read configuration file"));
    case LOCERR_NOCFG:
        return g_strdup(_("No configuration found"));
    case LOCERR_NOCFGFILE:
        return g_strdup(_("No configuration file found"));
    case LOCERR_BADCONTENT:
        return g_strdup(_("Server sent bad content"));
    case LOCERR_TRANSPFAIL:
        return g_strdup(_("Transport failure (no connection?)"));
    case LOCERR_TIMEOUT:
        return g_strdup(_("Connection timed out"));
    case LOCERR_CERT_EXPIRED:
        return g_strdup(_("Connection certificate has expired"));
    case LOCERR_CERT_INVALID:
        return g_strdup(_("Connection certificate is invalid"));
    case LOCERR_CONN:
    case LOCERR_NOCONN:
        return g_strdup(_("Connection failed"));
    case LOCERR_BADURL:
        return g_strdup(_("URL is bad"));
    case LOCERR_SRVNOTFOUND:
        return g_strdup(_("Server not found"));
    default:
        return g_strdup_printf (_("Error %d"), error_code);
    }
}


/*
static void
connman_props_changed (DBusGProxy *proxy, const char *key, GValue *v, app_data *data)
{
    const char *state;
    gboolean online;

    if (strcmp (key, "State") != 0)
        return;
    state = g_value_get_string (v);
    online = (strcmp (state, "online") == 0);
    if (online != data->online) {
        data->online = online;
        set_app_state (data, SYNC_UI_STATE_CURRENT_STATE);
    }
}
*/

static void
server_shutdown_cb (SyncevoServer *server,
                    app_data *data)
{
    if (data->syncing) {
        add_error_info (data, _("Syncevolution.Server D-Bus service exited unexpectedly"), NULL);

        gtk_label_set_text (GTK_LABEL (data->sync_status_label), 
                            _("Sync Failed"));
        set_sync_progress (data, 1.0 , "");
        set_app_state (data, SYNC_UI_STATE_SERVER_OK);
    }
}


static void
set_running_session (app_data *data, const char *path)
{
    if (data->running_session) {
        g_object_unref (data->running_session);
    }

    if (!path) {
        data->running_session = NULL;
        return;
    } else if (data->session &&
        strcmp (path, syncevo_session_get_path (data->session)) == 0) {
        data->running_session = g_object_ref (data->session);
    } else {
        data->running_session = syncevo_session_new (path);
    }

    g_signal_connect (data->running_session, "progress-changed",
                      G_CALLBACK (running_session_progress_changed_cb), data);
    g_signal_connect (data->running_session, "status-changed",
                      G_CALLBACK (running_session_status_changed_cb), data);
    syncevo_session_get_status (data->running_session,
                                (SyncevoSessionGetStatusCb)get_running_session_status_cb,
                                data);
}

static void
server_session_changed_cb (SyncevoServer *server,
                           char *path,
                           gboolean started,
                           app_data *data)
{
    if (started) {
        set_running_session (data, path);
    } else {
        if (data->running_session &&
            strcmp (syncevo_session_get_path (data->running_session), path) == 0 ) {
            set_running_session (data, NULL);
        }
    }
}

static void
get_sessions_cb (SyncevoServer *server,
                 SyncevoSessions *sessions,
                 GError *error,
                 app_data *data)
{
    const char *path;

    if (error) {
        g_warning ("Server.GetSessions failed: %s", error->message);
        g_error_free (error);

        /* TODO: check for other errors */
        set_app_state (data, SYNC_UI_STATE_SERVER_FAILURE);
        return;
    }

    /* assume first one is active */
    path = syncevo_sessions_index (sessions, 0);
    set_running_session (data, path);

    syncevo_sessions_free (sessions);
}

GtkWidget*
sync_ui_create_main_window ()
{
    app_data *data;

    data = g_slice_new0 (app_data);
    data->online = TRUE;
    data->current_state = SYNC_UI_STATE_GETTING_SERVER;
    if (!init_ui (data)) {
        return NULL;
    }

    data->server = syncevo_server_get_default();
    g_signal_connect (data->server, "shutdown", 
                      G_CALLBACK (server_shutdown_cb), data);
    g_signal_connect (data->server, "session-changed",
                      G_CALLBACK (server_session_changed_cb), data);
    syncevo_server_get_sessions (data->server,
                                 (SyncevoServerGetSessionsCb)get_sessions_cb,
                                 data);

    /* TODO: use Presence signal and CheckPresence to make sure we 
     * know if network is down etc. */
    
    init_configuration (data);

    gtk_window_present (GTK_WINDOW (data->sync_win));

    return data->sync_win;
}
