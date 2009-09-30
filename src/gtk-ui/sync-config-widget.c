#include "config.h"

#include <glib/gi18n.h>
#include <gnome-keyring.h>

#include "sync-ui.h"
#include "sync-config-widget.h"


#ifdef USE_MOBLIN_UX
G_DEFINE_TYPE (SyncConfigWidget, sync_config_widget, NBTK_TYPE_GTK_EXPANDER)
#else
G_DEFINE_TYPE (SyncConfigWidget, sync_config_widget, GTK_TYPE_VBOX)
#endif

enum
{
  PROP_0,

  PROP_SERVER,
  PROP_CURRENT,
  PROP_DBUS_SERVICE,
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_EXPANDED,
	LAST_SIGNAL
};
static guint32 signals[LAST_SIGNAL] = {0, };

static void get_server_config_for_template_cb (SyncevoService *service, GPtrArray *options, GError *error, SyncConfigWidget *self);

static void
show_error_dialog (SyncConfigWidget *self, const char* message)
{
    GtkWindow *window = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self)));

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
remove_server_config_cb (SyncevoService *service, 
                         GError *error, 
                         SyncConfigWidget *self)
{
    if (error) {
        g_warning ("Failed to remove service configuration from SyncEvolution: %s", 
                   error->message);
        g_error_free (error);
    }
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static gboolean
update_value (char **str, GtkWidget *entry)
{
    const char *new_str;

    new_str = gtk_entry_get_text (GTK_ENTRY (entry));

    if ((*str == NULL && strlen (new_str) != 0) ||
        (*str != NULL && strcmp (*str, new_str) != 0)) {

        g_free (*str);
        *str = g_strdup (new_str);

        return TRUE;
    }
    return FALSE;
}

static void
stop_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    sync_config_widget_set_current (self, FALSE);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
add_to_acl_cb (GnomeKeyringResult result)
{
    if (result != GNOME_KEYRING_RESULT_OK)
        g_warning ("Adding server to GNOME keyring access control list failed: %s",
                   gnome_keyring_result_to_message (result));
}

static void
set_password_cb (GnomeKeyringResult result, guint32 id, gpointer data)
{
    if (result != GNOME_KEYRING_RESULT_OK) {
        g_warning ("setting password in GNOME keyring failed: %s",
                   gnome_keyring_result_to_message (result));
        return;
    }

    /* add the server to access control list */
    /* TODO: name and path must match the ones syncevo-dbus-server really has,
     * so this call should be in the dbus-wrapper library */
    gnome_keyring_item_grant_access_rights (NULL, 
                                            "SyncEvolution",
                                            LIBEXECDIR "/syncevo-dbus-server",
                                            id,
                                            GNOME_KEYRING_ACCESS_READ,
                                            (GnomeKeyringOperationDoneCallback)add_to_acl_cb,
                                            NULL, NULL);
}

static void
save_password (const server_config *config)
{
    char *server_address;
    char *password;
    char *username;

    server_address = strstr (config->base_url, "://");
    if (server_address)
        server_address = server_address + 3;

    password = config->password;
    if (!password)
        password = "";

    username = config->username;
    if (!username)
        username = "";

    gnome_keyring_set_network_password (NULL, /* default keyring */
                                        username,
                                        NULL,
                                        server_address,
                                        NULL,
                                        NULL,
                                        NULL,
                                        0,
                                        password,
                                        (GnomeKeyringOperationGetIntCallback)set_password_cb,
                                        NULL,
                                        NULL);
}

static void
set_server_config_cb (SyncevoService *service, GError *error, SyncConfigWidget *self)
{
    if (error) {
        show_error_dialog (self,
                           _("Failed to save service configuration to SyncEvolution"));
        g_warning ("Failed to save service configuration to SyncEvolution: %s",
                   error->message);
        g_error_free (error);
        return;
    }

g_debug ("emit change");
    sync_config_widget_set_current (self, TRUE);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);

}

