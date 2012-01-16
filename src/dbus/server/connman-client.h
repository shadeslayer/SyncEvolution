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

#ifndef CONNMAN_CLIENT_H
#define CONNMAN_CLIENT_H

#include <boost/variant.hpp>

#include "gdbus-cxx-bridge.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class Server;

/*
 * Implements org.connman.Manager
 * GetProperty  : getPropCb
 * PropertyChanged: propertyChanged
 **/
class ConnmanClient : public GDBusCXX::DBusRemoteObject
{
public:
    ConnmanClient (Server &server);

    void propertyChanged(const std::string &name,
                         const boost::variant<std::vector<std::string>, std::string> &prop);

    void getPropCb(const std::map <std::string, boost::variant<std::string> >& props, const std::string &error);

    /** TRUE if watching ConnMan status */
    bool isAvailable() { return getConnection() != NULL; }

private:
    Server &m_server;

    GDBusCXX::SignalWatch2 <std::string, boost::variant<std::string> > m_propertyChanged;
};

SE_END_CXX

#endif // CONNMAN_CLIENT_H
