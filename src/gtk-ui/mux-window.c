/*
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include "mux-window.h"
#include "mux-icon-button.h"

static GdkColor mux_window_default_title_bar_bg = { 0, 0x3300, 0x3300, 0x3300 };
static GdkColor mux_window_default_title_bar_fg = { 0, 0x2c00, 0x2c00, 0x2c00 };

#define MUX_WINDOW_DEFAULT_TITLE_BAR_HEIGHT 63

GType
mux_decorations_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GFlagsValue values[] = {
      { MUX_DECOR_CLOSE, "MUX_CLOSE", "close" },
      { MUX_DECOR_SETTINGS, "MUX_SETTINGS", "settings" },
      { 0, NULL, NULL }
    };
    etype = g_flags_register_static (g_intern_static_string ("MuxDecorations"), values);
  }
  return etype;
}


enum {
  PROP_0,
  PROP_DECORATIONS,
};

enum {
  SETTINGS_VISIBILITY_CHANGED,
  LAST_SIGNAL
};

static guint mux_window_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (MuxWindow, mux_window, GTK_TYPE_WINDOW)

#define GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), MUX_TYPE_WINDOW, MuxWindowPrivate))

typedef struct _MuxWindowPrivate MuxWindowPrivate;

struct _MuxWindowPrivate {
    int dummy;
};

static void
mux_window_get_property (GObject *object, guint property_id,
                         GValue *value, GParamSpec *pspec)
{
    MuxWindow *win = MUX_WINDOW (object);

    switch (property_id) {
    case PROP_DECORATIONS:
        g_value_set_uint (value, win->decorations);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mux_window_set_property (GObject *object, guint property_id,
                         const GValue *value, GParamSpec *pspec)
{
    MuxWindow *win = MUX_WINDOW (object);

    switch (property_id) {
    case PROP_DECORATIONS:
        mux_window_set_decorations (win, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mux_window_update_style (MuxWindow *win)
{
    GdkColor *title_bar_bg = NULL;
    GdkColor *title_bar_fg = NULL;
    guint title_bar_height;
    char *title_bar_font = NULL;

    g_return_if_fail (win->title_bar && win->title_label);

    gtk_widget_style_get (GTK_WIDGET (win),
                          "title-bar-height", &title_bar_height, 
                          "title-bar-bg", &title_bar_bg,
                          "title-bar-fg", &title_bar_fg,
                          "title-bar-font", &title_bar_font,
                          NULL);

    if (title_bar_bg) {
        gtk_widget_modify_bg (win->title_bar, GTK_STATE_NORMAL, title_bar_bg);
        gdk_color_free (title_bar_bg);
    } else {
        gtk_widget_modify_bg (win->title_bar, GTK_STATE_NORMAL, 
                              &mux_window_default_title_bar_bg);
    }

    if (title_bar_fg) {
        gtk_widget_modify_fg (win->title_label, GTK_STATE_NORMAL, title_bar_fg);
        gdk_color_free (title_bar_fg);
    } else {
        gtk_widget_modify_fg (win->title_label, GTK_STATE_NORMAL, 
                              &mux_window_default_title_bar_fg);
    }

    if (title_bar_font) {
        PangoFontDescription *desc;
        desc = pango_font_description_from_string (title_bar_font);
        gtk_widget_modify_font (win->title_label, desc);
        pango_font_description_free (desc);
        g_free (title_bar_font);
    }
    
    gtk_widget_set_size_request (win->title_bar, -1, title_bar_height);
    gtk_misc_set_padding (GTK_MISC (win->title_label), title_bar_height / 3, 0);
}

static void 
mux_window_style_set (GtkWidget *widget,
                      GtkStyle *previous)
{
    MuxWindow *win = MUX_WINDOW (widget);

    mux_window_update_style (win);

    GTK_WIDGET_CLASS (mux_window_parent_class)->style_set (widget, previous);
}

static void
mux_window_forall (GtkContainer *container,
                   gboolean include_internals,
                   GtkCallback callback,
                   gpointer callback_data)
{
    MuxWindow *mux_win = MUX_WINDOW (container);
    GtkBin *bin = GTK_BIN (container);

    /* FIXME: call parents forall instead ? */
    if (bin->child)
        (* callback) (bin->child, callback_data);

    if (mux_win->title_bar)
        (* callback) (mux_win->title_bar, callback_data);
    if (mux_win->settings)
        (* callback) (mux_win->settings, callback_data);
}

