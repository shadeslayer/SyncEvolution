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

#include "syncevo-dbus.h"
/* for return value definitions */
/* TODO: would be nice to have a non-synthesis-dependent API but for now it's like this... */
#include <synthesis/syerror.h>
#include <synthesis/engine_defs.h>

#include "sync-ui-config.h"

#ifdef USE_MOBLIN_UX
#include "mux-frame.h"
#include "mux-window.h"
#endif

#define SYNC_UI_GCONF_DIR "/apps/sync-ui"
#define SYNC_UI_SERVER_KEY SYNC_UI_GCONF_DIR"/server"

#define SYNC_UI_ICON_SIZE 48
#define SYNC_UI_LIST_ICON_SIZE 32
#define SYNC_UI_LIST_BTN_WIDTH 150

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
    int added_remote;
    int modified_remote;
    int deleted_remote;
    int bytes_uploaded;
    int bytes_downloaded;
} source_progress;

typedef enum app_state {
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
    GtkWidget *services_win;
    GtkWidget *service_settings_win;

    GtkWidget *server_box;
    GtkWidget *server_failure_box;
    GtkWidget *no_server_box;
    GtkWidget *error_box;
    GtkWidget *error_img;
    GtkWidget *main_frame;
    GtkWidget *log_frame;
    GtkWidget *backup_frame;
    GtkWidget *services_frame;
    GtkWidget *server_icon_box;

    GtkWidget *progress;
    GtkWidget *sync_btn;
    GtkWidget *edit_service_btn;
    GtkWidget *change_service_btn;

    GtkWidget *server_label;
    GtkWidget *sources_box;

    GtkWidget *new_service_btn;
    GtkWidget *services_table;
    GtkWidget *manual_services_table;
    GtkWidget *manual_services_scrolled;

    GtkWidget *service_settings_frame;
    GtkWidget *service_link;
    GtkWidget *service_name_label;
    GtkWidget *service_name_entry;
    GtkWidget *username_entry;
    GtkWidget *password_entry;
    GtkWidget *server_settings_expander;
    GtkWidget *server_settings_table;
    GtkWidget *reset_server_btn;
    GtkWidget *delete_service_btn;

    gboolean syncing;
    int last_sync;
    guint last_sync_src_id;
    GList *source_progresses;

    SyncMode mode;
    SyncevoService *service;

    server_config *current_service;
} app_data;

static void set_sync_progress (app_data *data, float progress, char *status);
static void set_app_state (app_data *data, app_state state);
static void show_services_window (app_data *data);
static void show_settings_window (app_data *data, server_config *config);
static void ensure_default_sources_exist(server_config *server);
static void add_server_option (SyncevoOption *option, server_config *server);
static void setup_new_service_clicked (GtkButton *btn, app_data *data);

static void
remove_child (GtkWidget *widget, GtkContainer *container)
{
    gtk_container_remove (container, widget);
}

static void 
change_service_clicked_cb (GtkButton *btn, app_data *data)
{
    show_services_window (data);
}

static void 
edit_services_clicked_cb (GtkButton *btn, app_data *data)
{
    data->current_service->changed = FALSE;
    gtk_window_set_transient_for (GTK_WINDOW (data->service_settings_win), 
                                  GTK_WINDOW (data->sync_win));
    show_settings_window (data, data->current_service);
}

static void
update_server_config (GtkWidget *widget, server_config *config)
{
    if (GTK_IS_ENTRY (widget))
        server_config_update_from_entry (config, GTK_ENTRY (widget));
}

static void
show_error_dialog (GtkWindow *parent, const char* message)
{
    GtkWidget *w;
    w = gtk_message_dialog_new (parent,
                                GTK_DIALOG_MODAL,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_OK,
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

    gtk_widget_hide (data->error_img);
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

    gtk_widget_show (data->error_img);

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
                           "Failed to save current service in GConf configuration system");
        g_warning ("Failed to save current service in GConf configuration system: %s", err->message);
        g_error_free (err);
    }
}

static void
set_server_config_cb (SyncevoService *service, GError *error, app_data *data)
{
    if (error) {
        show_error_dialog (GTK_WINDOW (data->sync_win),
                           "Failed to save service configuration to SyncEvolution");
        g_warning ("Failed to save service configuration to SyncEvolution: %s",
                   error->message);
        g_error_free (error);
        return;
    }
    save_gconf_settings (data, data->current_service->name);
}


/* temporary data structure for syncevo_service_get_template_config_async and
 * syncevo_service_get_server_config_async. server is the server that 
 * the method was called for. options_override are options that should 
 * be overridden on the config we get. */
typedef struct server_data {
    char *server_name;
    GPtrArray *options_override;
    app_data *data;
} server_data;

static void
get_server_config_for_template_cb (SyncevoService *service, GPtrArray *options, GError *error, server_data *data)
{
    server_config *config;

    if (error) {
        show_error_dialog (GTK_WINDOW (data->data->sync_win),
                           "Failed to get service configuration from SyncEvolution");
        g_warning ("Failed to get service configuration from SyncEvolution: %s",
                   error->message);
        g_error_free (error);
    } else {
        config = g_slice_new0 (server_config);
        config->name = g_strdup (data->server_name);
        g_ptr_array_foreach (options, (GFunc)add_server_option, config);
        if (data->options_override)
            g_ptr_array_foreach (data->options_override, (GFunc)add_server_option, config);

        ensure_default_sources_exist (config);
        
        config->changed = TRUE;
        show_settings_window (data->data, config);
    }

    g_free (data->server_name);
    if (data->options_override) {
        g_ptr_array_foreach (data->options_override, (GFunc)syncevo_option_free, NULL);
        g_ptr_array_free (data->options_override, TRUE);
    }
    g_slice_free (server_data ,data);

    g_ptr_array_foreach (options, (GFunc)syncevo_option_free, NULL);
    g_ptr_array_free (options, TRUE);
}

