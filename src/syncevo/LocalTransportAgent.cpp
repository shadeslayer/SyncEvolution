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
#include <syncevo/StringDataBlob.h>
#include <syncevo/IniConfigNode.h>
#include <syncevo/GLibSupport.h>

#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <algorithm>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

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
    m_timeoutSeconds(0),
    m_status(INACTIVE),
    m_messageFD(-1),
    m_statusFD(-1),
    m_pid(0)
{
}

LocalTransportAgent::~LocalTransportAgent()
{
    if (m_statusFD >= 0) {
        close(m_statusFD);
    }
    if (m_messageFD >= 0) {
        close(m_messageFD);
    }
    if (m_pid) {
        SE_LOG_DEBUG(NULL, NULL, "starting to wait for child process %ld in destructor", (long)m_pid);
        int status;
        pid_t res = waitpid(m_pid, &status, 0);
        SE_LOG_DEBUG(NULL, NULL, "child %ld completed, status %d", (long)res, status);
    }
}

void LocalTransportAgent::start()
{
    int sockets[2][2];

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
    if (m_clientContext == m_server->getContextName()) {
        SE_THROW(StringPrintf("invalid local sync inside context '%s', need second context with different databases", context.c_str()));
    }

    memset(sockets, 0, sizeof(sockets));
    try {
        if (socketpair(AF_LOCAL,
                       SOCK_STREAM,
                       0, sockets[0]) ||
            socketpair(AF_LOCAL,
                       SOCK_STREAM,
                       0, sockets[1])) {
            m_server->throwError("socketpair()", errno);
        }

        // Set close-on-exec flag for all file descriptors:
        // they are used for tracking the death of either
        // parent or child, so additional processes should
        // not inherit them.
        //
        // Also set them to non-blocking, needed for the
        // timeout handling.
        for (int *fd = &sockets[0][0];
             fd < &sockets[2][2];
             ++fd) {
            long flags = fcntl(*fd, F_GETFD);
            fcntl(*fd, F_SETFD, flags | FD_CLOEXEC);
            flags = fcntl(*fd, F_GETFL);
            fcntl(*fd, F_SETFL, flags | O_NONBLOCK);
        }

        pid_t pid = fork();
        switch (pid) {
        case -1:
            m_server->throwError("fork()", errno);
            break;
        case 0:
            // child
            Logger::setProcessName(m_clientContext);
            close(sockets[0][0]);
            m_messageFD = sockets[0][1];
            close(sockets[1][0]);
            m_statusFD = sockets[1][1];
            run();
            break;
        default:
            // parent
            close(sockets[0][1]);
            m_messageFD = sockets[0][0];
            close(sockets[1][1]);
            m_statusFD = sockets[1][0];
            // first message must come from child
            m_status = ACTIVE;
            m_pid = pid;
            break;
        }
    } catch (...) {
        for (int *fd = &sockets[0][0];
             fd < &sockets[2][2];
             ++fd) {
            if (*fd) {
                close(*fd);
            }
        }
    }
}

