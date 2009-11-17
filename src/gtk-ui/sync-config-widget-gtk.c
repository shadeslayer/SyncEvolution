#include "config.h"

#ifndef USE_MOBLIN_UX


#include <glib/gi18n.h>
#include <gnome-keyring.h>


#include "sync-ui.h"
#include "sync-config-widget.h"
#include "sync-config-widget-gtk.h"


G_DEFINE_TYPE (SyncConfigWidgetGtk, sync_config_widget_gtk, GTK_TYPE_HBOX)


enum
{
  PROP_0,

  PROP_SERVER,
  PROP_CURRENT,
  PROP_DBUS_SERVICE,
};

static void
sync_config_widget_gtk_set_config (SyncConfigWidgetGtk *self, 
                                   server_config *config)
{
    if (self->config)
        server_config_free (self->config);
    self->config = config;
}

static void
find_password_for_settings_cb (GnomeKeyringResult result, GList *list, server_data *data)
{
    switch (result) {
    case GNOME_KEYRING_RESULT_NO_MATCH:
        g_warning ("no password found in keyring");
        break;
    case GNOME_KEYRING_RESULT_OK:
        if (list && list->data) {
            GnomeKeyringNetworkPasswordData *key_data;
            key_data = (GnomeKeyringNetworkPasswordData*)list->data;
            data->config->password = g_strdup (key_data->password);
        }
        break;
    default:
        g_warning ("getting password from GNOME keyring failed: %s",
                   gnome_keyring_result_to_message (result));
        break;
    }

    sync_config_widget_gtk_set_config (SYNC_CONFIG_WIDGET_GTK (data->data), data->config);

    server_data_free (data, FALSE);
    return;
}

static void
get_server_config_for_template_cb (SyncevoService *service, GPtrArray *options, GError *error, server_data *data)
{
    gboolean getting_password = FALSE;

    if (error) {
        g_warning ("Failed to get service configuration from SyncEvolution: %s",
                   error->message);
        g_error_free (error);
        server_data_free (data, TRUE);
    } else {
        char *server_address;

        g_ptr_array_foreach (options, (GFunc)add_server_option, data->config);
        if (data->options_override)
            g_ptr_array_foreach (data->options_override, (GFunc)add_server_option, data->config);

        server_config_ensure_default_sources_exist (data->config);
        
        data->config->changed = TRUE;

        /* get password from keyring if we have an url */
        if (data->config->base_url) {
            server_address = strstr (data->config->base_url, "://");
            if (server_address)
                server_address = server_address + 3;

            if (!server_address) {
                g_warning ("Server configuration has suspect URL '%s'",
                           data->config->base_url);
            } else {
                gnome_keyring_find_network_password (data->config->username,
                                                     NULL,
                                                     server_address,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     0,
                                                     (GnomeKeyringOperationGetListCallback)find_password_for_settings_cb,
                                                     data,
                                                     NULL);
                getting_password = TRUE;
            }
        }

        if (!getting_password) {
            sync_config_widget_gtk_set_config (SYNC_CONFIG_WIDGET_GTK (data->data),
                                                  data->config);

            server_data_free (data, FALSE);
        }

        if (options) {
            g_ptr_array_foreach (options, (GFunc)syncevo_option_free, NULL);
            g_ptr_array_free (options, TRUE);
        }
    }
}

static void
fetch_server_config (SyncConfigWidgetGtk *self)
{
    const char *name;

    if (!self->server || !self->dbus_service) {
        return;
    }

    syncevo_server_get (self->server, &name, NULL, NULL, NULL);
    syncevo_service_get_server_config_async (self->dbus_service, 
                                             (char*)name,
                                             (SyncevoGetServerConfigCb)get_server_config_for_template_cb,
                                             server_data_new (name, (gpointer)self));
}

