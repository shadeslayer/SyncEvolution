#ifndef _SYNC_CONFIG_WIDGET
#define _SYNC_CONFIG_WIDGET

#include <glib-object.h>
#include <gtk/gtk.h>

#include "syncevo-server.h"

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

    GtkWidget *device_selector_box;
    GtkWidget *device_text;
    GtkWidget *combo;
    GtkWidget *device_select_btn;

    GtkWidget *settings_box;

    gboolean current; /* is this currently used config */
    char *current_service_name; /* name of the current service */
    gboolean configured; /* actual service configuration exists on server */
    gboolean device_template_selected;
    gboolean has_template; /* this service configuration has a matching template */
    gboolean expanded;

    SyncevoServer *server;
    SyncevoConfig *config;
    GHashTable *configs; /* possible configs. config above is one of these */

    char *config_name;
    char *pretty_name;

    char *running_session;

    char *expand_id;

    /* label */
    GtkWidget *image;
    GtkWidget *label;
    GtkWidget *entry;
    GtkWidget *link;
    GtkWidget *button;

    /* content */
    GtkWidget *description_label;
    GtkWidget *userinfo_table;
    GtkWidget *name_label;
    GtkWidget *name_entry;
    GtkWidget *complex_config_info_bar;
    GtkWidget *mode_table;
    GtkWidget *send_check;
    GtkWidget *receive_check;
    GtkWidget *username_entry;
    GtkWidget *password_entry;
    GtkWidget *source_toggle_label;
    GtkWidget *baseurl_entry;
    GtkWidget *expander;
    GtkWidget *fake_expander;
    GtkWidget *server_settings_table;
    GtkWidget *reset_delete_button;
    GtkWidget *stop_button;
    GtkWidget *use_button;

    GHashTable *sources;   /* key is source name, value is source_widgets */

    gboolean mode_changed;

    gboolean no_source_toggles;
} SyncConfigWidget;

typedef struct {
    GtkContainerClass parent_class;

    void (*changed) (SyncConfigWidget *widget);
} SyncConfigWidgetClass;

GType sync_config_widget_get_type (void);

GtkWidget *sync_config_widget_new (SyncevoServer *server,
                                   const char *name,
                                   SyncevoConfig *config,
                                   gboolean current,
                                   const char *current_service_name,
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

void sync_config_widget_expand_id (SyncConfigWidget *self, const char *id);
void sync_config_widget_add_alternative_config (SyncConfigWidget *self, const char *name, SyncevoConfig *config, gboolean configured);
G_END_DECLS


#endif
