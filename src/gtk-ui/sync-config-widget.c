#include "config.h"

#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#ifdef USE_MOBLIN_UX
#include <mx/mx-gtk.h>
#endif

#include "sync-ui.h"
#include "sync-config-widget.h"

/* local copy of GtkInfoBar, used when GTK+ < 2.18 */
#include "gtkinfobar.h"


#define INDICATOR_SIZE 16
#define CHILD_PADDING 3

G_DEFINE_TYPE (SyncConfigWidget, sync_config_widget, GTK_TYPE_CONTAINER)


typedef struct source_widgets {
    GtkWidget *label;
    GtkWidget *entry;
    GtkWidget *check;

    GtkWidget *source_toggle_label;
} source_widgets;

enum
{
  PROP_0,

  PROP_SERVER,
  PROP_NAME,
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

static void sync_config_widget_update_label (SyncConfigWidget *self);

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
        return _("Google Sync can backup and synchronize your contacts "
                 "with your Gmail contacts.");
    }else if (strcmp (service, "Funambol") == 0) {
        /* TRANSLATORS: Please include the word "demo" (or the equivalent in
           your language): Funambol is going to be a 90 day demo service
           in the future */
        return _("Backup your contacts and calendar. Sync with a single "
                 "click, anytime, anywhere (DEMO).");
    }else if (strcmp (service, "Mobical") == 0) {
        return _("Mobical Backup and Restore service allows you to securely "
                 "backup your personal mobile data for free.");
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
        g_warning ("No widgets found for source %s", name);
        return;
    }

    uri = gtk_entry_get_text (GTK_ENTRY (widgets->entry));
    g_hash_table_insert (source_configuration, g_strdup ("uri"), g_strdup (uri));
}

