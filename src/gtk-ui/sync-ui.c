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
#include <glib/gi18n.h>
#include <gio/gio.h>
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

/* local copy of GtkInfoBar, used when GTK+ < 2.18 */
#include "gtkinfobar.h"

#ifdef USE_MOBLIN_UX
#include "mux-frame.h"
#include "mux-window.h"
#include <mx/mx-gtk.h>
#endif

static gboolean support_canceling = FALSE;

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
    GtkWidget *emergency_win; /* will be NULL when USE_MOBLIN_UX is set*/

    gint emergency_index; /* for use in mux_window_set_current_page() */

    GtkWidget *service_box;
    GtkWidget *info_bar;
    GtkWidget *no_connection_box;
    GtkWidget *main_frame;
    GtkWidget *log_frame;
    GtkWidget *server_icon_box;

    GtkWidget *offline_label;
    GtkWidget *progress;
    GtkWidget *sync_status_label;
    GtkWidget *sync_btn;
    GtkWidget *change_service_btn;
    GtkWidget *emergency_btn;

    GtkWidget *server_label;
    GtkWidget *last_synced_label;
    GtkWidget *sources_box;

    GtkWidget *new_service_btn;
    GtkWidget *services_box;
    GtkWidget *scrolled_window;
    GtkWidget *back_btn;
    GtkWidget *expanded_config;

    GtkWidget *emergency_label;
    GtkWidget *emergency_expander;
    GtkWidget *emergency_source_table;
    GtkWidget *emergency_from_client_btn;
    GtkWidget *emergency_from_server_btn;

    gboolean forced_emergency;
    GHashTable *emergency_sources;

    gboolean online;

    gboolean syncing;
    gboolean synced_this_session;
    int last_sync;
    guint last_sync_src_id;

    server_config *current_service;
    app_state current_state;
    gboolean open_current; /* should the service list open the current 
                              service when it populates next time*/

    SyncevoServer *server;

    SyncevoSession *running_session; /* session that is currently active */
} app_data;

typedef struct operation_data {
    app_data *data;
    enum op {
        OP_SYNC, /* use sync mode from config */
        OP_SYNC_SLOW,
        OP_SYNC_REFRESH_FROM_CLIENT,
        OP_SYNC_REFRESH_FROM_SERVER,
        OP_SAVE,
    } operation;
    gboolean started;
} operation_data;

static void set_sync_progress (app_data *data, float progress, char *status);
static void set_app_state (app_data *data, app_state state);
static void show_main_view (app_data *data);
static void update_emergency_view (app_data *data);
static void show_emergency_view (app_data *data);
static void show_services_list (app_data *data);
static void update_services_list (app_data *data);
static void update_service_ui (app_data *data);
static void setup_new_service_clicked (GtkButton *btn, app_data *data);
static gboolean source_config_update_widget (source_config *source);
static void get_presence_cb (SyncevoServer *server, char *status, char *transport,
                             GError *error, app_data *data);
static void get_reports_cb (SyncevoServer *server, SyncevoReports *reports, 
                            GError *error, app_data *data);
static void start_session_cb (SyncevoServer *server, char *path,
                              GError *error, operation_data *op_data);
static void get_config_for_main_win_cb (SyncevoServer *server, SyncevoConfig *config,
                                        GError *error, app_data *data);

void
show_error_dialog (GtkWidget *widget, const char* message)
{
    GtkWindow *window = GTK_WINDOW (gtk_widget_get_toplevel (widget));

    GtkWidget *w;
    w = gtk_message_dialog_new (window,
                                GTK_DIALOG_MODAL,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_OK,
                                "%s",
                                message);
    gtk_dialog_run (GTK_DIALOG (w));
    gtk_widget_destroy (w);
}


static void
remove_child (GtkWidget *widget, GtkContainer *container)
{
    gtk_container_remove (container, widget);
}

static void 
change_service_clicked_cb (GtkButton *btn, app_data *data)
{
    /* data->open_current = TRUE; */
    show_services_list (data);
}

static void 
emergency_clicked_cb (GtkButton *btn, app_data *data)
{
    show_emergency_view (data);
}


char*
get_pretty_source_name (const char *source_name)
{
    /* TRANSLATORS: There have been name changes to keep things in line with 
     * the rest of the moblin UI. Please make sure the name you use matches 
     * the ones in e.g. the panels. */
    if (strcmp (source_name, "addressbook") == 0) {
        return g_strdup (_("Contacts"));
    } else if (strcmp (source_name, "calendar") == 0) {
        return g_strdup (_("Appointments"));
    } else if (strcmp (source_name, "todo") == 0) {
        return g_strdup (_("Tasks"));
    } else if (strcmp (source_name, "memo") == 0) {
        return g_strdup (_("Memo"));
    } else if (strcmp (source_name, "calendar+todo") == 0) {
        /* TRANSLATORS: This is a "combination source" for syncing with devices
         * that combine appointments and tasks. the name should match the ones
         * used for calendar and todo above */
        return g_strdup (_("Appointments & Tasks"));
    } else {
        char *tmp;
        tmp =  g_strdup (source_name);
        tmp[0] = g_ascii_toupper (tmp[0]);
        return tmp;
    }
}

/*
static void
add_error_info (app_data *data, const char *message, const char *external_reason)
{
    GtkWidget *lbl;
    GList *l, *children;

    children = gtk_container_get_children (GTK_CONTAINER (data->service_error_box));
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

    gtk_widget_set_size_request (lbl, 160, -1);
    gtk_widget_show (lbl);
    gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
    gtk_box_pack_start (GTK_BOX (data->service_error_box), lbl, FALSE, FALSE, 0);

    if (external_reason) {
        g_warning ("%s: %s", message, external_reason);
    } else {
        g_warning ("%s", message);
    }
}
*/

static void
reload_config (app_data *data, const char *server)
{
    server_config_free (data->current_service);
    data->forced_emergency = FALSE;
    g_hash_table_remove_all (data->emergency_sources);

    if (!server || strlen (server) == 0) {
        data->current_service = NULL;
        update_service_ui (data);
        set_app_state (data, SYNC_UI_STATE_NO_SERVER);
    } else {
        data->synced_this_session = FALSE;
        data->current_service = g_slice_new0 (server_config);
        data->current_service->name = g_strdup (server);
        set_app_state (data, SYNC_UI_STATE_GETTING_SERVER);

        syncevo_server_get_config (data->server,
                                   data->current_service->name,
                                   FALSE,
                                   (SyncevoServerGetConfigCb)get_config_for_main_win_cb,
                                   data);
        
    }
}


static void
abort_sync_cb (SyncevoSession *session,
               GError *error,
               app_data *data)
{
    if (error) {
        /* TODO show in UI: failed to abort sync (while syncing) */
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
        /* TODO show in UI: sync failed (failed to even start) */
        g_error_free (error);
        g_object_unref (session);
        return;
    }

    set_sync_progress (data, 0.0, _("Starting sync"));
    /* stop updates of "last synced" */
    if (data->last_sync_src_id > 0)
        g_source_remove (data->last_sync_src_id);
    set_app_state (data, SYNC_UI_STATE_SYNCING);
}

static gboolean
confirm (app_data *data, const char *message, const char *yes)
{
    GtkWidget *w;
    int ret;

    w = gtk_message_dialog_new (GTK_WINDOW (data->sync_win),
                                GTK_DIALOG_MODAL,
                                GTK_MESSAGE_QUESTION,
                                GTK_BUTTONS_NONE,
                                "%s",
                                message);
    gtk_dialog_add_buttons (GTK_DIALOG (w),
                            _("No, cancel sync"),
                            GTK_RESPONSE_NO,
                            yes,
                            GTK_RESPONSE_YES,
                            NULL);
    ret = gtk_dialog_run (GTK_DIALOG (w));
    gtk_widget_destroy (w);

    return (ret == GTK_RESPONSE_YES);
}
static void
slow_sync_clicked_cb (GtkButton *btn, app_data *data)
{
    operation_data *op_data;
    char *message;

    message = g_strdup_printf (_("Do you want to slow sync with %s?"),
                               data->current_service->name);
    if (!confirm (data, message, _("Yes, do slow sync"))) {
        g_free (message);
        return;
    }
    g_free (message);

    op_data = g_slice_new (operation_data);
    op_data->data = data;
    op_data->operation = OP_SYNC_SLOW;
    op_data->started = FALSE;
    syncevo_server_start_session (data->server,
                                  data->current_service->name,
                                  (SyncevoServerStartSessionCb)start_session_cb,
                                  op_data);

    show_main_view (data);
}


