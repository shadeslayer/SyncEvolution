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

#ifndef NETWORK_MANAGER_CLIENT_H
#define NETWORK_MANAGER_CLIENT_H

#include <boost/variant.hpp>

#include "gdbus-cxx-bridge.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class Server;

/**
 * Client for org.freedesktop.NetworkManager
 * The initial state of NetworkManager is queried via
 * org.freedesktop.DBus.Properties. Dynamic changes are listened via
 * org.freedesktop.NetworkManager - StateChanged signal
 */
class NetworkManagerClient : public GDBusCXX::DBusRemoteObject
{
public:
    enum NM_State
      {
        NM_STATE_UNKNOWN = 0,

        /* following values for NM < 0.9 */
        NM_STATE_ASLEEP_DEPRECATED = 1,
        NM_STATE_CONNECTING_DEPRECATED = 2,
        NM_STATE_CONNECTED_DEPRECATED = 3,
        NM_STATE_DISCONNECTED_DEPRECATED = 4,

        /* following values for NM >= 0.9 */
        NM_STATE_ASLEEP = 10,
        NM_STATE_DISCONNECTED = 20,
        NM_STATE_DISCONNECTING = 30,
        NM_STATE_CONNECTING = 40,
        NM_STATE_CONNECTED_LOCAL = 50,
        NM_STATE_CONNECTED_SITE = 60,
        NM_STATE_CONNECTED_GLOBAL = 70,
      };
public:
    NetworkManagerClient(Server& server);

    void stateChanged(uint32_t uiState);

    /** TRUE if watching Network Manager status */
    bool isAvailable() { return getConnection() != NULL; }

private:

    class NetworkManagerProperties : public DBusRemoteObject
    {
    public:
        NetworkManagerProperties(NetworkManagerClient& manager);

        void get();
        void getCallback(const boost::variant<uint32_t, std::string> &prop,
                         const std::string &error);
    private:
        NetworkManagerClient &m_manager;
    };

    Server &m_server;
    GDBusCXX::SignalWatch1<uint32_t> m_stateChanged;
    NetworkManagerProperties m_properties;
};

SE_END_CXX

#endif // NETWORK_MANAGER_CLIENT_H
