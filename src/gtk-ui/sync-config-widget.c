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
  PROP_NAME,
  PROP_CURRENT,
  PROP_UNSET,
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_EXPANDED,
	LAST_SIGNAL
};
static guint32 signals[LAST_SIGNAL] = {0, };

/*
static void get_server_config_for_template_cb (SyncevoService *service, GPtrArray *options, GError *error, SyncConfigWidget *self);
*/
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

/*
static void
set_server_config_cb (SyncevoService *service, GError *error, SyncConfigWidget *self)
{
    if (error) {
        show_error_dialog (GTK_WIDGET (self),
                           _("Failed to save service configuration to SyncEvolution"));
        g_warning ("Failed to save service configuration to SyncEvolution: %s",
                   error->message);
        g_error_free (error);
        return;
    }

    sync_config_widget_set_current (self, TRUE);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);

}
*/

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
use_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    SyncevoConfig *config;
    const char *username, *password, *sync_url, *name;
    char *real_url;
    gboolean keyring_changed = FALSE;

    if (!self->config) {
        return;
    }

    config = self->config->config;

    name = gtk_entry_get_text (GTK_ENTRY (self->entry));

    username = gtk_entry_get_text (GTK_ENTRY (self->username_entry));
    if (syncevo_config_set_value (config, NULL, "username", username)) {
        keyring_changed = TRUE;
    }

    sync_url = gtk_entry_get_text (GTK_ENTRY (self->baseurl_entry));
    if (syncevo_config_set_value (config, NULL, "syncURL", sync_url)) {
        keyring_changed = TRUE;
    }

    password = gtk_entry_get_text (GTK_ENTRY (self->password_entry));
    if (self->keyring_password) {
        if (strcmp (self->keyring_password, password)) {
            keyring_changed = TRUE;
        }
    } else {
        keyring_changed = FALSE;
        syncevo_config_set_value (config, NULL, "password", password);
    }


    if (!name || strlen (name) == 0 ||
        !sync_url || strlen (sync_url) == 0) {
        show_error_dialog (GTK_WIDGET (self), 
                           _("Service must have a name and server URL"));
        return;
    }

    syncevo_config_foreach_source (config,
                                   (ConfigFunc)update_source_config,
                                   self);

    /* make a wild guess if no scheme in url */
    if (strstr (sync_url, "://") == NULL) {
        real_url = g_strdup_printf ("http://%s", sync_url);
    } else {
        real_url = g_strdup (sync_url);
    }

    if (keyring_changed) {
        gnome_keyring_set_network_password (NULL, /* default keyring */
                                            username,
                                            NULL,
                                            real_url,
                                            NULL,
                                            NULL,
                                            NULL,
                                            0,
                                            self->keyring_password,
                                            (GnomeKeyringOperationGetIntCallback)set_password_cb,
                                            NULL,
                                            NULL);
    }

    /* save the server, let callback change current server gconf key */

    /* TODO:
     *   StartSession,
     *   SetConfig
     *   Detach
     */
}

static void
reset_delete_clicked_cb (GtkButton *btn, SyncConfigWidget *self)
{
    const char *name;

    if (!self->config) {
        return;
    }

    /* TODO:
     *   StartSession,
     *   SetConfig
     *   Detach
     */
    
}

static void 
update_use_button (SyncConfigWidget *self)
{
    if (!self->use_button){ 
        return;
    }
    
    if (self->unset || self->current) {
        gtk_button_set_label (GTK_BUTTON (self->use_button),
                              _("Save and use"));
    } else { 
        gtk_button_set_label (GTK_BUTTON (self->use_button),
                              _("Save and replace\ncurrent service"));
    }
}


