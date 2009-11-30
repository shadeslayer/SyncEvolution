#include "config.h"

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "sync-ui.h"
#include "sync-config-widget.h"

#define INDICATOR_SIZE 16
#define CHILD_PADDING 3

G_DEFINE_TYPE (SyncConfigWidget, sync_config_widget, GTK_TYPE_CONTAINER)

enum
{
  PROP_0,

  PROP_SERVER,
  PROP_NAME,
  PROP_CURRENT,
  PROP_HAS_TEMPLATE,
  PROP_CONFIGURED,
  PROP_UNSET,
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
        return "";

    if (strcmp (service, "ScheduleWorld") == 0) {
        return _("ScheduleWorld enables you to keep your contacts, events, "
                 "tasks, and notes in sync.");
    }else if (strcmp (service, "Google") == 0) {
        return _("Google Sync can backup and synchronize your Address Book "
                 "with your Gmail contacts.");
    }else if (strcmp (service, "Funambol") == 0) {
        /* TRANSLATORS: Please include the word "demo" (or the equivalent in
           your language): Funambol is going to be a 90 day demo service
           in the future */
        return _("Backup your contacts and calendar. Sync with a single"
                 "click, anytime, anywhere (DEMO).");
    }

    return "";
}

static void
stop_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    sync_config_widget_set_current (self, FALSE);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
update_source_config (char *name,
                      GHashTable *source_configuration,
                      SyncConfigWidget *self)
{
    const char *uri;
    GtkEntry *entry;

    entry = GTK_ENTRY (g_hash_table_lookup (self->uri_entries, name));
    if (!entry) {
        g_warning ("No entry found for source %s", name);
        return;
    }

    uri = gtk_entry_get_text (entry);
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
    } else {
        sync_config_widget_set_current (self, TRUE);
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
use_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    SyncevoConfig *config;
    save_config_data *data;
    const char *username, *password, *sync_url, *name;
    char *real_url;

    if (!self->config) {
        return;
    }

    config = self->config->config;

    name = gtk_entry_get_text (GTK_ENTRY (self->entry));

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


    if (!name || strlen (name) == 0 ||
        !sync_url || strlen (sync_url) == 0) {
        /* TODO show in UI: service settings missing name or url */
        show_error_dialog (GTK_WIDGET (self), 
                           _("Service must have a name and server URL"));
        return;
    }

    syncevo_config_set_value (config, NULL, "defaultPeer", name);

    syncevo_config_foreach_source (config,
                                   (ConfigFunc)update_source_config,
                                   self);

    data = g_slice_new (save_config_data);
    data->widget = self;
    data->delete = FALSE;
    syncevo_server_start_session (self->server,
                                  name,
                                  (SyncevoServerStartSessionCb)start_session_for_config_write_cb,
                                  data);

    g_free (real_url);
}

static void
reset_delete_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    save_config_data *data;

    if (!self->config) {
        return;
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
        /* TRANSLATORS: button labels */
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Reset service"));
    } else {
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Delete service"));
        if (self->configured) {
            gtk_widget_show (GTK_WIDGET (self->reset_delete_button));
        } else {
            gtk_widget_hide (GTK_WIDGET (self->reset_delete_button));
        }
    }

    if (self->unset || self->current) {
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

typedef struct source_row_data {
    GtkWidget *label;
    GtkWidget *entry;
} source_row_data;

static void
check_source_cb (SyncevoServer *server,
                 GError *error,
                 source_row_data *data)
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
        gtk_widget_show (data->label);
        gtk_widget_show (data->entry);
    }
    g_slice_free (source_row_data, data);
}

static void
init_source (char *name,
             GHashTable *source_configuration,
             SyncConfigWidget *self)
{
    char *str, *pretty_name;
    const char *uri;
    guint row;
    source_row_data *row_data;

    row_data = g_slice_new (source_row_data);

    g_object_get (self->server_settings_table,
                  "n-rows", &row,
                  NULL);

    uri = g_hash_table_lookup (source_configuration, "uri");
    pretty_name = get_pretty_source_name (name);

    /* TRANSLATORS: label for an entry in service configuration.
     * Placeholder is a source  name in settings window. 
     * Example: "Addressbook URI" */
    str = g_strdup_printf (_("%s URI"), pretty_name);
    row_data->label = gtk_label_new (str);
    g_free (str);
    g_free (pretty_name);

    gtk_misc_set_alignment (GTK_MISC (row_data->label), 1.0, 0.5);
    gtk_table_attach (GTK_TABLE (self->server_settings_table), row_data->label,
                      0, 1, row, row + 1, GTK_FILL, GTK_EXPAND, 0, 0);

    row_data->entry = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (row_data->entry), 99);
    gtk_entry_set_width_chars (GTK_ENTRY (row_data->entry), 80);
    if (uri) {
        gtk_entry_set_text (GTK_ENTRY (row_data->entry), uri);
    }
    g_hash_table_insert (self->uri_entries, name, row_data->entry);
    gtk_table_attach_defaults (GTK_TABLE (self->server_settings_table), row_data->entry,
                               1, 2, row, row + 1);

    syncevo_server_check_source (self->server,
                                 self->config->name,
                                 name,
                                 (SyncevoServerGenericCb)check_source_cb,
                                 row_data);
}

