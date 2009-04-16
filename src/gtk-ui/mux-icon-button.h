#ifndef _MUX_ICON_BUTTON
#define _MUX_ICON_BUTTON

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MUX_TYPE_ICON_BUTTON mux_icon_button_get_type()

#define MUX_ICON_BUTTON(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MUX_TYPE_ICON_BUTTON, MuxIconButton))

#define MUX_ICON_BUTTON_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MUX_TYPE_ICON_BUTTON, MuxIconButtonClass))

#define MUX_IS_ICON_BUTTON(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MUX_TYPE_ICON_BUTTON))

#define MUX_IS_ICON_BUTTON_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MUX_TYPE_ICON_BUTTON))

#define MUX_ICON_BUTTON_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MUX_TYPE_ICON_BUTTON, MuxIconButtonClass))

typedef struct {
    GtkButton parent;

    char *normal_filename;
    GdkPixbuf *normal_pixbuf;
    char *hover_filename;
    GdkPixbuf *hover_pixbuf;
} MuxIconButton;

typedef struct {
    GtkButtonClass parent_class;
} MuxIconButtonClass;

GType mux_icon_button_get_type (void);

GtkWidget* mux_icon_button_new (const char *normal_file, const char *hover_file);

const char* mux_icon_button_get_normal_filename (MuxIconButton *btn);
void mux_icon_button_set_normal_filename (MuxIconButton *btn, const char *name);

const char* mux_icon_button_get_hover_filename (MuxIconButton *btn);
void mux_icon_button_set_hover_filename (MuxIconButton *btn, const char *name);

G_END_DECLS

#endif
