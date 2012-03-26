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

#include "presence-status.h"
#include "server.h"

SE_BEGIN_CXX

void PresenceStatus::init(){
    //initialize the configured peer list
    if (!m_initiated) {
        SyncConfig::ConfigList list = SyncConfig::getConfigs();
        BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server, list) {
            SyncConfig config (server.first);
            vector<string> urls = config.getSyncURL();
            m_peers[server.first].clear();
            BOOST_FOREACH (const string &url, urls) {
                // take current status into account,
                // PresenceStatus::checkPresence() calls init() and
                // expects up-to-date information
                PeerStatus status;
                if ((boost::starts_with(url, "obex-bt") && m_btPresence) ||
                    (boost::starts_with (url, "http") && m_httpPresence) ||
                    boost::starts_with (url, "local")) {
                    status = MIGHTWORK;
                } else {
                    status = NOTRANSPORT;
                }
                m_peers[server.first].push_back(make_pair(url, status));
            }
        }
        m_initiated = true;
    }
}

/* Implement PresenceStatus::checkPresence*/
void PresenceStatus::checkPresence (const string &peer, string& status, std::vector<std::string> &transport) {

    if (!m_initiated) {
        //might triggered by updateConfigPeers
        init();
    }

    string peerName = SyncConfig::normalizeConfigString (peer);
    vector< pair<string, PeerStatus> > mytransports = m_peers[peerName];
    if (mytransports.empty()) {
        //wrong config name?
        status = status2string(NOTRANSPORT);
        transport.clear();
        return;
    }
    PeerStatus mystatus = MIGHTWORK;
    transport.clear();
    //only if all transports are unavailable can we declare the peer
    //status as unavailable
    BOOST_FOREACH (PeerStatusPair &mytransport, mytransports) {
        if (mytransport.second == MIGHTWORK) {
            transport.push_back (mytransport.first);
        }
    }
    if (transport.empty()) {
        mystatus = NOTRANSPORT;
    }
    status = status2string(mystatus);
}

void PresenceStatus::updateConfigPeers (const std::string &peer, const ReadOperations::Config_t &config) {
    ReadOperations::Config_t::const_iterator iter = config.find ("");
    if (iter != config.end()) {
        //As a simple approach, just reinitialize the whole STATUSMAP
        //it will cause later updatePresenceStatus resend all signals
        //and a reload in checkPresence
        m_initiated = false;
    }
}

void PresenceStatus::updatePresenceStatus (bool newStatus, PresenceStatus::TransportType type) {
    if (type == PresenceStatus::HTTP_TRANSPORT) {
        updatePresenceStatus (newStatus, m_btPresence);
    } else if (type == PresenceStatus::BT_TRANSPORT) {
        updatePresenceStatus (m_httpPresence, newStatus);
    }else {
    }
}

void PresenceStatus::updatePresenceStatus (bool httpPresence, bool btPresence) {
    bool httpChanged = (m_httpPresence != httpPresence);
    bool btChanged = (m_btPresence != btPresence);
    if (m_initiated && !httpChanged && !btChanged) {
        //nothing changed
        return;
    }

    //initialize the configured peer list using old presence status
    bool initiated = m_initiated;
    if (!m_initiated) {
        init();
    }

    // switch to new status
    m_httpPresence = httpPresence;
    m_btPresence = btPresence;
    if (httpChanged) {
        m_httpPresenceSignal(httpPresence);
    }
    if (btChanged) {
        m_btPresenceSignal(btPresence);
    }


    //iterate all configured peers and fire singals
    BOOST_FOREACH (StatusPair &peer, m_peers) {
        //iterate all possible transports
        //TODO One peer might got more than one signals, avoid this
        std::vector<pair<string, PeerStatus> > &transports = peer.second;
        BOOST_FOREACH (PeerStatusPair &entry, transports) {
            string url = entry.first;
            if (boost::starts_with (url, "http") && (httpChanged || !initiated)) {
                entry.second = m_httpPresence ? MIGHTWORK: NOTRANSPORT;
                m_server.emitPresence (peer.first, status2string (entry.second), entry.first);
                SE_LOG_DEBUG(NULL, NULL,
                        "http presence signal %s,%s,%s",
                        peer.first.c_str(),
                        status2string (entry.second).c_str(), entry.first.c_str());
            } else if (boost::starts_with (url, "obex-bt") && (btChanged || !initiated)) {
                entry.second = m_btPresence ? MIGHTWORK: NOTRANSPORT;
                m_server.emitPresence (peer.first, status2string (entry.second), entry.first);
                SE_LOG_DEBUG(NULL, NULL,
                        "bluetooth presence signal %s,%s,%s",
                        peer.first.c_str(),
                        status2string (entry.second).c_str(), entry.first.c_str());
            } else if (boost::starts_with (url, "local") && !initiated) {
                m_server.emitPresence (peer.first, status2string (MIGHTWORK), entry.first);
                SE_LOG_DEBUG(NULL, NULL,
                        "local presence signal %s,%s,%s",
                        peer.first.c_str(),
                        status2string (MIGHTWORK).c_str(), entry.first.c_str());
            }
        }
    }
}

SE_END_CXX