static void
refresh_from_server_clicked_cb (GtkButton *btn, app_data *data)
{
    operation_data *op_data;
    char *message;

    message = g_strdup_printf (_("Do you want to delete all local data and replace it with "
                                 "data from %s? This is not usually advised."),
                               data->current_service->name);
    if (!confirm (data, message, _("Yes, delete and replace"))) {
        g_free (message);
        return;
    }
    g_free (message);

    op_data = g_slice_new (operation_data);
    op_data->data = data;
    op_data->operation = OP_SYNC_REFRESH_FROM_SERVER;
    op_data->started = FALSE;
    syncevo_server_start_session (data->server,
                                  data->current_service->name,
                                  (SyncevoServerStartSessionCb)start_session_cb,
                                  op_data);

    show_main_view (data);
}

static void
refresh_from_client_clicked_cb (GtkButton *btn, app_data *data)
{
    operation_data *op_data;
    char *message;

    message = g_strdup_printf (_("Do you want to delete all data in %s and replace it with "
                                 "your local data? This is not usually advised."),
                               data->current_service->name);
    if (!confirm (data, message, _("Yes, delete and replace"))) {
        g_free (message);
        return;
    }
    g_free (message);

    op_data = g_slice_new (operation_data);
    op_data->data = data;
    op_data->operation = OP_SYNC_REFRESH_FROM_CLIENT;
    op_data->started = FALSE;
    syncevo_server_start_session (data->server,
                                  data->current_service->name,
                                  (SyncevoServerStartSessionCb)start_session_cb,
                                  op_data);

    show_main_view (data);
}

