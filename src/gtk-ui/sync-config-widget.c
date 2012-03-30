#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#ifdef USE_MOBLIN_UX
#ifdef MX_GTK_0_99_1
#include <mx-gtk/mx-gtk.h>
#else
#include <mx/mx-gtk.h>
#endif
#endif

#include "sync-ui.h"
#include "sync-config-widget.h"

/* local copy of GtkInfoBar, used when GTK+ < 2.18 */
#include "gtkinfobar.h"


#define INDICATOR_SIZE 16
#define CHILD_PADDING 3

G_DEFINE_TYPE (SyncConfigWidget, sync_config_widget, GTK_TYPE_CONTAINER)


typedef struct source_widgets {
    char *name;

    GtkWidget *label;
    GtkWidget *entry;
    GtkWidget *check;

    GtkWidget *source_toggle_label;

    guint count;
} source_widgets;

enum
{
  PROP_0,

  PROP_SERVER,
  PROP_NAME,
  PROP_CONFIG,
  PROP_CURRENT,
  PROP_HAS_TEMPLATE,
  PROP_CONFIGURED,
  PROP_CURRENT_SERVICE_NAME,
  PROP_EXPANDED,
};

enum {
	SIGNAL_CHANGED,
	LAST_SIGNAL
};
static guint32 signals[LAST_SIGNAL] = {0, };

typedef struct save_config_data {
    SyncConfigWidget *widget;
    gboolean delete;
    gboolean temporary;
    source_widgets *widgets;
    char *basename;
} save_config_data;

static void start_session_for_config_write_cb (SyncevoServer *server, char *path, GError *error, save_config_data *data);
static void sync_config_widget_update_label (SyncConfigWidget *self);
static void sync_config_widget_set_name (SyncConfigWidget *self, const char *name);

static void
remove_child (GtkWidget *widget, GtkContainer *container)
{
    gtk_container_remove (container, widget);
}

const char*
get_service_description (const char *service)
{
    if (!service)
        return NULL;

    /* TRANSLATORS: Descriptions for specific services, shown in service
     * configuration form */
    if (strcmp (service, "ScheduleWorld") == 0) {
        return _("ScheduleWorld enables you to keep your contacts, events, "
                 "tasks, and notes in sync.");
    }else if (strcmp (service, "Google") == 0) {
        return _("Google Sync can back up and synchronize your contacts "
                 "with your Gmail contacts.");
    }else if (strcmp (service, "Funambol") == 0) {
        /* TRANSLATORS: Please include the word "demo" (or the equivalent in
           your language): Funambol is going to be a 90 day demo service
           in the future */
        return _("Back up your contacts and calendar. Sync with a single "
                 "click, anytime, anywhere (DEMO).");
    }else if (strcmp (service, "Mobical") == 0) {
        return _("Mobical Backup and Restore service allows you to securely "
                 "back up your personal mobile data for free.");
    }else if (strcmp (service, "ZYB") == 0) {
        return _("ZYB is a simple way for people to store and share mobile "
                 "information online.");
    }else if (strcmp (service, "Memotoo") == 0) {
        return _("Memotoo lets you access your personal data from any "
                 "computer connected to the Internet.");
    }

    return NULL;
}

static void
update_source_uri (char *name,
                   GHashTable *source_configuration,
                   SyncConfigWidget *self)
{
    const char *uri;
    source_widgets *widgets;

    widgets = (source_widgets*)g_hash_table_lookup (self->sources, name);
    if (!widgets) {
        return;
    }

    uri = gtk_entry_get_text (GTK_ENTRY (widgets->entry));
    g_hash_table_insert (source_configuration, g_strdup ("uri"), g_strdup (uri));
}

static source_widgets *
source_widgets_ref (source_widgets *widgets)
{
    if (widgets) {
        widgets->count++;
    }
    return widgets;
}

static void
source_widgets_unref (source_widgets *widgets)
{
    if (widgets) {
        widgets->count--;
        if (widgets->count == 0)
            g_slice_free (source_widgets, widgets);
    }
}

static void
check_source_cb (SyncevoSession *session,
                 GError *error,
                 source_widgets *widgets)
{
    gboolean show = TRUE;

    if (error) {
        if(error->code == DBUS_GERROR_REMOTE_EXCEPTION &&
           dbus_g_error_has_name (error, SYNCEVO_DBUS_ERROR_SOURCE_UNUSABLE)) {
            show = FALSE;
        } else {
            g_warning ("CheckSource failed: %s", error->message);
            /* non-fatal, ignore in UI */
        }
        g_error_free (error);
    }

    if (widgets->count > 1) {
        if (show) {
            /* NOTE: with the new two sources per row layout not showing things
             * may look weird in some cases... the layout should really only be
             * done at this point  */
            gtk_widget_show (widgets->source_toggle_label);
            gtk_widget_show (widgets->label);
            gtk_widget_show (widgets->entry);
            gtk_widget_show (widgets->check);
        } else {
            /* next save should disable this source */
            toggle_set_active (widgets->check, FALSE);
        }
    }
    source_widgets_unref (widgets);
    g_object_unref (session);
}

static void
set_config_cb (SyncevoSession *session,
               GError *error,
               save_config_data *data)
{
    if (error) {
        g_warning ("Error in Session.SetConfig: %s", error->message);
        g_error_free (error);
        g_object_unref (session);
        show_error_dialog (GTK_WIDGET (data->widget),
                           _("Sorry, failed to save the configuration"));
        return;
    }

    if (data->temporary) {
        syncevo_session_check_source (session,
                                      data->widgets->name,
                                      (SyncevoSessionGenericCb)check_source_cb,
                                      data->widgets);
    } else {
        data->widget->configured = TRUE;
        g_signal_emit (data->widget, signals[SIGNAL_CHANGED], 0);
        g_object_unref (session);
    }

}

static void
get_config_for_overwrite_prevention_cb (SyncevoSession *session,
                                        SyncevoConfig *config,
                                        GError *error,
                                        save_config_data *data)
{
    static int index = 0;
    char *name;

    if (error) {
        index = 0;
        if (error->code == DBUS_GERROR_REMOTE_EXCEPTION &&
            dbus_g_error_has_name (error, SYNCEVO_DBUS_ERROR_NO_SUCH_CONFIG)) {
            /* Config does not exist (as expected), we can now save */
            syncevo_session_set_config (session,
                                        data->temporary,
                                        data->temporary,
                                        data->widget->config,
                                        (SyncevoSessionGenericCb)set_config_cb,
                                        data);
            return;
        }
        g_warning ("Unexpected error in Session.GetConfig: %s", error->message);
        g_error_free (error);
        g_object_unref (session);
        return;
    }

    /* Config exists when we are trying to create a new config...
     * Need to start a new session with another name */    
    g_object_unref (session);
    name = g_strdup_printf ("%s__%d", data->basename, ++index);
    sync_config_widget_set_name (data->widget, name);
    g_free (name);

    syncevo_server_start_no_sync_session (data->widget->server,
                                  data->widget->config_name,
                                  (SyncevoServerStartSessionCb)start_session_for_config_write_cb,
                                  data);
}

static void
save_config (save_config_data *data,
             SyncevoSession *session)
{   
    SyncConfigWidget *w = data->widget;

    if (data->delete) {
        syncevo_config_free (w->config);
        w->config = g_hash_table_new (g_str_hash, g_str_equal);
    }

    /* if this is a client peer (a device) and not configured, we
     * need to test that we aren't overwriting existing
     * configs */
    /* TODO: This might be a good thing to do for any configurations.*/
    if (peer_is_client (w->config) &&
        !w->configured && !data->temporary) {

        syncevo_session_get_config (session,
                                    FALSE,
                                    (SyncevoSessionGetConfigCb)get_config_for_overwrite_prevention_cb,
                                    data);
    } else {
        syncevo_session_set_config (session,
                                    data->temporary,
                                    data->temporary,
                                    data->widget->config,
                                    (SyncevoSessionGenericCb)set_config_cb,
                                    data);
    }
}

static void
status_changed_for_config_write_cb (SyncevoSession *session,
                                    SyncevoSessionStatus status,
                                    guint error_code,
                                    SyncevoSourceStatuses *source_statuses,
                                    save_config_data *data)
{
    if (status == SYNCEVO_STATUS_IDLE) {
        save_config (data, session);
    }
}

