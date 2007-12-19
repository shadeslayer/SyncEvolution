/*
 *
 *  Library for simple D-Bus integration with GLib
 *
 *  Copyright (C) 2007  Intel Corporation. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License version 2.1 as published by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __GDBUS_H
#define __GDBUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <dbus/dbus.h>
#include <glib.h>

G_BEGIN_DECLS

void g_dbus_setup_connection(DBusConnection *connection,
						GMainContext *context);
void g_dbus_cleanup_connection(DBusConnection *connection);

DBusConnection *g_dbus_setup_bus(DBusBusType type, const char *name);

G_END_DECLS

#ifdef __cplusplus
}
#endif

#endif /* __GDBUS_H */
