#ifndef _SYNC_CONFIG_WIDGET
#define _SYNC_CONFIG_WIDGET

#include <glib-object.h>
#include <gtk/gtk.h>

#include "syncevo-server.h"
#include "sync-ui-config.h"

G_BEGIN_DECLS

#define SYNC_TYPE_CONFIG_WIDGET sync_config_widget_get_type()

#define SYNC_CONFIG_WIDGET(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNC_TYPE_CONFIG_WIDGET, SyncConfigWidget))

#define SYNC_CONFIG_WIDGET_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), SYNC_TYPE_CONFIG_WIDGET, SyncConfigWidgetClass))

#define SYNC_IS_CONFIG_WIDGET(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNC_TYPE_CONFIG_WIDGET))

#define SYNC_IS_CONFIG_WIDGET_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNC_TYPE_CONFIG_WIDGET))

#define SYNC_CONFIG_WIDGET_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNC_TYPE_CONFIG_WIDGET, SyncConfigWidgetClass))

typedef struct {
    GtkContainer parent;
    GtkWidget *expando_box;
    GtkWidget *label_box;

    gboolean current; /* is this currently used config */
    gboolean unset; /* is there a current config at all */
    gboolean configured; /* actual service configuration exists on server */
    gboolean has_template; /* this service configuration has a matching template */
    gboolean showing;
    gboolean expanded;

    SyncevoServer *server;
    server_config *config;
    
    char *running_session;

    /* label */
    GtkWidget *image;
    GtkWidget *label;
    GtkWidget *entry;
    GtkWidget *link;
    GtkWidget *button;

    /* content */
    GtkWidget *description_label;
    GtkWidget *name_label;
    GtkWidget *name_entry;
    GtkWidget *username_entry;
    GtkWidget *password_entry;
    GtkWidget *baseurl_entry;
    GtkWidget *expander;
    GtkWidget *server_settings_table;
    GtkWidget *reset_delete_button;
    GtkWidget *stop_button;
    GtkWidget *use_button;
    GHashTable *uri_entries;
} SyncConfigWidget;

typedef struct {
    GtkContainerClass parent_class;

    void (*changed) (SyncConfigWidget *widget);
} SyncConfigWidgetClass;

GType sync_config_widget_get_type (void);

GtkWidget *sync_config_widget_new (SyncevoServer *server,
                                   const char *name,
                                   gboolean current,
                                   gboolean unset,
                                   gboolean configured,
                                   gboolean has_template);

void sync_config_widget_set_expanded (SyncConfigWidget *widget, gboolean expanded);
gboolean sync_config_widget_get_expanded (SyncConfigWidget *widget);

gboolean sync_config_widget_get_current (SyncConfigWidget *widget);
void sync_config_widget_set_current (SyncConfigWidget *self, gboolean current);

void sync_config_widget_set_has_template (SyncConfigWidget *self, gboolean has_template);
gboolean sync_config_widget_get_has_template (SyncConfigWidget *self);

void sync_config_widget_set_configured (SyncConfigWidget *self, gboolean configured);
gboolean sync_config_widget_get_configured (SyncConfigWidget *self);

const char *sync_config_widget_get_name (SyncConfigWidget *widget);
G_END_DECLS


#endif