static void
get_status_for_config_write_cb (SyncevoSession *session,
                                SyncevoSessionStatus status,
                                guint error_code,
                                SyncevoSourceStatuses *source_statuses,
                                GError *error,
                                save_config_data *data)
{
    if (error) {
        g_warning ("Error in Session.GetStatus: %s", error->message);
        g_error_free (error);
        g_object_unref (session);
        /* TODO show in UI: save failed in service list */
        return;
    }

    syncevo_source_statuses_free (source_statuses);

    if (status == SYNCEVO_STATUS_IDLE) {
        save_config (data, session);
    }
}


static void
start_session_for_config_write_cb (SyncevoServer *server,
                                   char *path,
                                   GError *error,
                                   save_config_data *data)
{
    SyncevoSession *session;

    if (error) {
        g_warning ("Error in Server.StartSession: %s", error->message);
        g_error_free (error);
        /* TODO show in UI: save failed in service list */
        return;
    }

    session = syncevo_session_new (path);

    /* we want to know about status changes to our session */
    g_signal_connect (session, "status-changed",
                      G_CALLBACK (status_changed_for_config_write_cb), data);
    syncevo_session_get_status (session,
                                (SyncevoSessionGetStatusCb)get_status_for_config_write_cb,
                                data);
}

static void
stop_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    save_config_data *data;

    if (!self->config) {
        return;
    }

    syncevo_config_set_value (self->config, NULL, "defaultPeer", "");
    sync_config_widget_set_current (self, FALSE);

    data = g_slice_new (save_config_data);
    data->widget = self;
    data->delete = FALSE;
    data->temporary = FALSE;
    syncevo_server_start_no_sync_session (self->server,
                                  self->config_name,
                                  (SyncevoServerStartSessionCb)start_session_for_config_write_cb,
                                  data);
}

static void
use_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    save_config_data *data;
    const char *username, *password, *sync_url, *pretty_name;
    char *real_url, *device;
    gboolean send, receive;
    SyncevoSyncMode mode;

    if (!self->config) {
        return;
    }

    if (!self->config_name || strlen (self->config_name) == 0) {
        g_free (self->config_name);
        self->config_name = g_strdup (gtk_entry_get_text (GTK_ENTRY (self->entry)));
    }

    if (self->mode_changed) {
        GHashTableIter iter;
        source_widgets *widgets;
        char *name;
        gboolean client = peer_is_client (self->config);

        send = toggle_get_active (self->send_check);
        receive = toggle_get_active (self->receive_check);

        if (send && receive) {
            mode = SYNCEVO_SYNC_TWO_WAY;
        } else if (send) {
            mode = client ?
                SYNCEVO_SYNC_ONE_WAY_FROM_SERVER :
                SYNCEVO_SYNC_ONE_WAY_FROM_CLIENT;
        } else if (receive) {
            mode = client ?
                SYNCEVO_SYNC_ONE_WAY_FROM_CLIENT :
                SYNCEVO_SYNC_ONE_WAY_FROM_SERVER;
        } else {
            mode = SYNCEVO_SYNC_NONE;
        }

        g_hash_table_iter_init (&iter, self->sources);
        while (g_hash_table_iter_next (&iter, (gpointer)&name, (gpointer)&widgets)) {
            const char *mode_str;
            gboolean active;

            active = toggle_get_active (widgets->check) &&
                     GTK_WIDGET_SENSITIVE (widgets->check);
            if (active) {
                mode_str = syncevo_sync_mode_to_string (mode);
            } else {
                mode_str = "none";
            }
            syncevo_config_set_value (self->config, name, "sync", mode_str);
        }
    }

    username = gtk_entry_get_text (GTK_ENTRY (self->username_entry));
    syncevo_config_set_value (self->config, NULL, "username", username);

    sync_url = gtk_entry_get_text (GTK_ENTRY (self->baseurl_entry));
    /* make a wild guess if no scheme in url */
    if (strstr (sync_url, "://") == NULL) {
        real_url = g_strdup_printf ("http://%s", sync_url);
    } else {
        real_url = g_strdup (sync_url);
    }
    syncevo_config_set_value (self->config, NULL, "syncURL", real_url);

    password = gtk_entry_get_text (GTK_ENTRY (self->password_entry));
    syncevo_config_set_value (self->config, NULL, "password", password);

    syncevo_config_get_value (self->config, NULL, "deviceName", &device);
    if (!device || strlen (device) == 0) {
        if (!self->config_name || strlen (self->config_name) == 0 ||
            !sync_url || strlen (sync_url) == 0) {
            show_error_dialog (GTK_WIDGET (self), 
                               _("Service must have a name and server URL"));
            return;
        }
    }

    syncevo_config_foreach_source (self->config,
                                   (ConfigFunc)update_source_uri,
                                   self);

    pretty_name = gtk_entry_get_text (GTK_ENTRY (self->entry));
    syncevo_config_set_value (self->config, NULL, "PeerName", pretty_name);
    syncevo_config_get_value (self->config, NULL, "PeerName", &self->pretty_name);
    syncevo_config_set_value (self->config, NULL, "defaultPeer", self->config_name);
    sync_config_widget_set_current (self, TRUE);

    data = g_slice_new (save_config_data);
    data->widget = self;
    data->delete = FALSE;
    data->temporary = FALSE;
    data->basename = g_strdup (self->config_name);
    syncevo_server_start_no_sync_session (self->server,
                                  self->config_name,
                                  (SyncevoServerStartSessionCb)start_session_for_config_write_cb,
                                  data);

    g_free (real_url);
}

static void
reset_delete_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    char *msg, *yes, *no;
    save_config_data *data;

    if (!self->config) {
        return;
    }

    if (self->has_template) {
        /*TRANSLATORS: warning dialog text for resetting pre-defined
          services */
        msg = g_strdup_printf
            (_("Do you want to reset the settings for %s? "
               "This will not remove any synced information on either end."),
             self->pretty_name);
        /*TRANSLATORS: buttons in reset-service warning dialog */
        yes = _("Yes, reset");
        no = _("No, keep settings");
    } else {
        /*TRANSLATORS: warning dialog text for deleting user-defined
          services */
        msg = g_strdup_printf
            (_("Do you want to delete the settings for %s? "
               "This will not remove any synced information on either "
               "end but it will remove these settings."),
             self->pretty_name);
        /*TRANSLATORS: buttons in delete-service warning dialog */
        yes = _("Yes, delete");
        no = _("No, keep settings");
    }

    /*TRANSLATORS: decline button in "Reset/delete service" warning dialogs */
    if (!show_confirmation (GTK_WIDGET (self), msg, yes, no)) {
        g_free (msg);
        return;
    }
    g_free (msg);

    if (self->current) {
        sync_config_widget_set_current (self, FALSE);
    }

    data = g_slice_new (save_config_data);
    data->widget = self;
    data->delete = TRUE;
    data->temporary = FALSE;

    syncevo_server_start_no_sync_session (self->server,
                                  self->config_name,
                                  (SyncevoServerStartSessionCb)start_session_for_config_write_cb,
                                  data);
}

static void update_buttons (SyncConfigWidget *self)
{
    if (self->has_template) {
        /* TRANSLATORS: button labels in service configuration form */
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Reset settings"));
    } else {
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Delete settings"));
    }
    if (self->configured) {
        gtk_widget_show (GTK_WIDGET (self->reset_delete_button));
    } else {
        gtk_widget_hide (GTK_WIDGET (self->reset_delete_button));
    }

    if (self->current || !self->current_service_name) {
        gtk_button_set_label (GTK_BUTTON (self->use_button),
                              _("Save and use"));
    } else {
        gtk_button_set_label (GTK_BUTTON (self->use_button),
                              _("Save and replace\ncurrent service"));
    }



    if (self->current && self->config) {
        if (peer_is_client (self->config)) {
            gtk_button_set_label (GTK_BUTTON (self->stop_button),
                                              _("Stop using device"));
        } else {
            gtk_button_set_label (GTK_BUTTON (self->stop_button),
                                              _("Stop using service"));
        }
        gtk_widget_show (self->stop_button);
    } else { 
        gtk_widget_hide (self->stop_button);
    }
}

static void
mode_widget_notify_active_cb (GtkWidget *widget,
                              GParamSpec *pspec,
                              SyncConfigWidget *self)
{
    self->mode_changed = TRUE;
}

