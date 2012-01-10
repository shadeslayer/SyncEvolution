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

#include "mux-frame.h"
#include <math.h>

static GdkColor mux_frame_default_border_color = { 0, 0xdddd, 0xe2e2, 0xe5e5 };
static GdkColor mux_frame_default_bullet_color = { 0, 0xaaaa, 0xaaaa, 0xaaaa };
static gfloat mux_frame_bullet_size_factor = 1.3;
#define MUX_FRAME_BULLET_PADDING 10

static void mux_frame_buildable_init                (GtkBuildableIface *iface);
static void mux_frame_buildable_add_child           (GtkBuildable *buildable,
                                                     GtkBuilder   *builder,
                                                     GObject      *child,
                                                     const gchar  *type);

G_DEFINE_TYPE_WITH_CODE (MuxFrame, mux_frame, GTK_TYPE_FRAME,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, mux_frame_buildable_init))

static void
mux_frame_dispose (GObject *object)
{
    G_OBJECT_CLASS (mux_frame_parent_class)->dispose (object);
}

static void
mux_frame_finalize (GObject *object)
{
      G_OBJECT_CLASS (mux_frame_parent_class)->finalize (object);
}

static void
label_changed_cb (MuxFrame *frame)
{
    char *font = NULL;
    GtkFrame *gtk_frame = GTK_FRAME (frame);
    GtkWidget *label = gtk_frame_get_label_widget (gtk_frame);

    if (!label)
        return;

    /* ensure font is correct */
    gtk_widget_style_get (GTK_WIDGET (frame),
                          "title-font", &font,
                          NULL);
    if (font) {
        PangoFontDescription *desc;
        desc = pango_font_description_from_string (font);
        gtk_widget_modify_font (label, desc);
        pango_font_description_free (desc);
        g_free (font);
    }

    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 1.0);
}

static void
mux_frame_update_style (MuxFrame *frame)
{
    GdkColor *border_color, *bullet_color;

    gtk_widget_style_get (GTK_WIDGET (frame),
                          "border-color", &border_color, 
                          "bullet-color", &bullet_color,
                          NULL);

    if (border_color) {
        frame->border_color = *border_color;
        gdk_color_free (border_color);
    } else {
        frame->border_color = mux_frame_default_border_color;
    }
    if (bullet_color) {
        frame->bullet_color = *bullet_color;
        gdk_color_free (bullet_color);
    } else {
        frame->bullet_color = mux_frame_default_bullet_color;
    }

    label_changed_cb (frame);
}

static void
rounded_rectangle (cairo_t * cr,
                   double x, double y, double w, double h,
                   guint radius)
{
    if (radius > w / 2)
        radius = w / 2;
    if (radius > h / 2)
        radius = h / 2;

    cairo_move_to (cr, x + radius, y);
    cairo_arc (cr, x + w - radius, y + radius, radius, M_PI * 1.5, M_PI * 2);
    cairo_arc (cr, x + w - radius, y + h - radius, radius, 0, M_PI * 0.5);
    cairo_arc (cr, x + radius, y + h - radius, radius, M_PI * 0.5, M_PI);
    cairo_arc (cr, x + radius, y + radius, radius, M_PI, M_PI * 1.5);
}

