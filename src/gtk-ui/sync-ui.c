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

/*
 * TODO
 * 
 * * redesign main window? (talk with nick/patrick). Issues:
      - sync types (other than two-way) should maybe be available somewhere 
        else than main window?
      - showing usable statistic
      - sync errors can flood the ui... need to dicuss with nick.
        Possible solution: show only a few errors, but have a linkbutton to open a
        separate error window
 * * get history data from syncevolution
 * * backup/restore ? 
 * * GTK styling missing:
 *    - current implementation of MuxFrame results in a slight flicker on window open
 * * notes on dbus API:
 *    - a more cleaner solution would be to have StartSync return a 
 *      dbus path that could be used to connect to signals related to that specific
 *      sync
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

#include "syncevo-dbus.h"
#include "sync-ui-marshal.h"

/* for return value definitions */
/* TODO: would be nice to have a non-synthesis-dependent API but for now it's like this... */
#include <synthesis/syerror.h>
#include <synthesis/engine_defs.h>

#include "config.h"
#include "sync-ui-config.h"
#include "sync-ui.h"

#include <sync-config-widget.h>

#ifdef USE_MOBLIN_UX
#include "mux-frame.h"
#include "mux-window.h"
#endif

static gboolean support_canceling = FALSE;

#define SYNC_UI_GCONF_DIR "/apps/sync-ui"
#define SYNC_UI_SERVER_KEY SYNC_UI_GCONF_DIR"/server"

#define SYNC_UI_ICON_SIZE 48

#define STRING_VARIANT_HASHTABLE (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))

/* for connman state property */
static DBusGProxy *proxy = NULL;

typedef struct source_progress {
    char* name;
    
    /* progress */
    int prepare_current;
    int prepare_total;
    int send_current;
    int send_total;
    int receive_current;
    int receive_total;
    
    /* statistics */
    int added_local;
    int modified_local;
    int deleted_local;
    int rejected_local;
    int added_remote;
    int modified_remote;
    int deleted_remote;
    int rejected_remote;
    int bytes_uploaded;
    int bytes_downloaded;
} source_progress;

typedef enum app_state {
    SYNC_UI_STATE_CURRENT_STATE = 0, /* so you can call update_app_state with old values */
    SYNC_UI_STATE_GETTING_SERVER,
    SYNC_UI_STATE_NO_SERVER,
    SYNC_UI_STATE_SERVER_OK,
    SYNC_UI_STATE_SERVER_FAILURE,
    SYNC_UI_STATE_SYNCING,
} app_state;

/* absolute progress amounts 0-100 */
const float sync_progress_clicked = 0.02;
const float sync_progress_session_start = 0.04;
const float sync_progress_sync_start = 0.06;    /* prepare/send/receive cycles start here */
const float sync_progress_sync_end = 0.96;

/* how are prepare/send/receive weighed -- sum should be 100 */
const float sync_weight_prepare = 0.50;
const float sync_weight_send = 0.25;
const float sync_weight_receive = 0.25;

/* non-abolute progress amounts for prepare/send/receive (for all sources) */
#define SYNC_PROGRESS_PREPARE ((sync_progress_sync_end - sync_progress_sync_start) * sync_weight_prepare)
#define SYNC_PROGRESS_SEND ((sync_progress_sync_end - sync_progress_sync_start) * sync_weight_send)
#define SYNC_PROGRESS_RECEIVE ((sync_progress_sync_end - sync_progress_sync_start) * sync_weight_receive)

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
    GList *source_progresses;

    GHashTable *source_report_labels;

    SyncMode mode;
    SyncevoService *service;

    server_config *current_service;
    gboolean open_current;
} app_data;

static void set_sync_progress (app_data *data, float progress, char *status);
static void set_app_state (app_data *data, app_state state);
static void show_main_view (app_data *data);
static void show_services_list (app_data *data);
static void update_services_list (app_data *data);
static void setup_new_service_clicked (GtkButton *btn, app_data *data);

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

