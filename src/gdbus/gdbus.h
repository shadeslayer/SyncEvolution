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

#include <stdarg.h>

#include <dbus/dbus.h>
#include <glib.h>

G_BEGIN_DECLS

/** Destroy function */
typedef void (* GDBusDestroyFunction) (void *user_data);

/** Method function */
typedef DBusMessage * (* GDBusMethodFunction) (DBusConnection *connection,
					DBusMessage *message, void *user_data);

/** Property get function */
typedef dbus_bool_t (* GDBusPropertyGetFunction) (DBusConnection *connection,
					DBusMessageIter *iter, void *user_data);
/** Property set function */
typedef dbus_bool_t (* GDBusPropertySetFunction) (DBusConnection *connection,
					DBusMessageIter *iter, void *user_data);

/** Method flags */
typedef enum {
	G_DBUS_METHOD_FLAGS_DEPRECATED = (1 << 0),
	G_DBUS_METHOD_FLAGS_NOREPLY    = (1 << 1),
} GDBusMethodFlags;

/** Signal flags */
typedef enum {
	G_DBUS_SIGNAL_FLAGS_DEPRECATED = (1 << 0),
} GDBusSignalFlags;

/** Property flags */
typedef enum {
	G_DBUS_PROPERTY_FLAGS_DEPRECATED = (1 << 0),
} GDBusPropertyFlags;

typedef struct {
	const char *name;		/**< Method name */
	const char *signature;		/**< Method signature */
	const char *reply;		/**< Reply signature */
	GDBusMethodFunction function;	/**< Method function */
	GDBusMethodFlags flags;		/**< Method flags */
} GDBusMethodTable;

typedef struct {
	const char *name;		/**<Signal name */
	const char *signature;		/**<Signal signature */
	GDBusSignalFlags flags;		/**<Signal flags */
} GDBusSignalTable;

typedef struct {
	const char *name;		/**<Property name */
	const char *type;		/**<Property value type */
	GDBusPropertyGetFunction get;	/**<Property get function */
	GDBusPropertyGetFunction set;	/**<Property set function */
	GDBusPropertyFlags flags;	/**<Property flags */
} GDBusPropertyTable;

void g_dbus_setup_connection(DBusConnection *connection,
						GMainContext *context);
void g_dbus_cleanup_connection(DBusConnection *connection);

DBusConnection *g_dbus_setup_bus(DBusBusType type, const char *name);

gboolean g_dbus_register_object(DBusConnection *connection, const char *path,
				void *user_data, GDBusDestroyFunction destroy);
gboolean g_dbus_unregister_object(DBusConnection *connection, const char *path);
gboolean g_dbus_unregister_object_hierarchy(DBusConnection *connection,
							const char *path);
void g_dbus_unregister_all_objects(DBusConnection *connection);

gboolean g_dbus_register_interface(DBusConnection *connection,
					const char *path, const char *name,
					GDBusMethodTable *methods,
					GDBusSignalTable *signals,
					GDBusPropertyTable *properties);
gboolean g_dbus_unregister_interface(DBusConnection *connection,
					const char *path, const char *name);

gboolean g_dbus_emit_signal(DBusConnection *connection,
				const char *path, const char *interface,
				const char *name, int type, ...);
gboolean g_dbus_emit_signal_valist(DBusConnection *connection,
				const char *path, const char *interface,
				const char *name, int type, va_list args);

G_END_DECLS

#ifdef __cplusplus
}
#endif

#endif /* __GDBUS_H */
