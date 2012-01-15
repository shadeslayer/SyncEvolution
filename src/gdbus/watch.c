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

#include <string.h>

#include "gdbus.h"
#include "debug.h"

static dbus_int32_t connection_slot = -1;

typedef struct {
	GSList *watches;
	GSList *handlers;
	guint next_id;
} ConnectionData;

typedef struct {
	guint id;
	char *name;
	void *user_data;
	char *match;
	BDBusWatchFunction connect;
	BDBusWatchFunction disconn;
	BDBusDestroyFunction destroy;
} WatchData;

typedef struct {
	guint id;
	void *user_data;
	BDBusWatchFunction function;
	BDBusDestroyFunction destroy;
} DisconnectData;

typedef struct {
	guint id;
	void *user_data;
	char *match;
	BDBusSignalFunction function;
	BDBusDestroyFunction destroy;
} SignalData;

static DBusHandlerResult signal_function(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	ConnectionData *data = user_data;
	GSList *list;

	DBG("connection %p message %p", connection, message);

	for (list = data->handlers; list; list = list->next) {
		SignalData *signal = list->data;
		gboolean result;

		if (signal->function == NULL)
			continue;

		result = signal->function(connection, message,
							signal->user_data);
		if (result == TRUE)
			continue;

		if (signal->destroy != NULL)
			signal->destroy(signal->user_data);
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult owner_function(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	ConnectionData *data = user_data;
	GSList *list;
	const char *name, *old, *new;

	DBG("connection %p message %p", connection, message);

	if (dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &name,
						DBUS_TYPE_STRING, &old,
						DBUS_TYPE_STRING, &new,
						DBUS_TYPE_INVALID) == FALSE)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBG("name %s \"%s\" => \"%s\"", name, old, new);

	for (list = data->watches; list; list = list->next) {
		WatchData *watch = list->data;

		if (strcmp(name, watch->name) != 0)
			continue;

		if (watch->connect != NULL && *old == '\0' && *new != '\0')
			watch->connect(connection, watch->user_data);

		if (watch->disconn != NULL && *old != '\0' && *new == '\0')
			watch->disconn(connection, watch->user_data);
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult filter_function(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	if (dbus_message_is_signal(message, DBUS_INTERFACE_DBUS,
						"NameOwnerChanged") == TRUE)
		return owner_function(connection, message, user_data);

	if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_SIGNAL)
		return signal_function(connection, message, user_data);

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static ConnectionData *get_connection_data(DBusConnection *connection)
{
	ConnectionData *data;
	/* dbus_bool_t result; */

	DBG("connection %p", connection);

	if (dbus_connection_allocate_data_slot(&connection_slot) == FALSE)
		return NULL;

	DBG("connection slot %d", connection_slot);

	data = dbus_connection_get_data(connection, connection_slot);
	if (data == NULL) {
		data = g_try_new0(ConnectionData, 1);
		if (data == NULL) {
			dbus_connection_free_data_slot(&connection_slot);
			return NULL;
		}

		data->next_id = 1;

		if (dbus_connection_set_data(connection, connection_slot,
							data, NULL) == FALSE) {
			dbus_connection_free_data_slot(&connection_slot);
			g_free(data);
			return NULL;
		}

		/* result = */ dbus_connection_add_filter(connection,
						filter_function, data, NULL);
	}

	return data;
}

static void put_connection_data(DBusConnection *connection)
{
	dbus_connection_free_data_slot(&connection_slot);
}

/**
 * b_dbus_add_service_watch:
 * @connection: the connection
 * @name: unique or well known name
 * @connect: function called on name connect
 * @disconnect: function called on name disconnect
 * @user_data: user data to pass to the function
 * @destroy: function called to destroy user_data
 *
 * Add new watch to listen for connects and/or disconnects
 * of a client for the given connection.
 *
 * Returns: identifier of the watch
 */
guint b_dbus_add_service_watch(DBusConnection *connection, const char *name,
				BDBusWatchFunction connect,
				BDBusWatchFunction disconnect,
				void *user_data, BDBusDestroyFunction destroy)
{
	ConnectionData *data;
	WatchData *watch;
	DBusError error;

	DBG("connection %p name %s", connection, name);

	data = get_connection_data(connection);
	if (data == NULL)
		return 0;

	DBG("connection data %p", data);

	watch = g_try_new0(WatchData, 1);
	if (watch == NULL)
		goto error;

	watch->name = g_strdup(name);
	if (watch->name == NULL)
		goto error;

	watch->user_data = user_data;

	watch->connect = connect;
	watch->disconn = disconnect;
	watch->destroy = destroy;
	watch->match = g_strdup_printf("interface=%s,member=NameOwnerChanged,arg0=%s",
						DBUS_INTERFACE_DBUS, name);
	if (watch->match == NULL)
		goto error;

	dbus_error_init(&error);

	dbus_bus_add_match(connection, watch->match, &error);

	if (dbus_error_is_set(&error) == TRUE) {
		dbus_error_free(&error);
		goto error;
	}

	watch->id = data->next_id++;

	data->watches = g_slist_append(data->watches, watch);

	DBG("tag %d", watch->id);

	return watch->id;

error:
	if (watch != NULL) {
		g_free(watch->name);
		g_free(watch->match);
	}
	g_free(watch);

	put_connection_data(connection);

	return 0;
}

/**
 * b_dbus_remove_watch:
 * @connection: the connection
 * @tag: watch identifier
 *
 * Removes the watch for the given identifier.
 *
 * Returns: #TRUE on success
 */
gboolean b_dbus_remove_watch(DBusConnection *connection, guint tag)
{
	ConnectionData *data;
	GSList *list;

	DBG("connection %p tag %d", connection, tag);

	if (connection_slot < 0)
		return FALSE;

	data = dbus_connection_get_data(connection, connection_slot);
	if (data == NULL)
		return FALSE;

	for (list = data->watches; list; list = list->next) {
		WatchData *watch = list->data;

		if (watch->id == tag) {
			data->watches = g_slist_remove(data->watches, watch);
			if (watch->destroy != NULL)
				watch->destroy(watch->user_data);
			dbus_bus_remove_match(connection, watch->match, NULL);
			g_free(watch->name);
                        g_free(watch->match);
			g_free(watch);
			goto done;
		}
	}

	for (list = data->handlers; list; list = list->next) {
		SignalData *signal = list->data;

		if (signal->id == tag) {
			data->handlers = g_slist_remove(data->handlers, signal);
			if (signal->destroy != NULL)
				signal->destroy(signal->user_data);
			dbus_bus_remove_match(connection, signal->match, NULL);
			g_free(signal->match);
			g_free(signal);
			goto done;
		}
	}

	return FALSE;

done:
        if (!data->watches && !data->handlers) {
		dbus_connection_remove_filter(connection,
						filter_function, data);
		dbus_connection_set_data(connection, connection_slot, NULL, NULL);
                g_free(data);
	}

	dbus_connection_free_data_slot(&connection_slot);

	DBG("connection slot %d", connection_slot);

	return TRUE;
}

/**
 * b_dbus_remove_all_watches:
 * @connection: the connection
 *
 * Removes all registered watches.
 */
void b_dbus_remove_all_watches(DBusConnection *connection)
{
	ConnectionData *data;
	GSList *list;

	DBG("connection %p slot %d", connection, connection_slot);

	if (connection_slot < 0)
		return;

	data = dbus_connection_get_data(connection, connection_slot);
	if (data == NULL)
		return;

	DBG("connection data %p", data);

	for (list = data->watches; list; list = list->next) {
		WatchData *watch = list->data;

		DBG("watch data %p tag %d", watch, watch->id);

		if (watch->destroy != NULL)
			watch->destroy(watch->user_data);
		dbus_bus_remove_match(connection, watch->match, NULL);
		g_free(watch->match);
		g_free(watch->name);
		g_free(watch);

		dbus_connection_free_data_slot(&connection_slot);

		DBG("connection slot %d", connection_slot);
	}

	g_slist_free(data->watches);

	for (list = data->handlers; list; list = list->next) {
		SignalData *signal = list->data;

		DBG("signal data %p tag %d", signal, signal->id);

		if (signal->destroy != NULL)
			signal->destroy(signal->user_data);
		dbus_bus_remove_match(connection, signal->match, NULL);
		g_free(signal->match);
		g_free(signal);

		dbus_connection_free_data_slot(&connection_slot);

		DBG("connection slot %d", connection_slot);
	}

	g_slist_free(data->handlers);

	dbus_connection_remove_filter(connection, filter_function, data);

	g_free(data);
}

static void disconnect_function(DBusConnection *connection, void *user_data)
{
	DisconnectData *data = user_data;

	// The callback function might remove the watch,
	// which invalidates the data pointer. Remember
	// the ID.
	guint id = data->id;

	data->function(connection, data->user_data);

	b_dbus_remove_watch(connection, id);
}

static void disconnect_release(void *user_data)
{
	g_free(user_data);
}

/**
 * b_dbus_add_disconnect_watch:
 * @connection: the connection
 * @name: unique or well known name
 * @function: function called on name disconnect
 * @user_data: user data to pass to the function
 * @destroy: function called to destroy user_data
 *
 * Add new watch to listen for disconnect of a client
 * for the given connection.
 *
 * After the callback has been called, this watch will be
 * automatically removed.
 *
 * Returns: identifier of the watch
 */
guint b_dbus_add_disconnect_watch(DBusConnection *connection,
				const char *name, BDBusWatchFunction function,
				void *user_data, BDBusDestroyFunction destroy)
{
	DisconnectData *data;

	data = g_try_new0(DisconnectData, 1);
	if (data == NULL)
		return 0;

	data->user_data = user_data;
	data->function = function;
	data->destroy = destroy;

	data->id = b_dbus_add_service_watch(connection, name, NULL,
			disconnect_function, data, disconnect_release);

	if (data->id == 0) {
		g_free(data);
		return 0;
	}

	return data->id;
}

/**
 * b_dbus_add_signal_watch:
 * @connection: the connection
 * @rule: matching rule for this signal
 * @function: function called when signal arrives
 * @user_data: user data to pass to the function
 * @destroy: function called to destroy user_data
 * @is_bus_conn: whether the connection is with the bus
 *
 * Add new watch to listen for specific signals of
 * a client for the given connection.
 *
 * If the callback returns #FALSE this watch will be
 * automatically removed.
 *
 * Returns: identifier of the watch
 */
guint b_dbus_add_signal_watch(DBusConnection *connection, const char *rule,
                              BDBusSignalFunction function, void *user_data,
                              BDBusDestroyFunction destroy, gboolean is_bus_conn)
{
	ConnectionData *data;
	SignalData *signal;
	DBusError error;

	DBG("connection %p rule %s", connection, rule);

	data = get_connection_data(connection);
	if (data == NULL)
		return 0;

	DBG("connection data %p", data);

	signal = g_try_new0(SignalData, 1);
	if (signal == NULL)
		goto error;

	signal->match = g_strdup(rule);
	if (!signal->match)
		goto error;

	signal->user_data = user_data;

	signal->function = function;
	signal->destroy = destroy;

        if (is_bus_conn) {
            dbus_error_init(&error);

            dbus_bus_add_match(connection, rule, &error);

            if (dbus_error_is_set(&error) == TRUE) {
		dbus_error_free(&error);
		goto error;
            }
        }

	signal->id = data->next_id++;

	data->handlers = g_slist_append(data->handlers, signal);

	DBG("tag %d", signal->id);

	return signal->id;

error:
        if (signal)
		g_free(signal->match);
	g_free(signal);

	put_connection_data(connection);

	return 0;
}