static void
mux_frame_paint (GtkWidget *widget, GdkRectangle *area)
{
    MuxFrame *frame = MUX_FRAME (widget);
    cairo_t *cairo;
    GtkStyle *style;
    guint width;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (MUX_IS_FRAME (widget));
    g_return_if_fail (area != NULL);

    style = gtk_widget_get_style (widget);
    cairo = gdk_cairo_create (widget->window);
    width = gtk_container_get_border_width (GTK_CONTAINER (widget));

    /* draw border */
    if (width != 0) {
        gdk_cairo_set_source_color (cairo, &frame->border_color);

        rounded_rectangle (cairo,
                           widget->allocation.x,
                           widget->allocation.y,
                           widget->allocation.width,
                           widget->allocation.height,
                           width);
        cairo_clip (cairo);

        gdk_cairo_rectangle (cairo, area);
        cairo_clip (cairo);

        cairo_paint (cairo);
    }

    /* draw background */
    gdk_cairo_set_source_color (cairo, &style->bg[GTK_WIDGET_STATE(widget)]);
    rounded_rectangle (cairo,
                     widget->allocation.x + width,
                     widget->allocation.y + width,
                     widget->allocation.width - 2 * width,
                     widget->allocation.height- 2 * width,
                     width);
    cairo_clip (cairo);

    gdk_cairo_rectangle (cairo, area);
    cairo_clip (cairo);

    cairo_paint (cairo);

    /* draw bullet before title */
    if (gtk_frame_get_label_widget (GTK_FRAME (frame))) {
        gdk_cairo_set_source_color (cairo, &frame->bullet_color);

        rounded_rectangle (cairo,
                           frame->bullet_allocation.x,
                           frame->bullet_allocation.y,
                           frame->bullet_allocation.height,
                           frame->bullet_allocation.height,
                           4);
        cairo_clip (cairo);

        gdk_cairo_rectangle (cairo, area);
        cairo_clip (cairo);

        cairo_paint (cairo);
        
    }
    cairo_destroy (cairo);
}

static gboolean
mux_frame_expose(GtkWidget *widget,
                 GdkEventExpose *event)
{
    GtkWidgetClass *grand_parent;
    if (gtk_widget_is_drawable (widget)) {
        mux_frame_paint (widget, &event->area);

        grand_parent = GTK_WIDGET_CLASS (g_type_class_peek_parent (mux_frame_parent_class));
        grand_parent->expose_event (widget, event);
    }
    return FALSE;
}

static void
mux_frame_size_request (GtkWidget *widget,
                        GtkRequisition *requisition)
{
    GtkWidget *label = gtk_frame_get_label_widget (GTK_FRAME (widget));
    GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));
    GtkRequisition child_req;
    GtkRequisition title_req;

    child_req.width = child_req.height = 0;
    if (child)
        gtk_widget_size_request (child, &child_req);

    title_req.width = title_req.height = 0;
    if (label) {
        gtk_widget_size_request (label, &title_req);
        /* add room for bullet */
        title_req.height = title_req.height * mux_frame_bullet_size_factor +  
                           2 * MUX_FRAME_BULLET_PADDING;
        title_req.width += title_req.height * mux_frame_bullet_size_factor + 
                           2 * MUX_FRAME_BULLET_PADDING;
    }

    requisition->width = MAX (child_req.width, title_req.width) +
                         2 * (GTK_CONTAINER (widget)->border_width +
                              GTK_WIDGET (widget)->style->xthickness);
    requisition->height = title_req.height + child_req.height +
                          2 * (GTK_CONTAINER (widget)->border_width +
                               GTK_WIDGET (widget)->style->ythickness);
}