static void
mux_window_add (GtkContainer *container,
                GtkWidget *widget)
{
    MuxWindowClass *klass;
    GtkContainerClass *parent_container_class;
    GtkBin *bin = GTK_BIN (container);

    klass = MUX_WINDOW_GET_CLASS (container);
    parent_container_class = GTK_CONTAINER_CLASS (g_type_class_peek_parent (klass));

    if (!bin->child) {
        GtkWidget *w;
        /* create a dummy container so we can hide the contents when settings 
         * are shown */
        w = gtk_vbox_new (FALSE, 0);
        parent_container_class->add (container, w);
        if (!MUX_WINDOW (container)->settings_visible)
            gtk_widget_show (w);
    }
    gtk_container_add (GTK_CONTAINER (bin->child), widget);
}

static void
mux_window_remove (GtkContainer *container,
                   GtkWidget *child)
{
    MuxWindow *win = MUX_WINDOW (container);
    GtkBin *bin = GTK_BIN (container);

    if (child == win->title_bar) {
        gtk_widget_unparent (win->title_bar);
        win->title_bar = NULL;
        win->title_label = NULL;
    } else if (child == win->settings) {
        gtk_widget_unparent (win->settings);
        win->settings = NULL;
    } else if (bin->child) {
        if (bin->child == child) {
            /* should call parents remove... */
            gtk_widget_unparent (child);
            bin->child = NULL;
        } else {
            gtk_container_remove (GTK_CONTAINER (bin->child), child);
        }
    }
}

static void
mux_window_size_request (GtkWidget *widget,
                         GtkRequisition *requisition)
{
    GtkBin *bin = GTK_BIN (widget);
    MuxWindow *mux_win = MUX_WINDOW (widget);
    GtkRequisition req;

    /* we will always be maximized so none of this should be necessary
     * (requisition will never be used), but some widgets to assume
     * size_request is called */
    if (mux_win->title_bar && GTK_WIDGET_VISIBLE (mux_win->title_bar))
        gtk_widget_size_request (mux_win->title_bar, &req);
    if (bin->child && GTK_WIDGET_VISIBLE (bin->child))
        gtk_widget_size_request (bin->child, &req);
    if (mux_win->settings && GTK_WIDGET_VISIBLE (mux_win->settings))
        gtk_widget_size_request (mux_win->settings, &req);

    requisition->width = 1024;
    requisition->height = 600;
}

static void
mux_window_size_allocate (GtkWidget *widget,
                          GtkAllocation *allocation)
{
    GtkBin *bin = GTK_BIN (widget);
    MuxWindow *mux_win = MUX_WINDOW (widget);
    GtkAllocation child_allocation;
    int xmargin, ymargin, title_height;

    widget->allocation = *allocation;
    xmargin = GTK_CONTAINER (widget)->border_width +
              widget->style->xthickness;
    ymargin = GTK_CONTAINER (widget)->border_width +
              widget->style->ythickness;
    title_height = 0;

    if (mux_win->title_bar) {
        GtkAllocation title_allocation;
        GtkRequisition title_req;
        gtk_widget_get_child_requisition (mux_win->title_bar, &title_req);

        title_height = title_req.height;
        title_allocation.x = allocation->x;
        title_allocation.y = allocation->y;
        title_allocation.width = allocation->width;
        title_allocation.height = title_height;
        gtk_widget_size_allocate (mux_win->title_bar, &title_allocation);

    }

    child_allocation.x = allocation->x + xmargin;
    child_allocation.y = allocation->y + title_height + ymargin;
    child_allocation.width = allocation->width - 2 * xmargin;
    child_allocation.height = allocation->height - (2 * ymargin + title_height);

    if (GTK_WIDGET_MAPPED (widget) &&
        (child_allocation.x != mux_win->child_allocation.x ||
         child_allocation.y != mux_win->child_allocation.y ||
         child_allocation.width != mux_win->child_allocation.width ||
         child_allocation.height != mux_win->child_allocation.height)) {
        gdk_window_invalidate_rect (widget->window, &widget->allocation, FALSE);
    }

    if (bin->child && GTK_WIDGET_VISIBLE (bin->child)) {
        gtk_widget_size_allocate (bin->child, &child_allocation);
    }

    if (mux_win->settings && GTK_WIDGET_VISIBLE (mux_win->settings)) {
        gtk_widget_size_allocate (mux_win->settings, &child_allocation);
    }

    mux_win->child_allocation = child_allocation;
}

