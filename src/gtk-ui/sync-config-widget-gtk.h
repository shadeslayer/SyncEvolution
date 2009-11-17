#ifndef _SYNC_CONFIG_WIDGET_GTK
#define _SYNC_CONFIG_WIDGET_GTK

#ifndef USE_MOBLIN_UX


#include <glib-object.h>
#include <gtk/gtk.h>

#include "syncevo-dbus.h"
#include "sync-ui-config.h"

G_BEGIN_DECLS


#define SYNC_TYPE_CONFIG_WIDGET_GTK sync_config_widget_gtk_get_type()

#define SYNC_CONFIG_WIDGET_GTK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNC_TYPE_CONFIG_WIDGET_GTK, SyncConfigWidgetGtk))

#define SYNC_CONFIG_WIDGET_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), SYNC_TYPE_CONFIG_WIDGET_GTK, SyncConfigWidgetGtkClass))

#define SYNC_IS_CONFIG_WIDGET_GTK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNC_TYPE_CONFIG_WIDGET_GTK))

#define SYNC_IS_CONFIG_WIDGET_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNC_TYPE_CONFIG_WIDGET_GTK))

#define SYNC_CONFIG_WIDGET_GTK_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNC_TYPE_CONFIG_WIDGET_GTK, SyncConfigWidgetGtkClass))

typedef struct {
    GtkHBox parent;
  
    SyncevoService *dbus_service;
    gboolean current;

    SyncevoServer *server;
    server_config *config;

    GtkWidget *image;
    GtkWidget *label;
    GtkWidget *link;
    GtkWidget *button;
} SyncConfigWidgetGtk;

typedef struct {
    GtkHBoxClass parent_class;
} SyncConfigWidgetGtkClass;

GType sync_config_widget_gtk_get_type (void);


G_END_DECLS


#endif
#endif