static void
init_source (char *name,
             GHashTable *source_configuration,
             SyncConfigWidget *self)
{
    char *str, *pretty_name;
    const char *uri;
    guint row;
    GtkWidget *label, *entry;

    g_object_get (self->server_settings_table,
                  "n-rows", &row,
                  NULL);

    uri = g_hash_table_lookup (source_configuration, "uri");
    pretty_name = get_pretty_source_name (name);

    /* TRANSLATORS: label for an entry in service configuration.
     * Placeholder is a source  name in settings window. 
     * Example: "Addressbook URI" */
    str = g_strdup_printf (_("%s URI"), pretty_name);
    label = gtk_label_new (str);
    g_free (str);
    g_free (pretty_name);

    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_widget_show (label);
    gtk_table_attach (GTK_TABLE (self->server_settings_table), label,
                      0, 1, row, row + 1, GTK_FILL, GTK_EXPAND, 0, 0);

    entry = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (entry), 99);
    gtk_entry_set_width_chars (GTK_ENTRY (entry), 80);
    gtk_entry_set_text (GTK_ENTRY (entry),
                        uri ? uri : "");
    g_hash_table_insert (self->uri_entries, name, entry);
    gtk_widget_show (entry);
    gtk_table_attach_defaults (GTK_TABLE (self->server_settings_table), entry,
                               1, 2, row, row + 1);
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

    gtk_entry_set_text (GTK_ENTRY (self->entry),
                        self->config->name ? self->config->name : "");

    gtk_label_set_text (GTK_LABEL (self->description_label),
                        get_service_description (self->config->name));

    /* TODO set expanded status based on user pref? */

    if (self->from_template) {
        /* TRANSLATORS: button labels */
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Reset service"));
    } else {
        gtk_button_set_label (GTK_BUTTON (self->reset_delete_button),
                              _("Delete service"));
        if (self->saved) {
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
    update_use_button (self);

    syncevo_config_get_value (config, NULL, "username", &username);
    syncevo_config_get_value (config, NULL, "password", &password);
    syncevo_config_get_value (config, NULL, "syncURL", &sync_url);

    gtk_entry_set_text (GTK_ENTRY (self->username_entry), username);
    if (password && strcmp (password, "-") == 0) {
        gtk_entry_set_text (GTK_ENTRY (self->password_entry), "");
    } else {
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
    gtk_entry_set_text (GTK_ENTRY (self->baseurl_entry), sync_url);
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
find_password_cb (GnomeKeyringResult result, GList *list, SyncConfigWidget *self)
{
    switch (result) {
    case GNOME_KEYRING_RESULT_NO_MATCH:
        break;
    case GNOME_KEYRING_RESULT_OK:
        if (list && list->data) {
            GnomeKeyringNetworkPasswordData *key_data;
            key_data = (GnomeKeyringNetworkPasswordData*)list->data;

            gtk_entry_set_text (GTK_ENTRY (self->password_entry), key_data->password);
            self->keyring_password = g_strdup (key_data->password);
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
        if (!self->from_template) {
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

#ifdef USE_MOBLIN_UX
static void
widget_expanded_cb (SyncConfigWidget *self)
{
    if (nbtk_gtk_expander_get_expanded (NBTK_GTK_EXPANDER (self))) {
        gtk_widget_hide (self->button);
    } else {
        gtk_widget_show (self->button);
    }
}
#endif

void
sync_config_widget_set_unset (SyncConfigWidget *self,
                              gboolean unset)
{
    self->unset = unset;

    update_use_button (self);
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

    gtk_widget_set_sensitive (self->reset_delete_button, !self->running_session);
    gtk_widget_set_sensitive (self->use_button, !self->running_session);

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
        /* TODO disconnect signals ?*/
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
    case PROP_UNSET:
        sync_config_widget_set_unset (self, g_value_get_boolean (value));
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
    case PROP_UNSET:
        g_value_set_boolean (value, self->unset);
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
                              SyncevoConfig *config,
                              gboolean saved)
{
    char *url, *icon;
    GdkPixbuf *buf;
    server_config_init (self->config, config);

/* TODO find out "from_template" value... it seems I need to do two GetConfig calls
 * for each service :( */
    self->from_template = TRUE;
    self->saved = saved;
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

    /* hack for widgets added with "add new service" */
    gtk_widget_grab_focus (GTK_WIDGET (self));
}

static void
get_template_config_cb (SyncevoServer *syncevo,
                        SyncevoConfig *config,
                        GError *error,
                        SyncConfigWidget *self)
{
    if (error) {
        g_error_free (error);

        /* TODO: decide if there are cases where we want to show this in UI */
        return;
    }

    sync_config_widget_real_init (self, config, FALSE);
}

static void
get_config_cb (SyncevoServer *syncevo,
               SyncevoConfig *config,
               GError *error,
               SyncConfigWidget *self)
{
    char *password, *username, *url;

    if (error) {
        g_error_free (error);

        /* no configuration was found, ask for a template */
        syncevo_server_get_config (self->server,
                                   self->config->name,
                                   TRUE,
                                   (SyncevoServerGetConfigCb)get_template_config_cb,
                                   self);

        /* TODO: check that this really is a "no such config" error */
        /* TODO: decide if there are cases where we want to show this in UI */
        return;
    }

    sync_config_widget_real_init (self, config, TRUE);

    self->keyring_password = NULL;

    /* see if we need a password from keyring */
    syncevo_config_get_value (config, NULL, "password", &password);
    syncevo_config_get_value (config, NULL, "username", &username);
    syncevo_config_get_value (config, NULL, "syncURL", &url);

    if (url && username &&
        password && strcmp (password, "-") == 0) {

        const char *server_address;

        server_address = strstr (url, "://");
        if (server_address) 
            server_address = server_address + 3;

        if (!server_address) {
            g_warning ("Server configuration has suspect URL '%s',"
                       " not getting a password from keyring",
                       url);
        } else {
            gnome_keyring_find_network_password (username,
                                                 NULL,
                                                 server_address,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 0,
                                                 (GnomeKeyringOperationGetListCallback)find_password_cb,
                                                 self,
                                                 NULL);
        }
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

    /* try to fetch a already set configuration */
    syncevo_server_get_config (self->server,
                               self->config->name,
                               FALSE,
                               (SyncevoServerGetConfigCb)get_config_cb,
                               self);
    return G_OBJECT (self);
}

static void
sync_config_widget_class_init (SyncConfigWidgetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

    object_class->set_property = sync_config_widget_set_property;
    object_class->get_property = sync_config_widget_get_property;
    object_class->dispose = sync_config_widget_dispose;
    object_class->constructor = sync_config_widget_constructor;

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
                                  "Whether the server is currently used",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CURRENT, pspec);

    pspec = g_param_spec_boolean ("unset",
                                  "Unset",
                                  "Whether there is a curently used server at all",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_UNSET, pspec);

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

    /* TRANSLATORS: linkbutton label */
    self->link = gtk_link_button_new_with_label ("", _("Launch website"));
    gtk_widget_set_no_show_all (self->link, TRUE);
    gtk_box_pack_start (GTK_BOX (vbox), self->link, TRUE, FALSE, 0);

    vbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vbox);
    gtk_box_pack_end (GTK_BOX (hbox), vbox, FALSE, FALSE, 32);

    /* TRANSLATORS: button label  */
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
    g_signal_connect (self, "notify::expanded",
                      G_CALLBACK (widget_expanded_cb), self);

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
                        gboolean unset)
{
  return g_object_new (SYNC_TYPE_CONFIG_WIDGET,
                       "server", server,
                       "name", name,
                       "current", current,
                       "unset", unset,
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