static void
sync_config_widget_update_expander (SyncConfigWidget *self)
{
    char *username = "";
    char *password = "";
    char *sync_url = "";
    GtkWidget *label;
    SyncevoConfig *config = self->config->config;

    gtk_container_foreach (GTK_CONTAINER (self->server_settings_table),
                           (GtkCallback)remove_child,
                           self->server_settings_table);
    gtk_table_resize (GTK_TABLE (self->server_settings_table), 
                      2, 1);

    if (self->config->name) {
        gtk_entry_set_text (GTK_ENTRY (self->entry), self->config->name);
    }

    gtk_label_set_text (GTK_LABEL (self->description_label),
                        get_service_description (self->config->name));

    /* TODO set expanded status based on user pref? */

    update_buttons (self);

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
    label = gtk_label_new (_("Server URL"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
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

    /* update source uris */
    if (self->uri_entries) {
        g_hash_table_destroy (self->uri_entries);
    }
    self->uri_entries = g_hash_table_new (g_str_hash, g_str_equal);
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
server_settings_expand_cb (GtkExpander *expander, SyncConfigWidget *self)
{
    if (gtk_expander_get_expanded (expander)) {
        /* TRANSLATORS: this is the epander label for server settings */
        gtk_expander_set_label (expander, _("Hide server settings"));
    } else {
        gtk_expander_set_label (expander, _("Show server settings"));
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
sync_config_widget_set_unset (SyncConfigWidget *self,
                              gboolean unset)
{
    self->unset = unset;

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
    case PROP_UNSET:
        sync_config_widget_set_unset (self, g_value_get_boolean (value));
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
    case PROP_UNSET:
        g_value_set_boolean (value, self->unset);
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
    /* TRANSLATORS: title in service list for new services
       (there will be a entry to the right of the title) */
    config->name = g_strdup (_(""));

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
        gtk_widget_show (self->entry);
    } else {
        /**/
        gtk_widget_hide (self->entry);
    }

    syncevo_config_get_value (self->config->config, NULL, "syncURL", &url);
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

        /* TODO: show in UI */
        return;
    }
    sync_config_widget_real_init (self, config);

}

static gboolean
sync_config_widget_expose_event (GtkWidget      *widget,
                                 GdkEventExpose *event)
{
    GdkRectangle rect;
    GtkExpanderStyle style;
    gint indicator_x, indicator_y;
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
                   MAX (INDICATOR_SIZE, self->label_box->allocation.height);

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


    if (self->expanded) {
        style = GTK_EXPANDER_EXPANDED;
    } else {
        style = GTK_EXPANDER_COLLAPSED;
    }

    indicator_x = rect.x + widget->style->xthickness + INDICATOR_SIZE / 2;
    indicator_y = rect.y + (widget->style->ythickness * 2 +
                            MAX (self->label_box->allocation.height, INDICATOR_SIZE)) / 2;

    gtk_paint_expander (widget->style,
                        widget->window,
                        widget->state,
                        NULL,
                        widget,
                        NULL,
                        indicator_x,
                        indicator_y,
                        style);

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
    gint label_h;

    GTK_WIDGET_CLASS (sync_config_widget_parent_class)->size_allocate (widget,
                                                                       allocation);

    gtk_widget_size_request (self->label_box, &req);

    alloc.x = allocation->x + widget->style->xthickness;
    alloc.y = allocation->y + widget->style->ythickness;
    alloc.width = allocation->width - 2 * widget->style->xthickness;
    alloc.height = req.height;

    /* make room for indicator */
    alloc.x += INDICATOR_SIZE + widget->style->xthickness;
    alloc.width -= INDICATOR_SIZE + widget->style->xthickness;

    gtk_widget_size_allocate (self->label_box, &alloc);

    label_h = MAX (INDICATOR_SIZE, alloc.height);


    if (self->expanded) {
        gtk_widget_size_request (self->expando_box, &req);

        alloc.x = allocation->x + 2 * widget->style->xthickness;
        alloc.y = allocation->y + widget->style->ythickness +
                  label_h + CHILD_PADDING;
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
    requisition->height = MAX (req.height,
                               INDICATOR_SIZE +
                               widget->style->ythickness * 2);

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

    self->showing = TRUE;
    if (self->config && self->config->config) {
        syncevo_config_get_value (self->config->config,
                                  NULL, "ConsumerReady", &ready);

        if (ready && strcmp ("1", ready) == 0) {
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

    pspec = g_param_spec_boolean ("unset",
                                  "Unset",
                                  "Whether there is a currently used service at all",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_UNSET, pspec);

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
sync_config_widget_init (SyncConfigWidget *self)
{
    GtkWidget *tmp_box, *vbox, *table, *label;

    GTK_WIDGET_SET_FLAGS (GTK_WIDGET (self), GTK_NO_WINDOW);

    self->label_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_set_size_request (self->label_box, -1, SYNC_UI_LIST_ICON_SIZE + 6);
    gtk_widget_show (self->label_box);
    gtk_widget_set_parent (self->label_box, GTK_WIDGET (self));

    self->image = gtk_image_new ();
    gtk_widget_set_size_request (self->image, 
                                 SYNC_UI_LIST_ICON_SIZE,
                                 SYNC_UI_LIST_ICON_SIZE);
    gtk_widget_show (self->image);
    gtk_box_pack_start (GTK_BOX (self->label_box), self->image, FALSE, FALSE, 8);

    tmp_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (tmp_box);
    gtk_box_pack_start (GTK_BOX (self->label_box), tmp_box, FALSE, FALSE, 8);

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

    /* TRANSLATORS: linkbutton label */
    self->link = gtk_link_button_new_with_label ("", _("Launch website"));
    gtk_widget_set_no_show_all (self->link, TRUE);
    gtk_box_pack_start (GTK_BOX (vbox), self->link, TRUE, FALSE, 0);

    vbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vbox);
    gtk_box_pack_end (GTK_BOX (self->label_box), vbox, FALSE, FALSE, 32);

    /* TRANSLATORS: button label  */
    self->button = gtk_button_new_with_label (_("Setup now"));
    gtk_widget_set_size_request (self->button, SYNC_UI_LIST_BTN_WIDTH, -1);
    g_signal_connect (self->button, "clicked",
                      G_CALLBACK (setup_service_clicked), self);

    gtk_widget_show (self->button);
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

    table = gtk_table_new (3, 2, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (table), 2);
    gtk_table_set_col_spacings (GTK_TABLE (table), 5);
    gtk_widget_show (table);
    gtk_box_pack_start (GTK_BOX (tmp_box), table, FALSE, FALSE, 0);

    /* TRANSLATORS: labels of entries  */
    label = gtk_label_new (_("Username"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
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
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_widget_show (label);
    gtk_table_attach_defaults (GTK_TABLE (table), label,
                               0, 1,
                               1, 2);

    self->password_entry = gtk_entry_new ();
    gtk_widget_show (self->password_entry);
    gtk_entry_set_width_chars (GTK_ENTRY (self->password_entry), 40);
    gtk_entry_set_max_length (GTK_ENTRY (self->password_entry), 99);
    gtk_table_attach_defaults (GTK_TABLE (table), self->password_entry,
                               1, 2,
                               1, 2);

    self->expander = gtk_expander_new ("");
    gtk_widget_show (self->expander);
    gtk_box_pack_start (GTK_BOX (vbox), self->expander, FALSE, FALSE, 0);
    g_signal_connect (self->expander, "notify::expanded",
                      G_CALLBACK (server_settings_expand_cb), self);
    server_settings_expand_cb (GTK_EXPANDER (self->expander), self);

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

    /* TRANSLATORS: button labels  */
    self->use_button = gtk_button_new ();
    gtk_widget_show (self->use_button);
    gtk_box_pack_end (GTK_BOX (tmp_box), self->use_button, FALSE, FALSE, 8);
    g_signal_connect (self->use_button, "clicked",
                      G_CALLBACK (use_clicked_cb), self);

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
                        gboolean unset,
                        gboolean configured,
                        gboolean has_template)
{
  return g_object_new (SYNC_TYPE_CONFIG_WIDGET,
                       "server", server,
                       "name", name,
                       "current", current,
                       "unset", unset,
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
