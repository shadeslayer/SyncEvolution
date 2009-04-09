#ifndef _MUX_BIN
#define _MUX_BIN

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MUX_TYPE_BIN mux_bin_get_type()

#define MUX_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MUX_TYPE_BIN, MuxBin))

#define MUX_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MUX_TYPE_BIN, MuxBinClass))

#define MUX_IS_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MUX_TYPE_BIN))

#define MUX_IS_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MUX_TYPE_BIN))

#define MUX_BIN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MUX_TYPE_BIN, MuxBinClass))

typedef struct {
    GtkBin parent;

    GtkWidget *title;

    GtkAllocation child_allocation;
    GtkAllocation bullet_allocation;

    GdkColor bullet_color;
    GdkColor border_color;
} MuxBin;

typedef struct {
    GtkBinClass parent_class;
} MuxBinClass;

GType mux_bin_get_type (void);

GtkWidget* mux_bin_new (void);

const char* mux_bin_get_title (MuxBin *bin);
void mux_bin_set_title (MuxBin *bin, const char *title);

G_END_DECLS

#endif
