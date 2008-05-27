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

#include <stdarg.h>
#include <string.h>

#include <dbus/dbus.h>

#include "gdbus.h"
#include "debug.h"

/** @defgroup object Functions for object and interface handling */

static dbus_int32_t connection_slot = -1;

typedef struct {
	GStaticMutex mutex;
	GSList *objects;
} ConnectionData;

typedef struct {
	char *path;
	GSList *interfaces;
	char *introspect;
} ObjectData;

typedef struct {
	char *name;
	GDBusMethodTable *methods;
	GDBusSignalTable *signals;
	GDBusPropertyTable *properties;
	void *user_data;
	GDBusDestroyFunction destroy;
} InterfaceData;

static InterfaceData *find_interface(GSList *interfaces, const char *name)
{
	GSList *list;

	for (list = interfaces; list; list = list->next) {
		InterfaceData *interface = list->data;

		if (strcmp(name, interface->name) == 0)
			return interface;
	}

	return NULL;
}

static GDBusPropertyTable *find_property(InterfaceData *interface,
					 		const char *name)
{
	GDBusPropertyTable *property;

	if (interface == NULL)
		return NULL;

	for (property = interface->properties; property->name; property++)
		if (strcmp(property->name, name) == 0)
			return property;

	return NULL;
}

static void add_arguments(GString *xml, const char *direction,
						const char *signature)
{
	DBusSignatureIter iter;

	dbus_signature_iter_init(&iter, signature);

	if (dbus_signature_iter_get_current_type(&iter) == DBUS_TYPE_INVALID)
		return;

	do {
		g_string_append_printf(xml, "\t\t\t<arg type=\"%s\"",
				dbus_signature_iter_get_signature(&iter));

		if (direction != NULL)
			g_string_append_printf(xml, " direction=\"%s\"/>\n",
								direction);
		else
			g_string_append(xml, "/>\n");
	} while (dbus_signature_iter_next(&iter) == TRUE);
}

static inline void add_annotation(GString *xml, const char *name)
{
	g_string_append_printf(xml,
		"\t\t\t<annotation name=\"%s\" value=\"true\"/>\n", name);
}

static inline void add_methods(GString *xml, GDBusMethodTable *methods)
{
	GDBusMethodTable *method;

	for (method = methods; method && method->name; method++) {
		g_string_append_printf(xml, "\t\t<method name=\"%s\">\n",
								method->name);

		add_arguments(xml, "in", method->signature);
		add_arguments(xml, "out", method->reply);

		if (method->flags & G_DBUS_METHOD_FLAG_DEPRECATED)
			add_annotation(xml, "org.freedesktop.DBus.Deprecated");

		if (method->flags & G_DBUS_METHOD_FLAG_NOREPLY)
			add_annotation(xml, "org.freedesktop.DBus.Method.NoReply");

		g_string_append(xml, "\t\t</method>\n");
	}
}

static inline void add_signals(GString *xml, GDBusSignalTable *signals)
{
	GDBusSignalTable *signal;

	for (signal = signals; signal && signal->name; signal++) {
		g_string_append_printf(xml, "\t\t<signal name=\"%s\">\n",
								signal->name);

		add_arguments(xml, NULL, signal->signature);

		if (signal->flags & G_DBUS_SIGNAL_FLAG_DEPRECATED)
			add_annotation(xml, "org.freedesktop.DBus.Deprecated");

		g_string_append(xml, "\t\t</signal>\n");
	}
}

static inline void add_properties(GString *xml, GDBusPropertyTable *properties)
{
	GDBusPropertyTable *property;

	for (property = properties; property && property->name; property++) {
		const char *access;

		if (property->type == NULL)
			continue;

		if (property->get == NULL && property->set == NULL)
			continue;

		if (property->get != NULL) {
			if (property->set == NULL)
				access = "read";
			else
				access = "readwrite";
		} else
			access = "write";

		g_string_append_printf(xml,
			"\t\t<property name=\"%s\" type=\"%s\" access=\"%s\">\n",
					property->name, property->type, access);

		if (property->flags & G_DBUS_PROPERTY_FLAG_DEPRECATED)
			add_annotation(xml, "org.freedesktop.DBus.Deprecated");

		g_string_append(xml, "\t\t</property>\n");
	}
}