void LocalTransportAgent::run()
{
    // delay the client for debugging purposes
    const char *delay = getenv("SYNCEVOLUTION_LOCAL_CHILD_DELAY");
    if (delay) {
        sleep(atoi(delay));
    }

    // If we did an exec here, we could start with a clean slate.
    // But that forces us to pass all relevant parameters to some
    // specific executable, which is more complicated than simply
    // reading from the existing in-process variables. So let's
    // try without exec, after some clean up.

    // The parent may or may not have installed output redirection.
    // That instance needs to be remembered and flushed before this
    // process may terminate. The parent will also do the same kind of
    // flushing (race condition?!), but it might die before being
    // able to do so.
    //
    // This loop used to remove all other loggers above the
    // redirection, to get rid of the one which writes into the
    // -log.html file of the parent.  This was too coarse and also
    // removed the LoggerStdout which was installed by
    // client-test. Now the logger for -log.html is detected via
    // Logger::isProcessSafe().
    int index = LoggerBase::numLoggers();
    LogRedirect *redirect = NULL;
    bool removing = true;
    --index;
    while (index >= 0 &&
           !(redirect = dynamic_cast<LogRedirect *>(LoggerBase::loggerAt(index)))) {
        if (removing) {
            if (!LoggerBase::loggerAt(index)->isProcessSafe()) {
                LoggerBase::popLogger();
            } else {
                removing = false;
            }
        }
        --index;
    }

    // do not mix our own output into the output of the parent
    if (redirect) {
        redirect->redoRedirect();
    }

    // Ignore parent's timeout and event loop.
    m_timeoutSeconds = 0;
    m_loop = 0;

    // Now run. Under no circumstances must we leave this function,
    // because our caller is not prepared for running inside a forked
    // process.
    int res = 0;
    try {
        SE_LOG_DEBUG(NULL, NULL, "client is running, %s log redirection",
                     redirect ? "with" : "without");
        // TODO: password and abort handling in a derived class
        bool doLogging = m_server->getDoLogging();
        SyncContext client(std::string("source-config") + m_clientContext,
                           m_server->getConfigName(),
                           m_server->getRootPath() + "/." + m_clientContext,
                           boost::shared_ptr<TransportAgent>(this, NoopAgentDestructor()),
                           doLogging);

        // Apply temporary config filters, stored for us in m_server by the
        // command line.
        const FullProps &props = m_server->getConfigProps();
        client.setConfigFilter(true, "", props.createSyncFilter(client.getConfigName()));
        BOOST_FOREACH(const string &sourceName, m_server->getSyncSources()) {
            client.setConfigFilter(false, sourceName, props.createSourceFilter(client.getConfigName(), sourceName));
        }

        // Copy non-empty credentials from main config, because
        // that is where the GUI knows how to store them. A better
        // solution would be to require that credentials are in the
        // "source-config" config.
        string tmp = m_server->getSyncUsername();
        if (!tmp.empty()) {
            client.setSyncUsername(tmp, true);
        }
        tmp = m_server->getSyncPassword();
        if (!tmp.empty()) {
            client.setSyncPassword(tmp, true);
        }

        // debugging mode: write logs inside sub-directory of parent,
        // otherwise use normal log settings
        if (!doLogging) {
            client.setLogDir(std::string(m_server->getLogDir()) + "/child", true);
        }

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
        client.sync(&m_clientReport);
    } catch(...) {
        SyncMLStatus status = m_clientReport.getStatus();
        Exception::handle(&status, redirect);
        m_clientReport.setStatus(status);
    }

    // send final report
    try {
        if (m_messageFD >= 0) {
            close(m_messageFD);
            m_messageFD = -1;
        }

        // matches parent's code in shutdown()
        boost::shared_ptr<std::string> data(new std::string);
        boost::shared_ptr<StringDataBlob> dump(new StringDataBlob("buffer", data, false));
        IniHashConfigNode node(dump);
        node << m_clientReport;
        SE_LOG_DEBUG(NULL, NULL, "client: sending report (%s/ERROR '%s'):\n%s",
                     Status2String(m_clientReport.getStatus()).c_str(),
                     m_clientReport.getError().c_str(),
                     data->c_str());
        node.flush();
        writeMessage(m_statusFD, Message::MSG_SYNC_REPORT, data->c_str(), data->size(), Timespec());
    } catch (...) {
        SyncMLStatus status = m_clientReport.getStatus();
        Exception::handle(&status, redirect);
        m_clientReport.setStatus(status);
    }

    if (redirect) {
        redirect->flush();
    }
    _exit(res);
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
    if (m_messageFD) {
        // close message transports, tells peer to shut down
        close(m_messageFD);
        m_messageFD = -1;
    }

    if (m_pid) {
        // parent: receive SyncReport
        receiveChildReport();
 
        // join forked process
        int status;
        SE_LOG_DEBUG(NULL, NULL, "starting to wait for child process %ld in shutdown()", (long)m_pid);
        pid_t res = waitpid(m_pid, &status, 0);
        SE_LOG_DEBUG(NULL, NULL, "child %ld completed, status %d", (long)res, status);
        m_pid = 0;

        // now relay the result from our child, will be added to
        // our own sync report if it doesn't have an error already
        checkChildReport();
    } else {
        // child: sends SyncReport at the end of run()
    }
}