static void
source_entry_notify_text_cb (GObject *gobject,
                             GParamSpec *pspec,
                             source_widgets *widgets)
{
    gboolean new_editable, old_editable;
    const char *text;

    text = gtk_entry_get_text (GTK_ENTRY (widgets->entry));
    new_editable = (strlen (text) > 0);
    old_editable = GTK_WIDGET_SENSITIVE (widgets->check);
    if (new_editable != old_editable) {
        gtk_widget_set_sensitive (widgets->check, new_editable);
        toggle_set_active (widgets->check, new_editable);
    }
}

static GtkWidget*
add_toggle_widget (SyncConfigWidget *self,
                   const char *title,
                   gboolean active,
                   guint row, guint col)
{
    GtkWidget *toggle;
    int padding;

    padding = (col == 1) ? 0 : 32;

#ifdef USE_MOBLIN_UX
    GtkWidget *label;

    col = col * 2;
    label = gtk_label_new (title);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request (label, 260, -1);
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_table_attach (GTK_TABLE (self->mode_table), label,
                      col, col + 1, row, row + 1,
                      GTK_FILL, GTK_FILL, 0, 0);
    toggle = mx_gtk_light_switch_new ();
    g_signal_connect_swapped (toggle, "hide",
                              G_CALLBACK (gtk_widget_hide), label);
    g_signal_connect_swapped (toggle, "show",
                              G_CALLBACK (gtk_widget_show), label);
    toggle_set_active (toggle, active);
    g_signal_connect (toggle, "switch-flipped",
                      G_CALLBACK (mode_widget_notify_active_cb), self);
#else
    toggle = gtk_check_button_new_with_label (title);
    gtk_widget_set_size_request (toggle, 260, -1);
    toggle_set_active (toggle, active);
    g_signal_connect (toggle, "notify::active",
                      G_CALLBACK (mode_widget_notify_active_cb), self);
#endif

    gtk_table_attach (GTK_TABLE (self->mode_table), toggle,
                      col + 1, col + 2, row, row + 1,
                      GTK_FILL, GTK_FILL, padding, 0);

    return toggle;
}


/* check if config includes a virtual source that covers the given 
 * source */
static gboolean
virtual_source_exists (SyncevoConfig *config, const char *name)
{
    GHashTableIter iter;
    const char *source_string;
    GHashTable *source_config;

    g_hash_table_iter_init (&iter, config);
    while (g_hash_table_iter_next (&iter,
                                   (gpointer)&source_string,
                                   (gpointer)&source_config)) {
        char **strs;

        if (g_str_has_prefix (source_string, "source/")) {
            const char *uri, *type;
            type = g_hash_table_lookup (source_config, "backend");
            uri = g_hash_table_lookup (source_config, "uri");

            if (!uri || !type || !g_str_has_prefix (type, "virtual:")) {
                /* this source is not defined, or not virtual */
                continue;
            }

            strs = g_strsplit (source_string + 7, "+", 0);
            if (g_strv_length (strs) > 1) {
                int i;

                for (i = 0; strs[i]; i++) {
                    if (g_strcmp0 (name, strs[i]) == 0) {
                        g_strfreev (strs);
                        return TRUE;
                    }
                }
            }
            g_strfreev (strs);
        }
    }

    return FALSE;
}

static void
init_source (char *name,
             GHashTable *source_configuration,
             SyncConfigWidget *self)
{
    char *str, *pretty_name;
    const char *uri, *type;
    guint rows;
    guint row;
    static guint col = 0;
    source_widgets *widgets;
    SyncevoSyncMode mode;
    save_config_data *data;

    type = g_hash_table_lookup (source_configuration, "backend");
    uri = g_hash_table_lookup (source_configuration, "uri");
    if (!type || strlen (type) == 0) {
        return;
    }

    if (g_str_has_prefix (type, "virtual:") && !uri) {
        /* undefined virtual source */
        return;
    }

    if (virtual_source_exists (self->config, name)) {
        return;
    }

    g_object_get (self->mode_table,
                  "n-rows", &rows,
                  NULL);

    if (!self->no_source_toggles && col == 0) {
        col = 1;
        row = rows - 1;
    } else {
        col = 0;
        row = rows;
    }
    self->no_source_toggles = FALSE;

    widgets = g_slice_new0 (source_widgets);
    widgets->name = name;
    widgets->count = 1;
    g_hash_table_insert (self->sources, name, widgets);

    widgets->source_toggle_label = self->source_toggle_label;

    pretty_name = get_pretty_source_name (name);
    mode = syncevo_sync_mode_from_string
        (g_hash_table_lookup (source_configuration, "sync"));

    widgets->check = add_toggle_widget (self,
                                        pretty_name,
                                        (mode > SYNCEVO_SYNC_NONE),
                                        row, col);

    /* TRANSLATORS: label for an entry in service configuration form.
     * Placeholder is a source  name.
     * Example: "Appointments URI" */
    str = g_strdup_printf (_("%s URI"), pretty_name);
    widgets->label = gtk_label_new (str);
    g_free (str);
    g_free (pretty_name);

    g_object_get (self->server_settings_table,
                  "n-rows", &row,
                  NULL);

    gtk_misc_set_alignment (GTK_MISC (widgets->label), 0.0, 0.5);
    gtk_table_attach (GTK_TABLE (self->server_settings_table), widgets->label,
                      0, 1, row, row + 1, GTK_FILL, GTK_EXPAND, 0, 0);

    widgets->entry = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (widgets->entry), 99);
    gtk_entry_set_width_chars (GTK_ENTRY (widgets->entry), 80);
    if (uri) {
        gtk_entry_set_text (GTK_ENTRY (widgets->entry), uri);
    }
    gtk_table_attach_defaults (GTK_TABLE (self->server_settings_table),
                               widgets->entry,
                               1, 2, row, row + 1);
    g_signal_connect (widgets->entry, "notify::text",
                      G_CALLBACK (source_entry_notify_text_cb), widgets);

    gtk_widget_set_sensitive (widgets->check,
                              uri && strlen (uri) > 0);

    /* start a session so we save a temporary config so we can do
     * CheckSource, and show the source-related widgets if the 
     * source is available */
    data = g_slice_new (save_config_data);
    data->widget = self;
    data->delete = FALSE;
    data->temporary = TRUE;
    data->widgets = source_widgets_ref (widgets);

    syncevo_server_start_no_sync_session (self->server,
                                  self->config_name,
                                  (SyncevoServerStartSessionCb)start_session_for_config_write_cb,
                                  data);
}

static void
get_common_mode (char *name,
                 GHashTable *source_configuration,
                 SyncevoSyncMode *common_mode)
{
    SyncevoSyncMode mode;
    char *mode_str, *type;

    type = g_hash_table_lookup (source_configuration, "backend");
    if (!type || strlen (type) == 0) {
        return;
    }

    mode_str = g_hash_table_lookup (source_configuration, "sync");
    mode = syncevo_sync_mode_from_string (mode_str);

    if (mode == SYNCEVO_SYNC_NONE) {
        return;
    }

    if (*common_mode == SYNCEVO_SYNC_NONE) {
        *common_mode = mode;
    } else if (mode != *common_mode) {
        *common_mode = SYNCEVO_SYNC_UNKNOWN;
    }
}

void
sync_config_widget_expand_id (SyncConfigWidget *self,
                              const char *id)
{
    if (id && self->config) {
        char *sync_url;

        if (syncevo_config_get_value (self->config, NULL,
                                      "syncURL", &sync_url) &&
            strncmp (sync_url, id, strlen (id)) == 0) {

            sync_config_widget_set_expanded (self, TRUE);
        } else if (self->config_name &&
                   g_strcasecmp (self->config_name, id) == 0) {

            sync_config_widget_set_expanded (self, TRUE);
        }
    }
}