static char *generate_introspect(DBusConnection *connection,
					const char *path, ObjectData *data)
{
	GString *xml;
	GSList *list;
	char **children;
	int i;

	DBG("connection %p path %s", connection, path);

	xml = g_string_new(DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);

	g_string_append_printf(xml, "<node name=\"%s\">\n", path);

	g_string_append_printf(xml, "\t<interface name=\"%s\">\n",
					DBUS_INTERFACE_INTROSPECTABLE);

	g_string_append(xml, "\t\t<method name=\"Introspect\">\n");
	add_arguments(xml, "out", "s");
	g_string_append(xml, "\t\t</method>\n");

	g_string_append(xml, "\t</interface>\n");

	g_string_append_printf(xml, "\t<interface name=\"%s\">\n",
						DBUS_INTERFACE_PROPERTIES);

	g_string_append(xml, "\t\t<method name=\"Get\">\n");
	add_arguments(xml, "in", "ss");
	add_arguments(xml, "out", "v");
	g_string_append(xml, "\t\t</method>\n");

	g_string_append(xml, "\t\t<method name=\"Set\">\n");
	add_arguments(xml, "in", "ssv");
	g_string_append(xml, "\t\t</method>\n");

	g_string_append(xml, "\t\t<method name=\"GetAll\">\n");
	add_arguments(xml, "in", "s");
	add_arguments(xml, "out", "a{sv}");
	g_string_append(xml, "\t\t</method>\n");

	g_string_append(xml, "\t</interface>\n");

	for (list = data->interfaces; list; list = list->next) {
		InterfaceData *interface = list->data;

		g_string_append_printf(xml, "\t<interface name=\"%s\">\n",
							interface->name);

		add_methods(xml, interface->methods);
		add_signals(xml, interface->signals);
		add_properties(xml, interface->properties);

		g_string_append(xml, "\t</interface>\n");
	}

	if (dbus_connection_list_registered(connection,
						path, &children) == FALSE)
		goto done;

	for (i = 0; children[i]; i++)
		g_string_append_printf(xml, "\t<node name=\"%s\"/>\n",
								children[i]);

	dbus_free_string_array(children);

done:
	g_string_append(xml, "</node>\n");

	return g_string_free(xml, FALSE);
}

static void update_parent(DBusConnection *connection, const char *path)
{
	ObjectData *data;
	char *parent;

	DBG("connection %p path %s", connection, path);

	if (strlen(path) < 2 || path[0] != '/')
		return;

	parent = g_path_get_dirname(path);
	if (parent == NULL)
		return;

	if (dbus_connection_get_object_path_data(connection,
					parent, (void *) &data) == FALSE) {
		update_parent(connection, parent);
		goto done;
	}

	if (data == NULL) {
		update_parent(connection, parent);
		goto done;
	}

	g_free(data->introspect);
	data->introspect = generate_introspect(connection, parent, data);

done:
	g_free(parent);
}

static DBusHandlerResult send_message(DBusConnection *connection,
						DBusMessage *message)
{
	dbus_bool_t result;

	result = dbus_connection_send(connection, message, NULL);

	dbus_message_unref(message);

	if (result == FALSE)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult send_error(DBusConnection *connection,
					DBusMessage *message,
					const char *name, const char *text)
{
	DBusMessage *error;

	error = dbus_message_new_error(message, name, text);
	if (error == NULL)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	return send_message(connection, error);
}

static DBusHandlerResult introspect(DBusConnection *connection,
				DBusMessage *message, ObjectData *data)
{
	DBusMessage *reply;

	DBG("connection %p message %p object data %p",
					connection, message, data);

	if (data->introspect == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (dbus_message_has_signature(message,
				DBUS_TYPE_INVALID_AS_STRING) == FALSE)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_message_append_args(reply,
		DBUS_TYPE_STRING, &data->introspect, DBUS_TYPE_INVALID);

	return send_message(connection, reply);
}

static DBusHandlerResult properties_get(DBusConnection *connection,
				DBusMessage *message, ObjectData *data)
{
	DBusMessage *reply;
	GDBusPropertyTable *property;
	DBusMessageIter iter, value;
	dbus_bool_t result;
	const char *interface, *name;
	InterfaceData *iface;

	DBG("connection %p message %p object data %p",
					connection, message, data);

	if (dbus_message_has_signature(message,
			DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_STRING_AS_STRING
				DBUS_TYPE_INVALID_AS_STRING) == FALSE)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &interface,
				DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);

	DBG("interface %s name %s", interface, name);

	iface = find_interface(data->interfaces, interface);
	property = find_property(iface, name);
	if (property == NULL)
		return send_error(connection, message,
				DBUS_ERROR_BAD_ADDRESS, "Property not found");

	if (property->get == NULL)
		return send_error(connection, message,
					DBUS_ERROR_ACCESS_DENIED,
					"Reading of property not allowed");

	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT,
						property->type, &value);

	result = property->get(connection, &value, iface->user_data);

	dbus_message_iter_close_container(&iter, &value);

	if (result == FALSE) {
		dbus_message_unref(reply);
		return send_error(connection, message, DBUS_ERROR_FAILED,
						"Reading of property failed");
	}

	return send_message(connection, reply);
}