static void
mux_window_class_init (MuxWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);
    GParamSpec *pspec;

    g_type_class_add_private (klass, sizeof (MuxWindowPrivate));

    object_class->get_property = mux_window_get_property;
    object_class->set_property = mux_window_set_property;

    widget_class->size_request = mux_window_size_request;
    widget_class->size_allocate = mux_window_size_allocate;
    widget_class->style_set = mux_window_style_set;

    container_class->forall = mux_window_forall;
    container_class->remove = mux_window_remove;
    container_class->add = mux_window_add;

    pspec = g_param_spec_uint ("title-bar-height",
                               "Title bar height",
                               "Total height of the title bar",
                               0, G_MAXUINT, MUX_WINDOW_DEFAULT_TITLE_BAR_HEIGHT,
                               G_PARAM_READWRITE);
    gtk_widget_class_install_style_property(widget_class, pspec);
    pspec = g_param_spec_boxed ("title-bar-bg",
                                "Title bar bg color",
                                "Color of the title bar background",
                                GDK_TYPE_COLOR,
                                G_PARAM_READWRITE);
    gtk_widget_class_install_style_property(widget_class, pspec);
    pspec = g_param_spec_boxed ("title-bar-fg",
                                "Title bar fg color",
                                "Color of the title bar foreground (text)",
                                GDK_TYPE_COLOR,
                                G_PARAM_READWRITE);
    gtk_widget_class_install_style_property(widget_class, pspec);
    pspec = g_param_spec_string ("title-bar-font",
                                 "Title bar font",
                                 "Pango font description string for title bar text",
                                 "Bold 25",
                                 G_PARAM_READWRITE);
    gtk_widget_class_install_style_property(widget_class, pspec);

    pspec = g_param_spec_flags ("decorations", 
                               NULL,
                               "Bitfield of MuxDecorations defining used window decorations",
                               MUX_TYPE_DECORATIONS, 
                               MUX_DECOR_CLOSE,
                               G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_DECORATIONS,
                                     pspec);

    mux_window_signals[SETTINGS_VISIBILITY_CHANGED] = 
            g_signal_new ("settings-visibility-changed",
                          G_OBJECT_CLASS_TYPE (object_class),
                          G_SIGNAL_RUN_FIRST,
                          G_STRUCT_OFFSET (MuxWindowClass, settings_visibility_changed),
                          NULL, NULL,
                          g_cclosure_marshal_VOID__VOID,
                          G_TYPE_NONE, 0, NULL);

}

static void
update_show_settings (MuxWindow *window)
{
    GtkBin *bin = GTK_BIN (window);

    if (window->settings_visible) {
        if (bin->child) {
            gtk_widget_hide (bin->child);
        }
        if (window->settings){
            gtk_widget_show (window->settings);
            gtk_widget_map (window->settings);
            if (window->settings_title) {
                gtk_label_set_text (GTK_LABEL (window->title_label), 
                                    window->settings_title);
            }
        }
    } else {
        if (window->settings) {
            gtk_widget_hide (window->settings);
        }
        if (bin->child) {
            gtk_widget_show (bin->child);
        }
        /* reset the window title */
        gtk_label_set_text (GTK_LABEL (window->title_label), 
                            gtk_window_get_title(GTK_WINDOW (window)));
    }
    g_signal_emit (window, mux_window_signals[SETTINGS_VISIBILITY_CHANGED], 0);
}