void LocalTransportAgent::receiveChildReport()
{
    // don't try this again
    if (m_statusFD >= 0) {
        int statusFD = m_statusFD;
        m_statusFD = -1;
        try {
            SE_LOG_DEBUG(NULL, NULL, "parent: receiving report");
            m_receiveBuffer.m_used = 0;
            if (readMessage(statusFD, m_receiveBuffer, deadline()) == ACTIVE) {
                boost::shared_ptr<std::string> data(new std::string);
                data->assign(m_receiveBuffer.m_message->m_data, m_receiveBuffer.m_message->getDataLength());
                boost::shared_ptr<StringDataBlob> dump(new StringDataBlob("buffer", data, false));
                IniHashConfigNode node(dump);
                node >> m_clientReport;
                SE_LOG_DEBUG(NULL, NULL, "parent: received report (%s/ERROR '%s'):\n%s",
                             Status2String(m_clientReport.getStatus()).c_str(),
                             m_clientReport.getError().c_str(),
                             data->c_str());
            } else {
                SE_LOG_DEBUG(NULL, NULL, "parent: timeout receiving report");
            }
        } catch (...) {
            close(statusFD);
            throw;
        }
        close(statusFD);
    }
}

void LocalTransportAgent::checkChildReport()
{
    std::string childError = "child process failed";
    if (!m_clientReport.getError().empty()) {
        childError += ": ";
        childError += m_clientReport.getError();
        SE_LOG_ERROR(NULL, NULL, "%s", childError.c_str());
    }
    if (m_clientReport.getStatus() != STATUS_HTTP_OK &&
        m_clientReport.getStatus() != STATUS_OK) {
        SE_THROW_EXCEPTION_STATUS(TransportStatusException,
                                  childError,
                                  m_clientReport.getStatus());
    }
}

bool LocalTransportAgent::Buffer::haveMessage()
{
    bool complete = m_used >= sizeof(Message) &&
        m_message->m_length <= m_used;
    SE_LOG_DEBUG(NULL, NULL, "message of size %ld/%ld/%ld, %s",
                 (long)m_used,
                 m_used < sizeof(Message) ? -1l : (long)m_message->m_length,
                 (long)m_size,
                 complete ? "complete" : "incomplete");
    return complete;
}

void LocalTransportAgent::send(const char *data, size_t len)
{
    m_status = ACTIVE;
    // first throw away old received message
    if (m_receiveBuffer.haveMessage()) {
        size_t len = m_receiveBuffer.m_message->m_length;
        // memmove() probably never necessary because receiving
        // ends directly after complete message, but doesn't hurt
        // either...
        memmove(m_receiveBuffer.m_message.get(),
                (char *)m_receiveBuffer.m_message.get() + len,
                m_receiveBuffer.m_used - len);
        m_receiveBuffer.m_used -= len;
    }
    m_status = writeMessage(m_messageFD, m_sendType, data, len, deadline());
}

