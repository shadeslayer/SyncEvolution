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
#include "session-helper.h"

SE_BEGIN_CXX

DBusTransportAgent::DBusTransportAgent(SessionHelper &helper) :
    m_helper(helper),
    m_state(SessionCommon::SETUP)
{
}

void DBusTransportAgent::serverAlerted()
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus transport: server alerted (old state: %s, %s)",
                 SessionCommon::ConnectionStateToString(m_state).c_str(),
                 m_error.c_str());
    if (m_state == SessionCommon::SETUP) {
        m_state = SessionCommon::PROCESSING;
    } else {
        SE_THROW_EXCEPTION(TransportException,
                           "setting 'server alerted' only allowed during setup");
    }
}

void DBusTransportAgent::storeMessage(const GDBusCXX::DBusArray<uint8_t> &buffer,
                                      const std::string &type)
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus transport: store incoming message, %ld bytes, %s (old state: %s, %s)",
                 (long)buffer.first,
                 type.c_str(),
                 SessionCommon::ConnectionStateToString(m_state).c_str(),
                 m_error.c_str());
    if (m_state == SessionCommon::SETUP ||
        m_state == SessionCommon::WAITING) {
        m_incomingMsg = SharedBuffer(reinterpret_cast<const char *>(buffer.second), buffer.first);
        m_incomingMsgType = type;
        m_state = SessionCommon::PROCESSING;
    } else if (m_state == SessionCommon::PROCESSING &&
               m_incomingMsgType == type &&
               m_incomingMsg.size() == buffer.first &&
               !memcmp(m_incomingMsg.get(), buffer.second, buffer.first)) {
        // Exactly the same message, accept resend without error, and
        // without doing anything.
    } else {
        SE_THROW_EXCEPTION(TransportException,
                           "unexpected message");
    }
}

void DBusTransportAgent::storeState(const std::string &error)
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus transport: got error '%s', current error is '%s', state %s",
                 error.c_str(), m_error.c_str(), SessionCommon::ConnectionStateToString(m_state).c_str());

    if (!error.empty()) {
        // specific error encountered
        m_state = SessionCommon::FAILED;
        if (m_error.empty()) {
            m_error = error;
        }
    } else if (m_state == SessionCommon::FINAL) {
        // expected loss of connection
        m_state = SessionCommon::DONE;
    } else {
        // unexpected loss of connection
        m_state = SessionCommon::FAILED;
    }
}

void DBusTransportAgent::send(const char *data, size_t len)
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus transport: outgoing message %ld bytes, %s, %s",
                 (long)len, m_type.c_str(), m_url.c_str());
    if (m_state != SessionCommon::PROCESSING) {
        SE_THROW_EXCEPTION(TransportException,
                           "cannot send to our D-Bus peer");
    }
    m_state = SessionCommon::WAITING;
    m_incomingMsg = SharedBuffer();
    m_helper.emitMessage(GDBusCXX::DBusArray<uint8_t>(len, reinterpret_cast<const uint8_t *>(data)),
                         m_type,
                         m_url);
}

void DBusTransportAgent::shutdown()
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus transport: shut down (old state: %s, %s)",
                 SessionCommon::ConnectionStateToString(m_state).c_str(),
                 m_error.c_str());
    if (m_state != SessionCommon::FAILED) {
        m_state = SessionCommon::FINAL;
        m_helper.emitShutdown();
    }
}

void DBusTransportAgent::doWait()
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus transport: wait - old state: %s, %s",
                 SessionCommon::ConnectionStateToString(m_state).c_str(),
                 m_error.c_str());
    // Block for one iteration. Both D-Bus calls and signals (thanks
    // to the SuspendFlags guard in the running sync session) will
    // wake us up.
    g_main_context_iteration(NULL, true);

    SE_LOG_DEBUG(NULL, NULL, "D-Bus transport: wait - new state: %s, %s",
                 SessionCommon::ConnectionStateToString(m_state).c_str(),
                 m_error.c_str());
}


DBusTransportAgent::Status DBusTransportAgent::wait(bool noReply)
{
    switch (m_state) {
    case SessionCommon::PROCESSING:
        return GOT_REPLY;
        break;
    case SessionCommon::FINAL:
        doWait();

        // if the connection is still available, then keep waiting
        if (m_state == SessionCommon::FINAL) {
            return ACTIVE;
        } else if (m_error.empty()) {
            return INACTIVE;
        } else {
            SE_THROW_EXCEPTION(TransportException, m_error);
            return FAILED;
        }
        break;
    case SessionCommon::WAITING:
        if (noReply) {
            // message is sent as far as we know, so return
            return INACTIVE;
        }
        doWait();

        // tell caller to check again
        return ACTIVE;
        break;
    case SessionCommon::DONE:
        if (!noReply) {
            SE_THROW_EXCEPTION(TransportException,
                               "internal error: transport has shut down, can no longer receive reply");
        }
        return CLOSED;
    default:
        SE_THROW_EXCEPTION(TransportException,
                           "send() on connection which is not ready");
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
