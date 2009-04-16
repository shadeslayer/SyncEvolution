#ifndef _MUX_WINDOW
#define _MUX_WINDOW

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum {
    MUX_DECOR_NONE = 0,
    MUX_DECOR_CLOSE = 1 << 0,
    MUX_DECOR_SETTINGS = 1 << 1,
} MuxDecorations;

GType mux_decorations_get_type (void) G_GNUC_CONST;
#define MUX_TYPE_DECORATIONS (mux_decorations_get_type())


#define MUX_TYPE_WINDOW mux_window_get_type()
#define MUX_WINDOW(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), MUX_TYPE_WINDOW, MuxWindow))
#define MUX_WINDOW_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), MUX_TYPE_WINDOW, MuxWindowClass))
#define MUX_IS_WINDOW(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MUX_TYPE_WINDOW))
#define MUX_IS_WINDOW_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), MUX_TYPE_WINDOW))
#define MUX_WINDOW_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), MUX_TYPE_WINDOW, MuxWindowClass))

typedef struct {
    GtkWindow parent;

    GtkWidget *title_bar;
    GtkWidget *title_label;
    GtkWidget *title_alignment;

    GtkAllocation child_allocation;

    MuxDecorations decorations;
    GdkColor title_bar_color;
    guint title_bar_height;
} MuxWindow;

typedef struct {
    GtkWindowClass parent_class;

    void (*settings_clicked) (MuxWindow *window);
} MuxWindowClass;

GType mux_window_get_type (void);

GtkWidget* mux_window_new (void);

void mux_window_set_decorations (MuxWindow *window, MuxDecorations decorations);
MuxDecorations mux_window_get_decorations (MuxWindow *window);
G_END_DECLS

#endif