TransportAgent::Status LocalTransportAgent::writeMessage(int fd, Message::Type type, const char *data, size_t len, Timespec deadline)
{
    Message header;
    header.m_type = type;
    header.m_length = sizeof(Message) + len;
    struct iovec vec[2];
    vec[0].iov_base = &header;
    vec[0].iov_len = offsetof(Message, m_data);
    vec[1].iov_base = (void *)data;
    vec[1].iov_len = len;
    SE_LOG_DEBUG(NULL, NULL, "%s: sending %ld bytes via %s",
                 m_pid ? "parent" : "child",
                 (long)len,
                 fd == m_messageFD ? "message channel" : "other channel");
    do {
        // sleep, possibly with a deadline
        int res = 0;
        Timespec timeout;
        if (deadline) {
            Timespec now = Timespec::monotonic();
            if (now >= deadline) {
                return TIME_OUT;
            } else {
                timeout = deadline - now;
            }
        }
        SE_LOG_DEBUG(NULL, NULL, "%s: write select on %s %ld.%09lds",
                     m_pid ? "parent" : "child",
                     fd == m_messageFD ? "message channel" : "other channel",
                     (long)timeout.tv_sec,
                     (long)timeout.tv_nsec);
        if (m_loop) {
            switch (GLibSelect(m_loop, fd, GLIB_SELECT_WRITE, timeout ? &timeout : NULL)) {
            case GLIB_SELECT_TIMEOUT:
                res = 0;
                break;
            case GLIB_SELECT_READY:
                res = 1;
                break;
            case GLIB_SELECT_QUIT:
                SE_LOG_DEBUG(NULL, NULL, "quit transport as requested as part of GLib event loop");
                return FAILED;
                break;
            }
        } else {
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(fd, &writefds);
            timeval tv = timeout;
            res = select(fd + 1, NULL, &writefds, NULL,
                         timeout ? &tv : NULL);
        }
        switch (res) {
        case 0:
            SE_LOG_DEBUG(NULL, NULL, "%s: select timeout",
                         m_pid ? "parent" : "child");
            return TIME_OUT;
            break;
        case 1: {
            ssize_t sent = writev(fd, vec, 2);
            if (sent == -1) {
                SE_LOG_DEBUG(NULL, NULL, "%s: sending %ld bytes failed: %s",
                             m_pid ? "parent" : "child",
                             (long)len,
                             strerror(errno));
                SE_THROW_EXCEPTION(TransportException,
                                   StringPrintf("writev(): %s", strerror(errno)));
            }

            // potential partial write, reduce byte counters by amount of bytes sent
            ssize_t part1 = std::min((ssize_t)vec[0].iov_len, sent);
            vec[0].iov_len -= part1;
            sent -= part1;
            ssize_t part2 = std::min((ssize_t)vec[1].iov_len, sent);
            vec[1].iov_len -= part2;
            sent -= part2;
            break;
        }
        default:
            SE_LOG_DEBUG(NULL, NULL, "%s: select errror: %s",
                         m_pid ? "parent" : "child",
                         strerror(errno));
            SE_THROW_EXCEPTION(TransportException,
                               StringPrintf("select(): %s", strerror(errno)));
            break;
        }
    } while (vec[1].iov_len);

    SE_LOG_DEBUG(NULL, NULL, "%s: sending %ld bytes done",
                 m_pid ? "parent" : "child",
                 (long)len);
    return ACTIVE;
}

void LocalTransportAgent::cancel()
{
}

