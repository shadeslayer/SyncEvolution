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

#include "dbus-transport-agent.h"
#include "connection.h"

SE_BEGIN_CXX

DBusTransportAgent::DBusTransportAgent(GMainLoop *loop,
                                       Session &session,
                                       boost::weak_ptr<Connection> connection) :
    m_loop(loop),
    m_session(session),
    m_connection(connection),
    m_timeoutSeconds(0),
    m_eventTriggered(false),
    m_waiting(false)
{
}

DBusTransportAgent::~DBusTransportAgent()
{
    boost::shared_ptr<Connection> connection = m_connection.lock();
    if (connection) {
        connection->shutdown();
    }
}

void DBusTransportAgent::send(const char *data, size_t len)
{
    boost::shared_ptr<Connection> connection = m_connection.lock();

    if (!connection) {
        SE_THROW_EXCEPTION(TransportException,
                           "D-Bus peer has disconnected");
    }

    if (connection->m_state != Connection::PROCESSING) {
        SE_THROW_EXCEPTION(TransportException,
                           "cannot send to our D-Bus peer");
    }

    // Change state in advance. If we fail while replying, then all
    // further resends will fail with the error above.
    connection->m_state = Connection::WAITING;
    connection->m_incomingMsg = SharedBuffer();

    if (m_timeoutSeconds) {
        m_eventSource = g_timeout_add_seconds(m_timeoutSeconds, timeoutCallback, static_cast<gpointer>(this));
    }
    m_eventTriggered = false;

    // TODO: turn D-Bus exceptions into transport exceptions
    StringMap meta;
    meta["URL"] = m_url;
    connection->reply(std::make_pair(len, reinterpret_cast<const uint8_t *>(data)),
                      m_type, meta, false, connection->m_sessionID);
}

void DBusTransportAgent::shutdown()
{
    boost::shared_ptr<Connection> connection = m_connection.lock();

    if (!connection) {
        SE_THROW_EXCEPTION(TransportException,
                           "D-Bus peer has disconnected");
    }

    if (connection->m_state != Connection::FAILED) {
        // send final, empty message and wait for close
        connection->m_state = Connection::FINAL;
        connection->reply(std::pair<size_t, const uint8_t *>(0, 0),
                          "", StringMap(),
                          true, connection->m_sessionID);
    }
}

gboolean DBusTransportAgent::timeoutCallback(gpointer transport)
{
    DBusTransportAgent *me = static_cast<DBusTransportAgent *>(transport);
    me->m_eventTriggered = true;
    if (me->m_waiting) {
        g_main_loop_quit(me->m_loop);
    }
    return false;
}

void DBusTransportAgent::doWait(boost::shared_ptr<Connection> &connection)
{
    // let Connection wake us up when it has a reply or
    // when it closes down
    connection->m_loop = m_loop;

    // release our reference so that the Connection instance can
    // be destructed when requested by the D-Bus peer
    connection.reset();

    // now wait
    m_waiting = true;
    g_main_loop_run(m_loop);
    m_waiting = false;
}

DBusTransportAgent::Status DBusTransportAgent::wait(bool noReply)
{
    boost::shared_ptr<Connection> connection = m_connection.lock();

    if (!connection) {
        SE_THROW_EXCEPTION(TransportException,
                           "D-Bus peer has disconnected");
    }

    switch (connection->m_state) {
    case Connection::PROCESSING:
        m_incomingMsg = connection->m_incomingMsg;
        m_incomingMsgType = connection->m_incomingMsgType;
        return GOT_REPLY;
        break;
    case Connection::FINAL:
        if (m_eventTriggered) {
            return TIME_OUT;
        }
        doWait(connection);

        // if the connection is still available, then keep waiting
        connection = m_connection.lock();
        if (connection) {
            return ACTIVE;
        } else if (m_session.getStubConnectionError().empty()) {
            return INACTIVE;
        } else {
            SE_THROW_EXCEPTION(TransportException, m_session.getStubConnectionError());
            return FAILED;
        }
        break;
    case Connection::WAITING:
        if (noReply) {
            // message is sent as far as we know, so return
            return INACTIVE;
        }

        if (m_eventTriggered) {
            return TIME_OUT;
        }
        doWait(connection);

        // tell caller to check again
        return ACTIVE;
        break;
    case Connection::DONE:
        if (!noReply) {
            SE_THROW_EXCEPTION(TransportException,
                               "internal error: transport has shut down, can no longer receive reply");
        }

        return CLOSED;
    default:
        SE_THROW_EXCEPTION(TransportException,
                           "internal error: send() on connection which is not ready");
        break;
    }

    return FAILED;
}

void DBusTransportAgent::getReply(const char *&data, size_t &len, std::string &contentType)
{
    data = m_incomingMsg.get();
    len = m_incomingMsg.size();
    contentType = m_incomingMsgType;
}

SE_END_CXX