static void
mux_window_settings_clicked (MuxIconButton *button, MuxWindow *window)
{
    gboolean active;
    active = mux_icon_button_get_active (button);;

    if (window->settings_visible != active) {
        window->settings_visible = active;

        update_show_settings (window);
    }
}

static void
mux_window_close_clicked (MuxWindow *window)
{
    /* this is how GtkDialog does it... */
    GdkEvent *event;

    event = gdk_event_new (GDK_DELETE);
    event->any.window = g_object_ref (GTK_WIDGET (window)->window);
    event->any.send_event = TRUE;

    gtk_main_do_event (event);
    gdk_event_free (event);
}

static GdkPixbuf*
load_icon (MuxWindow *window, const char *icon_name)
{
    static GtkIconTheme *theme = NULL;
    GdkScreen *screen;
    GdkPixbuf *pixbuf;

    if (!theme) {
        screen = gtk_widget_get_screen (GTK_WIDGET (window));
        theme = gtk_icon_theme_get_for_screen (screen);
    }

    pixbuf = gtk_icon_theme_load_icon (theme, icon_name,
                                       48, 0, NULL);


    /* FIXME: workaround until icons are in Moblin Netbook theme */
    if (!pixbuf) {
        char *str = g_strdup_printf ("%s/%s.png", THEMEDIR, icon_name);
        pixbuf = gdk_pixbuf_new_from_file_at_size (str, 48, 48, NULL);

        g_free (str);
    }

    if (!pixbuf) {
        g_warning ("Icon '%s' not found in theme", icon_name);
        pixbuf = gtk_widget_render_icon (GTK_WIDGET (window),
                                         GTK_STOCK_MISSING_IMAGE,
                                         GTK_ICON_SIZE_DIALOG,
                                         NULL);

    }
    return pixbuf;
}

static void
mux_window_build_title_bar (MuxWindow *window)
{
    GtkWidget *box, *btn;
    GdkPixbuf *pixbuf, *pixbuf_hover;

    if (window->title_bar) {
        gtk_widget_unparent (window->title_bar);
    }

    window->title_bar = gtk_event_box_new ();
    gtk_widget_set_name (window->title_bar, "mux_window_title_bar");
    gtk_widget_set_parent (window->title_bar, GTK_WIDGET (window));
    gtk_widget_show (window->title_bar);

    box = gtk_hbox_new (FALSE, 0);
    gtk_container_add (GTK_CONTAINER (window->title_bar), box);
    gtk_widget_show (box);

    window->title_label = gtk_label_new (gtk_window_get_title (GTK_WINDOW (window)));
    gtk_box_pack_start (GTK_BOX (box), window->title_label,
                        FALSE, FALSE, 0);
    gtk_widget_show (window->title_label);

    if (window->decorations & MUX_DECOR_CLOSE) {
        pixbuf = load_icon (window, "close");
        pixbuf_hover = load_icon (window, "close_hover");
        btn = g_object_new (MUX_TYPE_ICON_BUTTON,
                            "normal-state-pixbuf", pixbuf,
                            "prelight-state-pixbuf", pixbuf_hover,
                            NULL);
        gtk_widget_set_name (btn, "mux_icon_button_close");
        g_signal_connect_swapped (btn, "clicked", 
                                  G_CALLBACK (mux_window_close_clicked), window);
        gtk_box_pack_end (GTK_BOX (box), btn, FALSE, FALSE, 0);
        gtk_widget_show (btn);
    }

    if (window->decorations & MUX_DECOR_SETTINGS) {
        pixbuf = load_icon (window, "settings");
        pixbuf_hover = load_icon (window, "settings_hover");
        window->settings_button = g_object_new (MUX_TYPE_ICON_BUTTON,
                                  "normal-state-pixbuf", pixbuf,
                                  "prelight-state-pixbuf", pixbuf_hover,
                                  "active-state-pixbuf", pixbuf_hover,
                                  "toggleable", TRUE,
                                  NULL);
        gtk_widget_set_name (window->settings_button, "mux_icon_button_settings");
        g_signal_connect (window->settings_button, "clicked", 
                          G_CALLBACK (mux_window_settings_clicked), window);
        gtk_box_pack_end (GTK_BOX (box), window->settings_button, FALSE, FALSE, 0);
        gtk_widget_show (window->settings_button);
    }

    mux_window_update_style (window);

    gtk_widget_map (window->title_bar); /*TODO: is there a better way to do this ? */
    if (GTK_WIDGET_VISIBLE (window))
        gtk_widget_queue_resize (GTK_WIDGET (window));

}

