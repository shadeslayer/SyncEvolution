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

namespace boost
{
    void intrusive_ptr_add_ref(DBusConnection  *con)  { dbus_connection_ref(con); }
    void intrusive_ptr_release(DBusConnection  *con)  { dbus_connection_unref(con); }
    void intrusive_ptr_add_ref(DBusMessage     *msg)  { dbus_message_ref(msg); }
    void intrusive_ptr_release(DBusMessage     *msg)  { dbus_message_unref(msg); }
    void intrusive_ptr_add_ref(DBusPendingCall *call) { dbus_pending_call_ref (call); }
    void intrusive_ptr_release(DBusPendingCall *call) { dbus_pending_call_unref (call); }
}

namespace GDBusCXX {

DBusConnection *dbus_get_bus_connection(const char *busType,
                                        const char *interface,
                                        bool unshared,
                                        DBusErrorCXX *err)
{
    return b_dbus_setup_bus(boost::iequals(busType, "SYSTEM") ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION,
                            interface, unshared, err);;
}

}