static void
set_config_cb (SyncevoSession *session,
               GError *error,
               SyncConfigWidget *self)
{
    if (error) {
        g_warning ("Error in Session.SetConfig: %s", error->message);
        g_error_free (error);
        g_object_unref (session);
        /* TODO show in UI: save failed in service list */
        return;
    }

    g_object_unref (session);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
sync_config_widget_save_config (SyncConfigWidget *self,
                                SyncevoSession *session,
                                gboolean delete)
{
    if (delete) {
        syncevo_config_free (self->config->config);
        self->config->config = g_hash_table_new (g_str_hash, g_str_equal);
    }

    syncevo_session_set_config (session,
                                FALSE,
                                FALSE,
                                self->config->config,
                                (SyncevoSessionGenericCb)set_config_cb,
                                self);
}

typedef struct save_config_data {
    SyncConfigWidget *widget;
    gboolean delete;
} save_config_data;

static void
status_changed_for_config_write_cb (SyncevoSession *session,
                                    SyncevoSessionStatus status,
                                    guint error_code,
                                    SyncevoSourceStatuses *source_statuses,
                                    save_config_data *data)
{
    if (status == SYNCEVO_STATUS_IDLE) {
        sync_config_widget_save_config (data->widget, session, data->delete);
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
        sync_config_widget_save_config (data->widget, session, data->delete);
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
    SyncevoConfig *config;

    if (!self->config) {
        return;
    }

    config = self->config->config;

    syncevo_config_set_value (config, NULL, "defaultPeer", "");
    sync_config_widget_set_current (self, FALSE);

    data = g_slice_new (save_config_data);
    data->widget = self;
    data->delete = FALSE;
    syncevo_server_start_session (self->server,
                                  self->config->name,
                                  (SyncevoServerStartSessionCb)start_session_for_config_write_cb,
                                  data);
}

static void
use_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    SyncevoConfig *config;
    save_config_data *data;
    const char *username, *password, *sync_url;
    char *real_url;
    gboolean send, receive;
    SyncevoSyncMode mode;

    if (!self->config) {
        return;
    }

    if (self->current_service_name && !self->current) {
        GtkWidget *w, *top_level;
        int ret;
        char *msg, *yes, *no;

        /*TRANSLATORS: warning dialog text for changing current service */
        msg = g_strdup_printf
            (_("Do you want to replace %s with %s? This\n"
               "will not remove any synced information on either\n"
               "end but you will no longer be able to sync with\n"
               "%s."),
             self->current_service_name,
             gtk_entry_get_text (GTK_ENTRY (self->entry)),
             self->current_service_name);
        /* TRANSLATORS: decline/accept buttons in warning dialog.
           Placeholder is service name */
        yes = g_strdup_printf (_("Yes, use %s"),
                               gtk_entry_get_text (GTK_ENTRY (self->entry)));
        no = g_strdup_printf (_("No, use %s"), self->current_service_name);
        top_level = gtk_widget_get_toplevel (GTK_WIDGET (self));
        w = gtk_message_dialog_new (GTK_WINDOW (top_level),
                                    GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_NONE,
                                    msg);
        gtk_dialog_add_buttons (GTK_DIALOG (w),
                                no, GTK_RESPONSE_NO,
                                yes, GTK_RESPONSE_YES,
                                NULL);
        ret = gtk_dialog_run (GTK_DIALOG (w));
        gtk_widget_destroy (w);
        g_free (msg);
        g_free (yes);
        g_free (no);

        if (ret != GTK_RESPONSE_YES) {
            return;
        }
    }

    config = self->config->config;
    self->config->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (self->entry)));

    if (self->mode_changed) {
        GHashTableIter iter;
        source_widgets *widgets;
        char *name;

        g_object_get (self->send_check, "active", &send, NULL);
        g_object_get (self->receive_check, "active", &receive, NULL);

        if (send && receive) {
            mode = SYNCEVO_SYNC_TWO_WAY;
        } else if (send) {
            mode = SYNCEVO_SYNC_ONE_WAY_FROM_CLIENT;
        } else if (receive) {
            mode = SYNCEVO_SYNC_ONE_WAY_FROM_SERVER;
        } else {
            mode = SYNCEVO_SYNC_NONE;
        }

        g_hash_table_iter_init (&iter, self->sources);
        while (g_hash_table_iter_next (&iter, (gpointer)&name, (gpointer)&widgets)) {
            const char *mode_str;
            gboolean active;

            g_object_get (widgets->check, "active", &active, NULL);
            if (active) {
                mode_str = syncevo_sync_mode_to_string (mode);
            } else {
                mode_str = "none";
            }
            syncevo_config_set_value (config, name, "sync", mode_str);
        }
    }

    username = gtk_entry_get_text (GTK_ENTRY (self->username_entry));
    syncevo_config_set_value (config, NULL, "username", username);

    sync_url = gtk_entry_get_text (GTK_ENTRY (self->baseurl_entry));
    /* make a wild guess if no scheme in url */
    if (strstr (sync_url, "://") == NULL) {
        real_url = g_strdup_printf ("http://%s", sync_url);
    } else {
        real_url = g_strdup (sync_url);
    }
    syncevo_config_set_value (config, NULL, "syncURL", real_url);

    password = gtk_entry_get_text (GTK_ENTRY (self->password_entry));
    syncevo_config_set_value (config, NULL, "password", password);


    if (!self->config->name || strlen (self->config->name) == 0 ||
        !sync_url || strlen (sync_url) == 0) {
        /* TODO show in UI: service settings missing name or url */
        show_error_dialog (GTK_WIDGET (self), 
                           _("Service must have a name and server URL"));
        return;
    }

    syncevo_config_foreach_source (config,
                                   (ConfigFunc)update_source_uri,
                                   self);

    syncevo_config_set_value (config, NULL, "defaultPeer", self->config->name);
    sync_config_widget_set_current (self, TRUE);

    data = g_slice_new (save_config_data);
    data->widget = self;
    data->delete = FALSE;
    syncevo_server_start_session (self->server,
                                  self->config->name,
                                  (SyncevoServerStartSessionCb)start_session_for_config_write_cb,
                                  data);

    g_free (real_url);
}