TransportAgent::Status LocalTransportAgent::wait(bool noReply)
{
    if (m_status == ACTIVE) {
        // need next message; for noReply == true we are done
        if (noReply) {
            m_status = INACTIVE;
        } else {
            if (!m_receiveBuffer.haveMessage()) {
                m_status = readMessage(m_messageFD, m_receiveBuffer, deadline());
                if (m_status == ACTIVE) {
                    // complete message received, check if it is SyncML
                    switch (m_receiveBuffer.m_message->m_type) {
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
        }
    }
    return m_status;
}

TransportAgent::Status LocalTransportAgent::readMessage(int fd, Buffer &buffer, Timespec deadline)
{
    while (!buffer.haveMessage()) {
        int res = 0;
        Timespec timeout;
        if (deadline) {
            Timespec now = Timespec::monotonic();
            if (now >= deadline) {
                // already too late
                return TIME_OUT;
            } else {
                timeout = deadline - now;
            }
        }
        SE_LOG_DEBUG(NULL, NULL, "%s: read select on %s %ld.%09lds",
                     m_pid ? "parent" : "child",
                     fd == m_messageFD ? "message channel" : "other channel",
                     (long)timeout.tv_sec,
                     (long)timeout.tv_nsec);
        if (m_loop) {
            switch (GLibSelect(m_loop, fd, GLIB_SELECT_READ, timeout ? &timeout : NULL)) {
            case GLIB_SELECT_TIMEOUT:
                res = 0;
                break;
            case GLIB_SELECT_READY:
                res = 1;
                break;
            case GLIB_SELECT_QUIT:
                SE_LOG_DEBUG(NULL, NULL, "quit transport as requested as part of GLib event loop");
                return FAILED;
                break;
            }
        } else {
            // use select to implement timeout
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(fd, &readfds);
            timeval tv = timeout;
            res = select(fd + 1, &readfds, NULL, NULL,
                         timeout ? &tv : NULL);
        }
        switch (res) {
        case 0:
            SE_LOG_DEBUG(NULL, NULL, "%s: select timeout",
                         m_pid ? "parent" : "child");
            return TIME_OUT;
            break;
        case 1: {
            // data ready, ensure that buffer is available
            if (!buffer.m_size) {
                buffer.m_size = m_server->getMaxMsgSize();
                if (!buffer.m_size) {
                    buffer.m_size = 1024;
                }
                buffer.m_message.set(static_cast<Message *>(malloc(buffer.m_size)),
                                     "Message Buffer");
            } else if (buffer.m_used >= sizeof(Message) &&
                       buffer.m_message->m_length > buffer.m_size) {
                buffer.m_message.set(static_cast<Message *>(realloc(buffer.m_message.release(), buffer.m_message->m_length)),
                                     "Message Buffer");
                buffer.m_size = buffer.m_message->m_length;
            }
            SE_LOG_DEBUG(NULL, NULL, "%s: recv %ld bytes",
                         m_pid ? "parent" : "child",
                         (long)(buffer.m_size - buffer.m_used));
            ssize_t recvd = recv(fd,
                                 (char *)buffer.m_message.get() + buffer.m_used,
                                 buffer.m_size - buffer.m_used,
                                 MSG_DONTWAIT);
            SE_LOG_DEBUG(NULL, NULL, "%s: received %ld: %s",
                         m_pid ? "parent" : "child",
                         (long)recvd,
                         recvd < 0 ? strerror(errno) : "okay");
            if (recvd < 0) {
                SE_THROW_EXCEPTION(TransportException,
                                   StringPrintf("message receive: %s", strerror(errno)));
            } else if (!recvd) {
                if (m_pid) {
                    // Child died. Try to get its sync report to find out why.
                    receiveChildReport();
                    checkChildReport();
                    // if no exception yet, raise a generic one
                    SE_THROW_EXCEPTION(TransportException,
                                       "child has died unexpectedly");                    
                } else {
                    SE_THROW_EXCEPTION(TransportException,
                                       "parent has died unexpectedly");
                }
            }
            buffer.m_used += recvd;
            break;
        }
        default:
            SE_LOG_DEBUG(NULL, NULL, "%s: select errror: %s",
                         m_pid ? "parent" : "child",
                         strerror(errno));
            SE_THROW_EXCEPTION(TransportException,
                               StringPrintf("select(): %s", strerror(errno)));
            break;
        }
    }

    return ACTIVE;
}

void LocalTransportAgent::getReply(const char *&data, size_t &len, std::string &contentType)
{
    if (m_status != GOT_REPLY) {
        SE_THROW("internal error, no reply available");
    }
    switch (m_receiveBuffer.m_message->m_type) {
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
        len = m_receiveBuffer.m_message->getDataLength();
        data = m_receiveBuffer.m_message->m_data;
    }
}

void LocalTransportAgent::setTimeout(int seconds)
{
    m_timeoutSeconds = seconds;
}

SE_END_CXX