static void
sync_config_widget_update_expander (SyncConfigWidget *self)
{
    char *username = "";
    char *password = "";
    char *sync_url = "";
    const char *descr;
    char *str;
    GtkWidget *label, *align;
    SyncevoSyncMode mode = SYNCEVO_SYNC_NONE;
    gboolean send, receive;
    gboolean client;

    gtk_container_foreach (GTK_CONTAINER (self->server_settings_table),
                           (GtkCallback)remove_child,
                           self->server_settings_table);
    gtk_table_resize (GTK_TABLE (self->server_settings_table), 
                      2, 1);
    gtk_container_foreach (GTK_CONTAINER (self->mode_table),
                           (GtkCallback)remove_child,
                           self->mode_table);
    gtk_table_resize (GTK_TABLE (self->mode_table),
                      2, 1);

    client = peer_is_client (self->config);
    if (client) {
        if (!self->device_template_selected) {
            gtk_widget_hide (self->settings_box);
            gtk_widget_show (self->device_selector_box);
            /* temporary solution for device template selection:
             * show list of templates only */
        } else {
            gtk_widget_show (self->settings_box);
            gtk_widget_hide (self->device_selector_box);
            gtk_widget_hide (self->userinfo_table);
            gtk_widget_hide (self->fake_expander);
        }
    } else {
        gtk_widget_show (self->settings_box);
        gtk_widget_hide (self->device_selector_box);
        gtk_widget_show (self->userinfo_table);
        gtk_widget_show (self->fake_expander);
    }

    syncevo_config_foreach_source (self->config,
                                   (ConfigFunc)get_common_mode,
                                   &mode);
    switch (mode) {
    case SYNCEVO_SYNC_TWO_WAY:
        send = receive = TRUE;
        break;
    case SYNCEVO_SYNC_ONE_WAY_FROM_CLIENT:
        if (client) {
            send = FALSE;
            receive = TRUE;
        } else {
            send = TRUE;
            receive = FALSE;
        }
        break;
    case SYNCEVO_SYNC_ONE_WAY_FROM_SERVER:
        if (client) {
            send = TRUE;
            receive = FALSE;
        } else {
            send = FALSE;
            receive = TRUE;
        }
        break;
    default:
        gtk_widget_show (self->complex_config_info_bar);
        send = FALSE;
        receive = FALSE;
    }
    self->mode_changed = FALSE;


    if (self->pretty_name) {
        gtk_entry_set_text (GTK_ENTRY (self->entry), self->pretty_name);
    }
    if (!self->config_name || strlen (self->config_name) == 0) {
        gtk_expander_set_expanded (GTK_EXPANDER (self->expander), TRUE);
    }

    descr = get_service_description (self->config_name);
    if (descr) {
        gtk_label_set_text (GTK_LABEL (self->description_label),
                            get_service_description (self->config_name));
        gtk_widget_show (self->description_label);
    } else {
        gtk_widget_hide (self->description_label);
    }

    update_buttons (self);

    /* TRANSLATORS: toggles in service configuration form, placeholder is service
     * or device name */
    str = g_strdup_printf (_("Send changes to %s"), self->pretty_name);
    self->send_check = add_toggle_widget (self, str, send, 0, 0);
    gtk_widget_show (self->send_check);
    g_free (str);

    str = g_strdup_printf (_("Receive changes from %s"), self->pretty_name);
    self->receive_check = add_toggle_widget (self, str, receive, 0, 1);
    gtk_widget_show (self->receive_check);
    g_free (str);

    align = gtk_alignment_new (0.0, 1.0, 0.0, 0.0);
    gtk_alignment_set_padding (GTK_ALIGNMENT (align), 10, 0, 0, 0);
    gtk_widget_show (align);
    gtk_table_attach (GTK_TABLE (self->mode_table), align,
                      0, 1, 1, 2,
                      GTK_FILL, GTK_FILL, 0, 0);

    self->source_toggle_label = gtk_label_new ("");
    /* TRANSLATORS: Label for the source toggles in configuration form.
       This is a verb, as in "Sync Calendar". */
    gtk_label_set_markup (GTK_LABEL (self->source_toggle_label),
                          _("<b>Sync</b>"));
    gtk_widget_show (self->source_toggle_label);
    gtk_container_add (GTK_CONTAINER (align), self->source_toggle_label);

    syncevo_config_get_value (self->config, NULL, "username", &username);
    syncevo_config_get_value (self->config, NULL, "password", &password);
    syncevo_config_get_value (self->config, NULL, "syncURL", &sync_url);

    if (username) {
        gtk_entry_set_text (GTK_ENTRY (self->username_entry), username);
    }
    if (password) {
        gtk_entry_set_text (GTK_ENTRY (self->password_entry), password);
    }

    // TRANSLATORS: label of a entry in service configuration
    label = gtk_label_new (_("Server address"));
    gtk_misc_set_alignment (GTK_MISC (label), 9.0, 0.5);
    gtk_widget_show (label);
    gtk_table_attach (GTK_TABLE (self->server_settings_table), label,
                      0, 1, 0, 1, GTK_FILL, GTK_EXPAND, 0, 0);

    self->baseurl_entry = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (self->baseurl_entry), 99);
    gtk_entry_set_width_chars (GTK_ENTRY (self->baseurl_entry), 80);
    if (sync_url) {
        gtk_entry_set_text (GTK_ENTRY (self->baseurl_entry), sync_url);
    }
    gtk_widget_show (self->baseurl_entry);

    gtk_table_attach_defaults (GTK_TABLE (self->server_settings_table),
                               self->baseurl_entry,
                               1, 2, 0, 1);

    /* update source widgets */
    if (self->sources) {
        g_hash_table_destroy (self->sources);
    }
    self->sources = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           NULL,
                                           (GDestroyNotify)source_widgets_unref);
    self->no_source_toggles = TRUE;
    syncevo_config_foreach_source (self->config,
                                   (ConfigFunc)init_source,
                                   self);
}

/* only adds config to hashtable and combo */
static void
sync_config_widget_add_config (SyncConfigWidget *self,
                               const char *name,
                               SyncevoConfig *config)
{
    GtkListStore *store;
    GtkTreeIter iter;
    const char *guess_name;
    SyncevoConfig *guess_config;
    int score = 1;
    int guess_score, second_guess_score = -1;
    char *str;

    store = GTK_LIST_STORE (gtk_combo_box_get_model (GTK_COMBO_BOX (self->combo)));
    if (syncevo_config_get_value (config, NULL, "score", &str)) {
        score = (int)strtol (str, NULL, 10);
    }
    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter,
                        0, name,
                        1, config,
                        2, score,
                        -1);

    /* make an educated guess if possible */
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter);
    gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
                        0, &guess_name,
                        1, &guess_config,
                        2, &guess_score,
                        -1);

    if (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter)) {
        gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
                            2, &second_guess_score,
                            -1);
    }

    if (guess_score > 1 && guess_score > second_guess_score) {
        gtk_combo_box_set_active (GTK_COMBO_BOX (self->combo), 0);
        /* TRANSLATORS: explanation before a device template combobox.
         * Placeholder is a device name like 'Nokia N85' or 'Syncevolution
         * Client' */
        str = g_strdup_printf (_("This device looks like it might be a '%s'. "
                                 "If this is not correct, please take a look at "
                                 "the list of supported devices and pick yours "
                                 "if it is listed"), guess_name);
    } else {
        gtk_combo_box_set_active (GTK_COMBO_BOX (self->combo), -1);
        str = g_strdup (_("We don't know what this device is exactly. "
                          "Please take a look at the list of "
                          "supported devices and pick yours if it "
                          "is listed"));
    }
    gtk_label_set_text (GTK_LABEL (self->device_text), str);
    g_free (str);
}

static void
sync_config_widget_update_pretty_name (SyncConfigWidget *self)
{
    self->pretty_name = NULL;

    if (self->config) {
        syncevo_config_get_value (self->config, NULL,
                                  "PeerName", &self->pretty_name);
        if (!self->pretty_name) {
            syncevo_config_get_value (self->config, NULL,
                                      "deviceName", &self->pretty_name);
        }
    }

    if (!self->pretty_name) {
        self->pretty_name = self->config_name;
    }
}

static void
sync_config_widget_set_config (SyncConfigWidget *self,
                               SyncevoConfig *config)
{
    self->config = config;
    sync_config_widget_update_pretty_name (self);
}


static void
setup_service_clicked (GtkButton *btn, SyncConfigWidget *self)
{
    sync_config_widget_set_expanded (self, TRUE);
}

static void
sync_config_widget_set_name (SyncConfigWidget *self,
                             const char *name)
{
    g_free (self->config_name);
    self->config_name = g_strdup (name);
    sync_config_widget_update_pretty_name (self);
}


static void
device_selection_btn_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    GtkTreeIter iter;

    if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (self->combo), &iter)) {
        const char *name;
        SyncevoConfig *config;
        GtkTreeModel *model;

        self->device_template_selected = TRUE;

        model = gtk_combo_box_get_model(GTK_COMBO_BOX (self->combo));
        gtk_tree_model_get (model, &iter, 0, &name, -1 );
        gtk_tree_model_get (model, &iter, 1, &config, -1 );

        sync_config_widget_set_config (self, config);

        sync_config_widget_update_expander (self);
    }
}

