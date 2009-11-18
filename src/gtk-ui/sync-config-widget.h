#ifndef _SYNC_CONFIG_WIDGET
#define _SYNC_CONFIG_WIDGET

#include <glib-object.h>

#ifdef USE_MOBLIN_UX
#include <nbtk/nbtk-gtk.h>
#else
#include <gtk/gtk.h>
#endif

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
#ifdef USE_MOBLIN_UX
    NbtkGtkExpander parent;
#else
    GtkVBox parent;
    GtkWidget *expando_box_for_gtk;
#endif

    gboolean current; /* is this currently used config */
    gboolean unset; /* is there a current config at all */

    SyncevoServer *server;
    char *name;
    server_config *config;
    
    GPtrArray *options_override;

    gboolean auth_changed;

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
    GList *uri_entries;
} SyncConfigWidget;

typedef struct {
#ifdef USE_MOBLIN_UX
    NbtkGtkExpanderClass parent_class;
#else
    GtkVBoxClass parent_class;
#endif

    void (*changed) (SyncConfigWidget *widget);
    void (*expanded) (SyncConfigWidget *widget);
} SyncConfigWidgetClass;

GType sync_config_widget_get_type (void);

GtkWidget *sync_config_widget_new (SyncevoServer *server,
                                   const char *name,
                                   gboolean current,
                                   gboolean unset);

void sync_config_widget_set_expanded (SyncConfigWidget *widget, gboolean expanded);

gboolean sync_config_widget_get_current (SyncConfigWidget *widget);
void sync_config_widget_set_current (SyncConfigWidget *self, gboolean current);

const char *sync_config_widget_get_name (SyncConfigWidget *widget);
G_END_DECLS


#endif
