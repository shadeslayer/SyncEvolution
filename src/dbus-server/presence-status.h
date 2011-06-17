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

#ifndef PRESENCE_STATUS_H
#define PRESENCE_STATUS_H

#include "timer.h"
#include "read-operations.h"

SE_BEGIN_CXX

class PresenceStatus {
    bool m_httpPresence;
    bool m_btPresence;
    bool m_initiated;
    DBusServer &m_server;

    /** two timers to record when the statuses of network and bt are changed */
    Timer m_httpTimer;
    Timer m_btTimer;

    enum PeerStatus {
        /* The transport is not available (local problem) */
        NOTRANSPORT,
        /* The peer is not contactable (remote problem) */
        UNREACHABLE,
        /* Not for sure whether the peer is presence but likely*/
        MIGHTWORK,

        INVALID
    };

    typedef map<string, vector<pair <string, PeerStatus> > > StatusMap;
    typedef pair<const string, vector<pair <string, PeerStatus> > > StatusPair;
    typedef pair <string, PeerStatus> PeerStatusPair;
    StatusMap m_peers;

    static std::string status2string (PeerStatus status) {
        switch (status) {
            case NOTRANSPORT:
                return "no transport";
                break;
            case UNREACHABLE:
                return "not present";
                break;
            case MIGHTWORK:
                return "";
                break;
            case INVALID:
                return "invalid transport status";
        }
        // not reached, keep compiler happy
        return "";
    }

    public:
    PresenceStatus (DBusServer &server)
        :m_httpPresence (false), m_btPresence (false), m_initiated (false), m_server (server),
        m_httpTimer(), m_btTimer()
    {
    }

    enum TransportType{
        HTTP_TRANSPORT,
        BT_TRANSPORT,
        INVALID_TRANSPORT
    };

    void init();

    /* Implement DBusServer::checkPresence*/
    void checkPresence (const string &peer, string& status, std::vector<std::string> &transport);

    void updateConfigPeers (const std::string &peer, const ReadOperations::Config_t &config);

    void updatePresenceStatus (bool httpPresence, bool btPresence);
    void updatePresenceStatus (bool newStatus, TransportType type);

    bool getHttpPresence() { return m_httpPresence; }
    bool getBtPresence() { return m_btPresence; }
    Timer& getHttpTimer() { return m_httpTimer; }
    Timer& getBtTimer() { return m_btTimer; }
};

SE_END_CXX

#endif // PRESENCE_STATUS_H