static void
sync_clicked_cb (GtkButton *btn, app_data *data)
{
    operation_data *op_data;

    if (data->syncing) {
        syncevo_session_abort (data->running_session,
                               (SyncevoSessionGenericCb)abort_sync_cb,
                               data);
        set_sync_progress (data, -1.0, _("Trying to cancel sync"));
    } else {

        op_data = g_slice_new (operation_data);
        op_data->data = data;
        op_data->operation = OP_SYNC;
        op_data->started = FALSE;
        syncevo_server_start_session (data->server,
                                      data->current_service->name,
                                      (SyncevoServerStartSessionCb)start_session_cb,
                                      op_data);
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

    if (!data->current_service) {
        msg = g_strdup (_("No service selected"));
        delay = -1;
    } else if (data->last_sync <= 0) {
        msg = g_strdup (data->current_service->name); /* we don't know */
        delay = -1;
    } else if (diff < 30) {
        /* TRANSLATORS: This is the title on main view. Placeholder is 
         * the service name. Example: "Google - synced just now" */
        msg = g_strdup_printf (_("%s - synced just now"),
                               data->current_service->name);
        delay = 30;
    } else if (diff < 90) {
        msg = g_strdup_printf (_("%s - synced a minute ago"),
                               data->current_service->name);
        delay = 60;
    } else if (diff < 60 * 60) {
        msg = g_strdup_printf (_("%s - synced %ld minutes ago"),
                               data->current_service->name,
                               (diff + 30) / 60);
        delay = 60;
    } else if (diff < 60 * 90) {
        msg = g_strdup_printf (_("%s - synced an hour ago"),
                               data->current_service->name);
        delay = 60 * 60;
    } else if (diff < 60 * 60 * 24) {
        msg = g_strdup_printf (_("%s - synced %ld hours ago"),
                               data->current_service->name,
                               (diff + 60 * 30) / (60 * 60));
        delay = 60 * 60;
    } else if (diff < 60 * 60 * 24 - (60 * 30)) {
        msg = g_strdup_printf (_("%s - synced a day ago"),
                               data->current_service->name);
        delay = 60 * 60 * 24;
    } else {
        msg = g_strdup_printf (_("%s - synced %ld days ago"),
                               data->current_service->name,
                               (diff + 24 * 60 * 30) / (60 * 60 * 24));
        delay = 60 * 60 * 24;
    }

    gtk_label_set_text (GTK_LABEL (data->server_label), msg);
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
set_info_bar (GtkWidget *widget,
              GtkMessageType type,
              SyncErrorResponse response_id,
              const char *message)
{
    GtkWidget *container, *label;
    GtkInfoBar *bar = GTK_INFO_BAR (widget);

    if (!message) {
        gtk_widget_hide (widget);
        return;
    }

    container = gtk_info_bar_get_action_area (bar);
    gtk_container_foreach (GTK_CONTAINER (container),
                           (GtkCallback)remove_child,
                           container);
    switch (response_id) {
    case SYNC_ERROR_RESPONSE_EMERGENCY:
        /* TRANSLATORS: Buttons in error/info bars. */
        gtk_info_bar_add_button (bar, _("Fix it"), response_id);
        break;
    case SYNC_ERROR_RESPONSE_SETTINGS_SELECT:
        gtk_info_bar_add_button (bar, _("Select sync service"), response_id);
        break;
    case SYNC_ERROR_RESPONSE_SETTINGS_OPEN:
        gtk_info_bar_add_button (bar, _("Edit service settings"), response_id);
        break;
    case SYNC_ERROR_RESPONSE_NONE:
        break;
    default:
        g_warn_if_reached ();
    }

    gtk_info_bar_set_message_type (bar, type);
    container = gtk_info_bar_get_content_area (bar);
    gtk_container_foreach (GTK_CONTAINER (container),
                           (GtkCallback)remove_child,
                           container);

    label = gtk_label_new (message);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_widget_set_size_request (label, 450, -1);
    gtk_box_pack_start (GTK_BOX (container), label, FALSE, FALSE, 8);
    gtk_widget_show (label);
    gtk_widget_show (widget);
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
        gtk_widget_hide (data->service_box);
        gtk_widget_hide (data->info_bar);
        gtk_label_set_text (GTK_LABEL (data->sync_status_label), "");
        refresh_last_synced_label (data);

        gtk_widget_set_sensitive (data->main_frame, TRUE);
        gtk_widget_set_sensitive (data->sync_btn, FALSE);
        gtk_widget_set_sensitive (data->change_service_btn, FALSE);
        gtk_widget_set_sensitive (data->emergency_btn, FALSE);
        break;
    case SYNC_UI_STATE_NO_SERVER:
        gtk_widget_hide (data->service_box);
        set_info_bar (data->info_bar,
                      GTK_MESSAGE_INFO, SYNC_ERROR_RESPONSE_SETTINGS_SELECT,
                      _("You haven't selected a sync service yet. "
                        "Sync services let you synchronize your data "
                        "between your netbook and a web service"));
        refresh_last_synced_label (data);

        gtk_label_set_text (GTK_LABEL (data->sync_status_label), "");

        gtk_widget_set_sensitive (data->main_frame, TRUE);
        gtk_widget_set_sensitive (data->sync_btn, FALSE);
        gtk_widget_set_sensitive (data->change_service_btn, TRUE);
        gtk_widget_set_sensitive (data->emergency_btn, FALSE);
        gtk_window_set_focus (GTK_WINDOW (data->sync_win), data->change_service_btn);
        break;
    case SYNC_UI_STATE_SERVER_FAILURE:
        gtk_widget_hide (data->service_box);
        refresh_last_synced_label (data);

        /* info bar content should be set earlier */
        gtk_widget_show (data->info_bar);

        gtk_label_set_text (GTK_LABEL (data->sync_status_label), "");

        gtk_widget_set_sensitive (data->main_frame, FALSE);
        gtk_widget_set_sensitive (data->sync_btn, FALSE);
        gtk_widget_set_sensitive (data->emergency_btn, FALSE);
        gtk_widget_set_sensitive (data->change_service_btn, FALSE);
        break;
    case SYNC_UI_STATE_SERVER_OK:
        /* we have a active, idle session */
        gtk_widget_show (data->service_box);
        gtk_widget_hide (data->info_bar);

        gtk_widget_set_sensitive (data->main_frame, TRUE);
        if (data->online) {
            gtk_widget_hide (data->no_connection_box);
        } else {
            gtk_widget_show (data->no_connection_box);
        }
        gtk_widget_set_sensitive (data->sync_btn, data->online);
        gtk_widget_set_sensitive (data->change_service_btn, TRUE);
        gtk_widget_set_sensitive (data->emergency_btn, TRUE);
        /* TRANSLATORS: These are for the button in main view, right side.
           Keep line length below ~20 characters, use two lines if needed */
        if (data->synced_this_session)
            gtk_button_set_label (GTK_BUTTON (data->sync_btn), _("Sync again"));
        else
            gtk_button_set_label (GTK_BUTTON (data->sync_btn), _("Sync now"));
        gtk_window_set_focus (GTK_WINDOW (data->sync_win), data->sync_btn);

        data->syncing = FALSE;
        break;

    case SYNC_UI_STATE_SYNCING:
        /* we have a active session, and a session is running
           (the running session may or may not be ours) */
        gtk_widget_show (data->progress);
        gtk_label_set_text (GTK_LABEL (data->sync_status_label), _("Syncing"));
        gtk_widget_set_sensitive (data->main_frame, FALSE);
        gtk_widget_set_sensitive (data->change_service_btn, FALSE);
        gtk_widget_set_sensitive (data->emergency_btn, FALSE);

        gtk_widget_set_sensitive (data->sync_btn, support_canceling);
        if (support_canceling) {
            /* TRANSLATORS: This is for the button in main view, right side.
               Keep line length below ~20 characters, use two lines if needed */
            gtk_button_set_label (GTK_BUTTON (data->sync_btn), _("Cancel sync"));
        }

        data->syncing = TRUE;
        break;
    default:
        g_assert_not_reached ();
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


static gboolean
key_press_cb (GtkWidget *widget,
              GdkEventKey *event,
              app_data *data)
{
    if (event->keyval == GDK_Escape &&
        mux_window_get_current_page (MUX_WINDOW (data->sync_win)) >= 0) {

        show_main_view (data);
    }

    return FALSE;
}

static void
setup_windows (app_data *data,
               GtkWidget *main,
               GtkWidget *settings,
               GtkWidget *emergency)
{
    GtkWidget *mux_main;
    GtkWidget *tmp;

    g_assert (GTK_IS_WINDOW (main));
    g_assert (GTK_IS_WINDOW (settings));
    g_assert (GTK_IS_WINDOW (emergency));

    mux_main = mux_window_new ();
    gtk_window_set_title (GTK_WINDOW (mux_main),
                          gtk_window_get_title (GTK_WINDOW (main)));
    gtk_window_set_default_size (GTK_WINDOW (mux_main), 1024, 600);
    gtk_widget_set_name (mux_main, gtk_widget_get_name (main));
    gtk_widget_reparent (gtk_bin_get_child (GTK_BIN (main)), mux_main);
    mux_window_set_decorations (MUX_WINDOW (mux_main), MUX_DECOR_SETTINGS|MUX_DECOR_CLOSE);
    g_signal_connect (mux_main, "settings-visibility-changed",
                      G_CALLBACK (settings_visibility_changed_cb), data);
    g_signal_connect (mux_main, "key-press-event",
                      G_CALLBACK (key_press_cb), data);


    tmp = g_object_ref (gtk_bin_get_child (GTK_BIN (settings)));
    gtk_container_remove (GTK_CONTAINER (settings), tmp);
    mux_window_append_page (MUX_WINDOW (mux_main),
                                        gtk_window_get_title (GTK_WINDOW (settings)),
                                        tmp,
                                        TRUE);
    g_object_unref (tmp);

    tmp = g_object_ref (gtk_bin_get_child (GTK_BIN (emergency)));
    gtk_container_remove (GTK_CONTAINER (emergency), tmp);
    data->emergency_index =
        mux_window_append_page (MUX_WINDOW (mux_main),
                                gtk_window_get_title (GTK_WINDOW (emergency)),
                                tmp,
                                FALSE);
    g_object_unref (tmp);

    data->sync_win = mux_main;
    data->services_win = NULL;
    data->emergency_win = NULL;
}


#else

/* return the placeholders themselves when not using Moblin UX */
static GtkWidget*
switch_dummy_to_mux_frame (GtkWidget *dummy) {
    return dummy;
}
static void
setup_windows (app_data *data,
               GtkWidget *main,
               GtkWidget *settings,
               GtkWidget *emergency)
{
    data->sync_win = main;
    data->services_win = settings;
    data->emergency_win = emergency;
    gtk_window_set_transient_for (GTK_WINDOW (data->services_win),
                                  GTK_WINDOW (data->sync_win));
    gtk_window_set_transient_for (GTK_WINDOW (data->emergency_win),
                                  GTK_WINDOW (data->sync_win));
    g_signal_connect (data->services_win, "delete-event",
                      G_CALLBACK (gtk_widget_hide_on_delete), NULL);
    g_signal_connect (data->emergency_win, "delete-event",
                      G_CALLBACK (gtk_widget_hide_on_delete), NULL);
}
#endif

/* This is a hacky way to achieve autoscrolling when the expanders open/close */
static void
services_box_allocate_cb (GtkWidget     *widget,
                          GtkAllocation *allocation,
                          app_data *data)
{
    if (GTK_IS_WIDGET (data->expanded_config)) {
        int y, height;
        GtkAdjustment *adj;

        gtk_widget_translate_coordinates (data->expanded_config,
                                          data->services_box,
                                          0, 0, NULL, &y);
        height = data->expanded_config->allocation.height;

        adj = gtk_scrolled_window_get_vadjustment
                (GTK_SCROLLED_WINDOW (data->scrolled_window));
        gtk_adjustment_clamp_page (adj, y, y + height);

        data->expanded_config = NULL;
    }
}

static void
info_bar_response_cb (GtkInfoBar          *info_bar,
                      SyncErrorResponse  response_id,
                      app_data            *data)
{
    switch (response_id) {
    case SYNC_ERROR_RESPONSE_EMERGENCY:
        show_emergency_view (data);
        break;
    case SYNC_ERROR_RESPONSE_SETTINGS_OPEN:
        data->open_current = TRUE;
        show_services_list (data);
        break;
    case SYNC_ERROR_RESPONSE_SETTINGS_SELECT:
        show_services_list (data);
        break;
    default:
        g_warn_if_reached ();
    }
}

static gboolean
init_ui (app_data *data)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GtkWidget *frame, * service_error_box, *btn;
    GtkAdjustment *adj;

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

    data->service_box = GTK_WIDGET (gtk_builder_get_object (builder, "service_box"));
    service_error_box = GTK_WIDGET (gtk_builder_get_object (builder, "service_error_box"));
    data->info_bar = gtk_info_bar_new ();
    gtk_widget_set_no_show_all (data->info_bar, TRUE);
    g_signal_connect (data->info_bar, "response",
                      G_CALLBACK (info_bar_response_cb), data);
    gtk_box_pack_start (GTK_BOX (service_error_box), data->info_bar,
                        TRUE, TRUE, 16);

    data->no_connection_box = GTK_WIDGET (gtk_builder_get_object (builder, "no_connection_box"));
    data->server_icon_box = GTK_WIDGET (gtk_builder_get_object (builder, "server_icon_box"));

    data->offline_label = GTK_WIDGET (gtk_builder_get_object (builder, "offline_label"));
    data->progress = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar"));
    data->change_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "change_service_btn"));
    data->emergency_btn = GTK_WIDGET (gtk_builder_get_object (builder, "emergency_btn"));
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

    /* service list view */
    data->scrolled_window = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow"));
    adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (data->scrolled_window));
    data->services_box = GTK_WIDGET (gtk_builder_get_object (builder, "services_box"));
    gtk_container_set_focus_vadjustment (GTK_CONTAINER (data->services_box), adj);
    g_signal_connect(data->services_box, "size-allocate",
                     G_CALLBACK (services_box_allocate_cb), data);
    data->back_btn = GTK_WIDGET (gtk_builder_get_object (builder, "back_btn"));

    /* emergency view */
    btn = GTK_WIDGET (gtk_builder_get_object (builder, "slow_sync_btn"));
    g_signal_connect (btn, "clicked",
                      G_CALLBACK (slow_sync_clicked_cb), data);
    data->emergency_from_server_btn = GTK_WIDGET (gtk_builder_get_object (builder, "refresh_from_server_btn"));
    g_signal_connect (data->emergency_from_server_btn, "clicked",
                      G_CALLBACK (refresh_from_server_clicked_cb), data);
    data->emergency_from_client_btn = GTK_WIDGET (gtk_builder_get_object (builder, "refresh_from_client_btn"));
    g_signal_connect (data->emergency_from_client_btn, "clicked",
                      G_CALLBACK (refresh_from_client_clicked_cb), data);

    data->emergency_label = GTK_WIDGET (gtk_builder_get_object (builder, "emergency_label"));
    data->emergency_expander = GTK_WIDGET (gtk_builder_get_object (builder, "emergency_expander"));
    data->emergency_source_table = GTK_WIDGET (gtk_builder_get_object (builder, "emergency_source_table"));

    /* No (documented) way to add own widgets to gtkbuilder it seems...
       swap the all dummy widgets with Muxwidgets */
    setup_windows (data,
                   GTK_WIDGET (gtk_builder_get_object (builder, "sync_win")),
                   GTK_WIDGET (gtk_builder_get_object (builder, "services_win")),
                   GTK_WIDGET (gtk_builder_get_object (builder, "emergency_win")));

    data->main_frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "main_frame")));
    data->log_frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "log_frame")));
    frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "services_list_frame")));
    frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "emergency_frame")));

    g_signal_connect (data->sync_win, "destroy",
                      G_CALLBACK (gtk_main_quit), NULL);
    g_signal_connect_swapped (data->back_btn, "clicked",
                      G_CALLBACK (show_main_view), data);
    g_signal_connect (data->change_service_btn, "clicked",
                      G_CALLBACK (change_service_clicked_cb), data);
    g_signal_connect (data->emergency_btn, "clicked",
                      G_CALLBACK (emergency_clicked_cb), data);
    g_signal_connect (data->sync_btn, "clicked", 
                      G_CALLBACK (sync_clicked_cb), data);

    g_object_unref (builder);
    return TRUE;
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
    gtk_container_foreach (GTK_CONTAINER(icon_box),
                           (GtkCallback)remove_child,
                           icon_box);
    gtk_box_pack_start (icon_box, image, FALSE, FALSE, 0);
    gtk_widget_show (image);
}

