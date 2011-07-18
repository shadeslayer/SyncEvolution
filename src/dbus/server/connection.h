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

#ifndef CONNECTION_H
#define CONNECTION_H

#include "session.h"

SE_BEGIN_CXX

class Server;

/**
 * Represents and implements the Connection interface.
 *
 * The connection interacts with a Session by creating the Session and
 * exchanging data with it. For that, the connection registers itself
 * with the Session and unregisters again when it goes away.
 *
 * In contrast to clients, the Session only keeps a weak_ptr, which
 * becomes invalid when the referenced object gets deleted. Typically
 * this means the Session has to abort, unless reconnecting is
 * supported.
 */
class Connection : public GDBusCXX::DBusObjectHelper, public Resource
{
    Server &m_server;
    StringMap m_peer;
    bool m_mustAuthenticate;
    enum {
        SETUP,          /**< ready for first message */
        PROCESSING,     /**< received message, waiting for engine's reply */
        WAITING,        /**< waiting for next follow-up message */
        FINAL,          /**< engine has sent final reply, wait for ACK by peer */
        DONE,           /**< peer has closed normally after the final reply */
        FAILED          /**< in a failed state, no further operation possible */
    } m_state;
    std::string m_failure;

    /** first parameter for Session::sync() */
    std::string m_syncMode;
    /** second parameter for Session::sync() */
    Session::SourceModes_t m_sourceModes;

    const std::string m_sessionID;
    boost::shared_ptr<Session> m_session;

    /**
     * main loop that our DBusTransportAgent is currently waiting in,
     * NULL if not waiting
     */
    GMainLoop *m_loop;

    /**
     * get our peer session out of the DBusTransportAgent,
     * if it is currently waiting for us (indicated via m_loop)
     */
    void wakeupSession();

    /**
     * buffer for received data, waiting here for engine to ask
     * for it via DBusTransportAgent::getReply().
     */
    SharedBuffer m_incomingMsg;
    std::string m_incomingMsgType;

    struct SANContent {
        std::vector <string> m_syncType;
        std::vector <uint32_t> m_contentType;
        std::vector <string> m_serverURI;
    };

    /**
     * The content of a parsed SAN package to be processed via
     * connection.ready
     */
    boost::shared_ptr <SANContent> m_SANContent;
    std::string m_peerBtAddr;

    /**
     * records the reason for the failure, sends Abort signal and puts
     * the connection into the FAILED state.
     */
    void failed(const std::string &reason);

    /**
     * returns "<description> (<ID> via <transport> <transport_description>)"
     */
    static std::string buildDescription(const StringMap &peer);

    /** Connection.Process() */
    void process(const GDBusCXX::Caller_t &caller,
                 const std::pair<size_t, const uint8_t *> &message,
                 const std::string &message_type);
    /** Connection.Close() */
    void close(const GDBusCXX::Caller_t &caller,
               bool normal,
               const std::string &error);
    /** wrapper around sendAbort */
    void abort();
    /** Connection.Abort */
    GDBusCXX::EmitSignal0 sendAbort;
    bool m_abortSent;
    /** Connection.Reply */
    GDBusCXX::EmitSignal5<const std::pair<size_t, const uint8_t *> &,
                          const std::string &,
                          const StringMap &,
                          bool,
                          const std::string &> reply;

    friend class DBusTransportAgent;

public:
    const std::string m_description;

    Connection(Server &server,
               const GDBusCXX::DBusConnectionPtr &conn,
               const std::string &session_num,
               const StringMap &peer,
               bool must_authenticate);

    ~Connection();

    /** session requested by us is ready to run a sync */
    void ready();

    /** connection is no longer needed, ensure that it gets deleted */
    void shutdown();

    /** peer is not trusted, must authenticate as part of SyncML */
    bool mustAuthenticate() const { return m_mustAuthenticate; }
};

SE_END_CXX

#endif // CONNECTION_H
