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

#include "dbus-user-interface.h"

#include <syncevo/util.h>

SE_BEGIN_CXX

std::string DBusUserInterface::askPassword(const std::string &passwordName,
                                           const std::string &descr,
                                           const ConfigPasswordKey &key)
{
    std::string password;
    if (GetLoadPasswordSignal()(passwordName, descr, key,  password)) {
        // handled
        return password;
    }

    //if not found, return empty
    return "";
}

bool DBusUserInterface::savePassword(const std::string &passwordName,
                                     const std::string &password,
                                     const ConfigPasswordKey &key)
{
    if (GetSavePasswordSignal()(passwordName, password, key)) {
        return true;
    }

    // not saved
    return false;
}

void DBusUserInterface::readStdin(std::string &content)
{
    SE_THROW("reading stdin in D-Bus server not supported, use --daemon=no in command line");
}

SE_END_CXX
