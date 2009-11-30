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

/* TODO: should probably ensure specific icon size? */

#include "mux-icon-button.h"

enum {
	PROP_0,
	PROP_TOGGLEABLE,
	PROP_PIXBUF_NORMAL,
	PROP_PIXBUF_ACTIVE,
	PROP_PIXBUF_PRELIGHT,
	PROP_PIXBUF_SELECTED,
	PROP_PIXBUF_INSENSITIVE
};

G_DEFINE_TYPE (MuxIconButton, mux_icon_button, GTK_TYPE_BUTTON)


static void
mux_icon_button_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
    MuxIconButton *btn = MUX_ICON_BUTTON (object);
    
    switch (property_id) {
    case PROP_TOGGLEABLE:
        g_value_set_boolean (value, btn->toggleable);
        break;
    case PROP_PIXBUF_NORMAL:
        g_value_set_object (value, mux_icon_button_get_pixbuf (btn, GTK_STATE_NORMAL));
        break;
    case PROP_PIXBUF_ACTIVE:
        g_value_set_object (value, mux_icon_button_get_pixbuf (btn, GTK_STATE_ACTIVE));
        break;
    case PROP_PIXBUF_PRELIGHT:
        g_value_set_object (value, mux_icon_button_get_pixbuf (btn, GTK_STATE_PRELIGHT));
        break;
    case PROP_PIXBUF_SELECTED:
        g_value_set_object (value, mux_icon_button_get_pixbuf (btn, GTK_STATE_SELECTED));
        break;
    case PROP_PIXBUF_INSENSITIVE:
        g_value_set_object (value, mux_icon_button_get_pixbuf (btn, GTK_STATE_INSENSITIVE));
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
    GdkPixbuf *pixbuf;
    
    switch (property_id) {
    case PROP_TOGGLEABLE:
        btn->toggleable = g_value_get_boolean (value);
        break;
    case PROP_PIXBUF_NORMAL:
        pixbuf = GDK_PIXBUF (g_value_get_object (value));
        mux_icon_button_set_pixbuf (btn, GTK_STATE_NORMAL, pixbuf);
        break;
    case PROP_PIXBUF_ACTIVE:
        pixbuf = GDK_PIXBUF (g_value_get_object (value));
        mux_icon_button_set_pixbuf (btn, GTK_STATE_ACTIVE, pixbuf);
        break;
    case PROP_PIXBUF_PRELIGHT:
        pixbuf = GDK_PIXBUF (g_value_get_object (value));
        mux_icon_button_set_pixbuf (btn, GTK_STATE_PRELIGHT, pixbuf);
        break;
    case PROP_PIXBUF_SELECTED:
        pixbuf = GDK_PIXBUF (g_value_get_object (value));
        mux_icon_button_set_pixbuf (btn, GTK_STATE_SELECTED, pixbuf);
        break;
    case PROP_PIXBUF_INSENSITIVE:
        pixbuf = GDK_PIXBUF (g_value_get_object (value));
        mux_icon_button_set_pixbuf (btn, GTK_STATE_INSENSITIVE, pixbuf);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mux_icon_button_dispose (GObject *object)
{
    int i;
    MuxIconButton *btn = MUX_ICON_BUTTON (object);
    
    for (i = 0; i < 5; i++) {
        if (btn->pixbufs[i]) {
            g_object_unref (btn->pixbufs[i]);
            btn->pixbufs[i] = NULL;
        }
    }
    G_OBJECT_CLASS (mux_icon_button_parent_class)->dispose (object);
}

static void
mux_icon_button_size_request (GtkWidget      *widget,
                              GtkRequisition *requisition)
{
    MuxIconButton *btn = MUX_ICON_BUTTON (widget);

    if (btn->pixbufs[GTK_STATE_NORMAL]) {
        requisition->width  = gdk_pixbuf_get_width  (btn->pixbufs[GTK_STATE_NORMAL]);
        requisition->height = gdk_pixbuf_get_height (btn->pixbufs[GTK_STATE_NORMAL]);
    }
}

static gboolean
mux_icon_button_expose (GtkWidget *widget,
                        GdkEventExpose *event)
{
    GdkRectangle dirty_area, btn_area;
    MuxIconButton *btn = MUX_ICON_BUTTON (widget);
    GdkPixbuf *pixbuf;
    GtkStateType state;

    if (btn->active) {
        /* this is a active toggle button */
        state = GTK_STATE_ACTIVE;
    } else {
        state = GTK_WIDGET_STATE (widget);
    }

    if (btn->pixbufs[state]) {
        pixbuf = btn->pixbufs[state];
    } else {
        pixbuf = btn->pixbufs[GTK_STATE_NORMAL];
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
mux_icon_button_clicked (GtkButton *button)
{
    MuxIconButton *icon_button = MUX_ICON_BUTTON (button);
    
    if (icon_button->toggleable) {
        icon_button->active = !icon_button->active;
    }
}

static void
mux_icon_button_class_init (MuxIconButtonClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    GtkButtonClass *button_class = GTK_BUTTON_CLASS (klass);
    GParamSpec *pspec;

    object_class->get_property = mux_icon_button_get_property;
    object_class->set_property = mux_icon_button_set_property;
    object_class->dispose = mux_icon_button_dispose;

    widget_class->size_request = mux_icon_button_size_request;
    widget_class->expose_event = mux_icon_button_expose;

    button_class->clicked = mux_icon_button_clicked;

    pspec = g_param_spec_boolean ("toggleable",
                                 "Toggleable",
                                 "Is icon button button a toggle or normal",
                                 FALSE,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_TOGGLEABLE, pspec);
    pspec = g_param_spec_object ("normal-state-pixbuf",
                                 "Normal state pixbuf",
                                 "GdkPixbuf for GTK_STATE_NORMAL",
                                 GDK_TYPE_PIXBUF,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PIXBUF_NORMAL, pspec);
    pspec = g_param_spec_object ("active-state-pixbuf",
                                 "Active state pixbuf",
                                 "GdkPixbuf for GTK_STATE_ACTIVE",
                                 GDK_TYPE_PIXBUF,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PIXBUF_ACTIVE, pspec);
    pspec = g_param_spec_object ("prelight-state-pixbuf",
                                 "Prelight state pixbuf",
                                 "GdkPixbuf for GTK_STATE_PRELIGHT",
                                 GDK_TYPE_PIXBUF,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PIXBUF_PRELIGHT, pspec);
    pspec = g_param_spec_object ("selected-state-pixbuf",
                                 "Selected state pixbuf",
                                 "GdkPixbuf for GTK_STATE_SELECTED",
                                 GDK_TYPE_PIXBUF,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PIXBUF_SELECTED, pspec);
    pspec = g_param_spec_object ("insensitive-state-pixbuf",
                                 "Insensitive state pixbuf",
                                 "GdkPixbuf for GTK_STATE_INSENSITIVE",
                                 GDK_TYPE_PIXBUF,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PIXBUF_INSENSITIVE, pspec);
}

static void
mux_icon_button_init (MuxIconButton *self)
{
}

GtkWidget*
mux_icon_button_new (GdkPixbuf *normal_pixbuf, gboolean toggleable)
{
    return g_object_new (MUX_TYPE_ICON_BUTTON, 
                         "normal-state-pixbuf", normal_pixbuf,
                         "toggleable", toggleable,
                         NULL);
}

void
mux_icon_button_set_pixbuf (MuxIconButton *button, GtkStateType state, GdkPixbuf *pixbuf)
{
    if (button->pixbufs[state]) {
        g_object_unref (button->pixbufs[state]);
    }
    button->pixbufs[state] = g_object_ref (pixbuf);

    if (state == GTK_STATE_NORMAL) {
        gtk_widget_queue_resize (GTK_WIDGET (button));
    } else if (state == GTK_WIDGET_STATE (GTK_WIDGET (button))) {
        gtk_widget_queue_draw (GTK_WIDGET (button));
    }
}

GdkPixbuf*
mux_icon_button_get_pixbuf (MuxIconButton *button, GtkStateType state)
{
    return button->pixbufs[state];
}

void
mux_icon_button_set_active (MuxIconButton *button, gboolean active)
{
    button->active = active;
    gtk_widget_queue_draw (GTK_WIDGET (button));
}

gboolean 
mux_icon_button_get_active (MuxIconButton *button)
{
    return button->active;
}
