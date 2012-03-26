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

#include <syncevo/TransportAgent.h>
#include <syncevo/SmartPtr.h>
#include <syncevo/SynthesisEngine.h>
#include <boost/weak_ptr.hpp>

#include <gdbus-cxx-bridge.h>

#include "session-common.h"

SE_BEGIN_CXX

class Connection;
class SessionHelper;

/**
 * A proxy for a Connection instance in the syncevo-dbus-server. The
 * Connection instance can go away (weak pointer, must be locked and
 * and checked each time it is needed). The agent must remain
 * available as long as the engine needs and basically becomes
 * unusuable once the connection dies. That information is relayed
 * to it via the D-Bus API.
 *
 * Reconnecting is not currently supported.
 */
class DBusTransportAgent : public TransportAgent
{
    SessionHelper &m_helper;

    /** information about outgoing message, provided by user of this instance */
    std::string m_url;
    std::string m_type;

    /** latest message sent to us */
    SharedBuffer m_incomingMsg;
    std::string m_incomingMsgType;

    /** explanation for problem, sent to us by syncevo-dbus-server */
    std::string m_error;

    /**
     * Current state. Changed by us as messages are sent and received
     * and by syncevo-dbus-server:
     * - connectionState with error -> failed
     * - connectionState without error -> closed
     */
    SessionCommon::ConnectionState m_state;

    void doWait();

 public:
    DBusTransportAgent(SessionHelper &helper);

    void serverAlerted();
    void storeMessage(const GDBusCXX::DBusArray<uint8_t> &buffer,
                      const std::string &type);
    void storeState(const std::string &error);

    virtual void setURL(const std::string &url) { m_url = url; }
    virtual void setContentType(const std::string &type) { m_type = type; }
    virtual void send(const char *data, size_t len);
    virtual void cancel() {}
    virtual void shutdown();
    virtual Status wait(bool noReply = false);
    virtual void setTimeout(int seconds) {}
    virtual void getReply(const char *&data, size_t &len, std::string &contentType);
};

SE_END_CXX

#endif // DBUS_TRANSPORT_AGENT_H