static void
remove_server_config_cb (SyncevoService *service, 
                         GError *error, 
                         server_data *data)
{
    g_assert (data);

    if (error) {
        show_error_dialog (GTK_WINDOW (data->data->sync_win),
                           "Failed to remove service configuration from SyncEvolution");
        g_warning ("Failed to remove service configuration from SyncEvolution: %s", 
                   error->message);
        g_error_free (error);
    } else {
        /* update list if visible */
        if (GTK_WIDGET_VISIBLE (data->data->services_win))
            show_services_window (data->data);

        if (data->data->current_service && data->data->current_service->name &&
            strcmp (data->data->current_service->name, data->server_name) == 0)
            save_gconf_settings (data->data, NULL);
    }

    g_free (data->server_name);
    g_slice_free (server_data ,data);
}

static void
delete_service_clicked_cb (GtkButton *btn, app_data *data)
{
    server_config *server;
    server_data* serv_data;

    server = g_object_get_data (G_OBJECT (data->service_settings_win), "server");
    g_assert (server);

    gtk_widget_hide (GTK_WIDGET (data->service_settings_win));

    serv_data = g_slice_new0 (server_data);
    serv_data->data = data;
    serv_data->server_name = g_strdup (server->name);

    syncevo_service_remove_server_config_async (data->service,
                                                server->name, 
                                                (SyncevoRemoveServerConfigCb)remove_server_config_cb,
                                                serv_data);
}

static void
reset_service_clicked_cb (GtkButton *btn, app_data *data)
{
    server_config *server;
    server_data* serv_data;
    SyncevoOption *option;

    server = g_object_get_data (G_OBJECT (data->service_settings_win), "server");
    g_assert (server);

    serv_data = g_slice_new0 (server_data);
    serv_data->data = data;
    serv_data->server_name = g_strdup (server->name);
    serv_data->options_override = g_ptr_array_new ();
    option = syncevo_option_new (NULL, g_strdup ("username"), g_strdup (server->username));
    g_ptr_array_add (serv_data->options_override, option);
    option = syncevo_option_new (NULL, g_strdup ("password"), g_strdup (server->password));
    g_ptr_array_add (serv_data->options_override, option);

    syncevo_service_get_template_config_async (data->service, 
                                               server->name, 
                                               (SyncevoGetTemplateConfigCb)get_server_config_for_template_cb,
                                               serv_data);
}

static void
service_save_clicked_cb (GtkButton *btn, app_data *data)
{
    GPtrArray *options;
    server_config *server;

    server = g_object_get_data (G_OBJECT (data->service_settings_win), "server");
    g_assert (server);

    server_config_update_from_entry (server, GTK_ENTRY (data->service_name_entry));
    server_config_update_from_entry (server, GTK_ENTRY (data->username_entry));
    server_config_update_from_entry (server, GTK_ENTRY (data->password_entry));

    gtk_container_foreach (GTK_CONTAINER (data->server_settings_table), 
                           (GtkCallback)update_server_config, server);

    if (!server->name || strlen (server->name) == 0) {
        show_error_dialog (GTK_WINDOW (data->service_settings_win), 
                           "service must have a name");
        return;
    }

    gtk_widget_hide (GTK_WIDGET (data->service_settings_win));
    gtk_widget_hide (GTK_WIDGET (data->services_win));

    if (data->current_service && data->current_service != server) {
        server_config_free (data->current_service);
        data->current_service = server;
    }

    if (!server->changed) {
        /* no need to save first, set the gconf key right away */
        save_gconf_settings (data, data->current_service->name);
    } else {
        /* save the server, let callback change current server gconf key */
        options = server_config_get_option_array (server);
        syncevo_service_set_server_config_async (data->service, 
                                                 server->name,
                                                 options,
                                                 (SyncevoSetServerConfigCb)set_server_config_cb, 
                                                 data);
        g_ptr_array_foreach (options, (GFunc)syncevo_option_free, NULL);
        g_ptr_array_free (options, TRUE);
    }
}

