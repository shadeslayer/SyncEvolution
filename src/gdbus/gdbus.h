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

#ifndef __BDBUS_H
#define __BDBUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdint.h>

#include <dbus/dbus.h>
#include <glib.h>

/**
 * SECTION:gdbus
 * @title: D-Bus helper library
 * @short_description: Library for simple D-Bus integration with GLib
 */

/**
 * BDBusDestroyFunction:
 * @user_data: user data to pass to the function
 *
 * Destroy function
 */
typedef void (* BDBusDestroyFunction) (void *user_data);

/**
 * BDBusWatchFunction:
 * @connection: a #DBusConnection
 * @user_data: user data to pass to the function
 *
 * Watch function
 */
typedef void (* BDBusWatchFunction) (DBusConnection *connection,
							void *user_data);

/**
 * BDBusSignalFunction:
 * @connection: a #DBusConnection
 * @message: a #DBusMessage
 * @user_data: user data to pass to the function
 *
 * Signal function
 *
 * Returns: #FALSE to remove this watch
 */
typedef gboolean (* BDBusSignalFunction) (DBusConnection *connection,
					DBusMessage *message, void *user_data);

/**
 * BDBusMethodFunction:
 * @connection: a #DBusConnection
 * @message: a #DBusMessage
 * @user_data: user data to pass to the function
 *
 * Method function
 *
 * Returns: #DBusMessage reply
 */
typedef DBusMessage * (* BDBusMethodFunction) (DBusConnection *connection,
					DBusMessage *message, void *user_data);

/**
 * BDBusPropertyGetFunction:
 * @connection: a #DBusConnection
 * @iter: a #DBusMessageIter
 * @user_data: user data to pass to the function
 *
 * Property get function
 *
 * Returns: #TRUE on success
 */
typedef dbus_bool_t (* BDBusPropertyGetFunction) (DBusConnection *connection,
					DBusMessageIter *iter, void *user_data);

/**
 * BDBusPropertySetFunction:
 * @connection: a #DBusConnection
 * @iter: a #DBusMessageIter
 * @user_data: user data to pass to the function
 *
 * Property set function
 *
 * Returns: #TRUE on success
 */
typedef dbus_bool_t (* BDBusPropertySetFunction) (DBusConnection *connection,
					DBusMessageIter *iter, void *user_data);

/**
 * BDBusInterfaceFunction:
 * @user_data: user data to pass to the function
 *
 * Callback function for interface
 */
typedef void (* BDBusInterfaceFunction) (void *user_data); 

/**
 * BDBusMethodFlags:
 * @G_DBUS_METHOD_FLAG_DEPRECATED: annotate deprecated methods
 * @G_DBUS_METHOD_FLAG_NOREPLY: annotate methods with no reply
 * @G_DBUS_METHOD_FLAG_ASYNC: annotate asynchronous methods
 * @G_DBUS_METHOD_FLAG_METHOD_DATA: the method is passed the
 *                                  BDBusMethodTable method_data pointer
 *                                  instead of the b_dbus_register_interface()
 *                                  user_data pointer
 *
 * Method flags
 */
typedef enum {
	G_DBUS_METHOD_FLAG_NONE = 0,
	G_DBUS_METHOD_FLAG_DEPRECATED = (1 << 0),
	G_DBUS_METHOD_FLAG_NOREPLY    = (1 << 1),
	G_DBUS_METHOD_FLAG_ASYNC      = (1 << 2),
	G_DBUS_METHOD_FLAG_METHOD_DATA = (1 << 3),
} BDBusMethodFlags;

/**
 * BDBusSignalFlags:
 * @G_DBUS_SIGNAL_FLAG_DEPRECATED: annotate deprecated signals
 *
 * Signal flags
 */
typedef enum {
	G_DBUS_SIGNAL_FLAG_NONE = 0,
	G_DBUS_SIGNAL_FLAG_DEPRECATED = (1 << 0),
} BDBusSignalFlags;

/**
 * BDBusPropertyFlags:
 * @G_DBUS_PROPERTY_FLAG_DEPRECATED: annotate deprecated properties
 *
 * Property flags
 */
typedef enum {
	G_DBUS_PROPERTY_FLAG_NONE = 0,
	G_DBUS_PROPERTY_FLAG_DEPRECATED = (1 << 0),
} BDBusPropertyFlags;

