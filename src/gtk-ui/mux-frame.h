#ifndef _MUX_FRAME
#define _MUX_FRAME

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MUX_TYPE_FRAME mux_frame_get_type()

#define MUX_FRAME(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MUX_TYPE_FRAME, MuxFrame))

#define MUX_FRAME_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MUX_TYPE_FRAME, MuxFrameClass))

#define MUX_IS_FRAME(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MUX_TYPE_FRAME))

#define MUX_IS_FRAME_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MUX_TYPE_FRAME))

#define MUX_FRAME_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MUX_TYPE_FRAME, MuxFrameClass))

typedef struct {
    GtkBin parent;

    GtkWidget *title;

    GtkAllocation child_allocation;
    GtkAllocation bullet_allocation;

    GdkColor bullet_color;
    GdkColor border_color;
} MuxFrame;

typedef struct {
    GtkBinClass parent_class;
} MuxFrameClass;

GType mux_frame_get_type (void);

GtkWidget* mux_frame_new (void);

const char* mux_frame_get_title (MuxFrame *frame);
void mux_frame_set_title (MuxFrame *bin, const char *title);

G_END_DECLS

#endif