static void
server_settings_notify_expand_cb (GtkExpander *expander,
                                  GParamSpec *pspec,
                                  SyncConfigWidget *self)
{
    /* NOTE: expander can be the fake or real one... */
    if (gtk_expander_get_expanded (GTK_EXPANDER (self->fake_expander))) {
        g_signal_handlers_disconnect_by_func (self->fake_expander,
                                              server_settings_notify_expand_cb,
                                              self);

        gtk_widget_hide (self->fake_expander);
        gtk_expander_set_expanded (GTK_EXPANDER (self->fake_expander), FALSE);
        gtk_expander_set_expanded (GTK_EXPANDER (self->expander), TRUE);
        gtk_widget_show (self->expander);

        g_signal_connect (self->expander, "notify::expanded",
                      G_CALLBACK (server_settings_notify_expand_cb), self);
    } else {
        g_signal_handlers_disconnect_by_func (self->expander,
                                              server_settings_notify_expand_cb,
                                              self);

        gtk_widget_hide (self->expander);
        gtk_widget_show (self->fake_expander);

        g_signal_connect (self->fake_expander, "notify::expanded",
                      G_CALLBACK (server_settings_notify_expand_cb), self);
    }
}

static GdkPixbuf*
load_icon (const char *uri, guint icon_size)
{
    GError *error = NULL;
    GdkPixbuf *pixbuf;
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
        return NULL;
    }

    return pixbuf;
}

static void
sync_config_widget_update_label (SyncConfigWidget *self)
{
    if (self->config && self->pretty_name) {
        char *url, *sync_url;
        char *str;

        syncevo_config_get_value (self->config, NULL, "WebURL", &url);
        syncevo_config_get_value (self->config, NULL, "syncURL", &sync_url);

        if (self->current) {
            str = g_strdup_printf ("<b>%s</b>", self->pretty_name);
        } else {
            str = g_strdup_printf ("%s", self->pretty_name);
        }
        if (g_str_has_prefix (sync_url, "obex-bt://")) {
            char *tmp = g_strdup_printf (_("%s - Bluetooth device"), str);
            g_free (str);
            str = tmp;
        } else if (!self->has_template) {
            /* TRANSLATORS: service title for services that are not based on a 
             * template in service list, the placeholder is the name of the service */
            char *tmp = g_strdup_printf (_("%s - manually setup"), str);
            g_free (str);
            str = tmp;
        } else if (url && strlen (url) > 0) {
            char *tmp = g_strdup_printf ("%s -",str);
            g_free (str);
            str = tmp;
        }

        gtk_label_set_markup (GTK_LABEL (self->label), str);
        g_free (str);
    }
}

void
sync_config_widget_set_current_service_name (SyncConfigWidget *self,
                                             const char *name)
{
    g_free (self->current_service_name);
    self->current_service_name = g_strdup (name);

    update_buttons (self);
}

void
sync_config_widget_set_current (SyncConfigWidget *self,
                                gboolean current)
{
    if (self->current != current) {
        self->current = current;
        sync_config_widget_update_label (self);
    }
}

static void
set_session (SyncConfigWidget *self, const char *path)
{
    g_free (self->running_session);
    self->running_session = g_strdup (path);

    gtk_widget_set_sensitive (GTK_WIDGET (self->reset_delete_button),
                              !self->running_session);
    gtk_widget_set_sensitive (GTK_WIDGET (self->use_button),
                              !self->running_session);

    /* TODO: maybe add a explanation text somewhere:
     * "Configuration changes are not possible while a sync is in progress" */
}

static void
session_changed_cb (SyncevoServer *server,
                    char *path,
                    gboolean started,
                    SyncConfigWidget *self)
{
    if (started) {
        set_session (self, path);
    } else if (g_strcmp0 (self->running_session, path) == 0 ) {
        set_session (self, NULL);
    }
}

static void
get_sessions_cb (SyncevoServer *server,
                 SyncevoSessions *sessions,
                 GError *error,
                 SyncConfigWidget *self)
{
    if (error) {
        g_warning ("Server.GetSessions failed: %s", error->message);
        g_error_free (error);
        /* non-fatal, ignore in UI */

        g_object_unref (self);
        return;
    }

    set_session (self, syncevo_sessions_index (sessions, 0));
    syncevo_sessions_free (sessions);
    g_object_unref (self);
}

void
sync_config_widget_set_server (SyncConfigWidget *self,
                               SyncevoServer *server)
{
    if (self->server) {
        g_signal_handlers_disconnect_by_func (self->server,
                                              session_changed_cb,
                                              self);
        g_object_unref (self->server);
        self->server = NULL;
    }
    if (!server && !self->server) {
        return;
    }

    self->server = g_object_ref (server);

    /* monitor sessions so we can set editing buttons insensitive */
    g_signal_connect (self->server, "session-changed",
                      G_CALLBACK (session_changed_cb), self);

    /* reference is released in callback */
    g_object_ref (self);
    syncevo_server_get_sessions (self->server,
                                 (SyncevoServerGetSessionsCb)get_sessions_cb,
                                 self);
}

