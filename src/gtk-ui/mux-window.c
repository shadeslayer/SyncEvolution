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
  PROP_BACK_TITLE,
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
    case PROP_BACK_TITLE:
        g_value_set_string (value,
                            gtk_button_get_label (GTK_BUTTON (win->back_btn)));
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
    case PROP_BACK_TITLE:
        g_free (win->back_title);
        win->back_title = g_strdup (g_value_get_string (value));
        if (win->back_btn) {
            gtk_button_set_label (GTK_BUTTON (win->back_btn), win->back_title);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mux_window_update_style (MuxWindow *win)
{
    GdkColor *title_bar_bg = NULL;
    guint title_bar_height;

    g_return_if_fail (win->title_bar);

    gtk_widget_style_get (GTK_WIDGET (win),
                          "title-bar-height", &title_bar_height, 
                          "title-bar-bg", &title_bar_bg,
                          NULL);

    if (title_bar_bg) {
        gtk_widget_modify_bg (win->title_bar, GTK_STATE_NORMAL, title_bar_bg);
        gdk_color_free (title_bar_bg);
    } else {
        gtk_widget_modify_bg (win->title_bar, GTK_STATE_NORMAL, 
                              &mux_window_default_title_bar_bg);
    }

    gtk_widget_set_size_request (win->title_bar, -1, title_bar_height);
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
    if (mux_win->notebook)
        (* callback) (mux_win->notebook, callback_data);
}

static void
mux_window_add (GtkContainer *container,
                GtkWidget *widget)
{
    MuxWindowClass *klass;
    GtkContainerClass *parent_container_class;

    klass = MUX_WINDOW_GET_CLASS (container);
    parent_container_class = GTK_CONTAINER_CLASS (g_type_class_peek_parent (klass));

    parent_container_class->add (container, widget);
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
    } else if (child == win->notebook) {
        gtk_widget_unparent (win->notebook);
        win->notebook = NULL;
    } else if (bin->child) {
        if (bin->child == child) {
            /* should call parents remove... */
            gtk_widget_unparent (child);
            bin->child = NULL;
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
    if (mux_win->notebook && GTK_WIDGET_VISIBLE (mux_win->notebook))
        gtk_widget_size_request (mux_win->notebook, &req);

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

    if (mux_win->notebook && GTK_WIDGET_VISIBLE (mux_win->notebook)) {
        gtk_widget_size_allocate (mux_win->notebook, &child_allocation);
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

    pspec = g_param_spec_flags ("decorations", 
                               NULL,
                               "Bitfield of MuxDecorations defining used window decorations",
                               MUX_TYPE_DECORATIONS, 
                               MUX_DECOR_CLOSE,
                               G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_DECORATIONS,
                                     pspec);

    pspec = g_param_spec_string ("back-title", 
                                 NULL,
                                 "title of the back button in the window decoration",
                                 "", 
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_BACK_TITLE,
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
mux_window_settings_clicked (MuxIconButton *button, MuxWindow *window)
{
    gboolean active;
    active = mux_icon_button_get_active (button);
    mux_window_set_settings_visible (window, active);
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
bread_crumb_clicked_cb (GtkButton *btn, MuxWindow *window)
{
    mux_window_set_current_page (window, -1);
}

static void
mux_window_build_title_bar (MuxWindow *window)
{
    GtkWidget *box, *button_box, *btn, *align;
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

    align = gtk_alignment_new (0, 0.5, 0, 0);
    gtk_box_pack_start (GTK_BOX (box), align, FALSE, FALSE, 4);
    gtk_widget_show (align);

    button_box = gtk_hbox_new (FALSE, 0);
    gtk_widget_set_name (window->title_bar, "mux_window_bread_crumbs");
    gtk_container_add (GTK_CONTAINER (align), button_box);
    gtk_widget_show (button_box);

    window->back_btn = gtk_button_new_with_label (window->back_title);
    gtk_box_pack_start (GTK_BOX (button_box), window->back_btn,
                        FALSE, FALSE, 4);
    g_signal_connect (window->back_btn, "clicked",
                      G_CALLBACK (bread_crumb_clicked_cb), window);
    if (mux_window_get_current_page (window) != -1) {
        gtk_widget_show (window->back_btn);
    }
    /*window->title_label = gtk_label_new (gtk_window_get_title (GTK_WINDOW (window)));
    gtk_box_pack_start (GTK_BOX (box), window->title_label,
                        FALSE, FALSE, 0);
    gtk_widget_show (window->title_label);*/

    button_box = gtk_hbox_new (TRUE, 0);
    gtk_box_pack_end (GTK_BOX (box), button_box, FALSE, FALSE, 4);
    gtk_widget_show (button_box);

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
        gtk_box_pack_end (GTK_BOX (button_box), btn, FALSE, FALSE, 0);
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
        gtk_box_pack_end (GTK_BOX (button_box), window->settings_button, FALSE, FALSE, 0);
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
    mux_window_build_title_bar (window);
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

    self->notebook = gtk_notebook_new ();
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (self->notebook), FALSE);
    gtk_notebook_set_show_border (GTK_NOTEBOOK (self->notebook), FALSE);
    gtk_widget_set_parent (self->notebook, GTK_WIDGET (self));
    self->settings_index = -2;


    gtk_window_maximize (GTK_WINDOW (self));
    g_timeout_add (10, (GSourceFunc)mux_window_try_maximize, self);
}

GtkWidget*
mux_window_new (const char *back_title)
{
    return g_object_new (MUX_TYPE_WINDOW,
                         "back-title", back_title,
                         NULL);
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
mux_window_set_settings_visible (MuxWindow *window, gboolean show)
{
    gboolean visible;

    visible = (mux_window_get_current_page (window) == window->settings_index);
    if (visible != show) {
        if (show) {
            mux_window_set_current_page (window, window->settings_index);
        } else {
            mux_window_set_current_page (window, -1);
        }

        g_signal_emit (window, mux_window_signals[SETTINGS_VISIBILITY_CHANGED], 0);

        if (window->settings_button)
            mux_icon_button_set_active (MUX_ICON_BUTTON (window->settings_button),
                                        show);
    }
}

gboolean
mux_window_get_settings_visible (MuxWindow *window)
{
    return (mux_window_get_current_page (window) == window->settings_index);
}

gint
mux_window_append_page (MuxWindow *window,
                        GtkWidget *page,
                        gboolean is_settings)
{
    gint index;

    index = gtk_notebook_append_page (GTK_NOTEBOOK (window->notebook), page, NULL);

    if (is_settings) {
        window->settings_index = index;
    }
    return index;
}

void mux_window_set_current_page (MuxWindow *window, gint index)
{
    GtkBin *bin = GTK_BIN (window);

    if (index == -1) {
        gtk_widget_hide (window->notebook);
        if (bin->child) {
            gtk_widget_show (bin->child);
        }
        gtk_widget_hide (window->back_btn);
    } else {
        gtk_notebook_set_current_page (GTK_NOTEBOOK (window->notebook), index);
        if (bin->child) {
            gtk_widget_hide (bin->child);
        }
        gtk_widget_show (window->notebook);
        gtk_widget_map (window->notebook);

        gtk_widget_show (window->back_btn);
    }
}

gint
mux_window_get_current_page (MuxWindow *window)
{
    GtkBin *bin = GTK_BIN (window);

    if (bin->child && GTK_WIDGET_VISIBLE (bin->child)) {
        return -1;
    } else if (window->notebook) {
        return gtk_notebook_get_current_page (GTK_NOTEBOOK (window->notebook));
    }
    return -1;
}
