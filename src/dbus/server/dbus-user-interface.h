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

#ifndef DBUS_USER_INTERFACE_H
#define DBUS_USER_INTERFACE_H

#include <syncevo/UserInterface.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * This class is mainly to implement two virtual functions 'askPassword'
 * and 'savePassword' of ConfigUserInterface. The main functionality is
 * to only get and save passwords in the gnome keyring.
 */
class DBusUserInterface : public UserInterface
{
public:
    /*
     * Ask password from gnome keyring, if not found, empty string
     * is returned
     */
    std::string askPassword(const std::string &passwordName,
                            const std::string &descr,
                            const ConfigPasswordKey &key);

    //save password to gnome keyring, if not successful, false is returned.
    bool savePassword(const std::string &passwordName,
                      const std::string &password,
                      const ConfigPasswordKey &key);

    /**
     * Read stdin via InfoRequest/Response.
     */
    void readStdin(std::string &content);
};

SE_END_CXX

#endif // DBUS_USER_INTERFACE_H