static void 
sync_clicked_cb (GtkButton *btn, app_data *data)
{
    GPtrArray *sources;
    GList *list;
    GError *error = NULL;

    if (data->syncing) {
        syncevo_service_abort_sync (data->service, data->current_service->name, &error);
        if (error) {
            add_error_info (data, "Failed to cancel sync", error->message);
            g_error_free (error);
            return;
        } else {
            set_sync_progress (data, -1.0, "Canceling sync");
        }
    } else {

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
            add_error_info (data, "No sources are enabled, not syncing", NULL);
            return;
        }
        syncevo_service_start_sync (data->service, 
                                    data->current_service->name,
                                    sources,
                                    &error);
        if (error) {
            add_error_info (data, "Failed to start SyncEvolution sync", error->message);
            g_error_free (error);
        } else {
            /* stop updates of "last synced" */
            if (data->last_sync_src_id > 0)
                g_source_remove (data->last_sync_src_id);
            set_sync_progress (data, sync_progress_clicked, "Starting sync");
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
        msg = g_strdup ("Sync finished");
        delay = 30;
    } else if (diff < 90) {
        msg = g_strdup ("Last synced a minute ago");
        delay = 60;
    } else if (diff < 60 * 60) {
        msg = g_strdup_printf ("Last synced %ld minutes ago", (diff + 30) / 60);
        delay = 60;
    } else if (diff < 60 * 90) {
        msg = g_strdup ("Last synced an hour ago");
        delay = 60 * 60;
    } else if (diff < 60 * 60 * 24) {
        msg = g_strdup_printf ("Last synced %ld hours ago", (diff + 60 * 30) / (60 * 60));
        delay = 60 * 60;
    } else if (diff < 60 * 60 * 24 - (60 * 30)) {
        msg = g_strdup ("Last synced a day ago");
        delay = 60 * 60 * 24;
    } else {
        msg = g_strdup_printf ("Last synced %ld days ago", (diff + 24 * 60 * 30) / (60 * 60 * 24));
        delay = 60 * 60 * 24;
    }

    set_sync_progress (data, -1, msg);
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
    switch (state) {
    case SYNC_UI_STATE_GETTING_SERVER:
        clear_error_info (data);
        gtk_widget_show (data->server_box);
        gtk_widget_hide (data->server_failure_box);
        gtk_widget_hide (data->no_server_box);

        gtk_widget_set_sensitive (data->main_frame, TRUE);
        gtk_widget_set_sensitive (data->log_frame, TRUE);
        gtk_widget_set_sensitive (data->backup_frame, FALSE);
        gtk_widget_set_sensitive (data->services_frame, TRUE);
        break;
    case SYNC_UI_STATE_NO_SERVER:
        clear_error_info (data);
        gtk_widget_hide (data->server_box);
        gtk_widget_hide (data->server_failure_box);
        gtk_widget_show (data->no_server_box);

        gtk_widget_set_sensitive (data->main_frame, TRUE);
        gtk_widget_set_sensitive (data->log_frame, FALSE);
        gtk_widget_set_sensitive (data->backup_frame, FALSE);
        gtk_widget_set_sensitive (data->services_frame, TRUE);
        gtk_window_set_focus (GTK_WINDOW (data->sync_win), data->change_service_btn);
        break;
    case SYNC_UI_STATE_SERVER_FAILURE:
        clear_error_info (data);
        gtk_widget_hide (data->server_box);
        gtk_widget_hide (data->no_server_box);
        gtk_widget_show (data->server_failure_box);

        gtk_widget_set_sensitive (data->main_frame, FALSE);
        gtk_widget_set_sensitive (data->log_frame, FALSE);
        gtk_widget_set_sensitive (data->backup_frame, FALSE);
        gtk_widget_set_sensitive (data->services_frame, FALSE);
        break;
    case SYNC_UI_STATE_SERVER_OK:
        gtk_widget_show (data->server_box);
        gtk_widget_hide (data->server_failure_box);
        gtk_widget_hide (data->no_server_box);

        gtk_widget_set_sensitive (data->main_frame, TRUE);
        gtk_widget_set_sensitive (data->log_frame, TRUE);
        gtk_widget_set_sensitive (data->backup_frame, FALSE);
        gtk_widget_set_sensitive (data->services_frame, TRUE);
        gtk_button_set_label (GTK_BUTTON (data->sync_btn), "Sync now");
        gtk_window_set_focus (GTK_WINDOW (data->sync_win), data->sync_btn);

        data->syncing = FALSE;
        break;
        
    case SYNC_UI_STATE_SYNCING:
        clear_error_info (data);
        gtk_widget_set_sensitive (data->main_frame, FALSE);
        gtk_widget_set_sensitive (data->log_frame, TRUE);
        gtk_widget_set_sensitive (data->backup_frame, FALSE);
        gtk_widget_set_sensitive (data->services_frame, FALSE);
        gtk_button_set_label (GTK_BUTTON (data->sync_btn), "Cancel sync");

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

    g_assert (GTK_IS_BIN (dummy));

    window = mux_window_new ();
    gtk_widget_set_name (window, gtk_widget_get_name (dummy));
    gtk_window_set_title (GTK_WINDOW (window), "Sync");
    mux_window_set_decorations (MUX_WINDOW (window), MUX_DECOR_CLOSE);
    gtk_widget_reparent (gtk_bin_get_child (GTK_BIN (dummy)), window);

    return window;
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
#endif


static void
show_link_button_url (GtkLinkButton *link)
{
    const char *url;
    GError *error = NULL;
    
    url = gtk_link_button_get_uri (GTK_LINK_BUTTON (link));
    if (!g_app_info_launch_default_for_uri (url, NULL, &error)) {
        g_warning ("Failed to show url '%s': %s", url, error->message);
        g_error_free (error);
    }
}

/* keypress handler for the transient windows (service list & service settings) */
static gboolean
key_press_cb (GtkWidget *widget,
              GdkEventKey *event,
              gpointer user_data)
{
    if (event->keyval == GDK_Escape) {
        gtk_widget_hide (widget);
        return TRUE;
    }
    return FALSE;
}

static gboolean
init_ui (app_data *data)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GObject *radio;
    GtkWidget *frame, *service_save_btn;

    gtk_rc_parse (THEMEDIR "sync-ui.rc");

    builder = gtk_builder_new ();
    gtk_builder_add_from_file (builder, GLADEDIR "ui.xml", &error);
    if (error) {
        g_printerr ("Failed to load user interface from %s\n", GLADEDIR "ui.xml");
        g_error_free (error);
        g_object_unref (builder);
        return FALSE;
    }

    data->server_box = GTK_WIDGET (gtk_builder_get_object (builder, "server_box"));
    data->no_server_box = GTK_WIDGET (gtk_builder_get_object (builder, "no_server_box"));
    data->server_failure_box = GTK_WIDGET (gtk_builder_get_object (builder, "server_failure_box"));
    data->error_img = GTK_WIDGET (gtk_builder_get_object (builder, "error_img"));
    data->error_box = GTK_WIDGET (gtk_builder_get_object (builder, "error_box"));
    data->server_icon_box = GTK_WIDGET (gtk_builder_get_object (builder, "server_icon_box"));

    data->progress = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar"));
    data->edit_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "edit_service_btn"));
    data->change_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "change_service_btn"));
    data->sync_btn = GTK_WIDGET (gtk_builder_get_object (builder, "sync_btn"));

    data->server_label = GTK_WIDGET (gtk_builder_get_object (builder, "sync_service_label"));
    data->sources_box = GTK_WIDGET (gtk_builder_get_object (builder, "sources_box"));

    data->new_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "new_service_btn"));
    gtk_widget_set_size_request (data->new_service_btn, 
                                 SYNC_UI_LIST_BTN_WIDTH, SYNC_UI_LIST_ICON_SIZE);
    g_signal_connect (data->new_service_btn, "clicked",
                      G_CALLBACK (setup_new_service_clicked), data);

    data->services_table = GTK_WIDGET (gtk_builder_get_object (builder, "services_table"));
    data->manual_services_table = GTK_WIDGET (gtk_builder_get_object (builder, "manual_services_table"));
    data->manual_services_scrolled = GTK_WIDGET (gtk_builder_get_object (builder, "manual_services_scrolled"));

    data->service_link = GTK_WIDGET (gtk_builder_get_object (builder, "service_link"));
    data->service_name_label = GTK_WIDGET (gtk_builder_get_object (builder, "service_name_label"));
    data->service_name_entry = GTK_WIDGET (gtk_builder_get_object (builder, "service_name_entry"));
    data->server_settings_expander = GTK_WIDGET (gtk_builder_get_object (builder, "server_settings_expander"));
    data->username_entry = GTK_WIDGET (gtk_builder_get_object (builder, "username_entry"));
    data->password_entry = GTK_WIDGET (gtk_builder_get_object (builder, "password_entry"));
    data->server_settings_table = GTK_WIDGET (gtk_builder_get_object (builder, "server_settings_table"));
    data->reset_server_btn = GTK_WIDGET (gtk_builder_get_object (builder, "reset_server_btn"));
    data->delete_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "delete_service_btn"));
    service_save_btn = GTK_WIDGET (gtk_builder_get_object (builder, "service_save_btn"));

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
    data->sync_win = switch_dummy_to_mux_window (GTK_WIDGET (gtk_builder_get_object (builder, "sync_win")));
    data->services_win = switch_dummy_to_mux_window (GTK_WIDGET (gtk_builder_get_object (builder, "services_win")));
    gtk_window_set_transient_for (GTK_WINDOW (data->services_win), 
                                  GTK_WINDOW (data->sync_win));
    data->service_settings_win = switch_dummy_to_mux_window (GTK_WIDGET (gtk_builder_get_object (builder, "service_settings_win")));

    data->main_frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "main_frame")));
    data->log_frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "log_frame")));
    data->services_frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "services_frame")));
    data->backup_frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "backup_frame")));
    frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "services_list_frame")));
    data->service_settings_frame = switch_dummy_to_mux_frame (GTK_WIDGET (gtk_builder_get_object (builder, "service_settings_frame")));

    g_signal_connect (data->sync_win, "destroy",
                      G_CALLBACK (gtk_main_quit), NULL);
    g_signal_connect (data->services_win, "delete-event",
                      G_CALLBACK (gtk_widget_hide_on_delete), NULL);
    g_signal_connect (data->services_win, "key-press-event",
                      G_CALLBACK (key_press_cb), NULL);
    g_signal_connect (data->service_settings_win, "delete-event",
                      G_CALLBACK (gtk_widget_hide_on_delete), NULL);
    g_signal_connect (data->service_settings_win, "key-press-event",
                      G_CALLBACK (key_press_cb), NULL);
    g_signal_connect (data->service_link, "clicked",
                      G_CALLBACK (show_link_button_url), NULL);
    g_signal_connect (data->delete_service_btn, "clicked",
                      G_CALLBACK (delete_service_clicked_cb), data);
    g_signal_connect (data->reset_server_btn, "clicked",
                      G_CALLBACK (reset_service_clicked_cb), data);
    g_signal_connect (service_save_btn, "clicked",
                      G_CALLBACK (service_save_clicked_cb), data);
    g_signal_connect (data->change_service_btn, "clicked",
                      G_CALLBACK (change_service_clicked_cb), 
                      data);
    g_signal_connect (data->edit_service_btn, "clicked",
                      G_CALLBACK (edit_services_clicked_cb), data);
    g_signal_connect (data->sync_btn, "clicked", 
                      G_CALLBACK (sync_clicked_cb), data);

    g_object_unref (builder);
    return TRUE;
}

