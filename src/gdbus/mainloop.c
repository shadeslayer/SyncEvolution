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

static dbus_int32_t connection_slot = -1;
static dbus_int32_t server_slot = -1;

typedef struct {
	DBusConnection *connection;
	GMainContext *context;
	GSource *queue;
        BDBusWatchFunction disconnect;
        void *disconnect_data;
        BDBusDestroyFunction disconnect_destroy;
	gboolean unshared;
} ConnectionData;

typedef struct {
	DBusWatch *watch;
	GSource *source;
} WatchData;

typedef struct {
	DBusTimeout *timeout;
	guint id;
} TimeoutData;

typedef struct {
	GSource source;
	DBusConnection *connection;
} QueueData;

static GSList *watches = NULL;
static GSList *timeouts = NULL;

static gboolean queue_prepare(GSource *source, gint *timeout)
{
	DBusConnection *connection = ((QueueData *) source)->connection;

	DBG("source %p", source);

	*timeout = -1;

	return (dbus_connection_get_dispatch_status(connection) ==
						DBUS_DISPATCH_DATA_REMAINS);
}

static gboolean queue_check(GSource *source)
{
	DBG("source %p", source);

	return FALSE;
}

static gboolean queue_dispatch(GSource *source, GSourceFunc callback,
							gpointer user_data)
{
	DBusConnection *connection = ((QueueData *) source)->connection;

	DBG("source %p", source);

	dbus_connection_ref(connection);

	dbus_connection_dispatch(connection);

	dbus_connection_unref(connection);

	return TRUE;
}

static GSourceFuncs queue_funcs = {
	queue_prepare,
	queue_check,
	queue_dispatch,
	NULL
};

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

	return TRUE;
}

static void finalize_watch(gpointer memory)
{
	DBG("watch data %p", memory);
}

static void free_watch(void *memory)
{
	DBG("watch data %p", memory);

	if (memory == NULL)
		return;

	watches = g_slist_remove(watches, memory);

	WatchData *watch_data = (WatchData*)memory;
	GSource* source = watch_data->source;

	if (source != NULL) {
		g_source_destroy(source);
		watch_data->source = NULL;
	}

	g_free(watch_data);
}

static dbus_bool_t add_watch(DBusWatch *watch, void *user_data)
{
	ConnectionData *data = user_data;
	GIOCondition condition = G_IO_ERR | G_IO_HUP;
	GIOChannel *channel;
	GSource *source;
	WatchData *watch_data;
	unsigned int flags;
	int fd;

	DBG("watch %p connection data %p", watch, data);

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

	g_source_set_callback(source, (GSourceFunc) dispatch_watch,
						watch_data, finalize_watch);

	g_source_attach(source, data->context);

	watches = g_slist_prepend(watches, watch_data);

	dbus_watch_set_data(watch, watch_data, free_watch);

	g_io_channel_unref(channel);

	DBG("watch data %p", watch_data);

	return TRUE;
}

static void remove_watch(DBusWatch *watch, void *user_data)
{
	WatchData *watch_data = dbus_watch_get_data(watch);

	DBG("watch %p watch data %p connection data %p", watch, watch_data, user_data);

	if (watch_data == NULL)
		return;

	watches = g_slist_remove(watches, watch_data);

	if (watch_data->source != NULL) {
		g_source_destroy(watch_data->source);
		// g_source_unref(watch_data->source);
		watch_data->source = NULL;
	}

        // this will call free_watch() and deallocate watch_data
	dbus_watch_set_data(watch, NULL, NULL);
}

static void watch_toggled(DBusWatch *watch, void *user_data)
{
	DBG("watch %p connection data %p", watch, user_data);

	if (dbus_watch_get_enabled(watch) == TRUE)
		add_watch(watch, user_data);
	else
		remove_watch(watch, user_data);
}

static gboolean dispatch_timeout(gpointer user_data)
{
	TimeoutData *timeout_data = user_data;

	DBG("timeout data %p", timeout_data);

	dbus_timeout_handle(timeout_data->timeout);

	return FALSE;
}

static void free_timeout(void *memory)
{
	TimeoutData *timeout_data = memory;

	DBG("timeout data %p", timeout_data);

	if (timeout_data->id > 0)
		g_source_remove(timeout_data->id);

	g_free(timeout_data);
}

static dbus_bool_t add_timeout(DBusTimeout *timeout, void *user_data)
{
	TimeoutData *timeout_data;

	DBG("timeout %p connection data %p", timeout, user_data);

	if (dbus_timeout_get_enabled(timeout) == FALSE)
		return TRUE;

	timeout_data = g_new0(TimeoutData, 1);

	timeout_data->timeout = timeout;

	timeout_data->id = g_timeout_add(dbus_timeout_get_interval(timeout),
					dispatch_timeout, timeout_data);

	timeouts = g_slist_prepend(timeouts, timeout_data);

	dbus_timeout_set_data(timeout, timeout_data, free_timeout);

	DBG("timeout data %p", timeout_data);

	return TRUE;
}