static void
use_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    GPtrArray *options;
    server_config *config;
    GList *l, *entry;

    if (!self->server || !self->dbus_service) {
        return;
    }

    config = self->config;

    /* compare config and stuff in entries... */
    update_value (&config->name, self->entry);

    if (update_value (&config->username, self->username_entry))
        self->auth_changed = TRUE;

    if (update_value (&config->password, self->password_entry))
        self->auth_changed = TRUE;

    if (update_value (&config->base_url, self->baseurl_entry))
        self->auth_changed = TRUE;

    entry = self->uri_entries;
    for (l = self->config->source_configs; l && entry; l = l->next) {
        source_config *source = (source_config*)l->data;

        update_value (&source->uri, GTK_WIDGET (entry->data));
        if (!source->uri)
            source->enabled = FALSE;
        entry = entry->next;
    }

    if (!config->name || strlen (config->name) == 0 ||
        !config->base_url || strlen (config->base_url) == 0) {
        show_error_dialog (self, 
                           _("Service must have a name and server URL"));
        return;
    }

    /* make a wild guess if no scheme in url */
    if (strstr (config->base_url, "://") == NULL) {
        char *tmp = g_strdup_printf ("http://%s", config->base_url);
        g_free (config->base_url);
        config->base_url = tmp;
    }

    if (self->auth_changed)
        save_password (self->config);

    /* save the server, let callback change current server gconf key */
    options = server_config_get_option_array (self->config);
    syncevo_service_set_server_config_async (self->dbus_service, 
                                             config->name,
                                             options,
                                             (SyncevoSetServerConfigCb)set_server_config_cb, 
                                             self);
    g_ptr_array_foreach (options, (GFunc)syncevo_option_free, NULL);
    g_ptr_array_free (options, TRUE);
}

static void
reset_delete_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    const char *name;

    if (!self->server || !self->dbus_service) {
        return;
    }

    syncevo_server_get (self->server, &name, NULL, NULL, NULL);
    if (self->config->from_template) {
        syncevo_service_get_template_config_async (self->dbus_service, 
                                                   (char*)name,
                                                   (SyncevoGetTemplateConfigCb)get_server_config_for_template_cb,
                                                   self);

    } else {
        syncevo_service_remove_server_config_async (self->dbus_service,
                                                   (char*)name,
                                                    (SyncevoRemoveServerConfigCb)remove_server_config_cb,
                                                    self);
        
    }
    
}

