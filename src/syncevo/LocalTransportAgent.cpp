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

#include <syncevo/LocalTransportAgent.h>
#include <syncevo/SyncContext.h>
#include <syncevo/SyncML.h>
#include <syncevo/LogRedirect.h>

#include <stddef.h>
#include <sys/socket.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * SyncML message data. Header followed directly by data.
 */
struct SyncMLMessage
{
    Message m_message;
    char m_data[0];
};

class NoopAgentDestructor
{
public:
    void operator () (TransportAgent *agent) throw() {}
};

LocalTransportAgent::LocalTransportAgent(SyncContext *server,
                                         const std::string &clientContext,
                                         void *loop) :
    m_server(server),
    m_clientContext(SyncConfig::normalizeConfigString(clientContext)),
    m_loop(static_cast<GMainLoop *>(loop)),
    m_status(INACTIVE),
    m_receiveBufferSize(0),
    m_receivedBytes(0)
{
}

LocalTransportAgent::~LocalTransportAgent()
{
}

void LocalTransportAgent::start()
{
    int sockets[2];

    // compare normalized context names to detect forbidden sync
    // within the same context; they could be set up, but are more
    // likely configuration mistakes
    string peer, context;
    SyncConfig::splitConfigString(m_clientContext, peer, context);
    if (!peer.empty()) {
        SE_THROW(StringPrintf("invalid local sync URL: '%s' references a peer config, should point to a context like @%s instead",
                              m_clientContext.c_str(),
                              context.c_str()));
    }
    SyncConfig::splitConfigString(m_server->getConfigName(),
                                  peer, context);
    if (m_clientContext == string("@") + context) {
        SE_THROW(StringPrintf("invalid local sync inside context '%s', need second context with different databases", context.c_str()));
    }

    if (socketpair(AF_LOCAL,
                   SOCK_STREAM,
                   0, sockets)) {
        m_server->throwError("socketpair()", errno);
    }

    pid_t pid = fork();
    switch (pid) {
    case -1:
        m_server->throwError("fork()", errno);
        break;
    case 0:
        // child
        close(sockets[0]);
        m_messageFD = sockets[1];
        run();
        break;
    default:
        // parent
        close(sockets[1]);
        m_messageFD = sockets[0];
        // first message must come from child
        m_status = ACTIVE;
        break;
    }
}

void LocalTransportAgent::run()
{
    // If we did an exec here, we could start with a clean slate.
    // But that forces us to pass all relevant parameters to some
    // specific executable, which is more complicated than simply
    // reading from the existing in-process variables. So let's
    // try without exec, after some clean up.

    // Remove writing into the parent's log file => implemented as
    // removing every logger until we run into the parents LogRedirect
    // instance. That instance needs to be remembered and flushed
    // before this process may terminate.
    int index = LoggerBase::numLoggers();
    LogRedirect *redirect = NULL;
    --index;
    while (index >= 0 &&
           !(redirect = dynamic_cast<LogRedirect *>(LoggerBase::loggerAt(index)))) {
        LoggerBase::popLogger();
        --index;
    }

    // Now run. Under no circumstances must we leave this function,
    // because our caller is not prepared for running inside a forked
    // process.
    int res = 0;
    try {
        SE_LOG_DEBUG(NULL, NULL, "client is running");
        // TODO: password and abort handling in a derived class
        SyncContext client(std::string("source-config") + m_clientContext,
                           m_server->getRootPath() + "/." + m_clientContext,
                           boost::shared_ptr<TransportAgent>(this, NoopAgentDestructor()),
                           true);

        // disable all sources temporarily, will be enabled by next loop
        BOOST_FOREACH(const string &targetName, client.getSyncSources()) {
            SyncSourceNodes targetNodes = client.getSyncSourceNodes(targetName);
            SyncSourceConfig targetSource(targetName, targetNodes);
            targetSource.setSync("disabled", true);
        }

        // activate all sources in client targeted by main config,
        // with right uri
        BOOST_FOREACH(const string &sourceName, m_server->getSyncSources()) {
            SyncSourceNodes nodes = m_server->getSyncSourceNodesNoTracking(sourceName);
            SyncSourceConfig source(sourceName, nodes);
            string sync = source.getSync();
            if (sync != "disabled") {
                string targetName = source.getURI();
                SyncSourceNodes targetNodes = client.getSyncSourceNodes(targetName);
                SyncSourceConfig targetSource(targetName, targetNodes);
                string fullTargetName = m_clientContext + "/" + targetName;

                if (!targetNodes.dataConfigExists()) {
                    client.throwError(StringPrintf("%s: source not configured",
                                                   fullTargetName.c_str()));

                }

                // All of the config setting is done as volatile,
                // so none of the regular config nodes have to
                // be written. If a sync mode was set, it must have been
                // done before in this loop => error in original config.
                if (string(targetSource.getSync()) != "disabled") {
                    client.throwError(StringPrintf("%s: source targetted twice by %s",
                                                   fullTargetName.c_str(),
                                                   m_clientContext.c_str()));
                }
                targetSource.setSync(sync.c_str(), true);
                targetSource.setURI(sourceName.c_str(), true);
            }
        }

        // now sync
        client.setLogLevel(m_server->getLogLevel(), true);
        client.setPrintChanges(m_server->getPrintChanges(), true);
        client.sync(&m_clientReport);
    } catch(...) {
        SyncMLStatus status = m_clientReport.getStatus();
        Exception::handle(&status, redirect);
        m_clientReport.setStatus(status);
    }

    if (redirect) {
        redirect->flush();
    }
    exit(res);
}

void LocalTransportAgent::getClientSyncReport(SyncReport &report)
{
    report = m_clientReport;
}

