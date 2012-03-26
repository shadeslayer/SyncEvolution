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
#include "session-common.h"
#include "resource.h"

#include <boost/signals2.hpp>

#include <gdbus-cxx-bridge.h>

#include <syncevo/SynthesisEngine.h>

SE_BEGIN_CXX

class Server;
class Session;

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
 private:
    Server &m_server;
    boost::weak_ptr<Connection> m_me;
    StringMap m_peer;
    bool m_mustAuthenticate;
    SessionCommon::ConnectionState m_state;
    std::string m_failure;

    /** first parameter for Session::sync() */
    std::string m_syncMode;
    /** second parameter for Session::sync() */
    SessionCommon::SourceModes_t m_sourceModes;

    const std::string m_sessionID;
    boost::shared_ptr<Session> m_session;

    /**
     * Defines the timeout in seconds. -1 and thus "no timeout" by default.
     *
     * The timeout is acticated each time the connection goes into
     * WAITING mode. Once it triggers, the connection is put into
     * the FAILED and queued for delayed deletion in the server.
     */
    int m_timeoutSeconds;
    Timeout m_timeout;
    void activateTimeout();
    void timeoutCb();

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
                 const GDBusCXX::DBusArray<uint8_t> &message,
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
    GDBusCXX::EmitSignal5<const GDBusCXX::DBusArray<uint8_t> &,
                          const std::string &,
                          const StringMap &,
                          bool,
                          const std::string &> reply;

    Connection(Server &server,
               const GDBusCXX::DBusConnectionPtr &conn,
               const std::string &session_num,
               const StringMap &peer,
               bool must_authenticate);

public:
    const std::string m_description;

    static boost::shared_ptr<Connection> createConnection(Server &server,
                                                          const GDBusCXX::DBusConnectionPtr &conn,
                                                          const std::string &session_num,
                                                          const StringMap &peer,
                                                          bool must_authenticate);

    ~Connection();

    /** session requested by us is ready to run a sync */
    void ready();

    /** send outgoing message via connection */
    void send(const GDBusCXX::DBusArray<uint8_t> buffer,
              const std::string &type,
              const std::string &url);

    /** send last, empty message and enter FINAL state */
    void sendFinalMsg();

    /** connection is no longer needed, ensure that it gets deleted */
    void shutdown();

    /** peer is not trusted, must authenticate as part of SyncML */
    bool mustAuthenticate() const { return m_mustAuthenticate; }

    /** new incoming message ready */
    typedef boost::signals2::signal<void (const GDBusCXX::DBusArray<uint8_t> &, const std::string &)> MessageSignal_t;
    MessageSignal_t m_messageSignal;

    /** connection went down (empty string) or failed (error message) */
    typedef boost::signals2::signal<void (const std::string &)> StatusSignal_t;
    StatusSignal_t m_statusSignal;
};

SE_END_CXX

#endif // CONNECTION_H