char*
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
set_server_config_cb (SyncevoService *service, GError *error, app_data *data)
{
    if (error) {
        show_error_dialog (GTK_WINDOW (data->sync_win),
                           _("Failed to save service configuration to SyncEvolution"));
        g_warning ("Failed to save service configuration to SyncEvolution: %s",
                   error->message);
        g_error_free (error);
        return;
    }
    save_gconf_settings (data, data->current_service->name);
}

static void
abort_sync_cb (SyncevoService *service, GError *error, app_data *data)
{
    if (error) {
        if (error->code == DBUS_GERROR_REMOTE_EXCEPTION &&
            dbus_g_error_has_name (error, SYNCEVO_DBUS_ERROR_INVALID_CALL)) {

            /* sync is no longer in progress for some reason */
            add_error_info (data, _("Failed to cancel: sync was no longer in progress"), error->message);
            set_sync_progress (data, 1.0 , "");
            set_app_state (data, SYNC_UI_STATE_SERVER_OK);
        } else {
            add_error_info (data, _("Failed to cancel sync"), error->message);
        }
        g_error_free (error);
    } else {
        set_sync_progress (data, -1.0, _("Canceling sync"));
    }
}

static void 
sync_clicked_cb (GtkButton *btn, app_data *data)
{
    GPtrArray *sources;
    GList *list;
    GError *error = NULL;

    if (data->syncing) {
        syncevo_service_abort_sync_async (data->service, data->current_service->name, 
                                          (SyncevoAbortSyncCb)abort_sync_cb, data);
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

        /* empty source progress list */
        list = data->source_progresses;
        for (list = data->source_progresses; list; list = list->next) {
            g_free (((source_progress*)list->data)->name);
            g_slice_free (source_progress, list->data);
        }
        g_list_free (data->source_progresses);
        data->source_progresses = NULL;

        sources = server_config_get_source_array (data->current_service, data->mode);
        if (sources->len == 0) {
            g_ptr_array_free (sources, TRUE);
            add_error_info (data, _("No sources are enabled, not syncing"), NULL);
            return;
        }
        syncevo_service_start_sync (data->service, 
                                    data->current_service->name,
                                    sources,
                                    &error);
        if (error) {
            if (error->code == DBUS_GERROR_REMOTE_EXCEPTION &&
                dbus_g_error_has_name (error, SYNCEVO_DBUS_ERROR_INVALID_CALL)) {

                /* stop updates of "last synced" */
                if (data->last_sync_src_id > 0)
                    g_source_remove (data->last_sync_src_id);
                set_app_state (data, SYNC_UI_STATE_SYNCING);
                set_sync_progress (data, sync_progress_clicked, _(""));
                
                add_error_info (data, _("A sync is already in progress"), error->message);
            } else {
                add_error_info (data, _("Failed to start sync"), error->message);
                g_error_free (error);
                return;
            }
        } else {
            set_sync_progress (data, sync_progress_clicked, _("Starting sync"));
            /* stop updates of "last synced" */
            if (data->last_sync_src_id > 0)
                g_source_remove (data->last_sync_src_id);
            set_app_state (data, SYNC_UI_STATE_SYNCING);
        }

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
    static int current_state = SYNC_UI_STATE_GETTING_SERVER;

    if (current_state == state)
        return;

    if (state != SYNC_UI_STATE_CURRENT_STATE)
        current_state = state;

    switch (current_state) {
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
    
    enabled = g_object_get_data (G_OBJECT (check), "enabled");
    *enabled = !*enabled;
    
    options = server_config_get_option_array (data->current_service);
    syncevo_service_set_server_config_async (data->service, 
                                             data->current_service->name,
                                             options,
                                             (SyncevoSetServerConfigCb)set_server_config_cb, 
                                             data);

    g_ptr_array_foreach (options, (GFunc)syncevo_option_free, NULL);
    g_ptr_array_free (options, TRUE);
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
update_service_ui (app_data *data)
{
    GList *l;

    g_assert (data->current_service);

    gtk_container_foreach (GTK_CONTAINER (data->sources_box),
                           (GtkCallback)remove_child,
                           data->sources_box);
    g_hash_table_remove_all (data->source_report_labels);

    if (data->current_service->name)
        gtk_label_set_markup (GTK_LABEL (data->server_label), data->current_service->name);

    gtk_container_foreach (GTK_CONTAINER (data->server_icon_box),
                           (GtkCallback)remove_child,
                           data->server_icon_box);
    load_icon (data->current_service->icon_uri,
               GTK_BOX (data->server_icon_box),
               SYNC_UI_ICON_SIZE);
    
    for (l = data->current_service->source_configs; l; l = l->next) {
        source_config *source = (source_config*)l->data;
        GtkWidget *check, *hbox, *box, *lbl;
        char *name;
        
        if (!source->supported_locally) {
            /* could also show as insensitive, like with unsupported services... */
            continue;
        }

        name = get_pretty_source_name (source->name);
        
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
        if (source->uri && strlen (source->uri) > 0) {
            lbl = gtk_label_new (name);
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), source->enabled);
            gtk_widget_set_sensitive (check, TRUE);
        } else {
            char *text;
            /* TRANSLATORS: placeholder is a source name, shown with checkboxes in main window */
            text = g_strdup_printf (_("%s (not supported by this service)"), name);
            lbl = gtk_label_new (text);
            g_free (text);
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), FALSE);
            gtk_widget_set_sensitive (check, FALSE);
        }
        g_free (name);
        gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
        gtk_box_pack_start_defaults (GTK_BOX (box), lbl);

        lbl = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
        gtk_box_pack_start_defaults (GTK_BOX (box), lbl);
        /* this is a bit hacky... maybe the link to the label should be in source_config ? */
        g_hash_table_insert (data->source_report_labels, source->name, lbl);

        g_object_set_data (G_OBJECT (check), "enabled", &source->enabled);
        g_signal_connect (check, "toggled",
                          G_CALLBACK (source_check_toggled_cb), data);
 
    }
    gtk_widget_show_all (data->sources_box);

}