static void
mux_window_title_changed (MuxWindow *window, 
                          GParamSpec *pspec,
                          gpointer user_data)
{
    if (window->title_label && !window->settings_visible) {
        gtk_label_set_text (GTK_LABEL (window->title_label), 
                            gtk_window_get_title (GTK_WINDOW (window)));
    }
}

/* For some reason metacity sometimes won't maximize but will if asked 
 * another time. For the record, I'm not proud of writing this */
static gboolean
mux_window_try_maximize (MuxWindow *self)
{
    static int count = 0;

    count++;
    gtk_window_maximize (GTK_WINDOW (self));

    return (count < 10);
}

static void
mux_window_init (MuxWindow *self)
{
    self->decorations = MUX_DECOR_CLOSE;

    gtk_window_set_decorated (GTK_WINDOW (self), FALSE);

    g_signal_connect (self, "notify::title",
                      G_CALLBACK (mux_window_title_changed), NULL);

    mux_window_build_title_bar (self);

    gtk_window_maximize (GTK_WINDOW (self));
    g_timeout_add (10, (GSourceFunc)mux_window_try_maximize, self);
}

GtkWidget*
mux_window_new (void)
{
    return g_object_new (MUX_TYPE_WINDOW, NULL);
}

void 
mux_window_set_decorations (MuxWindow *window, 
                            MuxDecorations decorations)
{
    g_return_if_fail (MUX_IS_WINDOW (window));
    
    if (decorations != window->decorations) {
        window->decorations = decorations;
        mux_window_build_title_bar (window);
    }
}

MuxDecorations 
mux_window_get_decorations (MuxWindow *window)
{
    g_return_val_if_fail (MUX_IS_WINDOW (window), MUX_DECOR_NONE);

    return window->decorations;
}

void
mux_window_set_settings_widget (MuxWindow *window, GtkWidget *widget)
{
    if (widget != window->settings) {
        if (window->settings) {
            gtk_widget_unparent (window->settings);
        }

        window->settings = widget;

        if (widget) {
            gtk_widget_set_parent (widget, GTK_WIDGET (window));
        }
    }
}

GtkWidget*
mux_window_get_settings_widget (MuxWindow *window)
{
    return window->settings;
}

void
mux_window_set_settings_visible (MuxWindow *window, gboolean visible)
{
    if (window->settings_visible != visible) {
        window->settings_visible = visible;

        update_show_settings (window);

        if (window->settings_button)
            mux_icon_button_set_active (MUX_ICON_BUTTON (window->settings_button),
                                        visible);
    }
}

gboolean
mux_window_get_settings_visible (MuxWindow *window)
{
    return window->settings_visible;
}

void
mux_window_set_settings_title (MuxWindow *window, const char *title)
{
    if (window->settings_title) {
        g_free (window->settings_title);
    }

    window->settings_title = g_strdup (title);

    if (window->settings_visible) {
        gtk_label_set_text (GTK_LABEL (window->title_label), 
                            window->settings_title);
    }
}

const char*
mux_window_get_settings_title (MuxWindow *window)
{
    return window->settings_title;
}