void LocalTransportAgent::setContentType(const std::string &type)
{
    if (type == m_contentTypeSyncML) {
        m_sendType = Message::MSG_SYNCML_XML;
    } else if (type == m_contentTypeSyncWBXML) {
        m_sendType = Message::MSG_SYNCML_WBXML;
    } else {
        SE_THROW(string("unsupported content type: ") + type);
    }
}

void LocalTransportAgent::shutdown()
{
    if (m_pid) {
        // TODO: get sync report, then wait for exit in child.
    }
    close(m_messageFD);
}

void LocalTransportAgent::send(const char *data, size_t len)
{
    if (m_loop) {
        SE_THROW("glib support not implemented");
    } else {
        // first throw away old received message
        if (m_receivedBytes >= sizeof(Message) &&
            m_receiveBuffer->m_length <= m_receivedBytes) {
            size_t len = m_receiveBuffer->m_length;
            // memmove() probably never necessary because receiving
            // ends directly after complete message, but doesn't hurt
            // either...
            memmove(m_receiveBuffer.get(),
                    (char *)m_receiveBuffer.get() + len,
                    m_receivedBytes - len);
            m_receivedBytes -= len;
        }

        SyncMLMessage header;
        header.m_message.m_type = m_sendType;
        header.m_message.m_length = sizeof(Message) + len;
        struct iovec vec[2];
        vec[0].iov_base = &header;
        vec[0].iov_len = offsetof(SyncMLMessage, m_data);
        vec[1].iov_base = (void *)data;
        vec[1].iov_len = len;
        // TODO: handle timeouts and aborts while writing
        if (writev(m_messageFD, vec, 2) == -1) {
            SE_THROW_EXCEPTION(TransportException,
                               StringPrintf("writev(): %s", strerror(errno)));
        }
    }
    m_status = ACTIVE;
}

void LocalTransportAgent::cancel()
{
}

TransportAgent::Status LocalTransportAgent::wait(bool noReply)
{
    switch (m_status) {
    case ACTIVE:
        // need next message; for noReply == true, it is the SyncReport (TODO)
        if (noReply) {
            // TODO: remove this code and send SyncReport as last message in client
            m_status = INACTIVE;
            return m_status;
        }
        if (m_loop) {
            SE_THROW("glib support not implemented");
        } else {
            while (m_status == ACTIVE &&
                   (!m_receiveBufferSize ||
                    m_receiveBufferSize < sizeof(Message) ||
                    m_receivedBytes < m_receiveBuffer->m_length)) {
                // use select to implement timeout (TODO)
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(m_messageFD, &readfds);
                int res = select(m_messageFD + 1, &readfds, NULL, NULL, NULL);
                switch (res) {
                case 0:
                    // TODO: timeout
                    SE_THROW("internal error, unexpected timeout");
                    break;
                case 1: {
                    // data ready, ensure that buffer is available
                    if (!m_receiveBufferSize) {
                        m_receiveBufferSize = m_server->getMaxMsgSize();
                        m_receiveBuffer.set(static_cast<Message *>(malloc(m_receiveBufferSize)),
                                            "Message Buffer");
                    } else if (m_receivedBytes >= sizeof(Message) &&
                               m_receiveBuffer->m_length > m_receiveBufferSize) {
                        m_receiveBuffer.set(static_cast<Message *>(realloc(m_receiveBuffer.release(), m_receiveBuffer->m_length)),
                                            "Message Buffer");
                    }
                    ssize_t recvd = recv(m_messageFD,
                                         (char *)m_receiveBuffer.get() + m_receivedBytes,
                                         m_receiveBufferSize - m_receivedBytes,
                                         MSG_DONTWAIT);
                    if (recvd < 0) {
                        SE_THROW_EXCEPTION(TransportException,
                                           StringPrintf("message receive: %s", strerror(errno)));
                    } else if (!recvd) {
                        SE_THROW_EXCEPTION(TransportException,
                                           "client has died unexpectedly");
                    }
                    m_receivedBytes += recvd;
                    break;
                }
                default:
                    SE_THROW_EXCEPTION(TransportException,
                                       StringPrintf("select(): %s", strerror(errno)));
                    break;
                }
            }
            if (m_status == ACTIVE) {
                // complete message received, check if it is SyncML
                switch (m_receiveBuffer->m_type) {
                case Message::MSG_SYNCML_XML:
                case Message::MSG_SYNCML_WBXML:
                    m_status = GOT_REPLY;
                    break;
                default:
                    // TODO: handle other types
                    SE_THROW("unsupported message type");
                    break;
                }
            }
        }
        break;
    default:
        return m_status;
    }
    return m_status;
}

void LocalTransportAgent::getReply(const char *&data, size_t &len, std::string &contentType)
{
    if (m_status != GOT_REPLY) {
        SE_THROW("internal error, no reply available");
    }
    switch (m_receiveBuffer->m_type) {
    case Message::MSG_SYNCML_XML:
        contentType = m_contentTypeSyncML;
        break;
    case Message::MSG_SYNCML_WBXML:
        contentType = m_contentTypeSyncWBXML;
        break;
    default:
        contentType = "";
        SE_THROW("internal error, no the right message");
        break;
    }
    if (!contentType.empty()) {
        SyncMLMessage *msg = reinterpret_cast<SyncMLMessage *>(m_receiveBuffer.get());
        len = m_receiveBuffer->m_length - offsetof(SyncMLMessage, m_data);
        data = msg->m_data;
    }
}

void LocalTransportAgent::setCallback (TransportCallback cb, void * udata, int interval)
{
    // TODO: implement timeout mechanism
}

SE_END_CXX