static char*
get_report_summary (int local_changes, int remote_changes, int local_rejects, int remote_rejects)
{
    char *rejects, *changes, *msg;

    if (local_rejects + remote_rejects == 0) {
        rejects = NULL;
    } else if (local_rejects == 0) {
        rejects = g_strdup_printf (ngettext ("There was one remote rejection.", 
                                             "There were %d remote rejections.",
                                             remote_rejects),
                                   remote_rejects);
    } else if (remote_rejects == 0) {
        rejects = g_strdup_printf (ngettext ("There was one local rejection.", 
                                             "There were %d local rejections.",
                                             local_rejects),
                                   local_rejects);
    } else {
        rejects = g_strdup_printf (_ ("There were %d local rejections and %d remote rejections."),
                                   local_rejects, remote_rejects);
    }

    if (local_changes + remote_changes == 0) {
        changes = g_strdup_printf (_("Last time: No changes."));
    } else if (local_changes == 0) {
        changes = g_strdup_printf (ngettext ("Last time: Sent one change.",
                                             "Last time: Sent %d changes.",
                                             remote_changes),
                                   remote_changes);
    } else if (remote_changes == 0) {
        // This is about changes made to the local data. Not all of these
        // changes were requested by the remote server, so "applied"
        // is a better word than "received" (bug #5185).
        changes = g_strdup_printf (ngettext ("Last time: Applied one change.",
                                             "Last time: Applied %d changes.",
                                             local_changes),
                                   local_changes);
    } else {
        changes = g_strdup_printf (_("Last time: Applied %d changes and sent %d changes."),
                                   local_changes, remote_changes);
    }

    if (rejects)
        msg = g_strdup_printf ("%s\n%s", changes, rejects);
    else
        msg = g_strdup (changes);
    g_free (rejects);
    g_free (changes);
    return msg;
}

static void
update_sync_report_data (SyncevoReport *report, app_data *data)
{
    const char *name;
    GtkLabel *lbl;
    
    name = syncevo_report_get_name (report);
    lbl = GTK_LABEL (g_hash_table_lookup (data->source_report_labels, name));
    if (lbl) {
        char *msg;
        int local_changes, local_adds, local_updates, local_removes, local_rejects;
        int remote_changes, remote_adds, remote_updates, remote_removes, remote_rejects;
        
        syncevo_report_get_local (report, &local_adds, &local_updates, &local_removes, &local_rejects);
        syncevo_report_get_remote (report, &remote_adds, &remote_updates, &remote_removes, &remote_rejects);
        local_changes = local_adds + local_updates + local_removes;
        remote_changes = remote_adds + remote_updates + remote_removes;

        msg = get_report_summary (local_changes, remote_changes, local_rejects, remote_rejects);
        gtk_label_set_text (lbl, msg);
        g_free (msg);
    }
}

