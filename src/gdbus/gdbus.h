/*
 *
 *  Library for simple D-Bus integration with GLib
 *
 *  Copyright (C) 2007-2008  Intel Corporation. All rights reserved.
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

/**
 * SECTION:gdbus
 * @title: D-Bus helper library
 * @short_description: Library for simple D-Bus integration with GLib
 */

/**
 * GDBusDestroyFunction:
 * @user_data: user data to pass to the function
 *
 * Destroy function
 */
typedef void (* GDBusDestroyFunction) (void *user_data);

/**
 * GDBusWatchFunction:
 * @user_data: user data to pass to the function
 *
 * Watch function
 */
typedef void (* GDBusWatchFunction) (void *user_data);

/**
 * GDBusSignalFunction:
 * @connection: a #DBusConnection
 * @message: a #DBusMessage
 * @user_data: user data to pass to the function
 *
 * Signal function
 *
 * Returns: #FALSE to remove this watch
 */
typedef gboolean (* GDBusSignalFunction) (DBusConnection *connection,
					DBusMessage *message, void *user_data);

/**
 * GDBusMethodFunction:
 * @connection: a #DBusConnection
 * @message: a #DBusMessage
 * @user_data: user data to pass to the function
 *
 * Method function
 *
 * Returns: #DBusMessage reply
 */
typedef DBusMessage * (* GDBusMethodFunction) (DBusConnection *connection,
					DBusMessage *message, void *user_data);

/**
 * GDBusPropertyGetFunction:
 * @connection: a #DBusConnection
 * @iter: a #DBusMessageIter
 * @user_data: user data to pass to the function
 *
 * Property get function
 *
 * Returns: #TRUE on success
 */
typedef dbus_bool_t (* GDBusPropertyGetFunction) (DBusConnection *connection,
					DBusMessageIter *iter, void *user_data);

/**
 * GDBusPropertySetFunction:
 * @connection: a #DBusConnection
 * @iter: a #DBusMessageIter
 * @user_data: user data to pass to the function
 *
 * Property set function
 *
 * Returns: #TRUE on success
 */
typedef dbus_bool_t (* GDBusPropertySetFunction) (DBusConnection *connection,
					DBusMessageIter *iter, void *user_data);

/**
 * GDBusMethodFlags:
 * @G_DBUS_METHOD_FLAG_DEPRECATED: annotate deprecated methods
 * @G_DBUS_METHOD_FLAG_NOREPLY: annotate methods with no reply
 * @G_DBUS_METHOD_FLAG_ASYNC: annotate asynchronous methods
 *
 * Method flags
 */
typedef enum {
	G_DBUS_METHOD_FLAG_DEPRECATED = (1 << 0),
	G_DBUS_METHOD_FLAG_NOREPLY    = (1 << 1),
	G_DBUS_METHOD_FLAG_ASYNC      = (1 << 2),
} GDBusMethodFlags;

/**
 * GDBusSignalFlags:
 * @G_DBUS_SIGNAL_FLAG_DEPRECATED: annotate deprecated signals
 *
 * Signal flags
 */
typedef enum {
	G_DBUS_SIGNAL_FLAG_DEPRECATED = (1 << 0),
} GDBusSignalFlags;

/**
 * GDBusPropertyFlags:
 * @G_DBUS_PROPERTY_FLAG_DEPRECATED: annotate deprecated properties
 *
 * Property flags
 */
typedef enum {
	G_DBUS_PROPERTY_FLAG_DEPRECATED = (1 << 0),
} GDBusPropertyFlags;

/**
 * GDBusMethodTable:
 * @name: method name
 * @signature: method signature
 * @reply: reply signature
 * @function: method function
 * @flags: method flags
 *
 * Method table
 */
typedef struct {
	const char *name;
	const char *signature;
	const char *reply;
	GDBusMethodFunction function;
	GDBusMethodFlags flags;
} GDBusMethodTable;

/**
 * GDBusSignalTable:
 * @name: signal name
 * @signature: signal signature
 * @flags: signal flags
 *
 * Signal table
 */
typedef struct {
	const char *name;
	const char *signature;
	GDBusSignalFlags flags;
} GDBusSignalTable;

/**
 * GDBusPropertyTable:
 * @name: property name
 * @type: property value type
 * @get: property get function
 * @set: property set function
 * @flags: property flags
 *
 * Property table
 */
typedef struct {
	const char *name;
	const char *type;
	GDBusPropertyGetFunction get;
	GDBusPropertyGetFunction set;
	GDBusPropertyFlags flags;
} GDBusPropertyTable;

void g_dbus_setup_connection(DBusConnection *connection,
						GMainContext *context);
void g_dbus_cleanup_connection(DBusConnection *connection);

DBusConnection *g_dbus_setup_bus(DBusBusType type, const char *name,
							DBusError *error);

DBusConnection *g_dbus_setup_address(const char *address, DBusError *error);

gboolean g_dbus_request_name(DBusConnection *connection, const char *name,
							DBusError *error);

gboolean g_dbus_set_disconnect_function(DBusConnection *connection,
				GDBusWatchFunction function,
				void *user_data, GDBusDestroyFunction destroy);

gboolean g_dbus_register_interface(DBusConnection *connection,
					const char *path, const char *name,
					GDBusMethodTable *methods,
					GDBusSignalTable *signals,
					GDBusPropertyTable *properties,
					void *user_data,
					GDBusDestroyFunction destroy);
gboolean g_dbus_unregister_interface(DBusConnection *connection,
					const char *path, const char *name);

DBusMessage *g_dbus_create_error(DBusMessage *message, const char *name,
						const char *format, ...);
DBusMessage *g_dbus_create_error_valist(DBusMessage *message, const char *name,
					const char *format, va_list args);
DBusMessage *g_dbus_create_reply(DBusMessage *message, int type, ...);
DBusMessage *g_dbus_create_reply_valist(DBusMessage *message,
						int type, va_list args);

gboolean g_dbus_send_message(DBusConnection *connection, DBusMessage *message);
gboolean g_dbus_send_error(DBusConnection *connection, DBusMessage *message,
				const char *name, const char *format, ...);

gboolean g_dbus_send_reply(DBusConnection *connection,
				DBusMessage *message, int type, ...);
gboolean g_dbus_send_reply_valist(DBusConnection *connection,
				DBusMessage *message, int type, va_list args);

gboolean g_dbus_emit_signal(DBusConnection *connection,
				const char *path, const char *interface,
				const char *name, int type, ...);
gboolean g_dbus_emit_signal_valist(DBusConnection *connection,
				const char *path, const char *interface,
				const char *name, int type, va_list args);

guint g_dbus_add_service_watch(DBusConnection *connection, const char *name,
				GDBusWatchFunction connect,
				GDBusWatchFunction disconnect,
				void *user_data, GDBusDestroyFunction destroy);
guint g_dbus_add_disconnect_watch(DBusConnection *connection,
				const char *name, GDBusWatchFunction function,
				void *user_data, GDBusDestroyFunction destroy);
guint g_dbus_add_signal_watch(DBusConnection *connection,
				const char *rule, GDBusSignalFunction function,
				void *user_data, GDBusDestroyFunction destroy);
gboolean g_dbus_remove_watch(DBusConnection *connection, guint tag);
void g_dbus_remove_all_watches(DBusConnection *connection);

#ifdef __cplusplus
}
#endif

#endif /* __GDBUS_H */