static void
add_server_option (SyncevoOption *option, server_config *server)
{
    server_config_update_from_option (server, option);
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

typedef struct icon_data {
    GtkBox *icon_box;
    guint icon_size;
}icon_data;

static void
icon_read_cb (GFile *img_file, GAsyncResult *res, icon_data *data)
{
    GFileInputStream *in;
    GError *error = NULL;
    GdkPixbuf *pixbuf;
    GtkWidget *image;

    in = g_file_read_finish (img_file, res, &error);
    g_object_unref (img_file);
    if (!in) {
        g_warning ("Failed to read from service icon uri: %s", error->message);
        g_error_free (error);
        g_slice_free (icon_data, data);
        return;
    }

    pixbuf = gdk_pixbuf_new_from_stream_at_scale (G_INPUT_STREAM (in), 
                                                  data->icon_size, data->icon_size,
                                                  TRUE, NULL, &error);
    g_object_unref (in);
    if (!pixbuf) {
        g_warning ("Failed to load service icon: %s", error->message);
        g_error_free (error);
        g_slice_free (icon_data, data);
        return;
    }

    image = gtk_image_new_from_pixbuf (pixbuf);
    g_object_unref (pixbuf);
    gtk_container_foreach (GTK_CONTAINER (data->icon_box),
                           (GtkCallback)remove_child,
                           data->icon_box);
    gtk_box_pack_start_defaults (data->icon_box, image);
    gtk_widget_show (image);

    g_slice_free (icon_data, data);
}

static void
load_icon (const char *uri, icon_data *data)
{
    GFile *img_file;

    if (!uri || strlen (uri) == 0) {
        img_file = g_file_new_for_path (THEMEDIR "sync-generic.png");
    } else {
        img_file = g_file_new_for_uri (uri);
    }
    g_file_read_async (img_file, G_PRIORITY_DEFAULT, NULL, 
                       (GAsyncReadyCallback)icon_read_cb,
                       data);
}

static void
update_service_ui (app_data *data)
{
    GList *l;
    icon_data *icondata;

    g_assert (data->current_service);

    gtk_container_foreach (GTK_CONTAINER (data->sources_box),
                           (GtkCallback)remove_child,
                           data->sources_box);


    if (data->current_service->name)
        gtk_label_set_markup (GTK_LABEL (data->server_label), data->current_service->name);

    icondata = g_slice_new (icon_data);
    icondata->icon_box = GTK_BOX (data->server_icon_box);
    icondata->icon_size = SYNC_UI_ICON_SIZE;
    load_icon (data->current_service->icon_uri, icondata);
    
    for (l = data->current_service->source_configs; l; l = l->next) {
        source_config *source = (source_config*)l->data;
        GtkWidget *check;
        
        if (source->uri && strlen (source->uri) > 0) {
            check = gtk_check_button_new_with_label (source->name);
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), source->enabled);
            gtk_widget_set_sensitive (check, TRUE);
        } else {
            char *name;
            name = g_strdup_printf ("%s (not supported by this service)", source->name);
            check = gtk_check_button_new_with_label (name);
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), FALSE);
            gtk_widget_set_sensitive (check, FALSE);
        }
        g_object_set_data (G_OBJECT (check), "enabled", &source->enabled);
        g_signal_connect (check, "toggled",
                          G_CALLBACK (source_check_toggled_cb), data);
        gtk_box_pack_start_defaults (GTK_BOX (data->sources_box), check);
    }
    gtk_widget_show_all (data->sources_box);

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
        SyncevoReportArray *session_report = (SyncevoReportArray*)g_ptr_array_index (reports, 0);
        syncevo_report_array_get (session_report, &data->last_sync, NULL);
    }
    refresh_last_synced_label (data);

    g_ptr_array_foreach (reports, (GFunc)syncevo_report_array_free, NULL);
    g_ptr_array_free (reports, TRUE);

}

