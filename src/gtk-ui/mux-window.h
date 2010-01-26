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
    GtkWidget *back_btn;
    GtkWidget *settings;
    GtkWidget *settings_button;
    GtkWidget *notebook;

    gint settings_index;

    GtkAllocation child_allocation;

    MuxDecorations decorations;
    GdkColor title_bar_color;
    guint title_bar_height;
    char *back_title;

} MuxWindow;

typedef struct {
    GtkWindowClass parent_class;

    void (*settings_visibility_changed) (MuxWindow *window);
} MuxWindowClass;

GType mux_window_get_type (void);

GtkWidget* mux_window_new (const char *back_title);

void mux_window_set_decorations (MuxWindow *window, MuxDecorations decorations);
MuxDecorations mux_window_get_decorations (MuxWindow *window);

void mux_window_set_settings_visible (MuxWindow *window, gboolean visible);
gboolean mux_window_get_settings_visible (MuxWindow *window);

gint mux_window_append_page (MuxWindow *window, GtkWidget *page, gboolean is_settings);

void mux_window_set_current_page (MuxWindow *window, gint index);
gint mux_window_get_current_page (MuxWindow *window);

G_END_DECLS

#endif