static void
sync_config_widget_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (object);

    switch (property_id) {
    case PROP_SERVER:
        sync_config_widget_set_server (self, g_value_get_pointer (value));
        break;
    case PROP_NAME:
        sync_config_widget_set_name (self, g_value_get_string (value));
        break;
    case PROP_CONFIG:
        sync_config_widget_set_config (self, g_value_get_pointer (value));
        break;
    case PROP_CURRENT:
        sync_config_widget_set_current (self, g_value_get_boolean (value));
        break;
    case PROP_HAS_TEMPLATE:
        sync_config_widget_set_has_template (self, g_value_get_boolean (value));
        break;
    case PROP_CONFIGURED:
        sync_config_widget_set_configured (self, g_value_get_boolean (value));
        break;
    case PROP_CURRENT_SERVICE_NAME:
        sync_config_widget_set_current_service_name (self, g_value_get_string (value));
        break;
    case PROP_EXPANDED:
        sync_config_widget_set_expanded (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
sync_config_widget_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (object);

    switch (property_id) {
    case PROP_SERVER:
        g_value_set_pointer (value, self->server);
    case PROP_NAME:
        g_value_set_string (value, self->config_name);
    case PROP_CONFIG:
        g_value_set_pointer (value, self->config);
    case PROP_CURRENT:
        g_value_set_boolean (value, self->current);
    case PROP_HAS_TEMPLATE:
        g_value_set_boolean (value, self->has_template);
    case PROP_CONFIGURED:
        g_value_set_boolean (value, self->configured);
    case PROP_CURRENT_SERVICE_NAME:
        g_value_set_string (value, self->current_service_name);
    case PROP_EXPANDED:
        g_value_set_boolean (value, self->expanded);
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
sync_config_widget_dispose (GObject *object)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (object);

    sync_config_widget_set_server (self, NULL);
    if (self->config) {
        syncevo_config_free (self->config);
    }
    self->config = NULL;

    g_free (self->config_name);
    self->config_name = NULL;
    g_free (self->current_service_name);
    self->current_service_name = NULL;
    g_free (self->running_session);
    self->running_session = NULL;
    if (self->sources) {
        g_hash_table_destroy (self->sources);
        self->sources = NULL;
    }



    G_OBJECT_CLASS (sync_config_widget_parent_class)->dispose (object);
}

static void
init_default_config (SyncConfigWidget *self)
{
    sync_config_widget_set_name (self, "");
    self->has_template = FALSE;

    syncevo_config_set_value (self->config, NULL, "username", "");
    syncevo_config_set_value (self->config, NULL, "password", "");
    syncevo_config_set_value (self->config, NULL, "syncURL", "");
    syncevo_config_set_value (self->config, NULL, "WebURL", "");
    syncevo_config_set_value (self->config, "memo", "uri", "");
    syncevo_config_set_value (self->config, "todo", "uri", "");
    syncevo_config_set_value (self->config, "addressbook", "uri", "");
    syncevo_config_set_value (self->config, "calendar", "uri", "");

}

static gboolean
label_button_expose_cb (GtkWidget      *widget,
                        GdkEventExpose *event,
                        SyncConfigWidget *self)
{
    GtkExpanderStyle style;
    gint indicator_x, indicator_y;

    indicator_x = widget->style->xthickness + INDICATOR_SIZE / 2;
    indicator_y = widget->style->ythickness +
                  widget->allocation.height / 2;

    if (self->expanded) {
        style = GTK_EXPANDER_EXPANDED;
    } else {
        style = GTK_EXPANDER_COLLAPSED;
    }

    gtk_paint_expander (widget->style,
                        widget->window,
                        widget->state,
                        NULL,
                        GTK_WIDGET (self),
                        NULL,
                        indicator_x,
                        indicator_y,
                        style);

    return FALSE;
}

static gboolean
sync_config_widget_expose_event (GtkWidget      *widget,
                                 GdkEventExpose *event)
{
    GdkRectangle rect;
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (widget);

    rect.x = widget->allocation.x;
    rect.y = widget->allocation.y;
    rect.height = widget->allocation.height;
    rect.width = widget->allocation.width;

    gtk_paint_box (widget->style,
                   widget->window,
                   widget->state,
                   GTK_SHADOW_OUT,
                   &rect,
                   widget,
                   NULL,
                   rect.x,
                   rect.y,
                   rect.width,
                   rect.height);

    gtk_container_propagate_expose (GTK_CONTAINER (self),
                                    self->label_box, event);
    gtk_container_propagate_expose (GTK_CONTAINER (self),
                                    self->expando_box, event);

    return FALSE;
}


static void
sync_config_widget_size_allocate (GtkWidget     *widget,
                                  GtkAllocation *allocation)
{
    GtkRequisition req;
    GtkAllocation alloc;
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (widget);

    GTK_WIDGET_CLASS (sync_config_widget_parent_class)->size_allocate (widget,
                                                                       allocation);

    gtk_widget_size_request (self->label_box, &req);

    alloc.x = allocation->x + widget->style->xthickness;
    alloc.y = allocation->y + widget->style->ythickness;
    alloc.width = allocation->width - 2 * widget->style->xthickness;
    alloc.height = req.height;

    gtk_widget_size_allocate (self->label_box, &alloc);


    if (self->expanded) {
        gtk_widget_size_request (self->expando_box, &req);

        alloc.x = allocation->x + 2 * widget->style->xthickness;
        alloc.y = allocation->y + widget->style->ythickness +
                  alloc.height + CHILD_PADDING;
        alloc.width = allocation->width - 4 * widget->style->xthickness;
        alloc.height = req.height;

        gtk_widget_size_allocate (self->expando_box, &alloc);
    }
}

static void
sync_config_widget_size_request (GtkWidget      *widget,
                                 GtkRequisition *requisition)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (widget);
    GtkRequisition req;

    requisition->width = widget->style->xthickness * 2;
    requisition->height = widget->style->ythickness * 2;

    gtk_widget_size_request (self->label_box, &req);

    requisition->width += req.width;
    requisition->height = MAX (req.height, INDICATOR_SIZE) +
                          widget->style->ythickness * 2;

    if (self->expanded) {

        gtk_widget_size_request (self->expando_box, &req);
        requisition->width = MAX (requisition->width,
                                  req.width + widget->style->xthickness * 4);
        requisition->height += req.height + 2 * widget->style->ythickness;
    }
}

static GObject *
sync_config_widget_constructor (GType                  gtype,
                                guint                  n_properties,
                                GObjectConstructParam *properties)
{
    SyncConfigWidget *self;
    GObjectClass *parent_class;  
    char *url, *icon;
    GdkPixbuf *buf;

    parent_class = G_OBJECT_CLASS (sync_config_widget_parent_class);
    self = SYNC_CONFIG_WIDGET (parent_class->constructor (gtype,
                                                          n_properties,
                                                          properties));

    if (!self->config || !self->server) {
        g_warning ("No SyncevoServer or Syncevoconfig set for SyncConfigWidget");
        return G_OBJECT (self);
    }

    if (g_strcmp0 (self->config_name, "default") == 0) {

        init_default_config (self);
        gtk_widget_show (self->entry);
        gtk_widget_hide (self->label);
    } else {
        gtk_widget_hide (self->entry);
        gtk_widget_show (self->label);
    }

    syncevo_config_get_value (self->config, NULL, "WebURL", &url);
    syncevo_config_get_value (self->config, NULL, "IconURI", &icon);

    buf = load_icon (icon, SYNC_UI_LIST_ICON_SIZE);
    gtk_image_set_from_pixbuf (GTK_IMAGE (self->image), buf);
    g_object_unref (buf);

    if (url && strlen (url) > 0) {
        gtk_link_button_set_uri (GTK_LINK_BUTTON (self->link), url);
        gtk_widget_show (self->link);
    } else {
        gtk_widget_hide (self->link);
    }

    sync_config_widget_update_label (self);
    sync_config_widget_update_expander (self);

    /* hack to get focus in the right place on "Setup new service" */
    if (gtk_widget_get_visible (self->entry)) {
        gtk_widget_grab_focus (self->entry);
    }

    return G_OBJECT (self);
}

static void
sync_config_widget_map (GtkWidget *widget)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (widget);

    if (self->label_box && gtk_widget_get_visible (self->expando_box)) {
        gtk_widget_map (self->label_box);
    }
    if (self->expando_box && gtk_widget_get_visible (self->expando_box)) {
        gtk_widget_map (self->expando_box);
    }
    GTK_WIDGET_CLASS (sync_config_widget_parent_class)->map (widget);
}

static void
sync_config_widget_unmap (GtkWidget *widget)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (widget);

    GTK_WIDGET_CLASS (sync_config_widget_parent_class)->unmap (widget);

    if (self->label_box) {
        gtk_widget_unmap (self->label_box);
    }
    if (self->expando_box) {
        gtk_widget_unmap (self->expando_box);
    }
}

static void
sync_config_widget_add (GtkContainer *container,
                        GtkWidget    *widget)
{
    g_warning ("Can't add widgets in to SyncConfigWidget!");
}

static void
sync_config_widget_remove (GtkContainer *container,
                           GtkWidget    *widget)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (container);

    
    if (self->label_box == widget) {
        gtk_widget_unparent (widget);
        self->label_box = NULL;
    }
    if (self->expando_box == widget) {
        gtk_widget_unparent (widget);
        self->expando_box = NULL;
    }
}

static void
sync_config_widget_forall (GtkContainer *container,
                           gboolean      include_internals,
                           GtkCallback   callback,
                           gpointer      callback_data)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (container);

    if (self->label_box) {
        (* callback) (self->label_box, callback_data);
    }
    if (self->expando_box) {
        (* callback) (self->expando_box, callback_data);
    }
}


static void
sync_config_widget_class_init (SyncConfigWidgetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *w_class = GTK_WIDGET_CLASS (klass);
    GtkContainerClass *c_class = GTK_CONTAINER_CLASS (klass);
    GParamSpec *pspec;

    object_class->set_property = sync_config_widget_set_property;
    object_class->get_property = sync_config_widget_get_property;
    object_class->dispose = sync_config_widget_dispose;
    object_class->constructor = sync_config_widget_constructor;

    w_class->expose_event = sync_config_widget_expose_event;
    w_class->size_request = sync_config_widget_size_request;
    w_class->size_allocate = sync_config_widget_size_allocate;
    w_class->map = sync_config_widget_map;
    w_class->unmap = sync_config_widget_unmap;

    c_class->add = sync_config_widget_add;
    c_class->remove = sync_config_widget_remove;
    c_class->forall = sync_config_widget_forall;

    pspec = g_param_spec_pointer ("server",
                                  "SyncevoServer",
                                  "The SyncevoServer to use in Syncevolution DBus calls",
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_SERVER, pspec);

    pspec = g_param_spec_string ("name",
                                 "Configuration name",
                                 "The name of the Syncevolution service configuration",
                                 NULL,
                                 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_NAME, pspec);

    pspec = g_param_spec_pointer ("config",
                                  "SyncevoConfig",
                                  "The SyncevoConfig struct this widget represents. Takes ownership.",
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONFIG, pspec);

    pspec = g_param_spec_boolean ("current",
                                  "Current",
                                  "Whether the service is currently used",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CURRENT, pspec);

    pspec = g_param_spec_boolean ("has-template",
                                  "has template",
                                  "Whether the service has a matching template",
                                  FALSE,
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_HAS_TEMPLATE, pspec);

    pspec = g_param_spec_boolean ("configured",
                                  "Configured",
                                  "Whether the service has a configuration already",
                                  FALSE,
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONFIGURED, pspec);

    pspec = g_param_spec_string ("current-service-name",
                                 "Current service name",
                                 "The name of the currently used service or NULL",
                                  NULL,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CURRENT_SERVICE_NAME, pspec);

    pspec = g_param_spec_boolean ("expanded",
                                  "Expanded",
                                  "Whether the expander is open or closed",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_EXPANDED, pspec);

    signals[SIGNAL_CHANGED] = 
            g_signal_new ("changed",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncConfigWidgetClass, changed),
                          NULL, NULL,
                          g_cclosure_marshal_VOID__VOID,
                          G_TYPE_NONE, 
                          0);

}