static void
get_server_config_cb (SyncevoService *service, GPtrArray *options, GError *error, app_data *data)
{
    if (error) {
        /* don't warn if server has disappeared -- probably just command line use */
        if (error->code != SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER) {
            add_error_info (data, 
                            "Failed to get server configuration from SyncEvolution", 
                            error->message);
            set_app_state (data, SYNC_UI_STATE_NO_SERVER);
        } else {
            set_app_state (data, SYNC_UI_STATE_SERVER_FAILURE);
        }
        g_error_free (error);
        return;
    }

    g_ptr_array_foreach (options, (GFunc)add_server_option, data->current_service);
    ensure_default_sources_exist (data->current_service);
    
    update_service_ui (data);
    set_app_state (data, SYNC_UI_STATE_SERVER_OK);

    /* get last sync report (for last sync time) */
    syncevo_service_get_sync_reports_async (service, data->current_service->name, 1,
                                            (SyncevoGetSyncReportsCb)get_sync_reports_cb,
                                            data);

    g_ptr_array_foreach (options, (GFunc)syncevo_option_free, NULL);
    g_ptr_array_free (options, TRUE);
}

static void
show_settings_window (app_data *data, server_config *config)
{
    GList *l;
    GtkWidget *label, *entry;
    int i = 0;

    gtk_container_foreach (GTK_CONTAINER (data->server_settings_table),
                           (GtkCallback)remove_child,
                           data->server_settings_table);
    gtk_table_resize (GTK_TABLE (data->server_settings_table), 
                      2, g_list_length (config->source_configs) + 1);

    gtk_entry_set_text (GTK_ENTRY (data->service_name_entry), 
                        config->name ? config->name : "");
    g_object_set_data (G_OBJECT (data->service_name_entry), "value", &config->name);
    if (config->name) {
        gtk_frame_set_label (GTK_FRAME (data->service_settings_frame), config->name);
        gtk_widget_hide (data->service_name_label);
        gtk_widget_hide (data->service_name_entry);
    } else {
        gtk_frame_set_label (GTK_FRAME (data->service_settings_frame), "New service");
        gtk_widget_show (data->service_name_label);
        gtk_widget_show (data->service_name_entry);
    }

    if (config->web_url) {
        gtk_link_button_set_uri (GTK_LINK_BUTTON (data->service_link), 
                                 config->web_url);
        gtk_widget_show (data->service_link);
    } else {
        gtk_widget_hide (data->service_link);
    }

    gtk_expander_set_expanded (GTK_EXPANDER (data->server_settings_expander), 
                               !config->from_template);
    if (config->from_template) {
        gtk_widget_show (GTK_WIDGET (data->reset_server_btn));
        gtk_widget_hide (GTK_WIDGET (data->delete_service_btn));
    } else {
        gtk_widget_hide (GTK_WIDGET (data->reset_server_btn));
        if (config->name) {
            gtk_widget_show (GTK_WIDGET (data->delete_service_btn));
        } else {
            gtk_widget_hide (GTK_WIDGET (data->delete_service_btn));
        }
    }

    gtk_entry_set_text (GTK_ENTRY (data->username_entry), 
                        config->username ? config->username : "");
    g_object_set_data (G_OBJECT (data->username_entry), "value", &config->username);

    gtk_entry_set_text (GTK_ENTRY (data->password_entry),
                        config->password ? config->password : "");
    g_object_set_data (G_OBJECT (data->password_entry), "value", &config->password);

    label = gtk_label_new ("Server URL");
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_table_attach (GTK_TABLE (data->server_settings_table), label,
                      0, 1, i, i + 1, GTK_FILL, GTK_EXPAND, 0, 0);

    entry = gtk_entry_new_with_max_length (100);
    gtk_entry_set_text (GTK_ENTRY (entry), 
                        config->base_url ? config->base_url : "");
    g_object_set_data (G_OBJECT (entry), "value", &config->base_url);
    gtk_table_attach_defaults (GTK_TABLE (data->server_settings_table), entry,
                               1, 2, i, i + 1);

    for (l = config->source_configs; l; l = l->next) {
        source_config *source = (source_config*)l->data;
        char *str;
        i++;
        
        str = g_strdup_printf ("%s URI", source->name);
        label = gtk_label_new (str);
        g_free (str);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_table_attach (GTK_TABLE (data->server_settings_table), label,
                          0, 1, i, i + 1, GTK_FILL, GTK_EXPAND, 0, 0);

        entry = gtk_entry_new_with_max_length (100);
        gtk_entry_set_text (GTK_ENTRY (entry), 
                            source->uri ? source->uri : "");
        g_object_set_data (G_OBJECT (entry), "value", &source->uri);
        g_object_set_data (G_OBJECT (entry), "enabled", &source->enabled);
        gtk_table_attach_defaults (GTK_TABLE (data->server_settings_table), entry,
                                   1, 2, i, i + 1);
    }
    gtk_widget_show_all (data->server_settings_table);

    g_object_set_data (G_OBJECT (data->service_settings_win), "server", config);

    gtk_window_present (GTK_WINDOW (data->service_settings_win));
}