static void
reset_delete_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    GtkWidget *w, *top_level;
    int ret;
    char *msg, *yes;
    save_config_data *data;

    if (!self->config) {
        return;
    }

    if (self->has_template) {
        /*TRANSLATORS: warning dialog text for resetting pre-defined
          services */
        msg = g_strdup_printf
            (_("Do you want to reset the settings for %s?\n"
               "This will not remove any synced information on either end."),
             self->config->name);
        /*TRANSLATORS: accept button in warning dialog */
        yes = _("Yes, reset");
    } else {
        /*TRANSLATORS: warning dialog text for deleting user-defined
          services */
        msg = g_strdup_printf
            (_("Do you want to delete the settings for %s?\n"
               "This will not remove any synced information on either\n"
               "end but it will remove this service configuration."),
             self->config->name);
        /*TRANSLATORS: accept button in warning dialog */
        yes = _("Yes, delete");
    }

    top_level = gtk_widget_get_toplevel (GTK_WIDGET (self));
    w = gtk_message_dialog_new (GTK_WINDOW (top_level),
                                GTK_DIALOG_MODAL,
                                GTK_MESSAGE_QUESTION,
                                GTK_BUTTONS_NONE,
                                msg);
    /*TRANSLATORS: decline button in warning dialog */
    gtk_dialog_add_buttons (GTK_DIALOG (w),
                            _("No, keep settings"),
                            GTK_RESPONSE_NO,
                            yes,
                            GTK_RESPONSE_YES,
                            NULL);
    ret = gtk_dialog_run (GTK_DIALOG (w));
    gtk_widget_destroy (w);
    g_free (msg);

    if (ret != GTK_RESPONSE_YES) {
        return;
    }

    if (self->current) {
        sync_config_widget_set_current (self, FALSE);
    }

    data = g_slice_new (save_config_data);
    data->widget = self;
    data->delete = TRUE;
    syncevo_server_start_session (self->server,
                                  self->config->name,
                                  (SyncevoServerStartSessionCb)start_session_for_config_write_cb,
                                  data);
}

static void update_buttons (SyncConfigWidget *self)
{
    if (self->has_template) {
        /* TRANSLATORS: button labels in service configuration form */
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Reset service"));
    } else {
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Delete service"));
    }
    if (self->configured) {
        gtk_widget_show (GTK_WIDGET (self->reset_delete_button));
    } else {
        gtk_widget_hide (GTK_WIDGET (self->reset_delete_button));
    }

    if (self->current_service_name || self->current) {
        gtk_button_set_label (GTK_BUTTON (self->use_button),
                              _("Save and use"));
    } else { 
        gtk_button_set_label (GTK_BUTTON (self->use_button),
                              _("Save and replace\ncurrent service"));
    }

    if (self->current) {
        gtk_widget_show (self->stop_button);
    } else { 
        gtk_widget_hide (self->stop_button);
    }
}


static void
check_source_cb (SyncevoServer *server,
                 GError *error,
                 source_widgets *widgets)
{
    gboolean show = TRUE;
    if (error) {
        if(error->code == DBUS_GERROR_REMOTE_EXCEPTION &&
           dbus_g_error_has_name (error, SYNCEVO_DBUS_ERROR_SOURCE_UNUSABLE)) {

            show = FALSE;
        } else if (error->code == DBUS_GERROR_REMOTE_EXCEPTION &&
                   dbus_g_error_has_name (error,
                                          SYNCEVO_DBUS_ERROR_NO_SUCH_CONFIG)){
            /* apparently templates can't be checked... */
            /* TODO: could use a temporary config to do it... */
        } else {
            g_warning ("CheckSource failed: %s", error->message);
            /* non-fatal, ignore in UI */
        }
        g_error_free (error);
    }

    if (show) {
        gtk_widget_show (widgets->source_toggle_label);
        gtk_widget_show (widgets->label);
        gtk_widget_show (widgets->entry);
        gtk_widget_show (widgets->check);
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
        g_object_set (widgets->check, "active", new_editable, NULL);
    }
}

