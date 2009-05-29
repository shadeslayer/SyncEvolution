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

#ifndef __SYNCEVO_DBUS_SERVER_H
#define __SYNCEVO_DBUS_SERVER_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

typedef struct _SyncevoDBusServer SyncevoDBusServer;
#include "DBusSyncClient.h"

#define SYNCEVO_TYPE_DBUS_SERVER (syncevo_dbus_server_get_type ())
#define SYNCEVO_DBUS_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCEVO_TYPE_DBUS_SERVER, SyncevoDBusServerClass))

typedef struct _SyncevoDBusServer {
	GObject parent_object;

	DBusSyncClient *client;
	char *server;
	GPtrArray *sources;
	
	gboolean aborted;

	guint shutdown_timeout_src;
} SyncevoDBusServer;

typedef struct _SyncevoDBusServerClass {
	GObjectClass parent_class;
	DBusGConnection *connection;
	
	/* DBus signals*/
	void (*progress) (SyncevoDBusServer *service,
	                  char *server,
	                  char *source,
	                  int type,
	                  int extra1, int extra2, int extra3);
	void (*server_message) (SyncevoDBusServer *service,
	                        char *server,
	                        char *message);
} SyncevoDBusServerClass;

GType syncevo_dbus_server_get_type (void);

void
emit_progress (const char *source,
               int type,
               int extra1,
               int extra2,
               int extra3, 
               gpointer data);

void
emit_server_message (const char *message, 
                     gpointer data);

gboolean 
check_for_suspend (gpointer data);

#endif // __SYNCEVO_DBUS_SERVER_H