static void
get_sync_reports_cb (SyncevoService *service, GPtrArray *reports, GError *error, app_data *data)
{
    if (error) {
        g_warning ("Failed to get sync reports from SyncEvolution: %s", error->message);
        g_error_free (error);
        return;
    }

    if (reports->len < 1) {
        data->last_sync = -1; 
    } else {
        GPtrArray *source_reports;
        SyncevoReportArray *session_report = (SyncevoReportArray*)g_ptr_array_index (reports, 0);
        syncevo_report_array_get (session_report, &data->last_sync, &source_reports);
        g_ptr_array_foreach (source_reports, (GFunc)update_sync_report_data, data);
    }
    refresh_last_synced_label (data);

    g_ptr_array_foreach (reports, (GFunc)syncevo_report_array_free, NULL);
    g_ptr_array_free (reports, TRUE);

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
            data->current_service->password = g_strdup (key_data->password);
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
get_server_config_cb (SyncevoService *service, GPtrArray *options, GError *error, app_data *data)
{
    char *server_address;

    if (error) {
        /* just warn if current server has disappeared -- probably just command line use */
        if (error->code == DBUS_GERROR_REMOTE_EXCEPTION &&
            dbus_g_error_has_name (error, SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER)) {
            add_error_info (data, 
                            _("Failed to get server configuration from SyncEvolution"), 
                            error->message);
            set_app_state (data, SYNC_UI_STATE_NO_SERVER);
        } else {
            set_app_state (data, SYNC_UI_STATE_SERVER_FAILURE);
        }
        g_error_free (error);
        return;
    }

    g_ptr_array_foreach (options, (GFunc)add_server_option, data->current_service);
    server_config_ensure_default_sources_exist (data->current_service);
    
    update_service_ui (data);
    set_app_state (data, SYNC_UI_STATE_SERVER_OK);

    server_address = strstr (data->current_service->base_url, "://");
    if (server_address)
        server_address = server_address + 3;

    if (!server_address) {
        g_warning ("Server configuration has suspect URL '%s'",
                   data->current_service->base_url);
    } else {
        gnome_keyring_find_network_password (data->current_service->username,
                                             NULL,
                                             server_address,
                                             NULL,
                                             NULL,
                                             NULL,
                                             0,
                                             (GnomeKeyringOperationGetListCallback)find_password_cb,
                                             data,
                                             NULL);
    }

    /* get last sync report (for last sync time) */
    syncevo_service_get_sync_reports_async (service, data->current_service->name, 1,
                                            (SyncevoGetSyncReportsCb)get_sync_reports_cb,
                                            data);

    g_ptr_array_foreach (options, (GFunc)syncevo_option_free, NULL);
    g_ptr_array_free (options, TRUE);
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
    GtkWidget *item;
    const char *name;
    gboolean current = FALSE;

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
        sync_config_widget_set_expanded (SYNC_CONFIG_WIDGET (item), data->open_current);
        data->open_current = FALSE;
    }
    return item;
}

static void
setup_new_service_clicked (GtkButton *btn, app_data *data)
{
    GtkWidget *config_widget;
    SyncevoServer *server;

    gtk_container_foreach (GTK_CONTAINER (data->services_box),
                           (GtkCallback)unexpand_config_widget,
                           NULL);

    server = syncevo_server_new (NULL, NULL, NULL, TRUE);
    config_widget = add_server_to_box (GTK_BOX (data->services_box), server, data);

    sync_config_widget_set_expanded (SYNC_CONFIG_WIDGET (config_widget), TRUE);
}


typedef struct templates_data {
    app_data *data;
    GPtrArray *templates;
}templates_data;

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