static GtkWidget*
add_emergency_toggle_widget (app_data *data,
                             const char *title,
                             gboolean active,
                             guint row, guint col)
{
    GtkWidget *toggle;

#ifdef USE_MOBLIN_UX
    GtkWidget *label;
    col = col * 2;
    label = gtk_label_new (title);
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_widget_show (label);
    gtk_table_attach (GTK_TABLE (data->emergency_source_table), label,
                      col, col + 1, row, row + 1,
                      GTK_FILL, GTK_FILL, 16, 0);
    toggle = mx_gtk_light_switch_new ();
#else
    toggle = gtk_check_button_new_with_label (title);
#endif
    g_object_set (toggle, "active", active, NULL);
    gtk_widget_show (toggle);
    gtk_table_attach (GTK_TABLE (data->emergency_source_table), toggle,
                      col + 1, col + 2, row, row + 1,
                      GTK_FILL, GTK_FILL, 0, 0);
    return toggle;
}

static void
update_emergency_expander (app_data *data)
{
    char *text, *sources = NULL;
    GHashTableIter iter;
    char *name;

    g_hash_table_iter_init (&iter, data->emergency_sources);
    while (g_hash_table_iter_next (&iter, (gpointer)&name, NULL)) {
        char *pretty, *tmp;
        pretty = get_pretty_source_name (name);
        if (sources) {
            tmp = g_strdup_printf ("%s, %s", sources, pretty);
            g_free (sources);
            g_free (pretty);
            sources = tmp;
        } else {
            sources = pretty;
        }
    }
    if (sources) {
        /* This is the expander label in emergency view. It summarizes the
         * currently selected data sources. First placeholder is service/device
         * name, second a comma separeted list of sources.
         * E.g. "Affected data: Google Contacts, Appointments" */
        text = g_strdup_printf (_("Affected data: %s %s"),
                                data->current_service->name,
                                sources);
        g_free (sources);
    } else {
        text = g_strdup_printf (_("Affected data: none"));
    }

    gtk_expander_set_label (GTK_EXPANDER (data->emergency_expander), text);
    g_free (text);
}

static void
emergency_toggle_notify_active_cb (GtkWidget *widget,
                                   GParamSpec *pspec,
                                   app_data *data)
{
    gboolean active;
    char *source;

    g_object_get (G_OBJECT (widget), "active", &active, NULL);
    source = g_object_get_data (G_OBJECT (widget), "source");

    g_return_if_fail (source);

    if (active) {
        g_hash_table_insert (data->emergency_sources, g_strdup (source), "");
    } else {
        g_hash_table_remove (data->emergency_sources, source);
    }
    update_emergency_expander (data);
}


static void
add_emergency_source (const char *name, source_config *conf, app_data *data)
{
    GtkWidget *toggle;
    guint rows, cols;
    guint row;
    static guint col;
    gboolean active = TRUE;
    char *pretty_name;

    g_object_get (data->emergency_source_table,
                  "n-rows", &rows,
                  "n-columns", &cols,
                  NULL);
    if (cols != 1 && col == 0){
        col = 1;
        row = rows - 1;
    } else {
        col = 0;
        row = rows;
    }

    active = (g_hash_table_lookup (data->emergency_sources, name) != NULL);

    pretty_name = get_pretty_source_name (name);
    toggle = add_emergency_toggle_widget (data, pretty_name, active, row, col);
    g_object_set_data_full (G_OBJECT (toggle), "source", g_strdup (name), g_free);
    g_free (pretty_name);

    g_signal_connect (toggle, "notify::active",
                      G_CALLBACK (emergency_toggle_notify_active_cb), data);
}

static void
update_emergency_view (app_data *data)
{
    char *text;

    if (!data->current_service) {
        g_warning ("no service defined in Emergency view");
        return;
    }

    if (data->forced_emergency) {
        text = g_strdup_printf (
                /* TRANSLATORS: this is an explanation in Emergency view.
                 * Placeholder is a service/device name */
                _("A normal sync with %s is not possible at this time."
                  "We can do a slow two-way sync, start from scratch or "
                  "restore from backup."),
                data->current_service->name);
    } else {
        /* TRANSLATORS: this is an explanation in Emergency view.
         * Placeholder is a service/device name */
        text = g_strdup_printf (
                _("If something has gone horribly wrong with %s, we can try a "
                  "slow sync, start from scratch or restore from backup."),
                data->current_service->name);
    }
    gtk_label_set_text (GTK_LABEL (data->emergency_label), text);
    g_free (text);

    /* TRANSLATORS: These are a buttons in Emergency view. Placeholder is a 
     * service/device name. Please don't use too long lines, but feel free to 
     * use several lines. */
    text = g_strdup_printf (_("Delete all your local\n"
                              "data and replace with\n"
                              "data from %s"),
                            data->current_service->name);
    gtk_button_set_label (GTK_BUTTON (data->emergency_from_server_btn), text);
    g_free (text);
    text = g_strdup_printf (_("Delete all data on\n"
                              "%s and replace\n"
                              "with your local data"),
                            data->current_service->name);
    gtk_button_set_label (GTK_BUTTON (data->emergency_from_client_btn), text);
    g_free (text);

    gtk_container_foreach (GTK_CONTAINER (data->emergency_source_table),
                           (GtkCallback)remove_child,
                           data->emergency_source_table);
    gtk_table_resize (GTK_TABLE (data->emergency_source_table), 1, 1);
    g_hash_table_foreach (data->current_service->source_configs,
                          (GHFunc)add_emergency_source,
                          data);
    update_emergency_expander (data);
}

static void
update_service_source_ui (const char *name, source_config *conf, app_data *data)
{
    GtkWidget *lbl, *box;
    char *pretty_name;
    const char *source_uri, *sync;

    source_uri = g_hash_table_lookup (conf->config, "uri");
    sync = g_hash_table_lookup (conf->config, "sync");

    if (!sync || 
        strcmp (sync, "disabled") == 0 ||
        strcmp (sync, "none") == 0 ||
        !source_uri ||
        strlen (source_uri) == 0 ||
        !conf->supported_locally) {
        return;
    }

    conf->box = gtk_vbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (data->sources_box), conf->box,
                        FALSE, FALSE, 8);

    pretty_name = get_pretty_source_name (name);
    lbl = gtk_label_new (pretty_name);
    g_free (pretty_name);
    gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
    gtk_box_pack_start_defaults (GTK_BOX (conf->box), lbl);

    box = gtk_hbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (conf->box), box, FALSE, FALSE, 0);

    conf->info_bar = gtk_info_bar_new ();
    gtk_box_pack_start (GTK_BOX (box), conf->info_bar, TRUE, TRUE, 16);
    gtk_widget_set_no_show_all (conf->info_bar, TRUE);
    g_signal_connect (conf->info_bar, "response",
                      G_CALLBACK (info_bar_response_cb), data);

    conf->label = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC (conf->label), 0.0, 0.5);
    gtk_box_pack_start_defaults (GTK_BOX (conf->box), conf->label);

    source_config_update_widget (conf);

    gtk_widget_show_all (conf->box); 
}