static DBusHandlerResult properties_set(DBusConnection *connection,
				DBusMessage *message, ObjectData *data)
{
	DBusMessage *reply;
	GDBusPropertyTable *property;
	DBusMessageIter iter, value;
	const char *interface, *name;
	InterfaceData *iface;

	DBG("connection %p message %p object data %p",
					connection, message, data);

	if (dbus_message_has_signature(message, DBUS_TYPE_STRING_AS_STRING
			DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
					DBUS_TYPE_INVALID_AS_STRING) == FALSE)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	dbus_message_iter_init(message, &iter);

	dbus_message_iter_get_basic(&iter, &interface);
	dbus_message_iter_next(&iter);

	dbus_message_iter_get_basic(&iter, &name);
	dbus_message_iter_next(&iter);

	dbus_message_iter_recurse(&iter, &value);

	DBG("interface %s name %s", interface, name);

	iface = find_interface(data->interfaces, interface);
	property = find_property(iface, name);
	if (property == NULL)
		return send_error(connection, message,
				DBUS_ERROR_BAD_ADDRESS, "Property not found");

	if (property->set == NULL)
		return send_error(connection, message,
					DBUS_ERROR_ACCESS_DENIED,
					"Writing to property not allowed");

	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_message_append_args(reply, DBUS_TYPE_INVALID);

	if (property->set(connection, &value, iface->user_data) == FALSE) {
		dbus_message_unref(reply);
		return send_error(connection, message, DBUS_ERROR_FAILED,
						"Writing to property failed");
	}

	return send_message(connection, reply);
}

static inline void append_message(DBusMessageIter *value, DBusMessage *message)
{
	DBusMessageIter iter, temp;

	dbus_message_iter_init(message, &temp);
	dbus_message_iter_recurse(&temp, &iter);

	do {
		int type = dbus_message_iter_get_arg_type(&iter);
		void *data;

		dbus_message_iter_get_basic(&iter, &data);
		dbus_message_iter_append_basic(value, type, &data);
	} while (dbus_message_iter_next(&iter) == TRUE);
}

static void do_getall(DBusConnection *connection, DBusMessageIter *iter,
				InterfaceData *interface, ObjectData *data)
{
	GDBusPropertyTable *property;

	if (interface == NULL)
		return;

	for (property = interface->properties; property->name; property++) {
		DBusMessage *message;
		DBusMessageIter entry, value;
		dbus_bool_t result;

		if (property->get == NULL)
			continue;

		message = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_RETURN);
		if (message == NULL)
			continue;

		dbus_message_iter_init_append(message, &entry);

		dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
						property->type, &value);

		result = property->get(connection, &value, interface->user_data);

		dbus_message_iter_close_container(&entry, &value);

		if (result == TRUE) {
			dbus_message_iter_open_container(iter,
					DBUS_TYPE_DICT_ENTRY, NULL, &entry);

			dbus_message_iter_append_basic(&entry,
					DBUS_TYPE_STRING, &property->name);

			dbus_message_iter_open_container(&entry,
						DBUS_TYPE_VARIANT,
						property->type, &value);

			append_message(&value, message);

			dbus_message_iter_close_container(&entry, &value);

			dbus_message_iter_close_container(iter, &entry);
		}

		dbus_message_unref(message);
	}
}