static void
label_enter_notify_cb (GtkWidget *widget,
                       GdkEventCrossing *event,
                       SyncConfigWidget *self)
{
    if (!self->expanded) {
        gtk_widget_show (self->button);
    }
    gtk_widget_set_state (self->label_box, GTK_STATE_PRELIGHT);
}

static void
label_leave_notify_cb (GtkWidget *widget,
                       GdkEventCrossing *event,
                       SyncConfigWidget *self)
{
    if (event->detail != GDK_NOTIFY_INFERIOR) {
        gtk_widget_hide (self->button);
        gtk_widget_set_state (self->label_box, GTK_STATE_NORMAL);
    }
}

static void
device_combo_changed (GtkComboBox *combo,
                      SyncConfigWidget *self)
{
    int active;

    active = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
    gtk_widget_set_sensitive (self->device_select_btn, active > -1);
}

static void
label_button_release_cb (GtkWidget *widget,
                         GdkEventButton *event,
                         SyncConfigWidget *self)

{
    if (event->button == 1) {
        sync_config_widget_set_expanded (self,
                                         !sync_config_widget_get_expanded (self));
    }
}

static gint
compare_list_items (GtkTreeModel *model,
                    GtkTreeIter  *a,
                    GtkTreeIter  *b,
                    SyncConfigWidget *self)
{
    int score_a, score_b;

    gtk_tree_model_get(model, a, 2, &score_a, -1);
    gtk_tree_model_get(model, b, 2, &score_b, -1);

    return score_a - score_b;
}

static void
sync_config_widget_init (SyncConfigWidget *self)
{
    GtkWidget *tmp_box, *hbox, *cont, *vbox, *label;
    GtkListStore *store;
    GtkCellRenderer *renderer;

    gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);

    self->label_box = gtk_event_box_new ();
    gtk_widget_set_app_paintable (self->label_box, TRUE);
    gtk_widget_show (self->label_box);
    gtk_widget_set_parent (self->label_box, GTK_WIDGET (self));
    gtk_widget_set_size_request (self->label_box, -1, SYNC_UI_LIST_ICON_SIZE + 6);
    g_signal_connect (self->label_box, "enter-notify-event",
                      G_CALLBACK (label_enter_notify_cb), self);
    g_signal_connect (self->label_box, "leave-notify-event",
                      G_CALLBACK (label_leave_notify_cb), self);
    g_signal_connect (self->label_box, "button-release-event",
                      G_CALLBACK (label_button_release_cb), self);
    g_signal_connect (self->label_box, "expose-event",
                      G_CALLBACK (label_button_expose_cb), self);

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (hbox);
    gtk_container_add (GTK_CONTAINER (self->label_box), hbox);

    self->image = gtk_image_new ();
    /* leave room for drawing the expander indicator in expose handler */
    gtk_widget_set_size_request (self->image, 
                                 SYNC_UI_LIST_ICON_SIZE + INDICATOR_SIZE,
                                 SYNC_UI_LIST_ICON_SIZE);
    gtk_misc_set_alignment (GTK_MISC (self->image), 1.0, 0.5);
    gtk_widget_show (self->image);
    gtk_box_pack_start (GTK_BOX (hbox), self->image, FALSE, FALSE, 8);

    tmp_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (tmp_box);
    gtk_box_pack_start (GTK_BOX (hbox), tmp_box, FALSE, FALSE, 8);

    self->label = gtk_label_new ("");
    gtk_label_set_max_width_chars (GTK_LABEL (self->label), 60);
    gtk_label_set_ellipsize (GTK_LABEL (self->label), PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment (GTK_MISC (self->label), 0.0, 0.5);
    gtk_widget_show (self->label);
    gtk_box_pack_start (GTK_BOX (tmp_box), self->label, FALSE, FALSE, 0);

    self->entry = gtk_entry_new ();
    gtk_widget_set_no_show_all (self->entry, TRUE);
    gtk_box_pack_start (GTK_BOX (tmp_box), self->entry, FALSE, FALSE, 4);

    vbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vbox);
    gtk_box_pack_start (GTK_BOX (tmp_box), vbox, FALSE, FALSE, 0);

    /* TRANSLATORS: link button in service configuration form */
    self->link = gtk_link_button_new_with_label ("", _("Launch website"));
    gtk_widget_set_no_show_all (self->link, TRUE);
    gtk_box_pack_start (GTK_BOX (vbox), self->link, TRUE, FALSE, 0);

    vbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vbox);
    gtk_box_pack_end (GTK_BOX (hbox), vbox, FALSE, FALSE, 32);

    /* TRANSLATORS: button in service configuration form */
    self->button = gtk_button_new_with_label (_("Set up now"));
    gtk_widget_set_size_request (self->button, SYNC_UI_LIST_BTN_WIDTH, -1);
    g_signal_connect (self->button, "clicked",
                      G_CALLBACK (setup_service_clicked), self);
    gtk_box_pack_start (GTK_BOX (vbox), self->button, TRUE, FALSE, 0);

    /* label_box built, now build expando_box */

    self->expando_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_set_no_show_all (self->expando_box, TRUE);
    gtk_widget_set_parent (self->expando_box, GTK_WIDGET (self));

    self->device_selector_box = gtk_vbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (self->expando_box), self->device_selector_box,
                        TRUE, TRUE, 16);

    hbox = gtk_hbox_new (FALSE, 8);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (self->device_selector_box), hbox,
                        FALSE, TRUE, 8);
    self->device_text = gtk_label_new (_("We don't know what this device is exactly. "
                                         "Please take a look at the list of "
                                         "supported devices and pick yours if it "
                                         "is listed"));
    gtk_widget_show (self->device_text);
    gtk_label_set_line_wrap (GTK_LABEL (self->device_text), TRUE);
    gtk_widget_set_size_request (self->device_text, 600, -1);
    gtk_box_pack_start (GTK_BOX (hbox), self->device_text,
                        FALSE, TRUE, 0);


    hbox = gtk_hbox_new (FALSE, 16);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (self->device_selector_box), hbox,
                        FALSE, TRUE, 16);

    store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_INT);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                          2, GTK_SORT_DESCENDING);
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store), 2,
                                     (GtkTreeIterCompareFunc)compare_list_items,
                                     NULL, NULL);

    self->combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (store));
    g_object_unref (G_OBJECT (store)); 
    gtk_widget_set_size_request (self->combo, 200, -1);
    gtk_widget_show (self->combo);
    gtk_box_pack_start (GTK_BOX (hbox), self->combo, FALSE, TRUE, 0);

    renderer = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT(self->combo), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT(self->combo), renderer,
                                    "text", 0, NULL);

    g_signal_connect (self->combo, "changed",
                      G_CALLBACK (device_combo_changed), self);


    self->device_select_btn = gtk_button_new_with_label (_("Use these settings"));
    gtk_widget_set_sensitive (self->device_select_btn, FALSE);
    gtk_widget_show (self->device_select_btn);
    gtk_box_pack_start (GTK_BOX (hbox), self->device_select_btn,
                        FALSE, TRUE, 0);
    g_signal_connect (self->device_select_btn, "clicked",
                      G_CALLBACK (device_selection_btn_clicked_cb), self);

    /* settings_box has normal expander contents */
    self->settings_box = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (self->settings_box);
    gtk_box_pack_start (GTK_BOX (self->expando_box), self->settings_box,
                        TRUE, TRUE, 16);

    vbox = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox);
    gtk_box_pack_start (GTK_BOX (self->settings_box), vbox, TRUE, TRUE, 0);

    tmp_box = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (tmp_box);
    gtk_box_pack_start (GTK_BOX (vbox), tmp_box, FALSE, FALSE, 8);

    self->description_label = gtk_label_new ("");
    gtk_misc_set_alignment (GTK_MISC (self->description_label), 0.0, 0.5);
    gtk_widget_set_size_request (self->description_label, 700, -1);
    gtk_label_set_line_wrap (GTK_LABEL (self->description_label), TRUE);
    gtk_box_pack_start (GTK_BOX (tmp_box), self->description_label, FALSE, FALSE, 0);

    tmp_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (tmp_box);
    gtk_box_pack_start (GTK_BOX (vbox), tmp_box, FALSE, FALSE, 0);

    self->userinfo_table = gtk_table_new (4, 2, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (self->userinfo_table), 2);
    gtk_table_set_col_spacings (GTK_TABLE (self->userinfo_table), 5);
    gtk_widget_show (self->userinfo_table);
    gtk_box_pack_start (GTK_BOX (tmp_box), self->userinfo_table, FALSE, FALSE, 0);

    /* TRANSLATORS: labels of entries in service configuration form */
    label = gtk_label_new (_("Username"));
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_widget_show (label);
    gtk_table_attach_defaults (GTK_TABLE (self->userinfo_table), label,
                               0, 1,
                               0, 1);

    self->username_entry = gtk_entry_new ();
    gtk_widget_show (self->username_entry);
    gtk_entry_set_width_chars (GTK_ENTRY (self->username_entry), 40);
    gtk_entry_set_max_length (GTK_ENTRY (self->username_entry), 99);
    gtk_table_attach_defaults (GTK_TABLE (self->userinfo_table), self->username_entry,
                               1, 2,
                               0, 1);

    label = gtk_label_new (_("Password"));
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_widget_show (label);
    gtk_table_attach_defaults (GTK_TABLE (self->userinfo_table), label,
                               0, 1,
                               1, 2);

    self->password_entry = gtk_entry_new ();
    gtk_widget_show (self->password_entry);
    gtk_entry_set_width_chars (GTK_ENTRY (self->password_entry), 40);
    gtk_entry_set_visibility (GTK_ENTRY (self->password_entry), FALSE);
    gtk_entry_set_max_length (GTK_ENTRY (self->password_entry), 99);
    gtk_table_attach_defaults (GTK_TABLE (self->userinfo_table), self->password_entry,
                               1, 2,
                               1, 2);

    self->complex_config_info_bar = gtk_info_bar_new ();
    gtk_info_bar_set_message_type (GTK_INFO_BAR (self->complex_config_info_bar),
                                   GTK_MESSAGE_WARNING);
    gtk_box_pack_start (GTK_BOX (vbox), self->complex_config_info_bar,
                        FALSE, FALSE, 0);
    /* TRANSLATORS: warning in service configuration form for people
       who have modified the configuration via other means. */
    label = gtk_label_new (_("Current configuration is more complex "
                             "than what can be shown here. Changes to sync "
                             "mode or synced data types will overwrite that "
                             "configuration."));
    gtk_widget_set_size_request (label, 600, -1);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_widget_show (label);
    cont = gtk_info_bar_get_content_area (
        GTK_INFO_BAR (self->complex_config_info_bar));
    gtk_container_add (GTK_CONTAINER (cont), label);

    self->mode_table = gtk_table_new (4, 1, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (self->mode_table), 2);
    gtk_table_set_col_spacings (GTK_TABLE (self->mode_table), 5);
    gtk_widget_show (self->mode_table);
    gtk_box_pack_start (GTK_BOX (vbox), self->mode_table, FALSE, FALSE, 0);

    /* TRANSLATORS: this is the epander label for server settings
       in service configuration form */
    self->expander = gtk_expander_new (_("Hide server settings"));
    gtk_box_pack_start (GTK_BOX (vbox), self->expander, FALSE, FALSE, 8);

    tmp_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (tmp_box);
    gtk_container_add (GTK_CONTAINER (self->expander), tmp_box);

    self->server_settings_table = gtk_table_new (1, 1, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (self->server_settings_table), 2);
    gtk_table_set_col_spacings (GTK_TABLE (self->server_settings_table), 5);
    gtk_widget_show (self->server_settings_table);
    gtk_box_pack_start (GTK_BOX (tmp_box), self->server_settings_table, FALSE, FALSE, 0);


    tmp_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (tmp_box);
    gtk_box_pack_start (GTK_BOX (vbox), tmp_box, FALSE, FALSE, 8);

    /* TRANSLATORS: this is the epander label for server settings
       in service configuration form */
    self->fake_expander = gtk_expander_new (_("Show server settings"));
    gtk_widget_show (self->fake_expander);
    gtk_box_pack_start (GTK_BOX (tmp_box), self->fake_expander, FALSE, FALSE, 0);
    g_signal_connect (self->fake_expander, "notify::expanded",
                      G_CALLBACK (server_settings_notify_expand_cb), self);

    self->use_button = gtk_button_new ();
    gtk_widget_show (self->use_button);
    gtk_box_pack_end (GTK_BOX (tmp_box), self->use_button, FALSE, FALSE, 8);
    g_signal_connect (self->use_button, "clicked",
                      G_CALLBACK (use_clicked_cb), self);

    self->stop_button = gtk_button_new ();
    gtk_box_pack_end (GTK_BOX (tmp_box), self->stop_button, FALSE, FALSE, 8);
    g_signal_connect (self->stop_button, "clicked",
                      G_CALLBACK (stop_clicked_cb), self);

    self->reset_delete_button = gtk_button_new ();
    gtk_widget_show (self->reset_delete_button);
    gtk_box_pack_end (GTK_BOX (tmp_box), self->reset_delete_button, FALSE, FALSE, 8);
    g_signal_connect (self->reset_delete_button, "clicked",
                      G_CALLBACK (reset_delete_clicked_cb), self);
}