static void
sync_config_widget_update_expander (SyncConfigWidget *self)
{
    GList *l;
    const char *str;
    GtkWidget *label, *entry;
    guint i = 0;

    gtk_container_foreach (GTK_CONTAINER (self->server_settings_table),
                           (GtkCallback)remove_child,
                           self->server_settings_table);
    gtk_table_resize (GTK_TABLE (self->server_settings_table), 
                      2, g_list_length (self->config->source_configs) + 1);

    gtk_entry_set_text (GTK_ENTRY (self->entry),
                        self->config->name ? self->config->name : "");

    if (self->config->name) {
        gtk_widget_hide (self->entry);
    } else {
        gtk_widget_show (self->entry);
    }

    gtk_label_set_text (GTK_LABEL (self->description_label),
                        get_service_description (self->config->name));

    gtk_expander_set_expanded (GTK_EXPANDER (self->expander), 
                               !self->config->name);

    if (self->config->from_template) {
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Reset service"));
    } else {
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Delete service"));
        if (self->config->name) {
            gtk_widget_show (GTK_WIDGET (self->reset_delete_button));
        } else {
            gtk_widget_hide (GTK_WIDGET (self->reset_delete_button));
        }
    }

    if (self->current) {
        gtk_widget_show (self->stop_button);
    } else { 
        gtk_widget_hide (self->stop_button);
    }

    if (self->config->username &&
        strcmp (self->config->username, "your SyncML server account name")) {
        str = self->config->username;
    } else {
        str = "";
    }
    gtk_entry_set_text (GTK_ENTRY (self->username_entry), str);

    gtk_entry_set_text (GTK_ENTRY (self->password_entry),
                        self->config->password ? self->config->password : "");

    label = gtk_label_new (_("Server URL"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_widget_show (label);
    gtk_table_attach (GTK_TABLE (self->server_settings_table), label,
                      0, 1, i, i + 1, GTK_FILL, GTK_EXPAND, 0, 0);

    self->baseurl_entry = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (self->baseurl_entry), 99);
    gtk_entry_set_width_chars (GTK_ENTRY (self->baseurl_entry), 80);
    gtk_entry_set_text (GTK_ENTRY (self->baseurl_entry), 
                        self->config->base_url ? self->config->base_url : "");
    gtk_widget_show (self->baseurl_entry);
    gtk_table_attach_defaults (GTK_TABLE (self->server_settings_table),
                               self->baseurl_entry,
                               1, 2, i, i + 1);

    g_list_free (self->uri_entries);
    self->uri_entries = NULL;

    for (l = self->config->source_configs; l; l = l->next) {
        source_config *source = (source_config*)l->data;
        char *str;
        char *name;
        i++;

        name = get_pretty_source_name (source->name);
        /* TRANSLATORS: placeholder is a source name in settings window */
        str = g_strdup_printf (_("%s URI"), name);
        label = gtk_label_new (str);
        g_free (str);
        g_free (name);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_widget_show (label);
        gtk_table_attach (GTK_TABLE (self->server_settings_table), label,
                          0, 1, i, i + 1, GTK_FILL, GTK_EXPAND, 0, 0);

        entry = gtk_entry_new ();
        gtk_entry_set_max_length (GTK_ENTRY (entry), 99);
        gtk_entry_set_width_chars (GTK_ENTRY (entry), 80);
        gtk_entry_set_text (GTK_ENTRY (entry), 
                            source->uri ? source->uri : "");
        self->uri_entries = g_list_append (self->uri_entries, entry);
        gtk_widget_show (entry);
        gtk_table_attach_defaults (GTK_TABLE (self->server_settings_table), entry,
                                   1, 2, i, i + 1);
    }

    /* hack for widgets added with "add new service" */
    gtk_widget_grab_focus (GTK_WIDGET (self));
}

static void
sync_config_widget_set_config (SyncConfigWidget *self,
                                      GPtrArray *options,
                                      GPtrArray *options_override)
{
    const char *name = NULL;

    if (self->config)
        server_config_free (self->config);
    self->config = g_slice_new0 (server_config);

    /* take name from original SyncevoServer (from GetConfigs call) */
    if (self->server)
        syncevo_server_get (self->server, &name, NULL, NULL, NULL);
    self->config->name = g_strdup (name);

    if (options)
        g_ptr_array_foreach (options, (GFunc)add_server_option, self->config);
    if (options_override)
        g_ptr_array_foreach (options_override, (GFunc)add_server_option, self->config);

    server_config_ensure_default_sources_exist (self->config);

    self->config->changed = TRUE;
}

static void
find_password_for_settings_cb (GnomeKeyringResult result, GList *list, SyncConfigWidget *self)
{
    switch (result) {
    case GNOME_KEYRING_RESULT_NO_MATCH:
        g_warning ("no password found in keyring");
        break;
    case GNOME_KEYRING_RESULT_OK:
        if (list && list->data) {
            GnomeKeyringNetworkPasswordData *key_data;
            key_data = (GnomeKeyringNetworkPasswordData*)list->data;
            self->config->password = g_strdup (key_data->password);
            sync_config_widget_update_expander (self);
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
get_server_config_for_template_cb (SyncevoService *service, GPtrArray *options, GError *error, SyncConfigWidget *self)
{
    if (error) {
        g_warning ("Failed to get service configuration from SyncEvolution: %s",
                   error->message);
        g_error_free (error);
    } else {
        char *server_address;
        
        sync_config_widget_set_config (self, options, self->options_override);
        sync_config_widget_update_expander (self);

        /* get password from keyring if we have an url */
        if (self->config->base_url) {
            server_address = strstr (self->config->base_url, "://");
            if (server_address)
                server_address = server_address + 3;

            if (!server_address) {
                g_warning ("Server configuration has suspect URL '%s'",
                           self->config->base_url);
            } else {
                gnome_keyring_find_network_password (self->config->username,
                                                     NULL,
                                                     server_address,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     0,
                                                     (GnomeKeyringOperationGetListCallback)find_password_for_settings_cb,
                                                     self,
                                                     NULL);
            }
        }

        if (options) {
            g_ptr_array_foreach (options, (GFunc)syncevo_option_free, NULL);
            g_ptr_array_free (options, TRUE);
        }
        if (self->options_override) {
            g_ptr_array_foreach (self->options_override, (GFunc)syncevo_option_free, NULL);
            g_ptr_array_free (self->options_override, TRUE);
            self->options_override = NULL;
        }
    }
}

static void
fetch_server_config (SyncConfigWidget *self)
{
    const char *name;

    if (!self->server || !self->dbus_service) {
        return;
    }

    syncevo_server_get (self->server, &name, NULL, NULL, NULL);
    if (!name) {
        /* get a fresh template */
        name = "default";
        SyncevoOption *option;

        self->options_override = g_ptr_array_new ();
        /* syncevolution defaults are not empty, override ... */
        self->options_override = g_ptr_array_new ();
        option = syncevo_option_new (NULL, "username", NULL);
        g_ptr_array_add (self->options_override, option);
        option = syncevo_option_new (NULL, "password", NULL);
        g_ptr_array_add (self->options_override, option);
        option = syncevo_option_new (NULL, "syncURL", NULL);
        g_ptr_array_add (self->options_override, option);
        option = syncevo_option_new (NULL, "webURL", NULL);
        g_ptr_array_add (self->options_override, option);
        option = syncevo_option_new (NULL, "fromTemplate", "no");
        g_ptr_array_add (self->options_override, option);
        option = syncevo_option_new ("memo", "uri", NULL);
        g_ptr_array_add (self->options_override, option);
        option = syncevo_option_new ("todo", "uri", NULL);
        g_ptr_array_add (self->options_override, option);
        option = syncevo_option_new ("addressbook", "uri", NULL);
        g_ptr_array_add (self->options_override, option);
        option = syncevo_option_new ("calendar", "uri", NULL);
        g_ptr_array_add (self->options_override, option);
        
    }


    syncevo_service_get_server_config_async (self->dbus_service, 
                                             (char*)name,
                                             (SyncevoGetServerConfigCb)get_server_config_for_template_cb,
                                             self);
}

static void
setup_service_clicked (GtkButton *btn, SyncConfigWidget *self)
{
    sync_config_widget_set_expanded (self, TRUE);
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
update_label (SyncConfigWidget *self)
{
    if (self->server) {
        const char *name, *url;
        char *str;
        syncevo_server_get (self->server, &name, &url, NULL, NULL);

        if (name) {
            if (self->current) {
                str = g_strdup_printf ("<b>%s</b>", name);
            } else {
                str = g_strdup_printf ("%s", name);
            }
            if (url && strlen (url) > 0) {
                char *tmp = g_strdup_printf ("%s -",str);
                g_free (str);
                str = tmp;
            }
        } else {
            str = g_strdup ("Server name");
        }
        gtk_label_set_markup (GTK_LABEL (self->label), str);
        g_free (str);
    }
}


void
sync_config_widget_set_dbus_service (SyncConfigWidget *self,
                                            SyncevoService *dbus_service)
{
    self->dbus_service = dbus_service;

    fetch_server_config (self);
}

void
sync_config_widget_set_current (SyncConfigWidget *self,
                                       gboolean current)
{
    if (self->current != current) {
        self->current = current;
        update_label (self);
    }
}

void
sync_config_widget_set_server (SyncConfigWidget *self,
                                      SyncevoServer *server)
{
    if (self->server) {
        syncevo_server_free (self->server);
        self->server = NULL;
    }
    if (!server && !self->server) {
        return;
    }

    self->server = server;

    if (!server) {
        gtk_image_clear (GTK_IMAGE (self->image));
        gtk_label_set_markup (GTK_LABEL (self->label), "");
        gtk_widget_hide (self->link);
    } else {
        const char *url, *icon;
        GdkPixbuf *buf;

        syncevo_server_get (server, NULL, &url, &icon, NULL);

        buf = load_icon (icon, SYNC_UI_LIST_ICON_SIZE);
        gtk_image_set_from_pixbuf (GTK_IMAGE (self->image), buf);
        g_object_unref (buf);

        update_label (self);

        if (url && strlen (url) > 0) {
            gtk_link_button_set_uri (GTK_LINK_BUTTON (self->link), url);
            gtk_widget_show (self->link);
        } else {
            gtk_widget_hide (self->link);
        }

        fetch_server_config (self);
    }

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
    case PROP_CURRENT:
        sync_config_widget_set_current (self, g_value_get_boolean (value));
        break;
    case PROP_DBUS_SERVICE:
        sync_config_widget_set_dbus_service (self, g_value_get_object (value));
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
    case PROP_CURRENT:
        g_value_set_boolean (value, self->current);
    case PROP_DBUS_SERVICE:
        g_value_set_object (value, self->dbus_service);

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
sync_config_widget_class_init (SyncConfigWidgetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

    object_class->set_property = sync_config_widget_set_property;
    object_class->get_property = sync_config_widget_get_property;
    object_class->dispose = sync_config_widget_dispose;

    pspec = g_param_spec_pointer ("server",
                                  "SyncevoServer",
                                  "The SyncevoServer struct this widget represents",
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_SERVER, pspec);

    pspec = g_param_spec_boolean ("current",
                                  "Current",
                                  "Whether the server is currently used",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CURRENT, pspec);

    pspec = g_param_spec_object ("dbus-service",
                                 "DBus service",
                                 "The SyncevoService DBus wrapper object",
                                  SYNCEVO_TYPE_SERVICE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_DBUS_SERVICE, pspec);

    signals[SIGNAL_CHANGED] = 
            g_signal_new ("changed",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncConfigWidgetClass, changed),
                          NULL, NULL,
                          g_cclosure_marshal_VOID__VOID,
                          G_TYPE_NONE, 
                          0);
    signals[SIGNAL_EXPANDED] = 
            g_signal_new ("expanded",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncConfigWidgetClass, expanded),
                          NULL, NULL,
                          g_cclosure_marshal_VOID__VOID,
                          G_TYPE_NONE, 
                          0);
}

static void
sync_config_widget_init (SyncConfigWidget *self)
{
    GtkWidget *tmp_box, *hbox, *vbox, *table, *label;

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_set_size_request (hbox, -1, SYNC_UI_LIST_ICON_SIZE + 6);
    gtk_widget_show (hbox);

    self->image = gtk_image_new ();
    gtk_widget_set_size_request (self->image, 
                                 SYNC_UI_LIST_ICON_SIZE,
                                 SYNC_UI_LIST_ICON_SIZE);
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

    self->link = gtk_link_button_new_with_label ("", _("Launch website"));
    gtk_widget_set_no_show_all (self->link, TRUE);
    gtk_box_pack_start (GTK_BOX (vbox), self->link, TRUE, FALSE, 0);

    vbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vbox);
    gtk_box_pack_end (GTK_BOX (hbox), vbox, FALSE, FALSE, 32);

    self->button = gtk_button_new_with_label (_("Setup now"));
    gtk_widget_set_size_request (self->button, SYNC_UI_LIST_BTN_WIDTH, -1);

    g_signal_connect (self->button, "clicked",
                      G_CALLBACK (setup_service_clicked), self);

    gtk_widget_show (self->button);
    gtk_box_pack_start (GTK_BOX (vbox), self->button, TRUE, FALSE, 0);

    /* label widget built */
#ifdef USE_MOBLIN_UX
    nbtk_gtk_expander_set_label_widget (NBTK_GTK_EXPANDER (self), hbox);
    nbtk_gtk_expander_set_has_indicator (NBTK_GTK_EXPANDER (self), FALSE);

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (hbox);
    gtk_container_add (GTK_CONTAINER (self), hbox);
#else
    gtk_box_pack_start (GTK_BOX (self), hbox, TRUE, TRUE, 0);

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_set_no_show_all (hbox, TRUE);
    gtk_box_pack_start (GTK_BOX (self), hbox, TRUE, TRUE, 8);
    self->expando_box_for_gtk = hbox;
#endif

    vbox = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox);
    gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 16);

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

    self->expander = gtk_expander_new (_("Show server settings"));
    gtk_widget_show (self->expander);
    gtk_box_pack_start (GTK_BOX (vbox), self->expander, FALSE, FALSE, 0);

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

    self->use_button = gtk_button_new_with_label ("Save and use");
    gtk_widget_show (self->use_button);
    gtk_box_pack_end (GTK_BOX (tmp_box), self->use_button, FALSE, FALSE, 8);
    g_signal_connect (self->use_button, "clicked",
                      G_CALLBACK (use_clicked_cb), self);

    self->stop_button = gtk_button_new_with_label ("Stop using service");
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
                        gboolean current,
                        SyncevoService *dbus_service)
{
  return g_object_new (SYNC_TYPE_CONFIG_WIDGET,
                       "server", server,
                       "current", current,
                       "dbus_service", dbus_service,
                       NULL);
}

void
sync_config_widget_set_expanded (SyncConfigWidget *widget, gboolean expanded)
{
    if (expanded) {
        gtk_widget_hide (widget->button);
#ifdef USE_MOBLIN_UX
        nbtk_gtk_expander_set_expanded (NBTK_GTK_EXPANDER (widget), TRUE);
#else
        gtk_widget_show (widget->expando_box_for_gtk);
#endif
        g_signal_emit (widget, signals[SIGNAL_EXPANDED], 0);
    } else {
        gtk_widget_show (widget->button);
#ifdef USE_MOBLIN_UX
        nbtk_gtk_expander_set_expanded (NBTK_GTK_EXPANDER (widget), FALSE);
#else
        gtk_widget_hide (widget->expando_box_for_gtk);
#endif
    }

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
