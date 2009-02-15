/*
 * Copyright (C) 2009 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef INCL_TRANSPORTAGENT
#define INCL_TRANSPORTAGENT

#include <string>

namespace SyncEvolution {

/**
 * Abstract API for a message send/receive agent.
 *
 * The calling sequence is as follows:
 * - set parameters for next message
 * - start message send
 * - optional: cancel transmission
 * - wait for completion and reply
 *
 * Data to be sent is owned by caller. Data received as reply is
 * allocated and owned by agent. Errors are reported via exceptions.
 */
class TransportAgent
{
 public:
    /**
     * set transport specific URL of next message
     */
    virtual void setURL(const std::string &url) = 0;

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
     * define content type for post, see content type constants
     */
    virtual void setContentType(const std::string &type) = 0;

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
         * message is being sent or reply received,
         * check again with wait()
         */
        ACTIVE,
        /**
         * received and buffered complete reply,
         * get acces to it with getReponse()
         */
        GOT_REPLY,
        /**
         * message wasn't sent, try again with send()
         */
        CANCELED,
        /**
         * sending message has failed
         */
        FAILED,
        /**
         * unused transport, configure and use send()
         */
        INACTIVE
    };

    /**
     * wait for reply
     *
     * Returns immediately if no transmission is pending.
     */
    virtual Status wait() = 0;

    /**
     * provides access to reply data
     *
     * Memory pointer remains valid as long as
     * transport agent is not deleted and no other
     * message is sent.
     */
    virtual void getReply(const char *&data, size_t &len) = 0;

    /** SyncML in XML format */
    static const char * const m_contentTypeSyncML;

    /** SyncML in WBXML format */
    static const char * const m_contentTypeSyncWBXML;

    /** normal HTTP URL encoded */
    static const char * const m_contentTypeURLEncoded;
};

} // namespace SyncEvolution

#endif // INCL_TRANSPORTAGENT
