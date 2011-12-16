/*
 * Copyright (C) 2011 Intel Corporation
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

#include "gdbus-cxx-bridge.h"

void intrusive_ptr_add_ref(GDBusConnection *con)  { g_object_ref(con); }
void intrusive_ptr_release(GDBusConnection *con)  { g_object_unref(con); }
void intrusive_ptr_add_ref(GDBusMessage    *msg)  { g_object_ref(msg); }
void intrusive_ptr_release(GDBusMessage    *msg)  { g_object_unref(msg); }

namespace GDBusCXX {

MethodHandler::MethodMap MethodHandler::m_methodMap;
boost::function<void (void)> MethodHandler::m_callback;

DBusConnectionPtr dbus_get_bus_connection(const char *busType,
                                          const char *name,
                                          bool unshared,
                                          DBusErrorCXX *err)
{
    DBusConnectionPtr conn;
    GError* error = NULL;

    if(unshared) {
        char *address = g_dbus_address_get_for_bus_sync(boost::iequals(busType, "SESSION") ?
                                                        G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM,
                                                        NULL, &error);
        if(address == NULL) {
            if (err) {
                err->set(error);
            }
            return NULL;
        }
        // Here we set up a private client connection using the chosen bus' address.
        conn = DBusConnectionPtr(g_dbus_connection_new_for_address_sync(address,
                                                                        (GDBusConnectionFlags)
                                                                        (G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                                         G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                                                                        NULL, NULL, &error),
                                 false);
        g_free(address);

        if(conn == NULL) {
            if (err) {
                err->set(error);
            }
            return NULL;
        }
    } else {
        // This returns a singleton, shared connection object.
        conn = DBusConnectionPtr(g_bus_get_sync(boost::iequals(busType, "SESSION") ?
                                                G_BUS_TYPE_SESSION :
                                                G_BUS_TYPE_SYSTEM,
                                                NULL, &error),
                                 false);
        if(conn == NULL) {
            if (err) {
                err->set(error);
            }
            return NULL;
        }
    }

    if(name) {
        g_bus_own_name_on_connection(conn.get(), name, G_BUS_NAME_OWNER_FLAGS_NONE,
                                     NULL, NULL, NULL, NULL);
        g_dbus_connection_set_exit_on_close(conn.get(), TRUE);
    }

    return conn;
}

}
