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

#ifndef SYNC_UI_H
#define SYNC_UI_H

#include <gtk/gtk.h>
#include "config.h"
#include "sync-ui-config.h"
#include "sync-ui.h"

#define SYNC_UI_LIST_ICON_SIZE 32
#define SYNC_UI_LIST_BTN_WIDTH 150


char* get_pretty_source_name (const char *source_name);
void show_error_dialog (GtkWidget *widget, const char* message);


GtkWidget* sync_ui_create_main_window ();

#endif