static void remove_timeout(DBusTimeout *timeout, void *user_data)
{
	TimeoutData *timeout_data = dbus_timeout_get_data(timeout);

	DBG("timeout %p connection data %p", timeout, user_data);

	if (timeout_data == NULL)
		return;

	timeouts = g_slist_remove(timeouts, timeout_data);

	if (timeout_data->id > 0)
		g_source_remove(timeout_data->id);

	timeout_data->id = 0;
}

static void timeout_toggled(DBusTimeout *timeout, void *user_data)
{
	DBG("timeout %p connection data %p", timeout, user_data);

	if (dbus_timeout_get_enabled(timeout) == TRUE)
		add_timeout(timeout, user_data);
	else
		remove_timeout(timeout, user_data);
}

static void wakeup_context(void *user_data)
{
	ConnectionData *data = user_data;

	DBG("connection data %p", data);

	g_main_context_wakeup(data->context);
}

static ConnectionData *setup_connection(DBusConnection *connection,
						gboolean unshared,
						GMainContext *context)
{
	ConnectionData *data;

	DBG("connection %p context %p", connection, context);

	data = g_new0(ConnectionData, 1);

	data->context = g_main_context_ref(context);
	data->unshared = unshared;

	DBG("connection data %p", data);

	if (connection == NULL)
		return data;

	data->connection = connection;

	data->queue = g_source_new(&queue_funcs, sizeof(QueueData));

	((QueueData *) data->queue)->connection = connection;

	g_source_attach(data->queue, context);

	return data;
}

static void free_connection(void *memory)
{
	ConnectionData *data = memory;

	DBG("connection data %p", data);

	b_dbus_remove_all_watches(data->connection);

	//b_dbus_unregister_all_objects(data->connection);

        if (data->queue)
            g_source_destroy(data->queue);

        // At the point when free_connection gets called,
        // the last unref already happened and the connection
        // is gone; trying to close it here is too later.
        // ConnectionData holds *no* reference on data->connection,
        // so don't unref either. If it did, the connection would
        // never get freed.
#if 0
	if (data->unshared)
		dbus_connection_close(data->connection);
	dbus_connection_unref(data->connection);
#endif

        if (data->disconnect_destroy)
            data->disconnect_destroy (data->disconnect_data);

	g_main_context_unref(data->context);

	g_free(data);
}

/**
 * b_dbus_setup_connection:
 * @connection: a #DBusConnection
 * @unshared: the connection is private and must be closed explicitly
 * @context: a #GMainContext or #NULL for default context
 *
 * Setup connection with main context
 *
 * Sets the watch and timeout functions of a #DBusConnection
 * to integrate the connection with the GLib main loop.
 * Pass in #NULL for the #GMainContext unless you're
 * doing something specialized.
 */
void b_dbus_setup_connection(DBusConnection *connection,
						gboolean unshared,
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

	data = setup_connection(connection, unshared, context);
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

	dbus_connection_set_wakeup_main_function(connection,
						wakeup_context, data, NULL);
}

/**
 * b_dbus_server_connection:
 * @server: a #DBusServer
 *
 * Setup server with main context
 *
 * Sets the watch and timeout functions of a #DBusServer
 * to integrate the connection with the GLib main loop.
 */
void b_dbus_setup_server(DBusServer *server)
{
	ConnectionData *data;

	if (dbus_server_allocate_data_slot(&server_slot) == FALSE)
		return;

	DBG("server slot %d", server_slot);

	data = dbus_server_get_data(server, server_slot);
	if (data != NULL)
		return;

	data = setup_connection(NULL, TRUE, g_main_context_default());
	if (data == NULL)
		return;

	if (dbus_server_set_data(server, server_slot,
                                 data, free_connection) == FALSE) {
		g_free(data);
		return;
	}

	dbus_server_set_watch_functions(server, add_watch,
                                        remove_watch, watch_toggled, data, NULL);

	dbus_server_set_timeout_functions(server, add_timeout,
                                          remove_timeout, timeout_toggled, data, NULL);
}

/**
 * b_dbus_cleanup_connection:
 * @connection: a #DBusConnection
 *
 * Cleanup the setup of DBusConnection and free the
 * allocated memory.
 */
void b_dbus_cleanup_connection(DBusConnection *connection)
{
	DBG("connection %p slot %d", connection, connection_slot);

	if (connection_slot < 0)
		return;

	dbus_connection_set_data(connection, connection_slot, NULL, NULL);

	dbus_connection_free_data_slot(&connection_slot);

	DBG("connection slot %d", connection_slot);
}