static void
check_source_cb (SyncevoSession *session,
                 GError *error,
                 source_config *source)
{
    if (error) {
        if(error->code == DBUS_GERROR_REMOTE_EXCEPTION &&
           dbus_g_error_has_name (error, SYNCEVO_DBUS_ERROR_SOURCE_UNUSABLE)) {
            /* source is not supported locally */
            if (source) {
                source->supported_locally = FALSE;
                if (source->box) {
                    /* widget is already on screen, hide it */
                    gtk_widget_hide (source->box); 
                }
            }
        } else {
            g_warning ("CheckSource failed: %s", error->message);
            /* non-fatal, unknown error */
        }

        g_error_free (error);
        return;
    }
}

static void
update_service_ui (app_data *data)
{
    char *icon_uri = NULL;

    gtk_container_foreach (GTK_CONTAINER (data->sources_box),
                           (GtkCallback)remove_child,
                           data->sources_box);

    if (data->current_service && data->current_service->config) {
        syncevo_config_get_value (data->current_service->config,
                                  NULL, "IconURI", &icon_uri);

        g_hash_table_foreach (data->current_service->source_configs,
                              (GHFunc)update_service_source_ui,
                              data);
    }
    load_icon (icon_uri,
               GTK_BOX (data->server_icon_box),
               SYNC_UI_ICON_SIZE);

    refresh_last_synced_label (data);

/* TODO: make sure all default sources are visible
 * (iow add missing sources as insensitive) */

    gtk_widget_show_all (data->sources_box);
}

static void
unexpand_config_widget (GtkWidget *w, GtkWidget *exception)
{
    if (SYNC_IS_CONFIG_WIDGET (w) && exception && exception != w) {
        sync_config_widget_set_expanded (SYNC_CONFIG_WIDGET (w), FALSE);
    }
}

static void
config_widget_expanded_cb (GtkWidget *widget, GParamSpec *pspec, app_data *data)
{
    if (sync_config_widget_get_expanded (SYNC_CONFIG_WIDGET (widget))) {
        data->expanded_config = widget;
        gtk_container_foreach (GTK_CONTAINER (data->services_box),
                               (GtkCallback)unexpand_config_widget,
                               widget);
    }
}

static void
config_widget_changed_cb (GtkWidget *widget, app_data *data)
{
    if (sync_config_widget_get_current (SYNC_CONFIG_WIDGET (widget))) {
        const char *name = NULL;
        name = sync_config_widget_get_name (SYNC_CONFIG_WIDGET (widget));
        reload_config (data, name);
        show_main_view (data);
    } else {
        reload_config (data, NULL);
        update_services_list (data);
    }
}

static GtkWidget*
add_server_to_box (GtkBox *box,
                   const char *name,
                   gboolean configured,
                   gboolean has_template,
                   app_data *data)
{
    GtkWidget *item = NULL;
    gboolean current = FALSE;
    const char *current_name = NULL;

    if (data->current_service) {
        current_name = data->current_service->name;
        if (data->current_service->name && name && 
            strcmp (name, data->current_service->name) == 0) {
            current = TRUE;
        }
     }

    item = sync_config_widget_new (data->server, name,
                                   current, current_name,
                                   configured, has_template);
    g_signal_connect (item, "changed",
                      G_CALLBACK (config_widget_changed_cb), data);
    g_signal_connect (item, "notify::expanded",
                      G_CALLBACK (config_widget_expanded_cb), data);
    gtk_widget_show (item);
    gtk_box_pack_start (box, item, FALSE, FALSE, 0);

    if (current) {
        sync_config_widget_set_expanded (SYNC_CONFIG_WIDGET (item),
                                        data->open_current);
        data->open_current = FALSE;
    }

    return item;
}

static void
find_new_service_config (SyncConfigWidget *w, GtkWidget **found)
{
    if (SYNC_IS_CONFIG_WIDGET (w)) {
        if (!sync_config_widget_get_configured (w) &&
            !sync_config_widget_get_has_template (w)) {
            *found = GTK_WIDGET (w);
        }
    }
}

static void
setup_new_service_clicked (GtkButton *btn, app_data *data)
{
    GtkWidget *widget = NULL;

    gtk_container_foreach (GTK_CONTAINER (data->services_box),
                           (GtkCallback)unexpand_config_widget,
                           NULL);

    /* if a new service config has already been added, use that.
     * Otherwise add one. */
    gtk_container_foreach (GTK_CONTAINER (data->services_box),
                           (GtkCallback)find_new_service_config,
                           &widget);
    if (!widget) {
        widget = add_server_to_box (GTK_BOX (data->services_box),
                                    "default",
                                    FALSE, TRUE,
                                    data);
    }
    sync_config_widget_set_expanded (SYNC_CONFIG_WIDGET (widget), TRUE);
}


typedef struct templates_data {
    app_data *data;
    char **templates;
} templates_data;

static void
get_configs_cb (SyncevoServer *server,
                char **configs,
                GError *error,
                templates_data *templ_data)
{
    char **config_iter, **template_iter, **templates;
    app_data *data;
    GtkWidget *widget;

    templates = templ_data->templates;
    data = templ_data->data;
    g_slice_free (templates_data, templ_data);

    if (error) {
        show_main_view (data);

        /* TODO show in UI: failed to show service list */
        g_warning ("Server.GetConfigs() failed: %s", error->message);
        g_strfreev (templates);
        g_error_free (error);
        return;
    }

    for (template_iter = templates; *template_iter; template_iter++){
        gboolean found_config = FALSE;

        for (config_iter = configs; *config_iter; config_iter++) {
            if (*template_iter && 
                *config_iter && 
                g_ascii_strncasecmp (*template_iter,
                                     *config_iter,
                                     strlen (*config_iter)) == 0) {
                widget = add_server_to_box (GTK_BOX (data->services_box),
                                            *template_iter,
                                            TRUE, TRUE,
                                            data);
                found_config = TRUE;
                break;
            }
        }
        if (!found_config) {
            widget = add_server_to_box (GTK_BOX (data->services_box),
                                        *template_iter,
                                        FALSE, TRUE,
                                        data);
        }
    }

    for (config_iter = configs; *config_iter; config_iter++) {
        gboolean found_template = FALSE;

        for (template_iter = templates; *template_iter; template_iter++) {
            if (*template_iter && 
                *config_iter && 
                g_ascii_strncasecmp (*template_iter,
                                     *config_iter,
                                     strlen (*config_iter)) == 0) {

                found_template = TRUE;
                break;
            }
        }
        if (!found_template) {
            widget = add_server_to_box (GTK_BOX (data->services_box),
                                        *config_iter,
                                        TRUE, FALSE,
                                        data);
        }
    }

    g_strfreev (configs);
    g_strfreev (templates);
}

static void
get_template_configs_cb (SyncevoServer *server,
                         char **templates,
                         GError *error,
                         app_data *data)
{
    templates_data *templ_data;

    if (error) {
        show_main_view (data);
        /* TODO show in UI: failed to show service list */
        show_error_dialog (data->sync_win, 
                           _("Failed to get list of supported services from SyncEvolution"));
        g_warning ("Server.GetConfigs() failed: %s", error->message);
        g_error_free (error);
        return;
    }

    templ_data = g_slice_new (templates_data);
    templ_data->data = data;
    templ_data->templates = templates;

    syncevo_server_get_configs (data->server,
                                FALSE,
                                (SyncevoServerGetConfigsCb)get_configs_cb,
                                templ_data);
}

static void
update_services_list (app_data *data)
{
    /* NOTE: could get this on ui startup as well for instant action.
       Downside is stale data.... */

    gtk_container_foreach (GTK_CONTAINER (data->services_box),
                           (GtkCallback)remove_child,
                           data->services_box);

    syncevo_server_get_configs (data->server,
                                TRUE,
                                (SyncevoServerGetConfigsCb)get_template_configs_cb,
                                data);
}

