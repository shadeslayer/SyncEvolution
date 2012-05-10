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
#include <boost/algorithm/string/predicate.hpp>

#include <stdio.h>

void intrusive_ptr_add_ref(DBusConnection  *con)  { dbus_connection_ref(con); }
void intrusive_ptr_release(DBusConnection  *con)  { dbus_connection_unref(con); }
void intrusive_ptr_add_ref(DBusMessage     *msg)  { dbus_message_ref(msg); }
void intrusive_ptr_release(DBusMessage     *msg)  { dbus_message_unref(msg); }
void intrusive_ptr_add_ref(DBusPendingCall *call) { dbus_pending_call_ref (call); }
void intrusive_ptr_release(DBusPendingCall *call) { dbus_pending_call_unref (call); }

namespace GDBusCXX {

DBusConnectionPtr dbus_get_bus_connection(const char *busType,
                                          const char *name,
                                          bool unshared,
                                          DBusErrorCXX *err)
{
    return DBusConnectionPtr(b_dbus_setup_bus(boost::iequals(busType, "SYSTEM") ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION,
                                              name, unshared, err),
                             false);
}

static void ConnectionLost(DBusConnection *connection,
                           void *user_data)
{
    DBusConnectionPtr::Disconnect_t *cb = static_cast<DBusConnectionPtr::Disconnect_t *>(user_data);
    (*cb)();
}

static void DestroyDisconnect(void *user_data)
{
    DBusConnectionPtr::Disconnect_t *cb = static_cast<DBusConnectionPtr::Disconnect_t *>(user_data);
    delete cb;
}

void DBusConnectionPtr::setDisconnect(const Disconnect_t &func)
{
    b_dbus_set_disconnect_function(get(),
                                   ConnectionLost,
                                   new Disconnect_t(func),
                                   DestroyDisconnect);
}

DBusConnectionPtr dbus_get_bus_connection(const std::string &address,
                                          DBusErrorCXX *err,
                                          bool /*delayed*/ /*= false*/)
{
    DBusConnectionPtr conn(dbus_connection_open_private(address.c_str(), err), false);
    if (conn) {
        b_dbus_setup_connection(conn.get(), TRUE, NULL);
        dbus_connection_set_exit_on_disconnect(conn.get(), FALSE);
    }
    return conn;
}

void dbus_bus_connection_undelay(const DBusConnectionPtr &/*ptr*/)
{
    // no op
}

boost::shared_ptr<DBusServerCXX> DBusServerCXX::listen(const std::string &address, DBusErrorCXX *err)
{
    DBusServer *server = NULL;
    const char *realAddr = address.c_str();
    char buffer[80];

    if (address.empty()) {
        realAddr = buffer;
        buffer[0] = 0;
        for (int counter = 1; counter < 100 && !server; counter++) {
            if (*err) {
                g_debug("dbus_server_listen(%s) failed, trying next candidate: %s",
                        buffer, err->message);
                dbus_error_init(err);
            }
            sprintf(buffer, "unix:abstract=gdbuscxx-%d", counter);
            server = dbus_server_listen(realAddr, err);
        }
    } else {
        server = dbus_server_listen(realAddr, err);
    }

    if (!server) {
        return boost::shared_ptr<DBusServerCXX>();
    }

    b_dbus_setup_server(server);
    boost::shared_ptr<DBusServerCXX> res(new DBusServerCXX(server, realAddr));
    dbus_server_set_new_connection_function(server, newConnection, res.get(), NULL);
    return res;
}

void DBusServerCXX::newConnection(DBusServer *server, DBusConnection *newConn, void *data) throw()
{
    DBusServerCXX *me = static_cast<DBusServerCXX *>(data);
    if (me->m_newConnection) {
        try {
            b_dbus_setup_connection(newConn, FALSE, NULL);
            dbus_connection_set_exit_on_disconnect(newConn, FALSE);
            DBusConnectionPtr conn(newConn);
            me->m_newConnection(*me, conn);
        } catch (...) {
            g_error("handling new D-Bus connection failed with C++ exception");
        }
    }
}

DBusServerCXX::DBusServerCXX(DBusServer *server, const std::string &address) :
    m_server(server),
    m_address(address)
{
}

DBusServerCXX::~DBusServerCXX()
{
    if (m_server) {
        dbus_server_disconnect(m_server.get());
    }
}

bool CheckError(const DBusMessagePtr &reply,
                std::string &buffer)
{
    const char* errname = dbus_message_get_error_name (reply.get());
    if (errname) {
        buffer = errname;
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply.get(), &iter)) {
            if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
                buffer += ": ";
                const char *str;
                dbus_message_iter_get_basic(&iter, &str);
                buffer += str;
            }
        }
        return true;
    } else {
        return false;
    }
}

}