/**
 * b_dbus_setup_bus:
 * @type: a #DBusBusType
 * @name: well known name
 * @unshared: use dbus_bus_get_private() to ensure that we have the connection
 *            for ourself (otherwise assertions and CRITICAL warnings were triggered
 *            inside glib-dbus when the app also used that)
 * @error: a #DBusError
 *
 * Connect to bus and setup connection
 *
 * Returns a connection to the given bus and requests a
 * well known name for it. Sets the watch and timeout
 * functions for it.
 *
 * Returns: newly setup #DBusConnection
 */
DBusConnection *b_dbus_setup_bus(DBusBusType type, const char *name,
							gboolean unshared,
							DBusError *error)
{
	DBusConnection *connection;

	DBG("type %d name %s error %p", type, name, error);

	connection = unshared ? dbus_bus_get_private(type, error) :
		dbus_bus_get(type, error);

	if (error != NULL) {
		if (dbus_error_is_set(error) == TRUE)
			return NULL;
	}

	if (connection == NULL)
		return NULL;

	if (name != NULL) {
		if (dbus_bus_request_name(connection, name,
				DBUS_NAME_FLAG_DO_NOT_QUEUE, error) !=
				DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ) {
			goto failed;
		}

		if (error != NULL) {
			if (dbus_error_is_set(error) == TRUE) {
				goto failed;
			}
		}
	}

	b_dbus_setup_connection(connection, unshared, NULL);

	return connection;

 failed:
	if (unshared)
		dbus_connection_close(connection);
	dbus_connection_unref(connection);
	return NULL;
}

/**
 * b_dbus_setup_address:
 * @address: bus address
 * @error: a #DBusError
 *
 * Connect to bus and setup connection
 *
 * Returns a connection to the bus specified via
 * the given address and sets the watch and timeout
 * functions for it.
 *
 * Returns: newly setup #DBusConnection
 */
DBusConnection *b_dbus_setup_address(const char *address, DBusError *error)
{
	DBusConnection *connection;

	DBG("address %s error %p", address, error);

	connection = dbus_connection_open(address, error);

	if (error != NULL) {
		if (dbus_error_is_set(error) == TRUE)
			return NULL;
	}

	if (connection == NULL)
		return NULL;

	b_dbus_setup_connection(connection, FALSE, NULL);

	return connection;
}

/**
 * b_dbus_request_name:
 * @connection: a #DBusConnection
 * @name: well known name
 * @error: a #DBusError
 *
 * Requests a well known name for connection.
 *
 * Returns: #TRUE on success
 */
gboolean b_dbus_request_name(DBusConnection *connection, const char *name,
							DBusError *error)
{
	DBG("connection %p name %s error %p", connection, name, error);

	if (name == NULL)
		return FALSE;

	if (dbus_bus_request_name(connection, name,
			DBUS_NAME_FLAG_DO_NOT_QUEUE, error) !=
				DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER )
		return FALSE;

	if (error != NULL) {
		if (dbus_error_is_set(error) == TRUE)
			return FALSE;
	}

	return TRUE;
}

static DBusHandlerResult disconnect_filter(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
        ConnectionData *data = user_data;

	if (dbus_message_is_signal(message,
			DBUS_INTERFACE_LOCAL, "Disconnected") == FALSE)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBG("disconnected");

        if (data->disconnect)
            data->disconnect (connection,
                              data->disconnect_data);

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * b_dbus_set_disconnect_function:
 * @connection: a #DBusConnection
 * @function: a #BDBusWatchFunction
 * @user_data: user data to pass to the function
 * @destroy: a #BDBusDestroyFunction
 *
 * Set a callback function that will be called when the
 * D-Bus message bus exits.
 *
 * Returns: #TRUE on success
 */
gboolean b_dbus_set_disconnect_function(DBusConnection *connection,
				BDBusWatchFunction function,
				void *user_data, BDBusDestroyFunction destroy)
{
        ConnectionData *data = dbus_connection_get_data(connection, connection_slot);
        if (!data)
            return FALSE;

        if (data->disconnect_destroy)
            data->disconnect_destroy (data->disconnect_data);
        data->disconnect_destroy = NULL;
        data->disconnect = NULL;
        data->disconnect_data = NULL;

	dbus_connection_set_exit_on_disconnect(connection, FALSE);

	if (dbus_connection_add_filter(connection,
				disconnect_filter, data, NULL) == FALSE)
		return FALSE;

        data = dbus_connection_get_data(connection, connection_slot);

        data->disconnect = function;
        data->disconnect_data = user_data;
        data->disconnect_destroy = destroy;
	return TRUE;
}