static void
setup_service_clicked (GtkButton *btn, SyncConfigWidgetGtk *self)
{
    show_settings_window (hack_data, self->config);
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
update_label (SyncConfigWidgetGtk *self)
{
    if (self->server) {
        const char *name, *url;
        char *str;
        syncevo_server_get (self->server, &name, &url, NULL, NULL);

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
        gtk_label_set_markup (GTK_LABEL (self->label), str);
        g_free (str);
    }
}


void
sync_config_widget_gtk_set_dbus_service (SyncConfigWidgetGtk *self,
                                            SyncevoService *dbus_service)
{
    self->dbus_service = dbus_service;

    fetch_server_config (self);
}

void
sync_config_widget_gtk_set_current (SyncConfigWidgetGtk *self,
                                       gboolean current)
{
    self->current = current;

    update_label (self);
}

void
sync_config_widget_gtk_set_server (SyncConfigWidgetGtk *self,
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
        g_debug (" 1 %d", (int)self);
        gtk_image_clear (GTK_IMAGE (self->image));
        g_debug ("...1");
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
sync_config_widget_gtk_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    SyncConfigWidgetGtk *self = SYNC_CONFIG_WIDGET_GTK (object);

    switch (property_id) {
    case PROP_SERVER:
        sync_config_widget_gtk_set_server (self, g_value_get_pointer (value));
        break;
    case PROP_CURRENT:
        sync_config_widget_gtk_set_current (self, g_value_get_boolean (value));
        break;
    case PROP_DBUS_SERVICE:
        sync_config_widget_gtk_set_dbus_service (self, g_value_get_object (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
sync_config_widget_gtk_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    SyncConfigWidgetGtk *self = SYNC_CONFIG_WIDGET_GTK (object);

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
sync_config_widget_gtk_dispose (GObject *object)
{
    SyncConfigWidgetGtk *self = SYNC_CONFIG_WIDGET_GTK (object);

    sync_config_widget_gtk_set_server (self, NULL);

    G_OBJECT_CLASS (sync_config_widget_gtk_parent_class)->dispose (object);
}

static void
sync_config_widget_gtk_class_init (SyncConfigWidgetGtkClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

    object_class->set_property = sync_config_widget_gtk_set_property;
    object_class->get_property = sync_config_widget_gtk_get_property;
    object_class->dispose = sync_config_widget_gtk_dispose;

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
}

static void
sync_config_widget_gtk_init (SyncConfigWidgetGtk *self)
{
    GtkWidget *tmp_box, *hbox, *vbox;

    hbox = gtk_hbox_new (FALSE, 8);
    gtk_widget_set_size_request (hbox, -1, SYNC_UI_LIST_ICON_SIZE + 6);
    gtk_widget_show (hbox);

    gtk_box_pack_start (GTK_BOX (self), hbox, TRUE, TRUE, 0);

    self->image = gtk_image_new ();
    gtk_widget_set_size_request (self->image, 
                                 SYNC_UI_LIST_ICON_SIZE,
                                 SYNC_UI_LIST_ICON_SIZE);
    gtk_widget_show (self->image);
    gtk_box_pack_start (GTK_BOX (hbox), self->image, FALSE, FALSE, 0);

    tmp_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (tmp_box);
    gtk_box_pack_start (GTK_BOX (hbox), tmp_box, FALSE, FALSE, 0);

    self->label = gtk_label_new ("");
    gtk_misc_set_alignment (GTK_MISC (self->label), 0.0, 0.5);
    gtk_widget_show (self->label);
    gtk_box_pack_start (GTK_BOX (tmp_box), self->label, FALSE, FALSE, 0);

    vbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vbox);
    gtk_box_pack_start (GTK_BOX (tmp_box), vbox, FALSE, FALSE, 0);

    self->link = gtk_link_button_new_with_label ("", _("Launch website"));
    gtk_widget_set_no_show_all (self->link, TRUE);
    gtk_box_pack_start (GTK_BOX (vbox), self->link, TRUE, FALSE, 0);

    vbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vbox);
    gtk_box_pack_end (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);

    self->button = gtk_button_new_with_label (_("Setup now"));
    gtk_widget_set_size_request (self->button, SYNC_UI_LIST_BTN_WIDTH, -1);

    g_signal_connect (self->button, "clicked",
                      G_CALLBACK (setup_service_clicked), self);

    gtk_widget_show (self->button);
    gtk_box_pack_start (GTK_BOX (vbox), self->button, TRUE, FALSE, 0);
}


GtkWidget*
sync_config_widget_new (SyncevoServer *server,
                        gboolean current,
                        SyncevoService *dbus_service)
{
  return g_object_new (SYNC_TYPE_CONFIG_WIDGET_GTK,
                       "server", server,
                       "current", current,
                       "dbus_service", dbus_service,
                       NULL);
}

#endif
