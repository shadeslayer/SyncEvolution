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

#include "common.h"
#include "gdbus/gdbus-cxx-bridge.h"

using namespace GDBusCXX;

SE_BEGIN_CXX

class DBusServer;

/*
 * Implements org.connman.Manager
 * GetProperty  : getPropCb
 * PropertyChanged: propertyChanged
 **/
class ConnmanClient : public DBusRemoteObject
{
public:
    ConnmanClient (DBusServer &server);
    virtual const char *getDestination() const {return "net.connman";}
    virtual const char *getPath() const {return "/";}
    virtual const char *getInterface() const {return "net.connman.Manager";}
    virtual DBusConnection *getConnection() const {return m_connmanConn.get();}

    void propertyChanged(const string &name,
                         const boost::variant<vector<string>, string> &prop);

    void getPropCb(const std::map <std::string, boost::variant <std::vector <std::string> > >& props, const string &error);

    /** TRUE if watching ConnMan status */
    bool isAvailable() { return m_connmanConn; }

private:
    DBusServer &m_server;
    DBusConnectionPtr m_connmanConn;

    SignalWatch2 <string,boost::variant<vector<string>, string> > m_propertyChanged;
};

SE_END_CXX

#endif // CONNMAN_CLIENT_H