static DBusHandlerResult properties_getall(DBusConnection *connection,
				DBusMessage *message, ObjectData *data)
{
	DBusMessage *reply;
	DBusMessageIter iter, dict;
	const char *interface;

	DBG("connection %p message %p object data %p",
					connection, message, data);

	if (dbus_message_has_signature(message, DBUS_TYPE_STRING_AS_STRING
					DBUS_TYPE_INVALID_AS_STRING) == FALSE)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &interface,
							DBUS_TYPE_INVALID);

	DBG("interface %s", interface);

	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
			DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
			DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
			DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	do_getall(connection, &dict,
			find_interface(data->interfaces, interface), data);

	dbus_message_iter_close_container(&iter, &dict);

	return send_message(connection, reply);
}

static DBusHandlerResult handle_message(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	ObjectData *data = user_data;
	InterfaceData *interface;
	GDBusMethodTable *method;

	DBG("object data %p", data);

	if (dbus_message_is_method_call(message,
			DBUS_INTERFACE_INTROSPECTABLE, "Introspect") == TRUE)
		return introspect(connection, message, data);

	if (dbus_message_is_method_call(message,
				DBUS_INTERFACE_PROPERTIES, "Get") == TRUE)
		return properties_get(connection, message, data);

	if (dbus_message_is_method_call(message,
				DBUS_INTERFACE_PROPERTIES, "Set") == TRUE)
		return properties_set(connection, message, data);

	if (dbus_message_is_method_call(message,
				DBUS_INTERFACE_PROPERTIES, "GetAll") == TRUE)
		return properties_getall(connection, message, data);

	interface = find_interface(data->interfaces,
					dbus_message_get_interface(message));
	if (interface == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBG("interface data %p name %s", interface, interface->name);

	for (method = interface->methods;
				method->name && method->function; method++) {
		DBusMessage *reply;

		if (dbus_message_is_method_call(message,
				interface->name, method->name) == FALSE)
			continue;

		if (dbus_message_has_signature(message,
					method->signature) == FALSE)
			continue;

		reply = method->function(connection,
						message, interface->user_data);

		if (method->flags & G_DBUS_METHOD_FLAG_NOREPLY) {
			if (reply != NULL)
				dbus_message_unref(reply);
			return DBUS_HANDLER_RESULT_HANDLED;
		}

		if (method->flags & G_DBUS_METHOD_FLAG_ASYNC) {
			if (reply == NULL)
				return DBUS_HANDLER_RESULT_HANDLED;
		}

		if (reply == NULL)
			return DBUS_HANDLER_RESULT_NEED_MEMORY;

		return send_message(connection, reply);
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void handle_unregister(DBusConnection *connection, void *user_data)
{
	ObjectData *data = user_data;
	GSList *list;

	DBG("object data %p path %s", data, data->path);

	g_free(data->path);

	for (list = data->interfaces; list; list = list->next) {
		InterfaceData *interface = list->data;

		DBG("interface data %p name %s", interface, interface->name);

		if (interface->destroy)
			interface->destroy(interface->user_data);

		g_free(interface->name);
		g_free(interface);
	}

	g_slist_free(data->interfaces);

	g_free(data->introspect);
	g_free(data);
}

static DBusObjectPathVTable object_table = {
	.unregister_function = handle_unregister,
	.message_function    = handle_message,
};

/**
 * Register path in object hierarchy
 * @param connection the connection
 * @param path object path
 * @return TRUE on success
 *
 * Registers a path in the object hierarchy.
 */
gboolean g_dbus_register_object(DBusConnection *connection, const char *path)
{
	ConnectionData *data;
	ObjectData *object;

	DBG("connection %p path %s", connection, path);

	if (dbus_connection_allocate_data_slot(&connection_slot) == FALSE)
		return FALSE;

	DBG("connection slot %d", connection_slot);

	data = dbus_connection_get_data(connection, connection_slot);
	if (data == NULL) {
		data = g_try_new0(ConnectionData, 1);
		if (data == NULL) {
			dbus_connection_free_data_slot(&connection_slot);
			return 0;
		}

		g_static_mutex_init(&data->mutex);

		if (dbus_connection_set_data(connection, connection_slot,
							data, NULL) == FALSE) {
			dbus_connection_free_data_slot(&connection_slot);
			g_free(data);
			return 0;
		}
	}

	DBG("connection data %p", data);

	object = g_new0(ObjectData, 1);

	object->path = g_strdup(path);
	object->interfaces = NULL;
	object->introspect = generate_introspect(connection, path, object);

	if (dbus_connection_register_object_path(connection, path,
					&object_table, object) == FALSE) {
		g_free(object->introspect);
		g_free(object);
		return FALSE;
	}

	DBG("object data %p", object);

	g_static_mutex_lock(&data->mutex);

	data->objects = g_slist_append(data->objects, object);

	g_static_mutex_unlock(&data->mutex);

	update_parent(connection, path);

	return TRUE;
}

/**
 * Unregister object path
 * @param connection the connection
 * @param path object path
 * @return TRUE on success
 *
 * Unregister the given path in the object hierarchy and
 * free the assigned data structures.
 */
gboolean g_dbus_unregister_object(DBusConnection *connection, const char *path)
{
	ConnectionData *data;
	ObjectData *object;

	DBG("connection %p path %s", connection, path);

	data = dbus_connection_get_data(connection, connection_slot);
	if (data == NULL)
		return FALSE;

	if (dbus_connection_get_object_path_data(connection,
					path, (void *) &object) == FALSE)
		return FALSE;

	if (object == NULL)
		return FALSE;

	g_static_mutex_lock(&data->mutex);

	data->objects = g_slist_remove(data->objects, object);

	g_static_mutex_unlock(&data->mutex);

	dbus_connection_free_data_slot(&connection_slot);

	DBG("connection slot %d", connection_slot);

	if (connection_slot < 0) {
		g_static_mutex_free(&data->mutex);
		g_free(data);
	}

	if (dbus_connection_unregister_object_path(connection, path) == FALSE)
		return FALSE;

	update_parent(connection, path);

	return TRUE;
}

/**
 * Unregister object hierarchy
 * @param connection the connection
 * @param path object path
 * @return TRUE on sccess
 *
 * Unregister the given path and all subpaths in the
 * object hierarchy and free all assigned data strcutures.
 */
gboolean g_dbus_unregister_object_hierarchy(DBusConnection *connection,
							const char *path)
{
	ConnectionData *data;
	ObjectData *object;
	GSList *list;
	size_t pathlen;
	dbus_bool_t result;

	DBG("connection %p path %s", connection, path);

	data = dbus_connection_get_data(connection, connection_slot);
	if (data == NULL)
		return FALSE;

	if (dbus_connection_get_object_path_data(connection,
					path, (void *) &object) == FALSE)
		return FALSE;

	if (object == NULL)
		return FALSE;

	pathlen = strlen(path);

	g_static_mutex_lock(&data->mutex);

	for (list = data->objects; list; list = list->next) {
		ObjectData *object = list->data;

		if (strlen(object->path) <= pathlen)
			continue;

		if (strncmp(object->path, path, pathlen) != 0)
			continue;

		DBG("object data %p path %s", object, object->path);

		data->objects = g_slist_remove(data->objects, object);

		dbus_connection_unregister_object_path(connection,
							object->path);

		dbus_connection_free_data_slot(&connection_slot);

		DBG("connection slot %d", connection_slot);
	}

	g_static_mutex_unlock(&data->mutex);

	result = g_dbus_unregister_object(connection, path);
	if (result == FALSE)
		return FALSE;

	update_parent(connection, path);

	return TRUE;
}

/**
 * Unregister all paths
 * @param connection the connection
 *
 * Unregister the all paths in the object hierarchy.
 */
void g_dbus_unregister_all_objects(DBusConnection *connection)
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

	g_static_mutex_lock(&data->mutex);

	for (list = data->objects; list; list = list->next) {
		ObjectData *object = list->data;

		DBG("object data %p path %s", object, object->path);

		dbus_connection_unregister_object_path(connection,
							object->path);

		dbus_connection_free_data_slot(&connection_slot);

		DBG("connection slot %d", connection_slot);
	}

	g_slist_free(data->objects);

	g_static_mutex_unlock(&data->mutex);

	g_free(data);
}

/**
 * Register interface
 * @param connection the connection
 * @param path object path
 * @param name interface name
 * @param methods method table
 * @param signals signal table
 * @param properties property table
 * @param user_data user data to assign to interface
 * @param destroy function called to destroy user_data
 * @return TRUE on success
 *
 * Registers an interface for the given path in the
 * object hierarchy with the given methods, signals
 * and/or properties.
 */
gboolean g_dbus_register_interface(DBusConnection *connection,
					const char *path, const char *name,
					GDBusMethodTable *methods,
					GDBusSignalTable *signals,
					GDBusPropertyTable *properties,
					void *user_data,
					GDBusDestroyFunction destroy)
{
	ObjectData *object;
	InterfaceData *interface;

	DBG("connection %p path %s name %s", connection, path, name);

	if (dbus_connection_get_object_path_data(connection,
					path, (void *) &object) == FALSE)
		return FALSE;

	if (object == NULL)
		return FALSE;

	if (find_interface(object->interfaces, name) != NULL)
		return FALSE;

	interface = g_new0(InterfaceData, 1);

	interface->name = g_strdup(name);
	interface->methods = methods;
	interface->signals = signals;
	interface->properties = properties;
	interface->user_data = user_data;
	interface->destroy = destroy;

	object->interfaces = g_slist_append(object->interfaces, interface);

	g_free(object->introspect);
	object->introspect = generate_introspect(connection, path, object);

	return TRUE;
}

/**
 * Unregister interface
 * @param connection the connection
 * @param path object path
 * @param name interface name
 * @return TRUE on success
 *
 * Unregister the given interface for the given path
 * in the object hierarchy.
 */
gboolean g_dbus_unregister_interface(DBusConnection *connection,
					const char *path, const char *name)
{
	ObjectData *object;
	InterfaceData *interface;

	DBG("connection %p path %s name %s", connection, path, name);

	if (dbus_connection_get_object_path_data(connection,
					path, (void *) &object) == FALSE)
		return FALSE;

	if (object == NULL)
		return FALSE;

	interface = find_interface(object->interfaces, name);
	if (interface == NULL)
		return FALSE;

	object->interfaces = g_slist_remove(object->interfaces, interface);

	g_free(interface->name);
	g_free(interface);

	g_free(object->introspect);
	object->introspect = generate_introspect(connection, path, object);

	return TRUE;
}

/**
 * Create error reply
 * @param message the originating message
 * @param name the error name
 * @param format the error description
 * @param args argument list
 * @return reply message on success
 *
 * Create error reply for the given message.
 */
DBusMessage *g_dbus_create_error_valist(DBusMessage *message, const char *name,
						const char *format, va_list args)
{
	DBG("message %p name %s", message, name);

	return dbus_message_new_error(message, name, NULL);
}

/**
 * Create error reply
 * @param message the originating message
 * @param name the error name
 * @param format the error description
 * @return reply message on success
 *
 * Create error reply for the given message.
 */
DBusMessage *g_dbus_create_error(DBusMessage *message, const char *name,
						const char *format, ...)
{
	va_list args;
	DBusMessage *reply;

	DBG("message %p name %s", message, name);

	va_start(args, format);

	reply = g_dbus_create_error_valist(message, name, format, args);

	va_end(args);

	return reply;
}

/**
 * Create reply message
 * @param message the originating message
 * @param type first argument type
 * @param args argument list
 * @return reply message on success
 *
 * Create reply for the given message.
 */
DBusMessage *g_dbus_create_reply_valist(DBusMessage *message,
						int type, va_list args)
{
	DBusMessage *reply;

	DBG("message %p", message);

	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		return NULL;

	if (dbus_message_append_args_valist(reply, type, args) == FALSE) {
		dbus_message_unref(reply);
		return NULL;
	}

	return reply;
}

/**
 * Create reply message
 * @param message the originating message
 * @param type first argument type
 * @return reply message on success
 *
 * Create reply for the given message.
 */
DBusMessage *g_dbus_create_reply(DBusMessage *message, int type, ...)
{
	va_list args;
	DBusMessage *reply;

	DBG("message %p", message);

	va_start(args, type);

	reply = g_dbus_create_reply_valist(message, type, args);

	va_end(args);

	return reply;
}

/**
 * Send message
 * @param connection the connection
 * @param message the message to send
 * @return TRUE on success
 *
 * Send message via the given D-Bus connection.
 *
 * The reference count for the message will be decremented by this
 * function.
 */
gboolean g_dbus_send_message(DBusConnection *connection, DBusMessage *message)
{
	dbus_bool_t result;

	DBG("connection %p message %p", connection, message);

	result = dbus_connection_send(connection, message, NULL);

	dbus_message_unref(message);

	return result;
}

/**
 * Send error reply
 * @param connection the connection
 * @param message the originating message
 * @param name the error name
 * @param format the error description
 * @return TRUE on success
 *
 * Send error reply for the given message and via the given D-Bus
 * connection.
 */
gboolean g_dbus_send_error(DBusConnection *connection, DBusMessage *message,
				const char *name, const char *format, ...)
{
	va_list args;
	DBusMessage *error;

	DBG("connection %p message %p", connection, message);

	va_start(args, format);

	error = g_dbus_create_error(message, name, format, args);

	va_end(args);

	if (error == NULL)
		return FALSE;

	return g_dbus_send_message(connection, error);
}

/**
 * Send reply message
 * @param connection the connection
 * @param message the originating message
 * @param type first argument type
 * @param args argument list
 * @return TRUE on success
 *
 * Send reply for the given message and via the given D-Bus
 * connection.
 */
gboolean g_dbus_send_reply_valist(DBusConnection *connection,
				DBusMessage *message, int type, va_list args)
{
	DBusMessage *reply;

	DBG("connection %p message %p", connection, message);

	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		return FALSE;

	if (dbus_message_append_args_valist(reply, type, args) == FALSE) {
		dbus_message_unref(reply);
		return FALSE;
	}

	return g_dbus_send_message(connection, reply);
}

/**
 * Send reply message
 * @param connection the connection
 * @param message the originating message
 * @param type first argument type
 * @return TRUE on success
 *
 * Send reply for the given message and via the given D-Bus
 * connection.
 */
gboolean g_dbus_send_reply(DBusConnection *connection,
				DBusMessage *message, int type, ...)
{
	va_list args;
	gboolean result;

	DBG("connection %p message %p", connection, message);

	va_start(args, type);

	result = g_dbus_send_reply_valist(connection, message, type, args);

	va_end(args);

	return result;
}

static GDBusSignalTable *find_signal(GSList *interfaces,
				const char *interface, const char *name)
{
	InterfaceData *data;
	GDBusSignalTable *signal;

	data = find_interface(interfaces, interface);
	if (data == NULL)
		return NULL;

	for (signal = data->signals; signal && signal->name; signal++) {
		if (strcmp(signal->name, name) == 0)
			break;
	}

	return signal;
}

/**
 * Emit signal
 * @param connection the connection
 * @param path object path
 * @param interface interface name
 * @param name signal name
 * @param type first argument type
 * @param args argument list
 * @return TRUE on success
 *
 * Emit a signal for the given path and interface with
 * the given signal name.
 *
 * The signal signature will be check against the registered
 * signal table.
 */
gboolean g_dbus_emit_signal_valist(DBusConnection *connection,
				const char *path, const char *interface,
				const char *name, int type, va_list args)
{
	ObjectData *object;
	GDBusSignalTable *signal;
	DBusMessage *message;
	const char *signature;
	gboolean result = FALSE;

	DBG("connection %p path %s name %s.%s",
			connection, path, interface, name);

	if (dbus_connection_get_object_path_data(connection,
					path, (void *) &object) == FALSE)
		return FALSE;

	if (object == NULL)
		return FALSE;

	signal = find_signal(object->interfaces, interface, name);
	if (signal == NULL)
		return FALSE;

	message = dbus_message_new_signal(path, interface, name);
	if (message == NULL)
		return FALSE;

	if (dbus_message_append_args_valist(message, type, args) == FALSE)
		goto done;

	signature = dbus_message_get_signature(message);
	if (strcmp(signal->signature, signature) != 0)
		goto done;

	DBG("connection %p signature \"%s\"", connection, signature);

	if (dbus_connection_send(connection, message, NULL) == FALSE)
		goto done;

	result = TRUE;

done:
	dbus_message_unref(message);

	return result;
}

/**
 * Emit signal
 * @param connection the connection
 * @param path object path
 * @param interface interface name
 * @param name signal name
 * @param type first argument type
 * @return TRUE on success
 *
 * Emit a signal for the given path and interface with
 * the given signal name.
 *
 * The signal signature will be check against the registered
 * signal table.
 */
gboolean g_dbus_emit_signal(DBusConnection *connection,
				const char *path, const char *interface,
				const char *name, int type, ...)
{
	va_list args;
	gboolean result;

	DBG("connection %p path %s name %s.%s",
			connection, path, interface, name);

	va_start(args, type);

	result = g_dbus_emit_signal_valist(connection, path, interface,
							name, type, args);

	va_end(args);

	return result;
}
