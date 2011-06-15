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

#ifndef DBUS_TRANSPORT_AGENT_H
#define DBUS_TRANSPORT_AGENT_H

#include "common.h"

SE_BEGIN_CXX

class Session;
class Connection;

/**
 * A proxy for a Connection instance. The Connection instance can go
 * away (weak pointer, must be locked and and checked each time it is
 * needed). The agent must remain available as long as the engine
 * needs and basically becomes unusuable once the connection dies.
 *
 * Reconnecting is not currently supported.
 */
class DBusTransportAgent : public TransportAgent
{
    GMainLoop *m_loop;
    Session &m_session;
    boost::weak_ptr<Connection> m_connection;

    std::string m_url;
    std::string m_type;

    /*
     * When the timeout occurs, we always abort the current
     * transmission.  If it is invoked while we are not in the wait()
     * of this transport, then we remember that in m_eventTriggered
     * and return from wait() right away. The main loop is only
     * quit when the transport is waiting in it. This is a precaution
     * to not interfere with other parts of the code.
     */
    int m_timeoutSeconds;
    GLibEvent m_eventSource;
    bool m_eventTriggered;
    bool m_waiting;

    SharedBuffer m_incomingMsg;
    std::string m_incomingMsgType;

    void doWait(boost::shared_ptr<Connection> &connection);
    static gboolean timeoutCallback(gpointer transport);

 public:
    DBusTransportAgent(GMainLoop *loop,
                       Session &session,
                       boost::weak_ptr<Connection> connection);
    ~DBusTransportAgent();

    virtual void setURL(const std::string &url) { m_url = url; }
    virtual void setContentType(const std::string &type) { m_type = type; }
    virtual void send(const char *data, size_t len);
    virtual void cancel() {}
    virtual void shutdown();
    virtual Status wait(bool noReply = false);
    virtual void setTimeout(int seconds)
    {
        m_timeoutSeconds = seconds;
        m_eventSource = 0;
    }
    virtual void getReply(const char *&data, size_t &len, std::string &contentType);
};

SE_END_CXX

#endif // DBUS_TRANSPORT_AGENT_H
