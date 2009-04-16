/* TODO: should probably ensure specific icon size? */

#include "mux-icon-button.h"

enum {
	PROP_0,
	PROP_NORMAL_FILENAME,
	PROP_HOVER_FILENAME,
};

G_DEFINE_TYPE (MuxIconButton, mux_icon_button, GTK_TYPE_BUTTON)


static void
mux_icon_button_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
    MuxIconButton *btn = MUX_ICON_BUTTON (object);
    
    switch (property_id) {
    case PROP_NORMAL_FILENAME:
        g_value_set_string (value, mux_icon_button_get_normal_filename (btn));
        break;
    case PROP_HOVER_FILENAME:
        g_value_set_string (value, mux_icon_button_get_hover_filename (btn));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mux_icon_button_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
    MuxIconButton *btn = MUX_ICON_BUTTON (object);
    
    switch (property_id) {
    case PROP_NORMAL_FILENAME:
        mux_icon_button_set_normal_filename (btn, g_value_get_string (value));
        break;
    case PROP_HOVER_FILENAME:
        mux_icon_button_set_hover_filename (btn, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mux_icon_button_dispose (GObject *object)
{
    G_OBJECT_CLASS (mux_icon_button_parent_class)->dispose (object);
}

static void
mux_icon_button_finalize (GObject *object)
{
    G_OBJECT_CLASS (mux_icon_button_parent_class)->finalize (object);
}

static void
mux_icon_button_size_request (GtkWidget      *widget,
                              GtkRequisition *requisition)
{
    MuxIconButton *btn = MUX_ICON_BUTTON (widget);

    if (btn->normal_pixbuf) {
        requisition->width  = gdk_pixbuf_get_width  (btn->normal_pixbuf);
        requisition->height = gdk_pixbuf_get_height (btn->normal_pixbuf);
    }
}

static gboolean
mux_icon_button_enter_notify (GtkWidget *widget, GdkEventCrossing *event)
{
    GTK_WIDGET_CLASS (mux_icon_button_parent_class)->enter_notify_event (widget, event);
    gtk_widget_queue_draw (widget);

    return FALSE;
}

static gboolean
mux_icon_button_leave_notify (GtkWidget *widget, GdkEventCrossing *event)
{
    GTK_WIDGET_CLASS (mux_icon_button_parent_class)->leave_notify_event (widget, event);
    gtk_widget_queue_draw (widget);

    return FALSE;
}

static gboolean
mux_icon_button_expose (GtkWidget *widget,
                        GdkEventExpose *event)
{
    GdkRectangle dirty_area, btn_area;

    MuxIconButton *btn = MUX_ICON_BUTTON (widget);
    GdkPixbuf *pixbuf;

    if (btn->hover_pixbuf && GTK_BUTTON (btn)->in_button) {
        pixbuf = btn->hover_pixbuf;
    } else {
        pixbuf = btn->normal_pixbuf;
    }

    if (!pixbuf)
        return FALSE;

    btn_area.width = gdk_pixbuf_get_width (pixbuf);
    btn_area.height = gdk_pixbuf_get_height (pixbuf);
    btn_area.x = widget->allocation.x + (widget->allocation.width - btn_area.width) / 2;
    btn_area.y = widget->allocation.y + (widget->allocation.height - btn_area.height) / 2;
    
    if (gdk_rectangle_intersect (&event->area, &widget->allocation, &dirty_area) &&
        gdk_rectangle_intersect (&btn_area, &dirty_area, &dirty_area)) {

        gdk_draw_pixbuf (widget->window, NULL, pixbuf,
                         dirty_area.x - btn_area.x, dirty_area.y - btn_area.y,
                         dirty_area.x, dirty_area.y,
                         dirty_area.width, dirty_area.height,
                         GDK_RGB_DITHER_NORMAL, 0, 0);
    }
    return FALSE;
}

static void
mux_icon_button_class_init (MuxIconButtonClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    GParamSpec *pspec;

    object_class->get_property = mux_icon_button_get_property;
    object_class->set_property = mux_icon_button_set_property;
    object_class->dispose = mux_icon_button_dispose;
    object_class->finalize = mux_icon_button_finalize;

    widget_class->size_request = mux_icon_button_size_request;
    widget_class->expose_event = mux_icon_button_expose;
    widget_class->enter_notify_event = mux_icon_button_enter_notify;
    widget_class->leave_notify_event = mux_icon_button_leave_notify;

    pspec = g_param_spec_string ("normal-filename",
                                 "Normal filename",
                                 "Icon filename for normal state",
                                 NULL,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_NORMAL_FILENAME, pspec);

    pspec = g_param_spec_string ("hover-filename",
                                 "Hover filename",
                                 "Icon filename for hover state",
                                 NULL,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_HOVER_FILENAME, pspec);
}

static void
mux_icon_button_init (MuxIconButton *self)
{
}

GtkWidget*
mux_icon_button_new (const char *normal_file, const char *hover_file)
{
    return g_object_new (MUX_TYPE_ICON_BUTTON, 
                         "normal-filename", normal_file,
                         "hover-filename", hover_file,
                         NULL);
}

const char *
mux_icon_button_get_normal_filename (MuxIconButton *btn)
{
    return (btn->normal_filename);
}

void
mux_icon_button_set_normal_filename (MuxIconButton *btn, const char *name)
{
    if (btn->normal_filename)
        g_free (btn->normal_filename);
    if (btn->normal_pixbuf)
        g_object_unref (btn->normal_pixbuf);

    btn->normal_filename = g_strdup (name);
    btn->normal_pixbuf = gdk_pixbuf_new_from_file (name, NULL);
}

const char *
mux_icon_button_get_hover_filename (MuxIconButton *btn)
{
    return (btn->hover_filename);
}

void
mux_icon_button_set_hover_filename (MuxIconButton *btn, const char *name)
{
    if (btn->hover_filename)
        g_free (btn->hover_filename);
    if (btn->hover_pixbuf)
        g_object_unref (btn->hover_pixbuf);

    btn->hover_filename = g_strdup (name);
    btn->hover_pixbuf = gdk_pixbuf_new_from_file (name, NULL);
}