/**
 * BDBusMethodTable:
 * @name: method name
 * @signature: method signature
 * @reply: reply signature
 * @function: method function
 * @flags: method flags
 * @method_data: passed as BDBusMethodFunction user_data if
 *               G_DBUS_METHOD_FLAG_METHOD_DATA is set
 * @destroy: destructor function for method table; not called
 *           by gdbus itself, because it never frees BDBusMethodTable
 *           entries, but useful in upper layers
 *
 * Method table
 */
typedef struct {
	const char *name;
	const char *signature;
	const char *reply;
	BDBusMethodFunction function;
	BDBusMethodFlags flags;
	void *method_data;
	BDBusDestroyFunction destroy;
} BDBusMethodTable;

/**
 * BDBusSignalTable:
 * @name: signal name
 * @signature: signal signature
 * @flags: signal flags
 *
 * Signal table
 */
typedef struct {
	const char *name;
	const char *signature;
	BDBusSignalFlags flags;
} BDBusSignalTable;

/**
 * BDBusPropertyTable:
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
	BDBusPropertyGetFunction get;
	BDBusPropertyGetFunction set;
	BDBusPropertyFlags flags;
} BDBusPropertyTable;

void b_dbus_setup_connection(DBusConnection *connection,
						gboolean unshared,
						GMainContext *context);
void b_dbus_cleanup_connection(DBusConnection *connection);

void b_dbus_setup_server(DBusServer *server);

DBusConnection *b_dbus_setup_bus(DBusBusType type, const char *name,
							gboolean unshared,
							DBusError *error);

DBusConnection *b_dbus_setup_address(const char *address, DBusError *error);

gboolean b_dbus_request_name(DBusConnection *connection, const char *name,
							DBusError *error);

gboolean b_dbus_set_disconnect_function(DBusConnection *connection,
				BDBusWatchFunction function,
				void *user_data, BDBusDestroyFunction destroy);

gboolean b_dbus_register_interface(DBusConnection *connection,
					const char *path, const char *name,
					BDBusMethodTable *methods,
					BDBusSignalTable *signals,
					BDBusPropertyTable *properties,
					void *user_data,
					BDBusDestroyFunction destroy);
gboolean b_dbus_register_interface_with_callback(DBusConnection *connection,
					const char *path, const char *name,
					BDBusMethodTable *methods,
					BDBusSignalTable *signals,
					BDBusPropertyTable *properties,
					void *user_data,
					BDBusDestroyFunction destroy,
					BDBusInterfaceFunction callback);
gboolean b_dbus_unregister_interface(DBusConnection *connection,
					const char *path, const char *name);

DBusMessage *b_dbus_create_error(DBusMessage *message, const char *name,
						const char *format, ...);
DBusMessage *b_dbus_create_error_valist(DBusMessage *message, const char *name,
					const char *format, va_list args);
DBusMessage *b_dbus_create_reply(DBusMessage *message, int type, ...);
DBusMessage *b_dbus_create_reply_valist(DBusMessage *message,
						int type, va_list args);

gboolean b_dbus_send_message(DBusConnection *connection, DBusMessage *message);
gboolean b_dbus_send_error(DBusConnection *connection, DBusMessage *message,
				const char *name, const char *format, ...);

gboolean b_dbus_send_reply(DBusConnection *connection,
				DBusMessage *message, int type, ...);
gboolean b_dbus_send_reply_valist(DBusConnection *connection,
				DBusMessage *message, int type, va_list args);

gboolean b_dbus_emit_signal(DBusConnection *connection,
				const char *path, const char *interface,
				const char *name, int type, ...);
gboolean b_dbus_emit_signal_valist(DBusConnection *connection,
				const char *path, const char *interface,
				const char *name, int type, va_list args);

guint b_dbus_add_service_watch(DBusConnection *connection, const char *name,
				BDBusWatchFunction connect,
				BDBusWatchFunction disconnect,
				void *user_data, BDBusDestroyFunction destroy);
guint b_dbus_add_disconnect_watch(DBusConnection *connection,
				const char *name, BDBusWatchFunction function,
				void *user_data, BDBusDestroyFunction destroy);
guint b_dbus_add_signal_watch(DBusConnection *connection,
                              const char *rule, BDBusSignalFunction function,
                              void *user_data, BDBusDestroyFunction destroy,
                              gboolean is_bus_conn);
gboolean b_dbus_remove_watch(DBusConnection *connection, guint tag);
void b_dbus_remove_all_watches(DBusConnection *connection);

#ifdef __cplusplus
}
#endif

#endif /* __BDBUS_H */
