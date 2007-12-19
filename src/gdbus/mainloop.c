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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dbus/dbus.h>
#include <glib.h>

#ifdef NEED_DBUS_WATCH_GET_UNIX_FD
#define dbus_watch_get_unix_fd dbus_watch_get_fd
#endif

#include "gdbus.h"
#include "debug.h"

/** @defgroup mainloop Functions for GLib main loop integration */

static dbus_int32_t connection_slot = -1;

typedef struct {
	DBusConnection *connection;
	GMainContext *context;
} ConnectionData;

typedef struct {
	DBusWatch *watch;
	GSource *source;
	DBusConnection *connection;
} WatchData;

typedef struct {
	DBusTimeout *timeout;
	guint id;
} TimeoutData;

static gboolean dispatch_message(void *data)
{
	DBusConnection *connection = data;

	DBG("connection %p", connection);

	dbus_connection_ref(connection);

	while (dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS);

	dbus_connection_unref(connection);

	return FALSE;
}

static gboolean dispatch_watch(GIOChannel *source,
				GIOCondition condition, gpointer user_data)
{
	WatchData *data = user_data;
	unsigned int flags = 0;

	DBG("source %p condition %d watch data %p", source, condition, data);

	if (condition & G_IO_IN)
		flags |= DBUS_WATCH_READABLE;

	if (condition & G_IO_OUT)
		flags |= DBUS_WATCH_WRITABLE;

	if (condition & G_IO_ERR)
		flags |= DBUS_WATCH_ERROR;

	if (condition & G_IO_HUP)
		flags |= DBUS_WATCH_HANGUP;

	dbus_watch_handle(data->watch, flags);

	if (dbus_connection_get_dispatch_status(data->connection) == DBUS_DISPATCH_DATA_REMAINS)
		g_timeout_add(0, dispatch_message, data->connection);

	return TRUE;
}

static void free_watch(void *memory)
{
	WatchData *data = memory;

	DBG("watch data %p", data);

	g_free(data);
}

static dbus_bool_t add_watch(DBusWatch *watch, void *data)
{
	ConnectionData *connection_data = data;
	GIOCondition condition = G_IO_ERR | G_IO_HUP;
	GIOChannel *channel;
	GSource *source;
	WatchData *watch_data;
	unsigned int flags;
	int fd;

	DBG("watch %p connection data %p", watch, connection_data);

	if (dbus_watch_get_enabled(watch) == FALSE)
		return TRUE;

	flags = dbus_watch_get_flags(watch);

	if (flags & DBUS_WATCH_READABLE)
		condition |= G_IO_IN;

	if (flags & DBUS_WATCH_WRITABLE)
		condition |= G_IO_OUT;

	fd = dbus_watch_get_unix_fd(watch);

	DBG("flags %d fd %d", flags, fd);

	watch_data = g_new0(WatchData, 1);

	channel = g_io_channel_unix_new(fd);

	source = g_io_create_watch(channel, condition);

	watch_data->watch = watch;
	watch_data->source = source;

	watch_data->connection = dbus_connection_ref(connection_data->connection);

	g_source_set_callback(source, (GSourceFunc) dispatch_watch,
							watch_data, NULL);

	g_source_attach(source, connection_data->context);

	dbus_watch_set_data(watch, watch_data, free_watch);

	g_io_channel_unref(channel);

	DBG("watch data %p", watch_data);

	return TRUE;
}

static void remove_watch(DBusWatch *watch, void *user_data)
{
	WatchData *watch_data;

	DBG("watch %p connection data %p", watch, user_data);

	watch_data = dbus_watch_get_data(watch);
	if (watch_data != NULL) {
		g_source_unref(watch_data->source);
		dbus_connection_unref(watch_data->connection);
		g_free(watch_data);
	}

	dbus_watch_set_data(watch, NULL, NULL);
}

static void watch_toggled(DBusWatch *watch, void *data)
{
	DBG("watch %p connection data %p", watch, data);

	if (dbus_watch_get_enabled(watch) == TRUE)
		add_watch(watch, data);
	else
		remove_watch(watch, data);
}

static gboolean dispatch_timeout(gpointer user_data)
{
	TimeoutData *data = user_data;

	DBG("timeout data %p", data);

	if (dbus_timeout_get_enabled(data->timeout) == TRUE)
		dbus_timeout_handle(data->timeout);

	return FALSE;
}

static void free_timeout(void *memory)
{
	TimeoutData *data = memory;

	DBG("timeout data %p", data);

	g_source_remove(data->id);

	g_free(data);
}

static dbus_bool_t add_timeout(DBusTimeout *timeout, void *data)
{
	TimeoutData *timeout_data;

	DBG("timeout %p connection data %p", timeout, data);

	if (dbus_timeout_get_enabled(timeout) == FALSE)
		return TRUE;

	timeout_data = g_new0(TimeoutData, 1);

	timeout_data->timeout = timeout;

	timeout_data->id = g_timeout_add(dbus_timeout_get_interval(timeout),
					dispatch_timeout, timeout_data);

	dbus_timeout_set_data(timeout, timeout_data, free_timeout);

	DBG("timeout data %p", timeout_data);

	return TRUE;
}

static void remove_timeout(DBusTimeout *timeout, void *data)
{
	DBG("timeout %p connection data %p", timeout, data);
}