static void
get_config_for_main_win_cb (SyncevoServer *server,
                            SyncevoConfig *config,
                            GError *error,
                            app_data *data)
{
    if (error) {
        if (error->code == DBUS_GERROR_REMOTE_EXCEPTION &&
            dbus_g_error_has_name (error, SYNCEVO_DBUS_ERROR_NO_SUCH_CONFIG)) {
            /* another syncevolution client probably removed the config */
        } else {
            g_warning ("Error in Server.GetConfig: %s", error->message);
            /* non-fatal, ignore in UI */
        }
        set_app_state (data, SYNC_UI_STATE_NO_SERVER);
        g_error_free (error);

        return;
    }
    
    if (config) {
        GHashTableIter iter;
        char *name;
        source_config *source;

        server_config_init (data->current_service, config);
        set_app_state (data, SYNC_UI_STATE_SERVER_OK);

        /* get "locally supported" status for all sources */
        g_hash_table_iter_init (&iter, data->current_service->source_configs);
        while (g_hash_table_iter_next (&iter, (gpointer)&name, (gpointer)&source)) {

            syncevo_server_check_source (data->server,
                                         data->current_service->name,
                                         name,
                                         (SyncevoServerGenericCb)check_source_cb,
                                         source);
        }

        syncevo_server_get_presence (server,
                                     data->current_service->name,
                                     (SyncevoServerGetPresenceCb)get_presence_cb,
                                     data);

        syncevo_server_get_reports (server,
                                    data->current_service->name,
                                    0, 1,
                                    (SyncevoServerGetReportsCb)get_reports_cb,
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
    static char *waiting_source = NULL;

    error = get_error_string_for_code (error_code, NULL);
    if (error) {
        /* TODO show sync error in UI -- but not duplicates */
        g_warning ("Source '%s' error: %s", name, error);
        g_free (error);
    }

    if (status & SYNCEVO_SOURCE_WAITING) {
        g_free (waiting_source);
        waiting_source = g_strdup (name);
        /* TODO: start spinner */
    } else if (waiting_source && strcmp (waiting_source, name) == 0) {
        g_free (waiting_source);
        waiting_source = NULL;
        /* TODO: stop spinner */
    }
}

static void
running_session_status_changed_cb (SyncevoSession *session,
                                   SyncevoSessionStatus status,
                                   guint error_code,
                                   SyncevoSourceStatuses *source_statuses,
                                   app_data *data)
{
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
        /* non-fatal, unknown error */
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
    long status;
    long mode;

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

static gboolean
handle_source_report_item (char **strs, const char *value, GHashTable *sources)
{
    source_stats *stats;
    char **tmp;
    char *name;

    if (g_strv_length (strs) < 3) {
        return FALSE;
    }

    /* replace '__' with '_' and '_+' with '-' */
    tmp = g_strsplit (strs[1], "__", 0);
    name = g_strjoinv ("_", tmp);
    g_strfreev (tmp);
    tmp = g_strsplit (name, "_+", 0);
    g_free (name);
    name = g_strjoinv ("-", tmp);
    g_strfreev (tmp);

    stats = g_hash_table_lookup (sources, name);
    if (!stats) {
        stats = g_slice_new0 (source_stats);
        g_hash_table_insert (sources, g_strdup (name), stats);
    }
    g_free (name);

    if (strcmp (strs[2], "stat") == 0) {
        if (g_strv_length (strs) != 6) {
            return FALSE;
        }
        
        if (strcmp (strs[3], "remote") == 0) {
            if (strcmp (strs[4], "added") == 0 ||
                strcmp (strs[4], "updated") == 0 ||
                strcmp (strs[4], "removed") == 0) {
                stats->remote_changes += strtol (value, NULL, 10);
            } else if (strcmp (strs[5], "reject") == 0) {
                stats->remote_rejections += strtol (value, NULL, 10);
            }

        } else if (strcmp (strs[3], "local") == 0) {
            if (strcmp (strs[4], "added") == 0 ||
                strcmp (strs[4], "updated") == 0 ||
                strcmp (strs[4], "removed") == 0) {
                stats->local_changes += strtol (value, NULL, 10);
            } else if (strcmp (strs[5], "reject") == 0) {
                stats->local_rejections += strtol (value, NULL, 10);
            }
        } else {
            return FALSE;
        }
    } else if (strcmp (strs[2], "mode") == 0) {
        stats->mode = strtol (value, NULL, 10);
    } else if (strcmp (strs[2], "status") == 0) {
        stats->status = strtol (value, NULL, 10);
    } else if (strcmp (strs[2], "resume") == 0) {
    } else if (strcmp (strs[2], "first") == 0) {
    } else if (strcmp (strs[2], "backup") == 0) {
        if (g_strv_length (strs) != 4) {
            return FALSE;
        }
        if (strcmp (strs[3], "before") == 0) {
        } else if (strcmp (strs[3], "after") == 0) {
        } else {
            return FALSE;
        }
    } else {
        return FALSE;
    }

    return TRUE;
}

static char*
get_report_summary (source_config *source)
{
    char *rejects = NULL;
    char *changes = NULL;
    char *msg = NULL;

    if (!source->stats_set) {
        return g_strdup ("");
    }

    if (source->local_rejections + source->remote_rejections == 0) {
        rejects = NULL;
    } else if (source->local_rejections == 0) {
        rejects = g_strdup_printf (ngettext ("There was one remote rejection.", 
                                             "There were %ld remote rejections.",
                                             source->remote_rejections),
                                   source->remote_rejections);
    } else if (source->remote_rejections == 0) {
        rejects = g_strdup_printf (ngettext ("There was one local rejection.", 
                                             "There were %ld local rejections.",
                                             source->local_rejections),
                                   source->local_rejections);
    } else {
        rejects = g_strdup_printf (_ ("There were %ld local rejections and %ld remote rejections."),
                                   source->local_rejections, source->remote_rejections);
    }

    if (source->local_changes + source->remote_changes == 0) {
        changes = g_strdup_printf (_("Last time: No changes."));
    } else if (source->local_changes == 0) {
        changes = g_strdup_printf (ngettext ("Last time: Sent one change.",
                                             "Last time: Sent %ld changes.",
                                             source->remote_changes),
                                   source->remote_changes);
    } else if (source->remote_changes == 0) {
        // This is about changes made to the local data. Not all of these
        // changes were requested by the remote server, so "applied"
        // is a better word than "received" (bug #5185).
        changes = g_strdup_printf (ngettext ("Last time: Applied one change.",
                                             "Last time: Applied %ld changes.",
                                             source->local_changes),
                                   source->local_changes);
    } else {
        changes = g_strdup_printf (_("Last time: Applied %ld changes and sent %ld changes."),
                                   source->local_changes, source->remote_changes);
    }

    if (rejects)
        msg = g_strdup_printf ("%s\n%s", changes, rejects);
    else
        msg = g_strdup (changes);
    g_free (rejects);
    g_free (changes);
    return msg;
}

/* return TRUE if no errors are shown */
static gboolean
source_config_update_widget (source_config *source)
{
    char *msg;
    gboolean show_error;
    SyncErrorResponse response;

    if (!source->label) {
        return TRUE;
    }

    /* TODO improve error visibility */
    msg = get_error_string_for_code (source->status, &response);
    if (msg) {
        show_error = TRUE;
        set_info_bar (source->info_bar, GTK_MESSAGE_ERROR, response, msg);
    } else {
        show_error = FALSE;
        gtk_widget_hide (source->info_bar);
        msg = get_report_summary (source);
        gtk_label_set_text (GTK_LABEL (source->label), msg);
    }
    g_free (msg);

    return !show_error;
}


static void
get_reports_cb (SyncevoServer *server,
                SyncevoReports *reports,
                GError *error,
                app_data *data)
{
    long status = -1;
    long common_status = -1;
    source_stats *stats;
    GHashTable *sources; /* key is source name, value is a source_stats */
    GHashTableIter iter;
    const char *key, *val;
    source_config *source_conf;
    char *error_msg;
    SyncErrorResponse response;
    gboolean have_source_errors;

    if (error) {
        g_warning ("Error in Session.GetReports: %s", error->message);
        g_error_free (error);
        /* non-fatal, unknown error */
        return;
    }

    sources = g_hash_table_new_full (g_str_hash, g_str_equal,
                                     g_free, (GDestroyNotify)free_source_stats);


    if (syncevo_reports_get_length (reports) > 0) {
        GHashTable *report = syncevo_reports_index (reports, 0);

        g_hash_table_iter_init (&iter, report);
        while (g_hash_table_iter_next (&iter, (gpointer)&key, (gpointer)&val)) {
            char **strs;
            strs = g_strsplit (key, "-", 6);
            if (!strs) {
                continue;
            }

            if (strcmp (strs[0], "source") == 0) {
                if (!handle_source_report_item (strs, val, sources)) {
                    g_warning ("Unidentified sync report item: %s=%s",
                               key, val);
                }
            } else if (strcmp (strs[0], "start") == 0) {
                /* not used */
            } else if (strcmp (strs[0], "end") == 0) {
                data->last_sync = strtol (val, NULL, 10);
            } else if (strcmp (strs[0], "status") == 0) {
                status = strtol (val, NULL, 10);
            } else if (strcmp (strs[0], "peer") == 0) {
                /* not used */
            } else if (strcmp (strs[0], "error") == 0) {
                /* not used */
            } else if (strcmp (strs[0], "dir") == 0) {
                /* not used */
            } else {
                g_warning ("Unidentified sync report item: %s=%s",
                           key, val);
            }

            g_strfreev (strs);
            
        }
    } else {
        common_status = 0;
    }

    /* sources now has all statistics we want */

    /* ficure out if all sources have same status or if there's a slow sync */
    data->forced_emergency = FALSE;
    g_hash_table_remove_all (data->emergency_sources);

    g_hash_table_iter_init (&iter, sources);
    while (g_hash_table_iter_next (&iter, (gpointer)&key, (gpointer)&stats)) {
        if (stats->status == 22001) {
            /* ignore abort because of another source slow syncing */
        } else if (stats->status == 22000) {
            common_status = stats->status;
            data->forced_emergency = TRUE;
            g_hash_table_insert (data->emergency_sources,
                                 g_strdup (key), "");
        } else if (common_status == -1) {
            common_status = stats->status;
        } else  if (common_status != stats->status) {
            common_status = 0;
        }
    }

    if (!data->forced_emergency) {
        /* if user initiates a emergency sync wihtout forced_emergency, 
           enable all sources by default*/
        g_hash_table_iter_init (&iter, sources);
        while (g_hash_table_iter_next (&iter, (gpointer)&key, NULL)) {
            g_hash_table_insert (data->emergency_sources,
                                 g_strdup (key), "");
        }
    }

    /* get common error message */
    error_msg = get_error_string_for_code (common_status, &response);
    have_source_errors = FALSE;

    g_hash_table_iter_init (&iter, sources);
    while (g_hash_table_iter_next (&iter, (gpointer)&key, (gpointer)&stats)) {
        /* store the statistics in source config */
        source_conf = g_hash_table_lookup (data->current_service->source_configs,
                                           key);
        if (source_conf) {
            source_conf->stats_set = TRUE;
            source_conf->local_changes = stats->local_changes;
            source_conf->remote_changes = stats->remote_changes;
            source_conf->local_rejections = stats->local_rejections;
            source_conf->remote_rejections = stats->remote_rejections;
            if (error_msg) {
                /* there is a service-wide error, no need to show here */
                source_conf->status = 0;
            } else {
                source_conf->status = stats->status;
            }
            /* if ui has been constructed already, update it */
            if (!source_config_update_widget (source_conf)) {
                have_source_errors = TRUE;
            }
        }
    }

    if (!error_msg && !have_source_errors) {
        /* no common source errors or individual source errors:
           it's still possible that there are sync errors */
        error_msg = get_error_string_for_code (status, &response);
    }

    /* update service UI */
    refresh_last_synced_label (data);
    if (error_msg) {
        set_info_bar (data->info_bar,
                      GTK_MESSAGE_ERROR, response,
                      error_msg);
        g_free (error_msg);
    }

    g_hash_table_destroy (sources);
}

static void
set_config_cb (SyncevoSession *session,
               GError *error,
               app_data *data)
{
    if (error) {
        g_warning ("Error in Session.SetConfig: %s", error->message);
        g_error_free (error);
        /* TODO show in UI: save failed */
    }
    g_object_unref (session);
}

static void
save_config (app_data *data, SyncevoSession *session)
{
    syncevo_session_set_config (session,
                                TRUE,
                                FALSE,
                                data->current_service->config,
                                (SyncevoSessionGenericCb)set_config_cb,
                                data);
}

static void
sync (operation_data *op_data, SyncevoSession *session)
{
    GHashTable *source_modes;
    GHashTableIter iter;
    source_config *source;
    SyncevoSyncMode mode = SYNCEVO_SYNC_NONE;

    app_data *data = op_data->data;

    source_modes = syncevo_source_modes_new ();

    if (op_data->operation != OP_SYNC) {
        /* in an emergency sync, set non-emergency sources to not sync*/
        g_hash_table_iter_init (&iter, data->current_service->source_configs);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer)&source)) {
            if (g_hash_table_lookup (data->emergency_sources, source->name) == NULL) {
                syncevo_source_modes_add (source_modes,
                                          source->name,
                                          SYNCEVO_SYNC_NONE);
            }
        }
    }

   /* override all non-supported with "none".  */
    g_hash_table_iter_init (&iter, data->current_service->source_configs);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer)&source)) {
        if (!source->supported_locally) {
            syncevo_source_modes_add (source_modes,
                                      source->name,
                                      SYNCEVO_SYNC_NONE);
        }
    }

    /* use given mode or use default for normal syncs */
    switch (op_data->operation) {
    case OP_SYNC:
        mode = SYNCEVO_SYNC_DEFAULT;
        break;
    case OP_SYNC_SLOW:
        mode = SYNCEVO_SYNC_SLOW;
        break;
    case OP_SYNC_REFRESH_FROM_CLIENT:
        mode = SYNCEVO_SYNC_REFRESH_FROM_CLIENT;
        break;
    case OP_SYNC_REFRESH_FROM_SERVER:
        mode = SYNCEVO_SYNC_REFRESH_FROM_SERVER;
        break;
    default:
        g_warn_if_reached();
    }
    syncevo_session_sync (session,
                          mode,
                          source_modes,
                          (SyncevoSessionGenericCb)sync_cb,
                          data);
    syncevo_source_modes_free (source_modes);
}