static void
ensure_default_sources_exist(server_config *server)
{
    server_config_get_source_config (server, "addressbook");
    server_config_get_source_config (server, "calendar");
    server_config_get_source_config (server, "memo");
    server_config_get_source_config (server, "todo");
}

static void
setup_service_clicked (GtkButton *btn, app_data *data)
{
    SyncevoServer *server;
    server_data *serv_data;
    const char *name;

    server = g_object_get_data (G_OBJECT (btn), "server");
    syncevo_server_get (server, &name, NULL, NULL);

    serv_data = g_slice_new0 (server_data);
    serv_data->data = data;
    serv_data->server_name = g_strdup (name);

    gtk_window_set_transient_for (GTK_WINDOW (data->service_settings_win), 
                                  GTK_WINDOW (data->services_win));

    syncevo_service_get_server_config_async (data->service, 
                                             (char*)serv_data->server_name,
                                             (SyncevoGetServerConfigCb)get_server_config_for_template_cb, 
                                             serv_data);
}

static void
setup_new_service_clicked (GtkButton *btn, app_data *data)
{
    server_data *serv_data;
    SyncevoOption *option;
    
    serv_data = g_slice_new0 (server_data);
    serv_data->data = data;

    /* syncevolution defaults are not empty, override ... */
    serv_data->options_override = g_ptr_array_new ();
    option = syncevo_option_new (NULL, "syncURL", NULL);
    g_ptr_array_add (serv_data->options_override, option);
    option = syncevo_option_new (NULL, "webURL", NULL);
    g_ptr_array_add (serv_data->options_override, option);
    option = syncevo_option_new (NULL, "fromTemplate", "no");
    g_ptr_array_add (serv_data->options_override, option);
    option = syncevo_option_new ("memo", "uri", NULL);
    g_ptr_array_add (serv_data->options_override, option);
    option = syncevo_option_new ("todo", "uri", NULL);
    g_ptr_array_add (serv_data->options_override, option);
    option = syncevo_option_new ("addressbook", "uri", NULL);
    g_ptr_array_add (serv_data->options_override, option);
    option = syncevo_option_new ("calendar", "uri", NULL);
    g_ptr_array_add (serv_data->options_override, option);

    gtk_window_set_transient_for (GTK_WINDOW (data->service_settings_win), 
                                  GTK_WINDOW (data->services_win));

    syncevo_service_get_server_config_async (data->service, 
                                             "default",
                                             (SyncevoGetServerConfigCb)get_server_config_for_template_cb, 
                                             serv_data);
}

enum ServerCols {
    COL_ICON = 0,
    COL_NAME,
    COL_LINK,
    COL_BUTTON,

    NR_SERVER_COLS
};