static void timeout_toggled(DBusTimeout *timeout, void *data)
{
	DBG("timeout %p connection data %p", timeout, data);

	if (dbus_timeout_get_enabled(timeout) == TRUE)
		add_timeout(timeout, data);
	else
		remove_timeout(timeout, data);
}

static ConnectionData *setup_connection(DBusConnection *connection,
						GMainContext *context)
{
	ConnectionData *data;

	DBG("connection %p context %p", connection, context);

	data = g_new0(ConnectionData, 1);

	data->connection = dbus_connection_ref(connection);

	data->context = g_main_context_ref(context);

	DBG("connection data %p", data);

	return data;
}

static void free_connection(void *memory)
{
	ConnectionData *data = memory;

	DBG("connection data %p", data);

	g_dbus_remove_all_watches(data->connection);

	g_dbus_unregister_all_objects(data->connection);

	dbus_connection_unref(data->connection);

	g_main_context_unref(data->context);

	g_free(data);
}

static void dispatch_status(DBusConnection *connection,
				DBusDispatchStatus status, void *data)
{
	DBG("connection %p status %d connection data %p",
					connection, status, data);

	if (!dbus_connection_get_is_connected(connection))
		return;

	if (status == DBUS_DISPATCH_DATA_REMAINS)
		g_timeout_add(0, dispatch_message, connection);
}

static void wakeup_context(void *user_data)
{
	ConnectionData *data = user_data;

	DBG("connection data %p", data);

	g_main_context_wakeup(data->context);
}

/**
 * Setup connection with main context
 * @param connection the connection
 * @param context the GMainContext or NULL for default context
 *
 * Sets the watch and timeout functions of a DBusConnection
 * to integrate the connection with the GLib main loop.
 * Pass in NULL for the GMainContext unless you're
 * doing something specialized.
 */
void g_dbus_setup_connection(DBusConnection *connection,
						GMainContext *context)
{
	ConnectionData *data;

	DBG("connection %p context %p", connection, context);

	if (dbus_connection_allocate_data_slot(&connection_slot) == FALSE)
		return;

	DBG("connection slot %d", connection_slot);

	data = dbus_connection_get_data(connection, connection_slot);
	if (data != NULL)
		return;

	dbus_connection_set_exit_on_disconnect(connection, TRUE);

	if (context == NULL)
		context = g_main_context_default();

	data = setup_connection(connection, context);
	if (data == NULL)
		return;

	if (dbus_connection_set_data(connection, connection_slot,
					data, free_connection) == FALSE) {
		g_free(data);
		return;
	}

	dbus_connection_set_watch_functions(connection, add_watch,
				remove_watch, watch_toggled, data, NULL);

	dbus_connection_set_timeout_functions(connection, add_timeout,
				remove_timeout, timeout_toggled, data, NULL);

	dbus_connection_set_dispatch_status_function(connection,
						dispatch_status, data, NULL);

	dbus_connection_set_wakeup_main_function(connection,
						wakeup_context, data, NULL);
}

/**
 * Cleanup a connection setup
 * @param connection the connection
 *
 * Cleanup the setup of DBusConnection and free the
 * allocated memory.
 */
void g_dbus_cleanup_connection(DBusConnection *connection)
{
	DBG("connection %p slot %d", connection, connection_slot);

	if (connection_slot < 0)
		return;

	dbus_connection_set_data(connection, connection_slot, NULL, NULL);

	dbus_connection_free_data_slot(&connection_slot);

	DBG("connection slot %d", connection_slot);
}

/**
 * Connect to bus and setup connection
 * @param type bus type
 * @param name well known name
 * @return a DBusConnection
 *
 * Returns a connection to the given bus and requests a
 * well known name for it. Sets the watch and timeout
 * functions for it.
 */
DBusConnection *g_dbus_setup_bus(DBusBusType type, const char *name)
{
	DBusConnection *connection;
	DBusError error;

	DBG("type %d name %s", type, name);

	connection = dbus_bus_get(type, NULL);

	if (name) {
		dbus_error_init(&error);

		if (dbus_bus_request_name(connection, name,
				DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) !=
				DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ) {
			dbus_connection_unref(connection);
			return NULL;
		}

		if (dbus_error_is_set(&error)) {
			dbus_error_free(&error);
			dbus_connection_unref(connection);
			return NULL;
		}
	}

	g_dbus_setup_connection(connection, NULL);

	dbus_connection_unref(connection);

	return connection;
}

static DBusHandlerResult disconnect_filter(DBusConnection *connection,
					DBusMessage *message, void *data)
{
	if (dbus_message_is_signal(message,
			DBUS_INTERFACE_LOCAL, "Disconnected") == FALSE)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBG("disconnected");

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Sets callback function for message bus disconnects
 * @param connection the connection
 * @param function function called on connection disconnect
 * @param user_data user data to pass to the function
 * @param destroy function called to destroy user_data
 * @return TRUE on success
 *
 * Set a callback function that will be called when the
 * D-Bus message bus exits.
 */
gboolean g_dbus_set_disconnect_function(DBusConnection *connection,
				GDBusDisconnectFunction function,
				void *user_data, DBusFreeFunction destroy)
{
	dbus_connection_set_exit_on_disconnect(connection, FALSE);

	if (dbus_connection_add_filter(connection,
				disconnect_filter, NULL, NULL) == FALSE)
		return FALSE;

	return TRUE;
}
