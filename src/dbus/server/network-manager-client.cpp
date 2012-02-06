
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

#include "network-manager-client.h"
#include "server.h"

SE_BEGIN_CXX

NetworkManagerClient::NetworkManagerClient(Server &server) :
    DBusRemoteObject(!strcmp(getEnv("DBUS_TEST_NETWORK_MANAGER", ""), "none") ?
                     NULL : /* simulate missing Network Manager */
                     GDBusCXX::dbus_get_bus_connection(!strcmp(getEnv("DBUS_TEST_NETWORK_MANAGER", ""), "session") ?
                                                       "SESSION" : /* use our own Network Manager stub */
                                                       "SYSTEM" /* use real Network Manager */,
                                                       NULL, true, NULL),
                     "/org/freedesktop/NetworkManager",
                     "org.freedesktop.NetworkManager",
                     "org.freedesktop.NetworkManager",
                     true),
    m_server(server),
    m_stateChanged(*this, "StateChanged"),
    m_properties(*this)
{
    if (getConnection()) {
        m_properties.get();
        m_stateChanged.activate(boost::bind(
                                    &NetworkManagerClient::stateChanged,
                                    this, _1));
    } else {
        SE_LOG_ERROR(NULL, NULL,
                     "DBus connection setup for NetworkManager failed");
    }
}

void NetworkManagerClient::stateChanged(uint32_t uiState)
{
    switch (uiState) {
    case NM_STATE_ASLEEP:
    case NM_STATE_DISCONNECTED:
    case NM_STATE_DISCONNECTING:
    case NM_STATE_CONNECTING:
    case NM_STATE_ASLEEP_DEPRECATED:
    case NM_STATE_CONNECTING_DEPRECATED:
    case NM_STATE_DISCONNECTED_DEPRECATED:
        SE_LOG_DEBUG(NULL, NULL, "NetworkManager disconnected");
        m_server.getPresenceStatus().updatePresenceStatus(
            false, PresenceStatus::HTTP_TRANSPORT);
        break;

    default:
        SE_LOG_DEBUG(NULL, NULL, "NetworkManager connected");
        m_server.getPresenceStatus().updatePresenceStatus(
            true, PresenceStatus::HTTP_TRANSPORT);
    }
}

NetworkManagerClient::NetworkManagerProperties::NetworkManagerProperties(
    NetworkManagerClient& manager) :
    GDBusCXX::DBusRemoteObject(manager.getConnection(),
                               "/org/freedesktop/NetworkManager",
                               "org.freedesktop.DBus.Properties",
                               "org.freedesktop.NetworkManager"),
    m_manager(manager)
{
}

void NetworkManagerClient::NetworkManagerProperties::get()
{
    GDBusCXX::DBusClientCall1<boost::variant<uint32_t, std::string> > get(*this, "Get");
    get.start(std::string(m_manager.getInterface()), std::string("State"),
              boost::bind(&NetworkManagerProperties::getCallback, this, _1, _2));
}

void NetworkManagerClient::NetworkManagerProperties::getCallback(
    const boost::variant<uint32_t, std::string> &prop,
    const std::string &error)
{
    if(!error.empty()) {
        SE_LOG_DEBUG (
            NULL, NULL,
            "Error in calling Get of Interface "
            "org.freedesktop.DBus.Properties : %s", error.c_str());
    } else {
        m_manager.stateChanged(boost::get<uint32_t>(prop));
    }
}

SE_END_CXX