static void
set_config_for_sync_cb (SyncevoSession *session,
                        GError *error,
                        operation_data *op_data)
{
    if (error) {
        g_warning ("Error in Session.SetConfig: %s", error->message);
        g_error_free (error);
        /* TODO show in UI: sync failed (failed to even start) */
        return;
    }

    sync (op_data, session);
}

static void
run_operation (operation_data *op_data, SyncevoSession *session)
{
  SyncevoConfig *config; 

    /* when we first get idle, start the operation */
    if (op_data->started) {
        return;
    }
    op_data->started = TRUE;

    /* time for business */
    switch (op_data->operation) {
    case OP_SYNC:
    case OP_SYNC_SLOW:
    case OP_SYNC_REFRESH_FROM_CLIENT:
    case OP_SYNC_REFRESH_FROM_SERVER:
        /* Make sure we don't get change diffs printed out, then sync */
        config = g_hash_table_new (g_str_hash, g_str_equal);
        syncevo_config_set_value (config,
                                  NULL, "printChanges", "0");
        syncevo_session_set_config (session,
                                    TRUE,
                                    TRUE,
                                    config,
                                    (SyncevoSessionGenericCb)set_config_for_sync_cb,
                                    op_data);
        syncevo_config_free (config);

        break;
    case OP_SAVE:
        save_config (op_data->data, session);
        break;
    default:
        g_warn_if_reached ();
    }
}
/* Our sync session status */
static void
status_changed_cb (SyncevoSession *session,
                   SyncevoSessionStatus status,
                   guint error_code,
                   SyncevoSourceStatuses *source_statuses,
                   operation_data *op_data)
{

    switch (status) {
    case SYNCEVO_STATUS_IDLE:
        run_operation (op_data, session);
        break;
    case SYNCEVO_STATUS_DONE:
        op_data->data->synced_this_session = TRUE;

        /* no need for sync session anymore */
        g_object_unref (session);

        /* refresh stats -- the service may no longer be the one syncing,
         * and we might have only saved config but what the heck... */
        syncevo_server_get_reports (op_data->data->server,
                                    op_data->data->current_service->name,
                                    0, 1,
                                    (SyncevoServerGetReportsCb)get_reports_cb,
                                    op_data->data);

        g_slice_free (operation_data, op_data);
    default:
        ;
    }
}

/* Our sync (or config-save) session status */
static void
get_status_cb (SyncevoSession *session,
               SyncevoSessionStatus status,
               guint error_code,
               SyncevoSourceStatuses *source_statuses,
               GError *error,
               operation_data *op_data)
{
    if (error) {
        g_warning ("Error in Session.GetStatus: %s", error->message);
        g_error_free (error);
        g_object_unref (session);

        switch (op_data->operation) {
        case OP_SYNC:
            /* TODO show in UI: sync failed (failed to even start) */
            break;
        case OP_SAVE:
            /* TODO show in UI: save failed */
            break;
        default:
            g_warn_if_reached ();
        }
        g_slice_free (operation_data, op_data);
        return;
    }

    if (status == SYNCEVO_STATUS_IDLE) {
        run_operation (op_data, session);
    }
}