static void
mux_frame_size_allocate (GtkWidget *widget,
                         GtkAllocation *allocation)
{
    GtkBin *bin = GTK_BIN (widget);
    MuxFrame *mux_frame = MUX_FRAME (widget);
    GtkFrame *frame = GTK_FRAME (widget);
    GtkAllocation child_allocation;
    int xmargin, ymargin, title_height;

    widget->allocation = *allocation;
    xmargin = GTK_CONTAINER (widget)->border_width +
              widget->style->xthickness;
    ymargin = GTK_CONTAINER (widget)->border_width +
              widget->style->ythickness;

    title_height = 0;
    if (frame->label_widget) {
        GtkAllocation title_allocation;
        GtkRequisition title_req;
        gtk_widget_get_child_requisition (frame->label_widget, &title_req);

        /* the bullet is bigger than the text */
        title_height = title_req.height * mux_frame_bullet_size_factor + 
                       2 * MUX_FRAME_BULLET_PADDING;

        /* x allocation starts after bullet */
        title_allocation.x = allocation->x + xmargin + title_height;
        title_allocation.y = allocation->y + ymargin + MUX_FRAME_BULLET_PADDING;
        title_allocation.width = MIN (title_req.width,
                                      allocation->width - 2 * xmargin - title_height);
        title_allocation.height = title_height - 2 * MUX_FRAME_BULLET_PADDING;
        gtk_widget_size_allocate (frame->label_widget, &title_allocation);

        mux_frame->bullet_allocation.x = allocation->x + xmargin + MUX_FRAME_BULLET_PADDING;
        mux_frame->bullet_allocation.y = allocation->y + ymargin + MUX_FRAME_BULLET_PADDING;
        mux_frame->bullet_allocation.width = title_allocation.height;
        mux_frame->bullet_allocation.height = title_allocation.height;
    }

    child_allocation.x = allocation->x + xmargin;
    child_allocation.y = allocation->y + ymargin + title_height;
    child_allocation.width = allocation->width - 2 * xmargin;
    child_allocation.height = allocation->height - 2 * ymargin - title_height;

    if (GTK_WIDGET_MAPPED (widget) &&
        (child_allocation.x != frame->child_allocation.x ||
         child_allocation.y != frame->child_allocation.y ||
         child_allocation.width != frame->child_allocation.width ||
         child_allocation.height != frame->child_allocation.height)) {
        gdk_window_invalidate_rect (widget->window, &widget->allocation, FALSE);
    }

    if (bin->child && GTK_WIDGET_VISIBLE (bin->child)) {
        gtk_widget_size_allocate (bin->child, &child_allocation);
    }

    frame->child_allocation = child_allocation;
}

static void mux_frame_style_set (GtkWidget *widget,
                                 GtkStyle *previous)
{
    MuxFrame *frame = MUX_FRAME (widget);

    mux_frame_update_style (frame);

    GTK_WIDGET_CLASS (mux_frame_parent_class)->style_set (widget, previous);
}

static void
mux_frame_class_init (MuxFrameClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    GParamSpec *pspec;

    object_class->dispose = mux_frame_dispose;
    object_class->finalize = mux_frame_finalize;

    widget_class->expose_event = mux_frame_expose;
    widget_class->size_request = mux_frame_size_request;
    widget_class->size_allocate = mux_frame_size_allocate;
    widget_class->style_set = mux_frame_style_set;

    pspec = g_param_spec_boxed ("border-color",
                                "Border color",
                                "Color of the outside border",
                                GDK_TYPE_COLOR,
                                G_PARAM_READABLE);
    gtk_widget_class_install_style_property(widget_class, pspec);
    pspec = g_param_spec_boxed ("bullet-color",
                                "Bullet color",
                                "Color of the rounded rectangle before a title",
                                GDK_TYPE_COLOR,
                                G_PARAM_READABLE);
    gtk_widget_class_install_style_property(widget_class, pspec);
    pspec = g_param_spec_string ("title-font",
                                 "Title font",
                                 "Pango font description string for title text",
                                 "12",
                                 G_PARAM_READWRITE);
    gtk_widget_class_install_style_property(widget_class, pspec);
}

static void
mux_frame_buildable_add_child (GtkBuildable *buildable,
                               GtkBuilder *builder,
                               GObject *child,
                               const gchar *type)
{
  if (!type)
    gtk_container_add (GTK_CONTAINER (buildable), GTK_WIDGET (child));
  else
    GTK_BUILDER_WARN_INVALID_CHILD_TYPE (MUX_FRAME (buildable), type);
}

static void
mux_frame_buildable_init (GtkBuildableIface *iface)
{
    iface->add_child = mux_frame_buildable_add_child;
}

static void
mux_frame_init (MuxFrame *self)
{
    g_signal_connect (self, "notify::label-widget",
                      G_CALLBACK (label_changed_cb), NULL);
}

GtkWidget*
mux_frame_new (void)
{
    return g_object_new (MUX_TYPE_FRAME, 
                         "border-width", 4,
                         NULL);
}