static GtkWidget*
add_toggle_widget (SyncConfigWidget *self,
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
    gtk_table_attach (GTK_TABLE (self->mode_table), label,
                      col, col + 1, row, row + 1,
                      GTK_FILL, GTK_FILL, 0, 0);
    toggle = mx_gtk_light_switch_new ();
#else
    toggle = gtk_check_button_new_with_label (title);
#endif
    g_object_set (toggle, "active", active, NULL);
    gtk_widget_show (toggle);
    gtk_table_attach (GTK_TABLE (self->mode_table), toggle,
                      col + 1, col + 2, row, row + 1,
                      GTK_FILL, GTK_FILL, 32, 0);
    g_signal_connect (toggle, "notify::active",
                      G_CALLBACK (mode_widget_notify_active_cb), self);

    return toggle;
}

static void
init_source (char *name,
             GHashTable *source_configuration,
             SyncConfigWidget *self)
{
    char *str, *pretty_name;
    const char *uri;
    guint rows;
    guint row;
    static guint col = 0;
    source_widgets *widgets;
    SyncevoSyncMode mode;

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

    widgets = g_slice_new (source_widgets);
    g_hash_table_insert (self->sources, name, widgets);

    widgets->source_toggle_label = self->source_toggle_label;

    uri = g_hash_table_lookup (source_configuration, "uri");
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

    if (self->configured) {
        syncevo_server_check_source (self->server,
                                     self->config->name,
                                     name,
                                     (SyncevoServerGenericCb)check_source_cb,
                                     widgets);
    } else {
        /* TODO: should do a temp config to test eve n template sources */
        gtk_widget_show (widgets->source_toggle_label);
        gtk_widget_show (widgets->label);
        gtk_widget_show (widgets->entry);
        gtk_widget_show (widgets->check);
    }
}