static void
add_server_to_table (GtkTable *table, int row, SyncevoServer *server, app_data *data)
{
    GtkWidget *label, *box, *link, *btn;
    const char *name, *url, *icon;
    icon_data *icondata;
    
    syncevo_server_get (server, &name, &url, &icon);
    
    box = gtk_hbox_new (FALSE, 0);
    gtk_widget_set_size_request (box, SYNC_UI_LIST_ICON_SIZE, SYNC_UI_LIST_ICON_SIZE);
    gtk_table_attach (table, box, COL_ICON, COL_ICON + 1, row, row+1,
                      GTK_SHRINK|GTK_FILL, GTK_EXPAND|GTK_FILL, 5, 0);
    icondata = g_slice_new (icon_data);
    icondata->icon_box = GTK_BOX (box);
    icondata->icon_size = SYNC_UI_LIST_ICON_SIZE;
    load_icon (icon, icondata);

    label = gtk_label_new (name);
    if (data->current_service && data->current_service->name &&
        strcmp (name, data->current_service->name) == 0) {
        char *str = g_strdup_printf ("<b>%s</b>", name);
        gtk_label_set_markup (GTK_LABEL (label), str);
        g_free (str);
    }

    gtk_widget_set_size_request (label, SYNC_UI_LIST_BTN_WIDTH, -1);
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_table_attach (table, label, COL_NAME, COL_NAME + 1, row, row+1,
                      GTK_SHRINK|GTK_FILL, GTK_EXPAND|GTK_FILL, 5, 0);

    box = gtk_hbox_new (FALSE, 0);
    gtk_table_attach (table, box, COL_LINK, COL_LINK + 1, row, row+1,
                      GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 5, 0);
    if (url && strlen (url) > 0) {
        link = gtk_link_button_new_with_label (url, "Launch website");
        g_signal_connect (link, "clicked", 
                          G_CALLBACK (show_link_button_url), NULL);
        gtk_box_pack_start (GTK_BOX (box), link, FALSE, FALSE, 0);
    }

    btn = gtk_button_new_with_label ("Setup and use");
    gtk_widget_set_size_request (btn, SYNC_UI_LIST_BTN_WIDTH, -1);
    g_signal_connect (btn, "clicked",
                      G_CALLBACK (setup_service_clicked), data);
    gtk_table_attach (table, btn, COL_BUTTON, COL_BUTTON + 1, row, row+1,
                      GTK_SHRINK|GTK_FILL, GTK_EXPAND|GTK_FILL, 5, 0);

    g_object_set_data_full (G_OBJECT (btn), "server", server, 
                            (GDestroyNotify)syncevo_server_free);
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

    syncevo_server_get (server, &name, NULL, NULL);

    for (i = 0; i < array->len; i++) {
        const char *n;
        SyncevoServer *s = (SyncevoServer*)g_ptr_array_index (array, i);
        
        syncevo_server_get (s, &n, NULL, NULL);
        if (strcmp (name, n) == 0)
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
    int i, k = 0;
    app_data *data = templ_data->data;

    if (error) {
        gtk_widget_hide (data->services_win);
        show_error_dialog (GTK_WINDOW (data->sync_win), 
                           "Failed to get list of manually setup services from SyncEvolution");
        g_warning ("Failed to get list of manually setup services from SyncEvolution: %s", 
                   error->message);
        g_error_free (error);
        return;
    }

    gtk_table_resize (GTK_TABLE (data->manual_services_table), 2, 4);
    for (i = 0; i < servers->len; i++) {
        SyncevoServer *server = (SyncevoServer*)g_ptr_array_index (servers, i);
        
        /* make sure server is not added as template already */
        if (!server_array_contains (templ_data->templates, server)) {
            add_server_to_table (GTK_TABLE (data->manual_services_table), k++,
                                 server, data);
        }
    }
    
    if (k > 0) {
        gtk_widget_show_all (data->manual_services_scrolled);
    } else {
        gtk_widget_hide (data->manual_services_scrolled);
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
        show_error_dialog (GTK_WINDOW (data->sync_win), 
                           "Failed to get list of supported services from SyncEvolution");
        g_warning ("Failed to get list of supported services from SyncEvolution: %s", 
                   error->message);
        g_error_free (error);
        gtk_widget_hide (data->services_win);
        return;
    }

    gtk_table_resize (GTK_TABLE (data->services_table), 
                      templates->len, 4);

    for (i = 0; i < templates->len; i++) {
        add_server_to_table (GTK_TABLE (data->services_table), i, 
                             (SyncevoServer*)g_ptr_array_index (templates, i),
                             data);
    }
    gtk_widget_show_all (data->services_table);

    temps_data = g_slice_new0 (templates_data);
    temps_data->data = data;
    temps_data->templates = templates;
    syncevo_service_get_servers_async (data->service,
                                       (SyncevoGetServerConfigCb)get_servers_cb,
                                       temps_data);
}


static void show_services_window (app_data *data)
{
    gtk_container_foreach (GTK_CONTAINER (data->services_table),
                           (GtkCallback)remove_child,
                           data->services_table);
    gtk_container_foreach (GTK_CONTAINER (data->manual_services_table),
                           (GtkCallback)remove_child,
                           data->manual_services_table);

    syncevo_service_get_templates_async (data->service,
                                         (SyncevoGetServerConfigCb)get_templates_cb,
                                         data);
    gtk_window_present (GTK_WINDOW (data->services_win));
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

    server_config_free (data->current_service);
    if (!server) {
        data->current_service = NULL; 
        set_app_state (data, SYNC_UI_STATE_NO_SERVER);
    } else {
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
        g_debug ("TODO: statistics for '%s':", p->name);
        g_debug ("      data: TX: %d B, RX %d B", p->bytes_uploaded, p->bytes_downloaded);
        g_debug ("      sent to server: %d new, %d updated, %d deleted", p->added_remote, p->modified_remote, p->deleted_remote);
        g_debug ("      received from server: %d new, %d updated, %d deleted", p->added_local, p->modified_local, p->deleted_local);
    }
}

static source_progress*
find_source_progress (GList *source_progresses, char *name)
{
    GList *list;

    for (list = source_progresses; list; list = list->next) {
        if (strcmp (((source_progress*)list->data)->name, name) == 0) {
            return (source_progress*)list->data;
        }
    }
    return NULL;
}

static void
sync_progress_cb (SyncevoService *service,
                  char *server,
                  char *source,
                  int type,
                  int extra1, int extra2, int extra3,
                  app_data *data)
{
    static source_progress *source_prog;
    char *msg;

    switch(type) {
    case -1:
        /* syncevolution finished sync */
        set_sync_progress (data, 1.0 , "Sync finished");
        set_app_state (data, SYNC_UI_STATE_SERVER_OK);

        if (extra1 != 0) {
            /* any errors should have been shown in earlier progress messages...
               not alerting user */
            g_warning ("Syncevolution sync returned error: %d", extra1);
        } else {
            GTimeVal val;
            g_get_current_time (&val);
            data->last_sync = val.tv_sec;
        }
        refresh_last_synced_label (data);
        refresh_statistics (data);

        break;
    case PEV_SESSIONSTART:
        /* double check we're in correct state*/
        set_app_state (data, SYNC_UI_STATE_SYNCING);
        set_sync_progress (data, sync_progress_session_start, NULL);
        break;
    case PEV_SESSIONEND:
        /* NOTE extra1 can be error here */
        set_sync_progress (data, sync_progress_sync_end, "Ending sync");
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
        /* find the right source (try last used one first) */
        if (strcmp (source_prog->name, source) != 0) {
            source_prog = find_source_progress (data->source_progresses, source);
            if (!source_prog) {
                g_warning ("Prepare: No alert received for source '%s'", source);
                return;
            }
        }
        source_prog->prepare_current = CLAMP (extra1, 0, extra2);
        source_prog->prepare_total = extra2;

        msg = g_strdup_printf ("Preparing '%s'", source);
        calc_and_update_progress(data, msg);
        g_free (msg);
        break;

    case PEV_ITEMSENT:

        /* TODO: update the stats table as well */

        /* find the right source (try last used one first) */
        if (strcmp (source_prog->name, source) != 0) {
            source_prog = find_source_progress (data->source_progresses, source);
            if (!source_prog) {
                g_warning ("Sent: No alert received for source '%s'", source);
                return;
            }
        }
        source_prog->send_current = CLAMP (extra1, 0, extra2);
        source_prog->send_total = extra2;

        msg = g_strdup_printf ("Sending '%s'", source);
        calc_and_update_progress (data, msg);
        g_free (msg);
        break;

    case PEV_ITEMRECEIVED:

        /* TODO: update the stats table as well */

        /* find the right source (try last used one first) */
        if (strcmp (source_prog->name, source) != 0) {
            source_prog = find_source_progress (data->source_progresses, source);
            if (!source_prog) {
                g_warning ("Received: No alert received for source '%s'", source);
                return;
            }
        }
        source_prog->receive_current = CLAMP (extra1, 0, extra2);
        source_prog->receive_total = extra2;

        msg = g_strdup_printf ("Receiving '%s'", source);
        calc_and_update_progress (data, msg);
        g_free (msg);
        break;

    case PEV_SYNCEND:
        msg = NULL;
        switch (extra1) {
        case 0:
        case LOCERR_USERABORT:
        case LOCERR_USERSUSPEND:
            break;
        case DB_Unauthorized:
            msg = g_strdup_printf ("%s: Not authorized", source);
            break;
        case DB_Forbidden:
            msg = g_strdup_printf ("%s: Forbidden", source);
            break;
        case DB_NotFound:
            msg = g_strdup_printf ("%s: Not found", source);
            break;
        case LOCERR_PROCESSMSG:
            /* TODO identify item somehow */
            msg = g_strdup_printf ("%s: Failed to process SyncML", source);
            break;
        case LOCERR_AUTHFAIL:
            msg = g_strdup_printf ("%s: Server authorization failed", source);
            break;
        case LOCERR_CFGPARSE:
            msg = g_strdup_printf ("%s: Failed to parse config file", source);
            break;
        case LOCERR_CFGREAD:
            msg = g_strdup_printf ("%s: Failed to read config file", source);
            break;
        case LOCERR_NOCFG:
            msg = g_strdup_printf ("%s: No configuration found", source);
            break;
        case LOCERR_NOCFGFILE:
            msg = g_strdup_printf ("%s: No config file found", source);
            break;
        case LOCERR_BADCONTENT:
            msg = g_strdup_printf ("%s: Server sent bad content", source);
            break;
        case LOCERR_TIMEOUT:
            msg = g_strdup_printf ("%s: Connection timed out", source);
            break;
        case LOCERR_CERT_EXPIRED:
            msg = g_strdup_printf ("%s: Connection certificate has expired", source);
            break;
        case LOCERR_CERT_INVALID:
            msg = g_strdup_printf ("%s: Connection certificate is invalid", source);
            break;
        case LOCERR_CONN:
        case LOCERR_NOCONN:
            msg = g_strdup_printf ("%s: Connection failed", source);
            break;
        case LOCERR_BADURL:
            msg = g_strdup_printf ("%s: URL is bad", source);
            break;
        case LOCERR_SRVNOTFOUND:
            msg = g_strdup_printf ("%s: Server not found", source);
            break;
        default:
            msg = g_strdup_printf ("%s: Error %d", source, extra1);
            break;
        }
        if (msg) {
            add_error_info (data, msg, NULL);
            g_free (msg);
        }
        break;
    case PEV_DSSTATS_L:
        source_prog = find_source_progress (data->source_progresses, source);
        if (!source_prog)
            return;

        source_prog->added_local = extra1;
        source_prog->modified_local = extra2;
        source_prog->deleted_local = extra3;
        break;
    case PEV_DSSTATS_R:
        source_prog = find_source_progress (data->source_progresses, source);
        if (!source_prog)
            return;

        source_prog->added_remote = extra1;
        source_prog->modified_remote = extra2;
        source_prog->deleted_remote = extra3;
        break;
    case PEV_DSSTATS_E:
        source_prog = find_source_progress (data->source_progresses, source);
        if (!source_prog)
            return;

        if (extra1 > 0 || extra2 > 0) {

            /* TODO show to user */

            g_debug ("Rejected for '%s':", source);
            g_debug ("      %d locally rejected", extra1);
            g_debug ("      %d remotely rejected", extra2);
        }
        break;
    case PEV_DSSTATS_D:
        source_prog = find_source_progress (data->source_progresses, source);
        if (!source_prog)
            return;

        source_prog->bytes_uploaded = extra1;
        source_prog->bytes_downloaded = extra2;
        break;
    default:
        ;
    }
}

int 
main (int argc, char *argv[]) {
    app_data *data;

    gtk_init (&argc, &argv);
    bindtextdomain (GETTEXT_PACKAGE, SYNCEVOLUTION_LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    data = g_slice_new0 (app_data);
    if (!init_ui (data)) {
        return (1);
    }

    data->service = syncevo_service_get_default();
    g_signal_connect (data->service, "progress", 
                      G_CALLBACK (sync_progress_cb), data);
    init_configuration (data);

    gtk_window_present (GTK_WINDOW (data->sync_win));

    gtk_main();
    return 0;
}
