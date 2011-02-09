/*
 * Copyright (C) 2010 Patrick Ohly
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

#ifndef INCL_LOCALTRANSPORTAGENT
#define INCL_LOCALTRANSPORTAGENT

#include "config.h"
#include <syncevo/TransportAgent.h>
#include <syncevo/SyncML.h>
#include <syncevo/SmartPtr.h>
#include <string>

#ifdef HAVE_GLIB
#include <glib/gmain.h>
#else
typedef void *GMainLoop;
#endif

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class SyncContext;

/**
 * Variable-sized message, used for both request and reply.
 * Allocated with malloc/realloc().
 */
struct Message
{
    /** determines which kind of data follows after m_length */
    enum Type {
        MSG_SYNCML_XML,
        MSG_SYNCML_WBXML,
        MSG_PASSWORD_REQUEST,
        MSG_PASSWORD_RESPONSE,
        MSG_SYNC_REPORT
    } m_type;

    /** length including header */
    size_t m_length;

    /** payload */
    char m_data[0];

    /** length excluding header */
    size_t getDataLength() const { return m_length - offsetof(Message, m_data); }
};


/**
 * message send/receive with a forked process as peer
 *
 * Uses pipes to send a message and then get the response.
 * Limited to server forking the client. Because the client
 * has access to the full server setup after the fork,
 * no SAN message is needed and the first message goes
 * from client to server.
 *
 * Most messages will be SyncML message and response. In addition,
 * password requests also need to be passed through the server via
 * dedicated messages, because it is the one with a UI.
 */
class LocalTransportAgent : public TransportAgent
{
 public:
    /**
     * @param server          the server side of the sync;
     *                        must remain valid while transport exists
     * @param clientContext   name of the context which contains the client's
     *                        sources, must start with @ sign
     * @param loop            optional glib loop to use when waiting for IO;
     *                        transport will *not* increase the reference count
     */
    LocalTransportAgent(SyncContext *server,
                        const std::string &clientContext,
                        void *loop = NULL);
    ~LocalTransportAgent();

    /**
     * Set up message passing and fork the client.
     */
    void start();

    /**
     * Copies the client's sync report. If the client terminated
     * unexpectedly or shutdown() hasn't completed yet, the
     * STATUS_DIED_PREMATURELY sync result code will be set.
     */
    void getClientSyncReport(SyncReport &report);

    // TransportAgent implementation
    virtual void setURL(const std::string &url) {}
    virtual void setContentType(const std::string &type);
    virtual void shutdown();
    virtual void send(const char *data, size_t len);
    virtual void cancel();
    virtual Status wait(bool noReply = false);
    virtual void getReply(const char *&data, size_t &len, std::string &contentType);
    virtual void setTimeout(int seconds);

 private:
    SyncContext *m_server;
    string m_clientContext;
    GMainLoop *m_loop;
    int m_timeoutSeconds;
    time_t m_sendStartTime;

    Status m_status;

    /** type of message for next send() */
    Message::Type m_sendType;

    /** buffer for message, with total length of buffer as size */
    class Buffer {
    public:
    Buffer() :
        m_size(0),
        m_used(0)
        {}    
        /** actual message */
        SmartPtr<Message *, Message *, UnrefFree<Message> > m_message;
        /** number of allocated bytes */
        size_t m_size;
        /** number of valid bytes in buffer */
        size_t m_used;

        bool haveMessage();
    } m_receiveBuffer;

    /**
     * Read/write stream socket for sending/receiving messages.
     * Data is sent as struct Message (includes type and length),
     * with per-type payload.
     */
    int m_messageFD;

    /**
     * Second read/write stream socket for transferring final
     * status. Same communication method as for m_messageFD.
     * Necessary because the regular communication
     * channel needs to be closed in case of a failure, to
     * notify the peer.
     */
    int m_statusFD;

    /** 0 in client, child PID in server */
    pid_t m_pid;

    SyncReport m_clientReport;

    void run();

    /**
     * Write Message with given type into file descriptor.
     * Retries until error or all data written.
     *
     * @return true for success, false if deadline is reached, exception for fatal error
     */
    bool writeMessage(int fd, Message::Type type, const char *data, size_t len, time_t deadline);

    /**
     * Read bytes into buffer until complete Message
     * is assembled. Will read additional bytes beyond
     * end of that Message if available. An existing
     * complete message is not overwritten.
     *
     * @return true for success, false if deadline is reached, exception for fatal error
     */
    bool readMessage(int fd, Buffer &buffer, time_t deadline);

    /** utility function for parent: copy child's report into m_clientReport */
    void receiveChildReport();

    /** utility function for parent: check m_clientReport and log/throw errors */
    void checkChildReport();

    /** utility function: calculate deadline for operation starting now */
    time_t deadline() { return m_timeoutSeconds ? (time(NULL) + m_timeoutSeconds) : 0; }
};

SE_END_CXX

#endif // INCL_LOCALTRANSPORTAGENT