static void
get_common_mode (char *name,
                 GHashTable *source_configuration,
                 SyncevoSyncMode *common_mode)
{
    SyncevoSyncMode mode;
    char *mode_str;

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

static void
source_widgets_free (source_widgets *widgets)
{
    if (widgets) {
        g_slice_free (source_widgets, widgets);
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
    SyncevoConfig *config = self->config->config;

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

    /* TODO: sources that are not supported locally will trigger the complex
     * config warning for no real reason... should do get_common_mode only after
     * check_source calls, and make sure unsupported sources do not get edited */
    syncevo_config_foreach_source (config,
                                   (ConfigFunc)get_common_mode,
                                   &mode);
    switch (mode) {
    case SYNCEVO_SYNC_TWO_WAY:
        send = receive = TRUE;
        break;
    case SYNCEVO_SYNC_ONE_WAY_FROM_CLIENT:
        send = TRUE;
        receive = FALSE;
        break;
    case SYNCEVO_SYNC_ONE_WAY_FROM_SERVER:
        send = FALSE;
        receive = TRUE;
        break;
    default:
        gtk_widget_show (self->complex_config_info_bar);
        g_warning ("sync mode config is more complex than UI can handle");
        send = FALSE;
        receive = FALSE;
    }
    self->mode_changed = FALSE;


    if (self->config->name) {
        gtk_entry_set_text (GTK_ENTRY (self->entry), self->config->name);
    }
    if (!self->config->name || strlen (self->config->name) == 0) {
        gtk_expander_set_expanded (GTK_EXPANDER (self->expander), TRUE);
    }

    descr = get_service_description (self->config->name);
    if (descr) {
        gtk_label_set_text (GTK_LABEL (self->description_label),
                            get_service_description (self->config->name));
        gtk_widget_show (self->description_label);
    } else {
        gtk_widget_hide (self->description_label);
    }

    update_buttons (self);

    /* TRANSLATORS: check button (or toggle) in service configuration form */
    str = g_strdup_printf (_("Send changes to %s"), self->config->name);
    self->send_check = add_toggle_widget (self, str, send, 0, 0);
    g_free (str);

    /* TRANSLATORS: check button (or toggle) in service configuration form */
    str = g_strdup_printf (_("Receive changes from %s"), self->config->name);
    self->receive_check = add_toggle_widget (self, str, receive, 0, 1);
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

    syncevo_config_get_value (config, NULL, "username", &username);
    syncevo_config_get_value (config, NULL, "password", &password);
    syncevo_config_get_value (config, NULL, "syncURL", &sync_url);

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
                                           (GDestroyNotify)source_widgets_free);
    self->no_source_toggles = TRUE;
    syncevo_config_foreach_source (config,
                                   (ConfigFunc)init_source,
                                   self);
}

static void
setup_service_clicked (GtkButton *btn, SyncConfigWidget *self)
{
    sync_config_widget_set_expanded (self, TRUE);
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
    if (self->config && self->config->name && self->config->config) {
        char *url;
        char *str;

        syncevo_config_get_value (self->config->config, NULL, "WebURL", &url);

        if (self->current) {
            str = g_strdup_printf ("<b>%s</b>", self->config->name);
        } else {
            str = g_strdup_printf ("%s", self->config->name);
        }
        if (!self->has_template) {
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
    } else if (self->running_session &&
               strcmp (self->running_session, path) == 0 ) {
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
        return;
    }

    set_session (self, syncevo_sessions_index (sessions, 0));
    syncevo_sessions_free (sessions);
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

    /* TODO: this is stupid, every widget running the same dbus call*/
    syncevo_server_get_sessions (self->server,
                                 (SyncevoServerGetSessionsCb)get_sessions_cb,
                                 self);
}

static void
sync_config_widget_set_name (SyncConfigWidget *self,
                             const char *name)
{
    self->config = g_slice_new0 (server_config);
    self->config->name = g_strdup (name);
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
        if (self->config)
            g_value_set_string (value, self->config->name);
        else
            g_value_set_string (value, NULL);
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

    G_OBJECT_CLASS (sync_config_widget_parent_class)->dispose (object);
}

static void
init_default_config (server_config *config)
{
    g_free (config->name);
    config->name = g_strdup ("");

    syncevo_config_set_value (config->config, NULL, "username", "");
    syncevo_config_set_value (config->config, NULL, "password", "");
    syncevo_config_set_value (config->config, NULL, "syncURL", "");
    syncevo_config_set_value (config->config, NULL, "WebURL", "");
    syncevo_config_set_value (config->config, "memo", "uri", "");
    syncevo_config_set_value (config->config, "todo", "uri", "");
    syncevo_config_set_value (config->config, "addressbook", "uri", "");
    syncevo_config_set_value (config->config, "calendar", "uri", "");

}

static void
sync_config_widget_real_init (SyncConfigWidget *self,
                              SyncevoConfig *config)
{
    char *url, *icon;
    GdkPixbuf *buf;
    server_config_init (self->config, config);
    if (self->config->name &&
        strcmp (self->config->name, "default") == 0) {

        init_default_config (self->config);
        self->has_template = FALSE;
        gtk_widget_show (self->entry);
        gtk_widget_hide (self->label);
    } else {
        /**/
        gtk_widget_hide (self->entry);
        gtk_widget_show (self->label);
    }

    syncevo_config_get_value (self->config->config, NULL, "WebURL", &url);
    syncevo_config_get_value (self->config->config, NULL, "IconURI", &icon);

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

    if (self->showing) {
        gtk_widget_show (GTK_WIDGET (self));

        /* hack to get focus in the right place on "Setup new service" */
        if (GTK_WIDGET_VISIBLE (self->entry)) {
            gtk_widget_grab_focus (self->entry);
        }
    }
}

static void
get_config_cb (SyncevoServer *syncevo,
               SyncevoConfig *config,
               GError *error,
               SyncConfigWidget *self)
{
    if (error) {
        g_warning ("Server.GetConfig failed: %s", error->message);
        g_error_free (error);
        g_object_thaw_notify (G_OBJECT (self));

        /* TODO: show in UI */
        return;
    }
    sync_config_widget_real_init (self, config);
    g_object_thaw_notify (G_OBJECT (self));

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

    if (self->expanded) {
        gint shadow_x, shadow_y;

        shadow_x = rect.x + widget->style->xthickness;
        shadow_y = rect.y + 2 * widget->style->ythickness +
                   self->label_box->allocation.height;

        gtk_paint_box (widget->style,
                       widget->window,
                       widget->state,
                       GTK_SHADOW_IN,
                       &rect,
                       widget,
                       NULL,
                       shadow_x,
                       shadow_y,
                       rect.width - (shadow_x - rect.x) - widget->style->xthickness,
                       rect.height - (shadow_y - rect.y) - widget->style->ythickness);
    }


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

    parent_class = G_OBJECT_CLASS (sync_config_widget_parent_class);
    self = SYNC_CONFIG_WIDGET (parent_class->constructor (gtype,
                                                          n_properties,
                                                          properties));

    if (!self->server) {
        g_warning ("No SyncevoServer set for SyncConfigWidget");
    }

    /* freeze notifys so we don't claim to have expanded until we have...
       this could be achieved in more clean ways as well... */
    g_object_freeze_notify (G_OBJECT (self));
    syncevo_server_get_config (self->server,
                               self->config->name,
                               self->has_template && !self->configured,
                               (SyncevoServerGetConfigCb)get_config_cb,
                               self);
    return G_OBJECT (self);
}

static void
sync_config_widget_show (GtkWidget *widget)
{
    char *ready;
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (widget);

    /* this is a bit dirty... might be better to show the widget
     * in any case and handle removing non-ready templates otherwise */
    self->showing = TRUE;
    if (self->config && self->config->config) {
        syncevo_config_get_value (self->config->config,
                                  NULL, "ConsumerReady", &ready);

        if (self->configured || g_strcmp0 ("1", ready) == 0) {
            GTK_WIDGET_CLASS (sync_config_widget_parent_class)->show (widget);
        }
    }
}

static void
sync_config_widget_hide (GtkWidget *widget)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (widget);

    self->showing = FALSE;
    GTK_WIDGET_CLASS (sync_config_widget_parent_class)->hide (widget);
}

static void
sync_config_widget_map (GtkWidget *widget)
{
    SyncConfigWidget *self = SYNC_CONFIG_WIDGET (widget);

    if (self->label_box && GTK_WIDGET_VISIBLE (self->expando_box)) {
        gtk_widget_map (self->label_box);
    }
    if (self->expando_box && GTK_WIDGET_VISIBLE (self->expando_box)) {
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
sync_config_widget_realize (GtkWidget *widget)
{
    GTK_WIDGET_CLASS (sync_config_widget_parent_class)->realize (widget);
}

static void
sync_config_widget_unrealize (GtkWidget *widget)
{
    GTK_WIDGET_CLASS (sync_config_widget_parent_class)->unrealize (widget);
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

    w_class->show = sync_config_widget_show;
    w_class->hide = sync_config_widget_hide;
    w_class->expose_event = sync_config_widget_expose_event;
    w_class->size_request = sync_config_widget_size_request;
    w_class->size_allocate = sync_config_widget_size_allocate;
    w_class->map = sync_config_widget_map;
    w_class->unmap = sync_config_widget_unmap;
    w_class->realize = sync_config_widget_realize;
    w_class->unrealize = sync_config_widget_unrealize;

    c_class->add = sync_config_widget_add;
    c_class->remove = sync_config_widget_remove;
    c_class->forall = sync_config_widget_forall;

    pspec = g_param_spec_pointer ("server",
                                  "SyncevoServer",
                                  "The SyncevoServer struct this widget represents",
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_SERVER, pspec);

    pspec = g_param_spec_string ("name",
                                 "Configuration name",
                                 "The name of the Syncevolution service configuration",
                                 NULL,
                                 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_NAME, pspec);

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

    pspec = g_param_spec_int ("expander-size",
                              "Expander Size",
                              "Size of the expander indicator",
                              0, G_MAXINT, INDICATOR_SIZE,
                              G_PARAM_READABLE);

  gtk_widget_class_install_style_property (w_class, pspec);


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
label_button_release_cb (GtkWidget *widget,
                         GdkEventButton *event,
                         SyncConfigWidget *self)

{
    if (event->button == 1) {
        sync_config_widget_set_expanded (self,
                                         !sync_config_widget_get_expanded (self));
    }
}

static void
sync_config_widget_init (SyncConfigWidget *self)
{
    GtkWidget *tmp_box, *hbox, *cont, *vbox, *table, *label;

    GTK_WIDGET_SET_FLAGS (GTK_WIDGET (self), GTK_NO_WINDOW);

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
    self->button = gtk_button_new_with_label (_("Setup now"));
    gtk_widget_set_size_request (self->button, SYNC_UI_LIST_BTN_WIDTH, -1);
    g_signal_connect (self->button, "clicked",
                      G_CALLBACK (setup_service_clicked), self);
    gtk_box_pack_start (GTK_BOX (vbox), self->button, TRUE, FALSE, 0);

    /* label_box built, now build expando_box */

    self->expando_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_set_no_show_all (self->expando_box, TRUE);
    gtk_widget_set_parent (self->expando_box, GTK_WIDGET (self));

    vbox = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox);
    gtk_box_pack_start (GTK_BOX (self->expando_box), vbox, TRUE, TRUE, 16);

    tmp_box = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (tmp_box);
    gtk_box_pack_start (GTK_BOX (vbox), tmp_box, FALSE, FALSE, 8);

    self->description_label = gtk_label_new ("");
    gtk_misc_set_alignment (GTK_MISC (self->description_label), 0.0, 0.5);
    gtk_widget_set_size_request (self->description_label, 700, -1);
    gtk_box_pack_start (GTK_BOX (tmp_box), self->description_label, FALSE, FALSE, 0);

    tmp_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (tmp_box);
    gtk_box_pack_start (GTK_BOX (vbox), tmp_box, FALSE, FALSE, 0);

    table = gtk_table_new (4, 2, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (table), 2);
    gtk_table_set_col_spacings (GTK_TABLE (table), 5);
    gtk_widget_show (table);
    gtk_box_pack_start (GTK_BOX (tmp_box), table, FALSE, FALSE, 0);

    /* TRANSLATORS: labels of entries in service configuration form */
    label = gtk_label_new (_("Username"));
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_widget_show (label);
    gtk_table_attach_defaults (GTK_TABLE (table), label,
                               0, 1,
                               0, 1);

    self->username_entry = gtk_entry_new ();
    gtk_widget_show (self->username_entry);
    gtk_entry_set_width_chars (GTK_ENTRY (self->username_entry), 40);
    gtk_entry_set_max_length (GTK_ENTRY (self->username_entry), 99);
    gtk_table_attach_defaults (GTK_TABLE (table), self->username_entry,
                               1, 2,
                               0, 1);

    label = gtk_label_new (_("Password"));
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_widget_show (label);
    gtk_table_attach_defaults (GTK_TABLE (table), label,
                               0, 1,
                               1, 2);

    self->password_entry = gtk_entry_new ();
    gtk_widget_show (self->password_entry);
    gtk_entry_set_width_chars (GTK_ENTRY (self->password_entry), 40);
    gtk_entry_set_visibility (GTK_ENTRY (self->password_entry), FALSE);
    gtk_entry_set_max_length (GTK_ENTRY (self->password_entry), 99);
    gtk_table_attach_defaults (GTK_TABLE (table), self->password_entry,
                               1, 2,
                               1, 2);

    self->complex_config_info_bar = gtk_info_bar_new ();
    gtk_info_bar_set_message_type (GTK_INFO_BAR (self->complex_config_info_bar),
                                   GTK_MESSAGE_WARNING);
    gtk_box_pack_start (GTK_BOX (vbox), self->complex_config_info_bar,
                        FALSE, FALSE, 0);
    /* TRANSLATORS: warning in service configuration form for people
       who have modified the configuration via other means. */
    label = gtk_label_new (_("Current service configuration is more complex "
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

    /* TRANSLATORS: button in service configuration form */
    self->stop_button = gtk_button_new_with_label (_("Stop using service"));
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
                        gboolean current,
                        const char *current_service_name,
                        gboolean configured,
                        gboolean has_template)
{
  return g_object_new (SYNC_TYPE_CONFIG_WIDGET,
                       "server", server,
                       "name", name,
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
            if (GTK_WIDGET_VISIBLE (self->entry)) {
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
    if (!widget->config)
        return NULL;

    return widget->config->name;
}
