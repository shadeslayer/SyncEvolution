/*
 * Copyright (C) 2009 Intel Corporation
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

#ifndef INCL_TRANSPORTAGENT
#define INCL_TRANSPORTAGENT

#include <string>
#include <syncevo/util.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class SyncConfig;

/**
 * Abstract API for a message send/receive agent.
 *
 * The calling sequence is as follows:
 * - set parameters for next message
 * - start message send
 * - optional: cancel transmission
 * - wait for completion and the optional reply
 * - close
 * - wait for completion of the shutdown
 *
 * Data to be sent is owned by caller. Data received as reply is
 * allocated and owned by agent. Errors are reported via
 * TransportAgentException.
 */
class TransportAgent
{
 public:
    /**
     * set transport specific URL of next message
     */
    virtual void setURL(const std::string &url) = 0;

    /**
     * define content type for post, see content type constants
     */
    virtual void setContentType(const std::string &type) = 0;

    /**
     * Requests an normal shutdown of the transport. This can take a
     * while, for example if communication is still pending.
     * Therefore wait() has to be called to ensure that the
     * shutdown is complete and that no error occurred.
     *
     * Simply deleting the transport is an *unnormal* shutdown that
     * does not communicate with the peer.
     */
    virtual void shutdown() = 0;

    /**
     * start sending message
     *
     * Memory must remain valid until reply is received or
     * message transmission is canceled.
     *
     * @param data      start address of data to send
     * @param len       number of bytes
     */
    virtual void send(const char *data, size_t len) = 0;

    /**
     * cancel an active message transmission
     *
     * Blocks until send buffer is no longer in use.
     * Returns immediately if nothing pending.
     */
    virtual void cancel() = 0;

    enum Status {
        /**
         * operation is on-going, check again with wait()
         */
        ACTIVE,
        /**
         * received and buffered complete reply,
         * get access to it with getReponse()
         */
        GOT_REPLY,
        /**
         * message wasn't sent, try again with send()
         */
        CANCELED,
        /**
         * sending message has failed, transport should not throw an exception
         * if the error is recoverable (such as a temporary network error)
         */
        FAILED,
        /**
         * transport was closed normally without error
         */
        CLOSED,
        /**
         * transport timeout
         */
        TIME_OUT,
        /**
         * unused transport, configure and use send()
         */
        INACTIVE
    };

    /**
     * Wait for completion of an operation initiated earlier.
     * The operation can be a send with optional reply or
     * a close request.
     *
     * Returns immediately if no operations is pending.
     *
     * @param noReply    true if no reply is required for a running send;
     *                   only relevant for transports used by a SyncML server
     */
    virtual Status wait(bool noReply = false) = 0;

    /**
     * Tells the transport agent to stop the transmission the given
     * amount of seconds after send() was called. The transport agent
     * will then stop the message transmission and return a TIME_OUT
     * status in wait().
     *
     * @param seconds      number of seconds to wait before timing out, zero for no timeout
     */
    virtual void setTimeout(int seconds) = 0;

    /**
     * provides access to reply data
     *
     * Memory pointer remains valid as long as
     * transport agent is not deleted and no other
     * message is sent.
     */
    virtual void getReply(const char *&data, size_t &len, std::string &contentType) = 0;

    /** SyncML in XML format */
    static const char * const m_contentTypeSyncML;

    /** SyncML in WBXML format */
    static const char * const m_contentTypeSyncWBXML;

    /** normal HTTP URL encoded */
    static const char * const m_contentTypeURLEncoded;

    /** binary Server Alerted Notification (SAN) for data sync */
    static const char * const m_contentTypeServerAlertedNotificationDS;
};

class HTTPTransportAgent : public TransportAgent
{
 public:
    /**
     * set proxy for transport, in protocol://[user@]host[:port] format
     */
    virtual void setProxy(const std::string &proxy) = 0;

    /**
     * set proxy user name (if not specified in proxy string)
     * and password
     */
    virtual void setProxyAuth(const std::string &user,
                              const std::string &password) = 0;

    /**
     * control how SSL certificates are checked
     *
     * @param cacerts         path to a single CA certificate file
     * @param verifyServer    enable server verification (should always be on)
     * @param verifyHost      do strict hostname checking in the certificate
     */
    virtual void setSSL(const std::string &cacerts,
                        bool verifyServer,
                        bool verifyHost) = 0;

    /**
     * override default user agent string
     */
    virtual void setUserAgent(const std::string &agent) = 0;

    /**
     * convenience method which copies the HTTP settings from
     * SyncConfig
     */
    void setConfig(SyncConfig &config);
};

SE_END_CXX
#endif // INCL_TRANSPORTAGENT