GtkWidget*
sync_config_widget_new (SyncevoServer *server,
                        const char *name,
                        SyncevoConfig *config,
                        gboolean current,
                        const char *current_service_name,
                        gboolean configured,
                        gboolean has_template)
{
  return g_object_new (SYNC_TYPE_CONFIG_WIDGET,
                       "server", server,
                       "name", name,
                       "config", config,
                       "current", current,
                       "current-service-name", current_service_name,
                       "configured", configured,
                       "has-template", has_template,
                       NULL);
}

void
sync_config_widget_set_expanded (SyncConfigWidget *self, gboolean expanded)
{
    g_return_if_fail (SYNC_IS_CONFIG_WIDGET (self));

    if (self->expanded != expanded) {

        self->expanded = expanded;
        if (self->expanded) {
            gtk_widget_hide (self->button);
            gtk_widget_show (self->expando_box);
            if (gtk_widget_get_visible (self->entry)) {
                gtk_widget_grab_focus (self->entry);
            } else {
                gtk_widget_grab_focus (self->username_entry);
            }
        } else {
            gtk_widget_show (self->button);
            gtk_widget_hide (self->expando_box);
        }
        g_object_notify (G_OBJECT (self), "expanded");

    }
}

gboolean
sync_config_widget_get_expanded (SyncConfigWidget *self)
{
    return self->expanded;
}

void
sync_config_widget_set_has_template (SyncConfigWidget *self, gboolean has_template)
{
    if (self->has_template != has_template) {
        self->has_template = has_template;
        update_buttons (self);
        sync_config_widget_update_label (self);
    }
}

gboolean
sync_config_widget_get_has_template (SyncConfigWidget *self)
{
    return self->has_template;
}

void
sync_config_widget_set_configured (SyncConfigWidget *self, gboolean configured)
{
    if (self->configured != configured) {
        self->configured = configured;
        self->device_template_selected = configured;
        update_buttons (self);
    }
}

gboolean
sync_config_widget_get_configured (SyncConfigWidget *self)
{
    return self->configured;
}


gboolean
sync_config_widget_get_current (SyncConfigWidget *widget)
{
    return widget->current;
}

const char*
sync_config_widget_get_name (SyncConfigWidget *widget)
{
    return widget->config_name;
}

void
sync_config_widget_add_alternative_config (SyncConfigWidget *self,
                                           const char *template_name,
                                           SyncevoConfig *config,
                                           gboolean configured)
{
    sync_config_widget_add_config (self, template_name, config);
    if (configured) {
        sync_config_widget_set_config (self, config);
        sync_config_widget_set_configured (self, TRUE);
    }


    sync_config_widget_update_expander (self);

}
