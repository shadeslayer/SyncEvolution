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

#include "connman-client.h"
#include "syncevo-dbus-server.h"

SE_BEGIN_CXX

ConnmanClient::ConnmanClient(DBusServer &server):
    m_server(server),
    m_propertyChanged(*this, "PropertyChanged")
{
    const char *connmanTest = getenv ("DBUS_TEST_CONNMAN");
    m_connmanConn = b_dbus_setup_bus (connmanTest ? DBUS_BUS_SESSION: DBUS_BUS_SYSTEM, NULL, true, NULL);
    if (m_connmanConn){
        typedef std::map <std::string, boost::variant <std::vector <std::string> > > PropDict;
        DBusClientCall1<PropDict>  getProp(*this,"GetProperties");
        getProp (boost::bind(&ConnmanClient::getPropCb, this, _1, _2));
        m_propertyChanged.activate(boost::bind(&ConnmanClient::propertyChanged, this, _1, _2));
    }else{
        SE_LOG_ERROR (NULL, NULL, "DBus connection setup for connman failed");
    }
}

void ConnmanClient::getPropCb (const std::map <std::string,
                               boost::variant <std::vector <std::string> > >& props, const string &error){
    if (!error.empty()) {
        if (error == "org.freedesktop.DBus.Error.ServiceUnknown") {
            // ensure there is still first set of singal set in case of no
            // connman available
            m_server.getPresenceStatus().updatePresenceStatus (true, true);
            SE_LOG_DEBUG (NULL, NULL, "No connman service available %s", error.c_str());
            return;
        }
        SE_LOG_DEBUG (NULL, NULL, "error in connmanCallback %s", error.c_str());
        return;
    }

    typedef std::pair <std::string, boost::variant <std::vector <std::string> > > element;
    bool httpPresence = false, btPresence = false;
    BOOST_FOREACH (element entry, props) {
        //match connected for HTTP based peers (wifi/wimax/ethernet)
        if (entry.first == "ConnectedTechnologies") {
            std::vector <std::string> connected = boost::get <std::vector <std::string> > (entry.second);
            BOOST_FOREACH (std::string tech, connected) {
                if (boost::iequals (tech, "wifi") || boost::iequals (tech, "ethernet")
                || boost::iequals (tech, "wimax")) {
                    httpPresence = true;
                    break;
                }
            }
        } else if (entry.first == "AvailableTechnologies") {
            std::vector <std::string> enabled = boost::get <std::vector <std::string> > (entry.second);
            BOOST_FOREACH (std::string tech, enabled){
                if (boost::iequals (tech, "bluetooth")) {
                    btPresence = true;
                    break;
                }
            }
        } else {
            continue;
        }
    }
    //now delivering the signals
    m_server.getPresenceStatus().updatePresenceStatus (httpPresence, btPresence);
}

void ConnmanClient::propertyChanged(const string &name,
                                    const boost::variant<vector<string>, string> &prop)
{
    bool httpPresence=false, btPresence=false;
    bool httpChanged=false, btChanged=false;
    if (boost::iequals(name, "ConnectedTechnologies")) {
        httpChanged=true;
        vector<string> connected = boost::get<vector<string> >(prop);
        BOOST_FOREACH (std::string tech, connected) {
            if (boost::iequals (tech, "wifi") || boost::iequals (tech, "ethernet")
                    || boost::iequals (tech, "wimax")) {
                httpPresence=true;
                break;
            }
        }
    } else if (boost::iequals (name, "AvailableTechnologies")){
        btChanged=true;
        vector<string> enabled = boost::get<vector<string> >(prop);
        BOOST_FOREACH (std::string tech, enabled){
            if (boost::iequals (tech, "bluetooth")) {
                btPresence = true;
                break;
            }
        }
    }
    if(httpChanged) {
        m_server.getPresenceStatus().updatePresenceStatus (httpPresence, PresenceStatus::HTTP_TRANSPORT);
    } else if (btChanged) {
        m_server.getPresenceStatus().updatePresenceStatus (btPresence, PresenceStatus::BT_TRANSPORT);
    } else {
    }
}

SE_END_CXX