static void
get_servers_cb (SyncevoService *service, 
                GPtrArray *servers, 
                GError *error, 
                templates_data *templ_data)
{
    int i;
    app_data *data = templ_data->data;

    if (error) {
        show_error_dialog (GTK_WINDOW (data->sync_win), 
                           _("Failed to get list of manually setup services from SyncEvolution"));
        g_warning ("Failed to get list of manually setup services from SyncEvolution: %s", 
                   error->message);
        g_error_free (error);
        return;
    }

    for (i = 0; i < servers->len; i++) {
        SyncevoServer *server = (SyncevoServer*)g_ptr_array_index (servers, i);
        
        /* make sure server is not added as template already */
        if (!server_array_contains (templ_data->templates, server)) {
            add_server_to_box (GTK_BOX (data->services_box),
                               server, data);
        }
    }

    /* the SyncevoServers in arrays are freed when the table gets freed */
    g_ptr_array_free (templ_data->templates, TRUE);
    g_ptr_array_free (servers, TRUE);
    g_slice_free (templates_data, templ_data);
}

static void
get_templates_cb (SyncevoService *service, 
                  GPtrArray *templates, 
                  GError *error, 
                  app_data *data)
{
    int i;
    templates_data *temps_data;

    if (error) {
        show_main_view (data);
        show_error_dialog (GTK_WINDOW (data->sync_win), 
                           _("Failed to get list of supported services from SyncEvolution"));
        g_warning ("Failed to get list of supported services from SyncEvolution: %s", 
                   error->message);
        g_error_free (error);
        return;
    }

    for (i = 0; i < templates->len; i++) {
        SyncevoServer *server;
        gboolean consumer_ready;

        server = (SyncevoServer*)g_ptr_array_index (templates, i);
        syncevo_server_get (server, NULL, NULL, NULL, &consumer_ready);
        if (consumer_ready) {
            add_server_to_box (GTK_BOX (data->services_box),
                               server, data);
        }
    }
    gtk_widget_show_all (data->services_box);

    temps_data = g_slice_new0 (templates_data);
    temps_data->data = data;
    temps_data->templates = templates;
    syncevo_service_get_servers_async (data->service,
                                       (SyncevoGetServerConfigCb)get_servers_cb,
                                       temps_data);
}

