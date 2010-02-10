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

typedef struct _app_data app_data;

typedef enum SyncErrorResponse {
    SYNC_ERROR_RESPONSE_NONE,
    SYNC_ERROR_RESPONSE_SYNC,
    SYNC_ERROR_RESPONSE_SETTINGS_SELECT,
    SYNC_ERROR_RESPONSE_SETTINGS_OPEN,
    SYNC_ERROR_RESPONSE_EMERGENCY,
    SYNC_ERROR_RESPONSE_EMERGENCY_SLOW_SYNC,
} SyncErrorResponse;


char* get_pretty_source_name (const char *source_name);
char* get_error_string_for_code (int error_code, SyncErrorResponse *response);
void show_error_dialog (GtkWidget *widget, const char* message);
gboolean show_confirmation (GtkWidget *widget, const char *message, const char *yes, const char *no);

app_data *sync_ui_create ();
GtkWindow *sync_ui_get_main_window (app_data *data);
void sync_ui_show_settings (app_data *data, const char *id);

void toggle_set_active (GtkWidget *toggle, gboolean active);
gboolean toggle_get_active (GtkWidget *toggle);

#endif