static void
start_session_cb (SyncevoServer *server,
                  char *path,
                  GError *error,
                  operation_data *op_data)
{
    SyncevoSession *session;
    app_data *data = op_data->data;

    if (error) {
        g_warning ("Error in Server.StartSession: %s", error->message);
        g_error_free (error);
        g_free (path);

        switch (op_data->operation) {
        case OP_SYNC:
            /* TODO show in UI: sync failed (failed to even start) */
            break;
        case OP_SAVE:
            /* TODO show in UI: save failed */
            break;
        default:
            g_warn_if_reached ();
        }
        g_slice_free (operation_data, op_data);
        return;
    }

    session = syncevo_session_new (path);

    if (data->running_session &&
        strcmp (path, syncevo_session_get_path (data->running_session)) != 0) {
        /* This is a really unfortunate event:
           Someone got a session and we did not have time to set UI insensitive... */
        gtk_label_set_markup (GTK_LABEL (data->server_label), 
                              _("Waiting for current operation to finish..."));
        gtk_widget_show_all (data->sources_box);
    }

    /* we want to know about status changes to our session */
    g_signal_connect (session, "status-changed",
                      G_CALLBACK (status_changed_cb), op_data);
    syncevo_session_get_status (session,
                                (SyncevoSessionGetStatusCb)get_status_cb,
                                op_data);

    g_free (path);
}

static void
show_emergency_view (app_data *data)
{
    update_emergency_view (data);
#ifdef USE_MOBLIN_UX
    mux_window_set_current_page (MUX_WINDOW (data->sync_win),
                                 data->emergency_index);
#else
    gtk_window_present (GTK_WINDOW (data->emergency_win));
#endif
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
    mux_window_set_current_page (MUX_WINDOW (data->sync_win), -1);
#else
    gtk_widget_hide (data->services_win);
#endif
    gtk_window_present (GTK_WINDOW (data->sync_win));
}

/* TODO: this function should accept source/peer name as param */
char*
get_error_string_for_code (int error_code, SyncErrorResponse *response)
{
    if (response) {
        *response = SYNC_ERROR_RESPONSE_NONE;
    }

    switch (error_code) {
    case -1: /* no errorcode */
    case 0:
    case 200:
    case LOCERR_USERABORT:
    case LOCERR_USERSUSPEND:
        return NULL;
    case 22000:
        if (response) {
            *response = SYNC_ERROR_RESPONSE_EMERGENCY;
        }
        return g_strdup (_("A normal sync is not possible at this time. You "
                           "will need to fix things before we can sync again."));
    case DB_Unauthorized:
        if (response) {
            *response = SYNC_ERROR_RESPONSE_SETTINGS_OPEN;
        }
        return g_strdup(_("Failed to login. Could there be a problem with "
                          "your username or password?"));
    case DB_Forbidden:
        return g_strdup(_("Forbidden"));
    case DB_NotFound:
        if (response) {
            *response = SYNC_ERROR_RESPONSE_SETTINGS_OPEN;
        }
        return g_strdup(_("The source could not be found. Could there be a "
                          "problem with the server settings?"));
    case DB_Fatal:
        /* This can happen when EDS is borked, restart may help... */
        return g_strdup(_("Fatal database error"));
    case DB_Error:
        return g_strdup(_("Database error"));
    case DB_Full:
        return g_strdup(_("No space left"));
    case LOCERR_PROCESSMSG:
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
    case LOCERR_CERT_EXPIRED:
        return g_strdup(_("Connection certificate has expired"));
    case LOCERR_CERT_INVALID:
        return g_strdup(_("Connection certificate is invalid"));
    case LOCERR_CONN:
    case LOCERR_NOCONN:
    case LOCERR_TRANSPFAIL:
    case LOCERR_TIMEOUT:
        if (response) {
            *response = SYNC_ERROR_RESPONSE_SETTINGS_OPEN;
        }
        return g_strdup(_("We were unable to connect to the server. The problem "
                          "could be temporary or there could be something wrong "
                          "with the server settings."));
    case LOCERR_BADURL:
        return g_strdup(_("URL is bad"));
    case LOCERR_SRVNOTFOUND:
        return g_strdup(_("Server not found"));
    default:
        return g_strdup_printf (_("Error %d"), error_code);
    }
}

static void
server_shutdown_cb (SyncevoServer *server,
                    app_data *data)
{
    if (data->syncing) {
        gtk_label_set_text (GTK_LABEL (data->sync_status_label), 
                            _("Sync failed"));
        set_sync_progress (data, 1.0 , "");
        set_app_state (data, SYNC_UI_STATE_SERVER_OK);
    }

    /* re-init server here */
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
    }

    data->running_session = syncevo_session_new (path);

    g_signal_connect (data->running_session, "progress-changed",
                      G_CALLBACK (running_session_progress_changed_cb), data);
    g_signal_connect (data->running_session, "status-changed",
                      G_CALLBACK (running_session_status_changed_cb), data);
    syncevo_session_get_status (data->running_session,
                                (SyncevoSessionGetStatusCb)get_running_session_status_cb,
                                data);
}

static void
set_online_status (app_data *data, gboolean online)
{
   if (online != data->online) {
        data->online = online;

        if (data->current_state == SYNC_UI_STATE_SERVER_OK) {
            if (data->online) {
                gtk_widget_hide (data->no_connection_box);
            } else {
                gtk_widget_show (data->no_connection_box);
            }
        }
        gtk_widget_set_sensitive (data->sync_btn, data->online);
    }
}

static void
get_presence_cb (SyncevoServer *server,
                 char *status,
                 char *transport,
                 GError *error,
                 app_data *data)
{
    if (error) {
        g_warning ("Server.GetSessions failed: %s", error->message);
        g_error_free (error);
        /* non-fatal, ignore in UI */
        return;
    }

    if (data->current_service && status) {
        set_online_status (data, strcmp (status, "") == 0);
    }
    g_free (status);
    g_free (transport);
}

static void
info_request_cb (SyncevoServer *syncevo,
                 char *id,
                 char *session_path,
                 char *state,
                 char *handler_path,
                 char *type,
                 app_data *data)
{
    /* Implementation waiting for moblin bug #6376*/
    g_warning ("InfoRequest handler not implemented yet");
}

static void
server_presence_changed_cb (SyncevoServer *server,
                            char *config_name,
                            char *status,
                            char *transport,
                            app_data *data)
{
    if (data->current_service &&
        config_name && status &&
        strcmp (data->current_service->name, config_name) == 0) {

        set_online_status (data, strcmp (status, "") == 0);
    }
}

static void
server_session_changed_cb (SyncevoServer *server,
                           char *path,
                           gboolean started,
                           app_data *data)
{
    if (started) {
        set_running_session (data, path);
    } else if (data->running_session &&
               strcmp (syncevo_session_get_path (data->running_session), path) == 0 ) {
        set_running_session (data, NULL);
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

        /* TODO show in UI: failed first syncevo call (unexpected, fatal?) */
        return;
    }

    /* assume first one is active */
    path = syncevo_sessions_index (sessions, 0);
    set_running_session (data, path);

    syncevo_sessions_free (sessions);
}

static void
get_config_for_default_peer_cb (SyncevoServer *syncevo,
                                SyncevoConfig *config,
                                GError *error,
                                app_data *data)
{
    char *name;

    if (error) {
        g_warning ("Server.GetConfig failed: %s", error->message);
        g_error_free (error);
        /* TODO show in UI: failed first syncevo call (unexpected, fatal?) */
        return;
    }

    syncevo_config_get_value (config, NULL, "defaultPeer", &name);
    reload_config (data, name);

    syncevo_config_free (config);
}

GtkWidget*
sync_ui_create_main_window ()
{
    app_data *data;

    data = g_slice_new0 (app_data);
    data->online = TRUE;
    data->current_state = SYNC_UI_STATE_GETTING_SERVER;
    data->forced_emergency = FALSE;
    data->emergency_sources = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                     g_free, NULL);
    if (!init_ui (data)) {
        return NULL;
    }

    data->server = syncevo_server_get_default();
    g_signal_connect (data->server, "shutdown", 
                      G_CALLBACK (server_shutdown_cb), data);
    g_signal_connect (data->server, "session-changed",
                      G_CALLBACK (server_session_changed_cb), data);
    g_signal_connect (data->server, "presence_changed",
                      G_CALLBACK (server_presence_changed_cb), data);
    g_signal_connect (data->server, "info-request",
                      G_CALLBACK (info_request_cb), data);

    syncevo_server_get_config (data->server,
                               "",
                               FALSE,
                               (SyncevoServerGetConfigCb)get_config_for_default_peer_cb,
                               data);

    syncevo_server_get_sessions (data->server,
                                 (SyncevoServerGetSessionsCb)get_sessions_cb,
                                 data);

    gtk_window_present (GTK_WINDOW (data->sync_win));

    return data->sync_win;
}