static void
update_services_list (app_data *data)
{
    gtk_container_foreach (GTK_CONTAINER (data->services_box),
                           (GtkCallback)remove_child,
                           data->services_box);
    syncevo_service_get_templates_async (data->service,
                                         (SyncevoGetTemplatesCb)get_templates_cb,
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

    /*TODO: avoid the rest if server did not actually change */

    gtk_widget_hide (data->progress);

    server_config_free (data->current_service);
    if (!server || strlen (server) == 0) {
        data->current_service = NULL; 
        set_app_state (data, SYNC_UI_STATE_NO_SERVER);
    } else {
        data->synced_this_session = FALSE;
        data->current_service = g_slice_new0 (server_config);
        data->current_service->name = server;
        set_app_state (data, SYNC_UI_STATE_GETTING_SERVER);
        syncevo_service_get_server_config_async (data->service, 
                                                 server,
                                                 (SyncevoGetServerConfigCb)get_server_config_cb,
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
    float progress;
    GList *list;
    int count = 0;
    
    progress = 0.0;
    for (list = data->source_progresses; list; list = list->next) {
        source_progress *p = (source_progress*)list->data;
        if (p->prepare_total > 0)
            progress += SYNC_PROGRESS_PREPARE * p->prepare_current / p->prepare_total;
        if (p->send_total > 0)
            progress += SYNC_PROGRESS_SEND * p->send_current / p->send_total;
        if (p->receive_total > 0)
            progress += SYNC_PROGRESS_RECEIVE * p->receive_current / p->receive_total;
        count++;
    }
    set_sync_progress (data, sync_progress_sync_start + (progress / count), msg);
}

static void
refresh_statistics (app_data *data)
{
    GList *list;

    for (list = data->source_progresses; list; list = list->next) {
        source_progress *p = (source_progress*)list->data;
        GtkLabel *lbl;
        
        lbl = GTK_LABEL (g_hash_table_lookup (data->source_report_labels, p->name));
        if (lbl) {
            char *msg;
            
            msg = get_report_summary (p->added_local + p->modified_local + p->deleted_local,
                                      p->added_remote + p->modified_remote + p->deleted_remote,
                                      p->rejected_local,
                                      p->rejected_remote);
            gtk_label_set_text (lbl, msg);
            g_free (msg);
    }
    }
}

static source_progress*
find_source_progress (GList *source_progresses, char *name)
{
    GList *list;

    /* TODO: it would make more sense if source_progresses was a GHashTable */
    for (list = source_progresses; list; list = list->next) {
        if (strcmp (((source_progress*)list->data)->name, name) == 0) {
            return (source_progress*)list->data;
        }
    }
    return NULL;
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


static void
server_shutdown_cb (SyncevoService *service,
                    app_data *data)
{
    if (data->syncing) {
        add_error_info (data, _("Sync D-Bus service exited unexpectedly"), NULL);

        gtk_label_set_text (GTK_LABEL (data->sync_status_label), 
                            _("Sync Failed"));
        set_sync_progress (data, 1.0 , "");
        set_app_state (data, SYNC_UI_STATE_SERVER_OK);
    }
}

static void
sync_progress_cb (SyncevoService *service,
                  char *server,
                  char *source,
                  int type,
                  int extra1, int extra2, int extra3,
                  app_data *data)
{
    static source_progress *source_prog = NULL;
    char *msg = NULL;
    char *error = NULL;
    char *name = NULL;
    GTimeVal val;

    /* just in case UI was just started and there is another sync in progress */
    set_app_state (data, SYNC_UI_STATE_SYNCING);

    /* if this is a source event, find the right source_progress */
    if (source) {
        source_prog = find_source_progress (data->source_progresses, source);
    }

    switch(type) {
    case -1:
        /* syncevolution finished sync */
        error = get_error_string_for_code (extra1);
        if (error)
            add_error_info (data, error, NULL);

        switch (extra1) {
        case 0:
            g_get_current_time (&val);
            data->last_sync = val.tv_sec;
            refresh_last_synced_label (data);
            
            data->synced_this_session = TRUE;
            gtk_label_set_text (GTK_LABEL (data->sync_status_label), 
                                _("Sync complete"));
            break;
        case LOCERR_USERABORT:
        case LOCERR_USERSUSPEND:
            gtk_label_set_text (GTK_LABEL (data->sync_status_label), 
                                _("Sync canceled"));
        default:
            gtk_label_set_text (GTK_LABEL (data->sync_status_label), 
                                _("Sync Failed"));
        }
        /* get sync report */
        refresh_statistics (data);
        set_sync_progress (data, 1.0 , "");
        set_app_state (data, SYNC_UI_STATE_SERVER_OK);

        break;
    case PEV_SESSIONSTART:
        /* double check we're in correct state*/
        set_app_state (data, SYNC_UI_STATE_SYNCING);
        set_sync_progress (data, sync_progress_session_start, NULL);
        break;
    case PEV_SESSIONEND:
        /* NOTE extra1 can be error here */
        set_sync_progress (data, sync_progress_sync_end, _("Ending sync"));
        break;

    case PEV_ALERTED:
        source_prog = g_slice_new0 (source_progress);
        source_prog->name = g_strdup (source);
        data->source_progresses = g_list_append (data->source_progresses, source_prog);
        break;

    case PEV_SENDSTART:
    case PEV_SENDEND:
    case PEV_RECVSTART:
    case PEV_RECVEND:
        /* these would be useful but they have no source so I can't tell how far we are... */
        break;
 
    case PEV_PREPARING:
        if (source_prog) {
            source_prog->prepare_current = CLAMP (extra1, 0, extra2);
            source_prog->prepare_total = extra2;
        }

        name = get_pretty_source_name (source);
        /* TRANSLATORS: placeholder is a source name (e.g. 'Calendar') in a progress text */
        msg = g_strdup_printf (_("Preparing '%s'"), name);
        calc_and_update_progress(data, msg);
        break;

    case PEV_ITEMSENT:
        if (source_prog) {
            source_prog->send_current = CLAMP (extra1, 0, extra2);
            source_prog->send_total = extra2;
        }

        name = get_pretty_source_name (source);
        /* TRANSLATORS: placeholder is a source name in a progress text */
        msg = g_strdup_printf (_("Sending '%s'"), name);
        calc_and_update_progress (data, msg);
        break;

    case PEV_ITEMRECEIVED:
        if (source_prog) {
            source_prog->receive_current = CLAMP (extra1, 0, extra2);
            source_prog->receive_total = extra2;
        }

        name = get_pretty_source_name (source);
        /* TRANSLATORS: placeholder is a source name in a progress text */
        msg = g_strdup_printf (_("Receiving '%s'"), name);
        calc_and_update_progress (data, msg);
        break;

    case PEV_SYNCEND:
        error = get_error_string_for_code (extra1);
        if (error) {
            name = get_pretty_source_name (source);
            msg = g_strdup_printf ("%s: %s", name, error);
            add_error_info (data, msg, NULL);
        }
        break;
    case PEV_DSSTATS_L:
        if (!source_prog)
            return;

        source_prog->added_local = extra1;
        source_prog->modified_local = extra2;
        source_prog->deleted_local = extra3;
        break;
    case PEV_DSSTATS_R:
        if (!source_prog)
            return;

        source_prog->added_remote = extra1;
        source_prog->modified_remote = extra2;
        source_prog->deleted_remote = extra3;
        break;
    case PEV_DSSTATS_E:
        if (!source_prog)
            return;

        source_prog->rejected_local = extra1;
        source_prog->rejected_remote = extra2;
        break;
    case PEV_DSSTATS_D:
        if (!source_prog)
            return;

        source_prog->bytes_uploaded = extra1;
        source_prog->bytes_downloaded = extra2;
        break;
    default:
        ;
    }
    g_free (msg);
    g_free (error);
    g_free (name);
}


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

static void
init_connman (app_data *data)
{
    DBusGConnection *connection;
    GHashTable *props;
    GError *error = NULL;

    connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
    if (connection == NULL) {
        g_warning ("Failed to open connection to bus: %s\n",
                   error->message);
        g_error_free (error);
        proxy = NULL;
        return;
    }

    proxy = dbus_g_proxy_new_for_name (connection,
                                       "org.moblin.connman",
                                       "/",
                                       "org.moblin.connman.Manager");
    if (proxy == NULL) {
        g_printerr ("Failed to get a proxy for Connman");
        return;
    }  

    dbus_g_object_register_marshaller (sync_ui_marshal_VOID__STRING_BOXED,
                                       G_TYPE_NONE,
                                       G_TYPE_STRING, G_TYPE_BOXED, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (proxy, "PropertyChanged",
                             G_TYPE_STRING, G_TYPE_VALUE, NULL);
    dbus_g_proxy_connect_signal (proxy, "PropertyChanged",
                                 G_CALLBACK (connman_props_changed), data, NULL);

    /* get initial State value*/
    if (dbus_g_proxy_call (proxy, "GetProperties", NULL,
                            G_TYPE_INVALID,
                            STRING_VARIANT_HASHTABLE, &props, G_TYPE_INVALID)) {
        GValue *value;

        value = g_hash_table_lookup (props, "State");
        if (value) {
            connman_props_changed (proxy, "State", value, data);
        }
        g_hash_table_unref (props);
    }
}

GtkWidget*
sync_ui_create_main_window ()
{
    app_data *data;

    data = g_slice_new0 (app_data);
    data->source_report_labels = g_hash_table_new (g_str_hash, g_str_equal);
    data->online = TRUE;
    if (!init_ui (data)) {
        return NULL;
    }

    init_connman (data);

    data->service = syncevo_service_get_default();
    g_signal_connect (data->service, "progress", 
                      G_CALLBACK (sync_progress_cb), data);
    g_signal_connect (data->service, "server-shutdown", 
                      G_CALLBACK (server_shutdown_cb), data);
    init_configuration (data);

    gtk_window_present (GTK_WINDOW (data->sync_win));

    return data->sync_win;
}
