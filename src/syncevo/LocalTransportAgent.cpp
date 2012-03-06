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

namespace GDBusCXX {
    /**
     * Actual content is a std::map, so serialization can be done using that.
     * We only have to ensure that instances and parameters use FullProps.
     */
    template <> struct dbus_traits<SyncEvo::FullProps> :
        public dbus_traits < std::map<std::string, SyncEvo::ContextProps, SyncEvo::Nocase<std::string> > >
    {
        typedef SyncEvo::FullProps host_type;
        typedef const SyncEvo::FullProps &arg_type;
    };

    /**
     * Similar to SyncEvo::FullProps.
     */
    template <> struct dbus_traits<SyncEvo::SourceProps> :
        public dbus_traits < std::map<std::string, SyncEvo::ConfigProps, SyncEvo::Nocase<std::string> > >
    {
        typedef SyncEvo::SourceProps host_type;
        typedef const SyncEvo::SourceProps &arg_type;
    };
    template <> struct dbus_traits<SyncEvo::ConfigProps> :
        public dbus_traits < std::map<std::string, std::string, SyncEvo::Nocase<std::string> > >
    {
        typedef SyncEvo::ConfigProps host_type;
        typedef const SyncEvo::ConfigProps &arg_type;
    };



    /**
     * a struct containing ConfigProps + SourceProps
     */
    template <> struct dbus_traits<SyncEvo::ContextProps> :
        public dbus_struct_traits<SyncEvo::ContextProps,
                                  GDBusCXX::dbus_member<SyncEvo::ContextProps, SyncEvo::ConfigProps, &SyncEvo::ContextProps::m_syncProps,
                                                        GDBusCXX::dbus_member_single<SyncEvo::ContextProps, SyncEvo::SourceProps, &SyncEvo::ContextProps::m_sourceProps> > >
    {};

    /**
     * a struct containing various strings and an integer
     */
    template <> struct dbus_traits<SyncEvo::ConfigPasswordKey> :
        public dbus_struct_traits<SyncEvo::ConfigPasswordKey,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::user,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::server,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::domain,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::object,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::protocol,
                                  GDBusCXX::dbus_member<SyncEvo::ConfigPasswordKey, std::string, &SyncEvo::ConfigPasswordKey::authtype,
                                  GDBusCXX::dbus_member_single<SyncEvo::ConfigPasswordKey, unsigned int, &SyncEvo::ConfigPasswordKey::port> > > > > > > >
    {};
}

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
    m_status(INACTIVE),
    m_loop(loop ?
           GMainLoopCXX(static_cast<GMainLoop *>(loop)) /* increase reference */ :
           GMainLoopCXX(g_main_loop_new(NULL, false), false) /* use reference handed to us by _new */)
{
}

LocalTransportAgent::~LocalTransportAgent()
{
}

void LocalTransportAgent::start()
{
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

    if (m_forkexec) {
        SE_THROW("local transport already started");
    }
    m_status = ACTIVE;
    m_forkexec = ForkExecParent::create("syncevo-local-sync");
    m_forkexec->m_onConnect.connect(boost::bind(&LocalTransportAgent::onChildConnect, this, _1));
    // fatal problems, including quitting child with non-zero status
    m_forkexec->m_onFailure.connect(boost::bind(&LocalTransportAgent::onFailure, this, _2));
    // watch onQuit and remember whether the child is still running,
    // because it might quit prematurely with a zero return code (for
    // example, when an unexpected slow sync is detected)
    m_forkexec->m_onQuit.connect(boost::bind(&LocalTransportAgent::onChildQuit, this, _1));
    m_forkexec->start();
}

/**
 * Uses the D-Bus API provided by LocalTransportParent.
 */
class LocalTransportParent : private GDBusCXX::DBusRemoteObject
{
 public:
    static const char *path() { return "/"; }
    static const char *interface() { return "org.syncevolution.localtransport.parent"; }
    static const char *destination() { return "local.destination"; }
    static const char *askPasswordName() { return "AskPassword"; }
    static const char *storeSyncReportName() { return "StoreSyncReport"; }

    LocalTransportParent(const GDBusCXX::DBusConnectionPtr &conn) :
        GDBusCXX::DBusRemoteObject(conn, path(), interface(), destination()),
        m_askPassword(*this, askPasswordName()),
        m_storeSyncReport(*this, storeSyncReportName())
    {}

    /** LocalTransportAgent::askPassword() */
    GDBusCXX::DBusClientCall1<std::string> m_askPassword;
    /** LocalTransportAgent::storeSyncReport() */
    GDBusCXX::DBusClientCall0 m_storeSyncReport;
};

/**
 * Uses the D-Bus API provided by LocalTransportAgentChild.
 */
class LocalTransportChild : private GDBusCXX::DBusRemoteObject
{
 public:
    static const char *path() { return "/"; }
    static const char *interface() { return "org.syncevolution.localtransport.child"; }
    static const char *destination() { return "local.destination"; }
    static const char *startSyncName() { return "StartSync"; }
    static const char *sendMsgName() { return "SendMsg"; }

    LocalTransportChild(const GDBusCXX::DBusConnectionPtr &conn) :
        GDBusCXX::DBusRemoteObject(conn, path(), interface(), destination()),
        m_startSync(*this, startSyncName()),
        m_sendMsg(*this, sendMsgName())
    {}

    /**
     * information from server config about active sources:
     * mapping is from server source names to child source name + sync mode
     * (again as set on the server side!)
     */
    typedef std::map<std::string, StringPair> ActiveSources_t;
    /** use this to send a message back from child to parent */
    typedef boost::shared_ptr< GDBusCXX::Result2< std::string, GDBusCXX::DBusArray<uint8_t> > > ReplyPtr;

    /** LocalTransportAgentChild::startSync() */
    GDBusCXX::DBusClientCall2<std::string, GDBusCXX::DBusArray<uint8_t> > m_startSync;
    /** LocalTransportAgentChild::sendMsg() */
    GDBusCXX::DBusClientCall2<std::string, GDBusCXX::DBusArray<uint8_t> > m_sendMsg;
};


void LocalTransportAgent::onChildConnect(const GDBusCXX::DBusConnectionPtr &conn)
{
    SE_LOG_DEBUG(NULL, NULL, "child is ready");
    m_parent.reset(new GDBusCXX::DBusObjectHelper(conn,
                                                  LocalTransportParent::path(),
                                                  LocalTransportParent::interface(),
                                                  GDBusCXX::DBusObjectHelper::Callback_t(),
                                                  true));
    m_parent->add(this, &LocalTransportAgent::askPassword, LocalTransportParent::askPasswordName());
    m_parent->add(this, &LocalTransportAgent::storeSyncReport, LocalTransportParent::storeSyncReportName());
    m_parent->activate();
    m_child.reset(new LocalTransportChild(conn));

    // now tell child what to do
    LocalTransportChild::ActiveSources_t sources;
    BOOST_FOREACH(const string &sourceName, m_server->getSyncSources()) {
        SyncSourceNodes nodes = m_server->getSyncSourceNodesNoTracking(sourceName);
        SyncSourceConfig source(sourceName, nodes);
        std::string sync = source.getSync();
        if (sync != "disabled") {
            string targetName = source.getURINonEmpty();
            sources[sourceName] = std::make_pair(targetName, sync);
        }
    }
    m_child->m_startSync.start(m_clientContext,
                               StringPair(m_server->getConfigName(),
                                          m_server->getRootPath()),
                               static_cast<std::string>(m_server->getLogDir()),
                               m_server->getDoLogging(),
                               StringPair(m_server->getSyncUsername(),
                                          m_server->getSyncPassword()),
                               m_server->getConfigProps(),
                               sources,
                               boost::bind(&LocalTransportAgent::storeReplyMsg, this, _1, _2, _3));
}

void LocalTransportAgent::onFailure(const std::string &error)
{
    m_status = FAILED;
    g_main_loop_quit(m_loop.get());

    SE_LOG_ERROR(NULL, NULL, "local transport failed: %s", error.c_str());
    m_parent.reset();
    m_child.reset();
}

void LocalTransportAgent::onChildQuit(int status)
{
    SE_LOG_DEBUG(NULL, NULL, "child process has quit with status %d", status);
    g_main_loop_quit(m_loop.get());
}

void LocalTransportAgent::askPassword(const std::string &passwordName,
                                      const std::string &descr,
                                      const ConfigPasswordKey &key,
                                      const boost::shared_ptr< GDBusCXX::Result1<const std::string &> > &reply)
{
    // pass that work to our own SyncContext and its UI - currently blocks
    SE_LOG_DEBUG(NULL, NULL, "local sync parent: asked for password %s, %s",
                 passwordName.c_str(),
                 descr.c_str());
    if (m_server) {
        std::string password;
        password = m_server->getUserInterfaceNonNull().askPassword(passwordName, descr, key);
        SE_LOG_DEBUG(NULL, NULL, "local sync parent: %s",
                     password.empty() ? "got no password" : "got password");
        reply->done(password);
    } else {
        SE_LOG_DEBUG(NULL, NULL, "local sync parent: password request failed because no m_server");
        reply->failed(GDBusCXX::dbus_error("org.syncevolution.localtransport.error",
                                           "not connected to UI"));
    }
}

void LocalTransportAgent::storeSyncReport(const std::string &report)
{
    SE_LOG_DEBUG(NULL, NULL, "got child sync report:\n%s",
                 report.c_str());
    m_clientReport = SyncReport(report);
}

void LocalTransportAgent::getClientSyncReport(SyncReport &report)
{
    report = m_clientReport;
}

void LocalTransportAgent::setContentType(const std::string &type)
{
    m_contentType = type;
}

// workaround for limitations of bind+signals when used together with plain GMainLoop pointer
// (pointer to undefined struct)
static void gMainLoopQuit(GMainLoopCXX *loop)
{
    g_main_loop_quit(loop->get());
}

void LocalTransportAgent::shutdown()
{
    SE_LOG_DEBUG(NULL, NULL, "parent is shutting down");
    if (m_forkexec) {
        // block until child is done
        boost::signals2::scoped_connection c(m_forkexec->m_onQuit.connect(boost::bind(gMainLoopQuit,
                                                                                      &m_loop)));
        // don't kill the child here - we expect it to complete by
        // itself at some point
        // TODO: how do we detect a child which gets stuck after its last
        // communication with the parent?
        // m_forkexec->stop();
        while (m_forkexec->getState() != ForkExecParent::TERMINATED) {
            SE_LOG_DEBUG(NULL, NULL, "waiting for child to stop");
            g_main_loop_run(m_loop.get());
        }

        m_forkexec.reset();
        m_parent.reset();
        m_child.reset();
    }
}

void LocalTransportAgent::send(const char *data, size_t len)
{
    if (m_child) {
        m_status = ACTIVE;
        m_child->m_sendMsg.start(m_contentType, GDBusCXX::makeDBusArray(len, (uint8_t *)(data)),
                                 boost::bind(&LocalTransportAgent::storeReplyMsg, this, _1, _2, _3));
    } else {
        m_status = FAILED;
        SE_THROW_EXCEPTION(TransportException,
                           "cannot send message because child process is gone");
    }
}

void LocalTransportAgent::storeReplyMsg(const std::string &contentType,
                                        const GDBusCXX::DBusArray<uint8_t> &reply,
                                        const std::string &error)
{
    m_replyMsg.assign(reinterpret_cast<const char *>(reply.second),
                      reply.first);
    m_replyContentType = contentType;
    if (error.empty()) {
        m_status = GOT_REPLY;
    } else {
        m_status = FAILED;
        SE_LOG_ERROR(NULL, NULL, "sending message to child failed: %s", error.c_str());
    }
    g_main_loop_quit(m_loop.get());
}

void LocalTransportAgent::cancel()
{
    if (m_forkexec) {
        SE_LOG_DEBUG(NULL, NULL, "killing local transport child in cancel()");
        m_forkexec->stop();
    }
    m_status = CANCELED;
}

TransportAgent::Status LocalTransportAgent::wait(bool noReply)
{
    if (m_status == ACTIVE) {
        // need next message; for noReply == true we are done
        if (noReply) {
            m_status = INACTIVE;
        } else {
            while (m_status == ACTIVE) {
                SE_LOG_DEBUG(NULL, NULL, "waiting for child to send message");
                if (m_forkexec &&
                    m_forkexec->getState() == ForkExecParent::TERMINATED) {
                    m_status = FAILED;
                    if (m_clientReport.getStatus() != STATUS_OK &&
                        m_clientReport.getStatus() != STATUS_HTTP_OK) {
                        // report that status
                        SE_THROW_EXCEPTION_STATUS(StatusException,
                                                  "failure in local sync child",
                                                  m_clientReport.getStatus());
                    } else {
                        SE_THROW_EXCEPTION(TransportException,
                                           "child process quit without sending its message");
                    }
                }
                g_main_loop_run(m_loop.get());
            }
        }
    }
    return m_status;
}

void LocalTransportAgent::getReply(const char *&data, size_t &len, std::string &contentType)
{
    if (m_status != GOT_REPLY) {
        SE_THROW("internal error, no reply available");
    }
    contentType = m_replyContentType;
    data = m_replyMsg.c_str();
    len = m_replyMsg.size();
}

void LocalTransportAgent::setTimeout(int seconds)
{
    // setTimeout() was meant for unreliable transports like HTTP
    // which cannot determine whether the peer is still alive. The
    // LocalTransportAgent uses sockets and will notice when a peer
    // dies unexpectedly, so timeouts should never be necessary.
    //
    // Quite the opposite, because the "client" in a local sync
    // with WebDAV on the client side can be quite slow, incorrect
    // timeouts were seen where the client side took longer than
    // the default timeout of 5 minutes to process a message and
    // send a reply.
    //
    // Therefore we ignore the request to set a timeout here and thus
    // local send/receive operations are allowed to continue for as
    // long as they like.
    //
    // m_timeoutSeconds = seconds;
}

class LocalTransportUI : public UserInterface
{
    boost::shared_ptr<LocalTransportParent> m_parent;
    SyncEvo::GMainLoopCXX m_loop;

public:
    LocalTransportUI(const boost::shared_ptr<LocalTransportParent> &parent,
                     const SyncEvo::GMainLoopCXX loop) :
        m_parent(parent),
        m_loop(loop)
    {}

    /** implements password request by asking the parent via D-Bus */
    virtual string askPassword(const string &passwordName, const string &descr, const ConfigPasswordKey &key)
    {
        SE_LOG_DEBUG(NULL, NULL, "local transport child: requesting password %s, %s via D-Bus",
                     passwordName.c_str(),
                     descr.c_str());
        std::string password;
        m_parent->m_askPassword.start(passwordName, descr, key,
                                      boost::bind(&LocalTransportUI::storePassword, this,
                                                  boost::ref(password), _1, _2));
        g_main_loop_run(m_loop.get());
        return password;
    }

    virtual bool savePassword(const std::string &passwordName, const std::string &password, const ConfigPasswordKey &key) { SE_THROW("not implemented"); return false; }
    virtual void readStdin(std::string &content) { SE_THROW("not implemented"); }

private:
    void storePassword(std::string &res, const std::string &password, const std::string &error)
    {
        if (!error.empty()) {
            SE_LOG_DEBUG(NULL, NULL, "local transport child: D-Bus password request failed: %s",
                         error.c_str());
        } else {
            SE_LOG_DEBUG(NULL, NULL, "local transport child: D-Bus password request succeeded");
            res = password;
        }
        g_main_loop_quit(m_loop.get());
    }
};

static void abortLocalSync(int sigterm)
{
    // logging anything here is not safe (our own logging system might
    // have been interrupted by the SIGTERM and thus be in an inconsistent
    // state), but let's try it anyway
    SE_LOG_INFO(NULL, NULL, "local sync child shutting down due to SIGTERM");
    // raise the signal again after disabling the handler, to ensure that
    // the exit status is "killed by signal xxx" - good because then
    // the whoever killed used gets the information that we didn't die for
    // some other reason
    signal(sigterm, SIG_DFL);
    raise(sigterm);
}

class LocalTransportAgentChild : public TransportAgent
{
    /** final return code of our main(): non-zero indicates that we need to shut down */
    int m_ret;

    /**
     * sync report for client side of the local sync
     */
    SyncReport m_clientReport;

    /**
     * the loop will run by step() while waiting for something;
     * normally the process is outside of the loop
     */
    SyncEvo::GMainLoopCXX m_loop;

    /**
     * provides connection to parent, created in constructor
     */
    boost::shared_ptr<ForkExecChild> m_forkexec;

    /**
     * proxy for the parent's D-Bus API in onConnect()
     */
    boost::shared_ptr<LocalTransportParent> m_parent;

    /**
     * our D-Bus interface, created in onConnect()
     */
    boost::scoped_ptr<GDBusCXX::DBusObjectHelper> m_child;

    /**
     * sync context, created in Sync() D-Bus call
     */
    boost::scoped_ptr<SyncContext> m_client;

    /**
     * use this D-Bus result handle to send a message from child to parent
     * in response to sync() or (later) sendMsg()
     */
    LocalTransportChild::ReplyPtr m_msgToParent;
    void setMsgToParent(const LocalTransportChild::ReplyPtr &reply,
                        const std::string &reason)
    {
        if (m_msgToParent) {
            m_msgToParent->failed(GDBusCXX::dbus_error("org.syncevolution.localtransport.error",
                                                       "cancelling message: " + reason));
        }
        m_msgToParent = reply;
    }

    /** content type for message to parent */
    std::string m_contentType;

    /**
     * message from parent
     */
    std::string m_message;

    /**
     * content type of message from parent
     */
    std::string m_messageType;

    /**
     * true after parent has received sync report, or sending failed
     */
    bool m_reportSent;

    /**
     * INACTIVE when idle,
     * ACTIVE after having sent and while waiting for next message,
     * GOT_REPLY when we have a message to be processed,
     * FAILED when permanently broken
     */
    Status m_status;

    /**
     * one loop run + error checking
     */
    void step(const std::string &status)
    {
        SE_LOG_DEBUG(NULL, NULL, "local transport: %s", status.c_str());
        g_main_loop_run(m_loop.get());
        if (m_ret) {
            SE_THROW("local transport child encountered a problem, terminating");
        }
    }

    void onConnect(const GDBusCXX::DBusConnectionPtr &conn)
    {
        SE_LOG_DEBUG(NULL, NULL, "child connected to parent");

        // provide our own API
        m_child.reset(new GDBusCXX::DBusObjectHelper(conn,
                                                     LocalTransportChild::path(),
                                                     LocalTransportChild::interface(),
                                                     GDBusCXX::DBusObjectHelper::Callback_t(),
                                                     true));
        m_child->add(this, &LocalTransportAgentChild::startSync, LocalTransportChild::startSyncName());
        m_child->add(this, &LocalTransportAgentChild::sendMsg, LocalTransportChild::sendMsgName());
        m_child->activate();

        // set up connection to parent
        m_parent.reset(new LocalTransportParent(conn));
    }

    void onFailure(SyncMLStatus status, const std::string &reason)
    {
        SE_LOG_DEBUG(NULL, NULL, "child fork/exec failed: %s", reason.c_str());

        // record failure for parent
        if (!m_clientReport.getStatus()) {
            m_clientReport.setStatus(status);
        }
        if (!reason.empty() &&
            m_clientReport.getError().empty()) {
            m_clientReport.setError(reason);
        }

        // return to step()
        m_ret = 1;
        g_main_loop_quit(m_loop.get());
    }

    // D-Bus API, see LocalTransportChild;
    // must keep number of parameters < 9, the maximum supported by
    // our D-Bus binding
    void startSync(const std::string &clientContext,
                   const StringPair &serverConfig, // config name + root path
                   const std::string &serverLogDir,
                   bool serverDoLogging,
                   const StringPair &serverSyncCredentials,
                   const FullProps &serverConfigProps,
                   const LocalTransportChild::ActiveSources_t &sources,
                   const LocalTransportChild::ReplyPtr &reply)
    {
        setMsgToParent(reply, "sync() was called");
        Logger::setProcessName(clientContext);
        SE_LOG_DEBUG(NULL, NULL, "Sync() called, starting the sync");

        // initialize sync context
        m_client.reset(new SyncContext(std::string("target-config") + clientContext,
                                       serverConfig.first,
                                       serverConfig.second + "/." + clientContext,
                                       boost::shared_ptr<TransportAgent>(this, NoopAgentDestructor()),
                                       serverDoLogging));
        boost::shared_ptr<UserInterface> ui(new LocalTransportUI(m_parent, m_loop));
        m_client->setUserInterface(ui);

        // allow proceeding with sync even if no "target-config" was created,
        // because information about username/password (for WebDAV) or the
        // sources (for file backends) might be enough
        m_client->setConfigNeeded(false);

        // apply temporary config filters
        m_client->setConfigFilter(true, "", serverConfigProps.createSyncFilter(m_client->getConfigName()));
        BOOST_FOREACH(const string &sourceName, m_client->getSyncSources()) {
            m_client->setConfigFilter(false, sourceName, serverConfigProps.createSourceFilter(m_client->getConfigName(), sourceName));
        }

        // Copy non-empty credentials from main config, because
        // that is where the GUI knows how to store them. A better
        // solution would be to require that credentials are in the
        // "target-config" config.
        //
        // Interactive password requests later in SyncContext::sync()
        // will end up in our LocalTransportContext::askPassword()
        // implementation above, which will pass the question to
        // the local sync parent.
        if (!serverSyncCredentials.first.empty()) {
            m_client->setSyncUsername(serverSyncCredentials.first, true);
        }
        if (!serverSyncCredentials.second.empty()) {
            m_client->setSyncPassword(serverSyncCredentials.second, true);
        }

        // debugging mode: write logs inside sub-directory of parent,
        // otherwise use normal log settings
        if (!serverDoLogging) {
            m_client->setLogDir(std::string(serverLogDir) + "/child", true);
        }

        // disable all sources temporarily, will be enabled by next loop
        BOOST_FOREACH(const string &targetName, m_client->getSyncSources()) {
            SyncSourceNodes targetNodes = m_client->getSyncSourceNodes(targetName);
            SyncSourceConfig targetSource(targetName, targetNodes);
            targetSource.setSync("disabled", true);
        }

        // activate all sources in client targeted by main config,
        // with right uri
        BOOST_FOREACH(const LocalTransportChild::ActiveSources_t::value_type &entry, sources) {
            // mapping is from server (source) to child (target)
            const std::string &sourceName = entry.first;
            const std::string &targetName = entry.second.first;
            std::string sync = entry.second.second;
            if (sync != "disabled") {
                SyncSourceNodes targetNodes = m_client->getSyncSourceNodes(targetName);
                SyncSourceConfig targetSource(targetName, targetNodes);
                string fullTargetName = clientContext + "/" + targetName;

                if (!targetNodes.dataConfigExists()) {
                    if (targetName.empty()) {
                        m_client->throwError("missing URI for one of the sources");
                    } else {
                        m_client->throwError(StringPrintf("%s: source not configured",
                                                          fullTargetName.c_str()));
                    }
                }

                // All of the config setting is done as volatile,
                // so none of the regular config nodes have to
                // be written. If a sync mode was set, it must have been
                // done before in this loop => error in original config.
                if (!targetSource.isDisabled()) {
                    m_client->throwError(StringPrintf("%s: source targetted twice by %s",
                                                      fullTargetName.c_str(),
                                                      serverConfig.first.c_str()));
                }
                // invert data direction
                if (sync == "refresh-from-local") {
                    sync = "refresh-from-remote";
                } else if (sync == "refresh-from-remote") {
                    sync = "refresh-from-local";
                } else if (sync == "one-way-from-local") {
                    sync = "one-way-from-remote";
                } else if (sync == "one-way-from-remote") {
                    sync = "one-way-from-local";
                }
                targetSource.setSync(sync, true);
                targetSource.setURI(sourceName, true);
            }
        }


        // ready for m_client->sync()
        m_status = ACTIVE;
        g_main_loop_quit(m_loop.get());
    }

    void sendMsg(const std::string &contentType,
                 const GDBusCXX::DBusArray<uint8_t> &data,
                 const LocalTransportChild::ReplyPtr &reply)
    {
        SE_LOG_DEBUG(NULL, NULL, "child got message of %ld bytes", (long)data.first);
        setMsgToParent(LocalTransportChild::ReplyPtr(), "sendMsg() was called");
        if (m_status == ACTIVE) {
            m_msgToParent = reply;
            m_message.assign(reinterpret_cast<const char *>(data.second),
                             data.first);
            m_messageType = contentType;
            m_status = GOT_REPLY;
            g_main_loop_quit(m_loop.get());
        } else {
            reply->failed(GDBusCXX::dbus_error("org.syncevolution.localtransport.error",
                                               "child not expecting any message"));
        }
    }

public:
    LocalTransportAgentChild() :
        m_ret(0),
        m_loop(g_main_loop_new(NULL, FALSE), false),
        m_forkexec(SyncEvo::ForkExecChild::create()),
        m_reportSent(false),
        m_status(INACTIVE)
    {
        m_forkexec->m_onConnect.connect(boost::bind(&LocalTransportAgentChild::onConnect, this, _1));
        m_forkexec->m_onFailure.connect(boost::bind(&LocalTransportAgentChild::onFailure, this, _1, _2));
        m_forkexec->connect();
    }

    void run()
    {
        while (!m_parent) {
            step("waiting for parent");
        }
        while (!m_client) {
            step("waiting for Sync() call from parent");
        }
        try {
            // ignore SIGINT signal in local sync helper from now on:
            // the parent process will handle those and tell us when
            // we are expected to abort by sending a SIGTERM
            struct sigaction new_action;
            memset(&new_action, 0, sizeof(new_action));
            new_action.sa_handler = SIG_IGN;
            sigemptyset(&new_action.sa_mask);
            sigaction(SIGINT, &new_action, NULL);

            // SIGTERM would be caught by SuspendFlags and set the "abort"
            // state. But a lot of code running in this process cannot
            // check that flag in a timely manner (blocking calls in
            // libneon, activesync client libraries, ...). Therefore
            // it is better to abort inside the signal handler.
            new_action.sa_handler = abortLocalSync;
            sigaction(SIGTERM, &new_action, NULL);

            SE_LOG_DEBUG(NULL, NULL, "LocalTransportChild: ignore SIGINT, die in SIGTERM");

            m_client->sync(&m_clientReport);
        } catch (...) {
            string explanation;
            SyncMLStatus status = Exception::handle(explanation);
            m_clientReport.setStatus(status);
            if (!explanation.empty() &&
                m_clientReport.getError().empty()) {
                m_clientReport.setError(explanation);
            }
            if (m_parent) {
                std::string report = m_clientReport.toString();
                SE_LOG_DEBUG(NULL, NULL, "child sending sync report after failure:\n%s", report.c_str());
                m_parent->m_storeSyncReport.start(report,
                                                  boost::bind(&LocalTransportAgentChild::syncReportReceived, this, _1));
                // wait for acknowledgement for report once:
                // we are in some kind of error state, better
                // do not wait too long
                if (m_parent) {
                    SE_LOG_DEBUG(NULL, NULL, "waiting for parent's ACK for sync report");
                    g_main_loop_run(m_loop.get());
                }
            }
            throw;
        }

        if (m_parent) {
            // send final report, ignore result
            std::string report = m_clientReport.toString();
            SE_LOG_DEBUG(NULL, NULL, "child sending sync report:\n%s", report.c_str());
            m_parent->m_storeSyncReport.start(report,
                                              boost::bind(&LocalTransportAgentChild::syncReportReceived, this, _1));
            while (!m_reportSent && m_parent) {
                SE_LOG_DEBUG(NULL, NULL, "waiting for parent's ACK for sync report");
                g_main_loop_run(m_loop.get());
            }
        }
    }

    void syncReportReceived(const std::string &error)
    {
        SE_LOG_DEBUG(NULL, NULL, "sending sync report to parent: %s",
                     error.empty() ? "done" : error.c_str());
        m_reportSent = true;
        g_main_loop_quit(m_loop.get());
    }

    int getReturnCode() const { return m_ret; }

    /**
     * set transport specific URL of next message
     */
    virtual void setURL(const std::string &url) {}

    /**
     * define content type for post, see content type constants
     */
    virtual void setContentType(const std::string &type)
    {
        m_contentType = type;
    }

    /**
     * Requests an normal shutdown of the transport. This can take a
     * while, for example if communication is still pending.
     * Therefore wait() has to be called to ensure that the
     * shutdown is complete and that no error occurred.
     *
     * Simply deleting the transport is an *unnormal* shutdown that
     * does not communicate with the peer.
     */
    virtual void shutdown()
    {
        SE_LOG_DEBUG(NULL, NULL, "child local transport shutting down");
        if (m_msgToParent) {
            // Must send non-zero message, empty messages cause an
            // error during D-Bus message decoding on the receiving
            // side. Content doesn't matter, ignored by parent.
            m_msgToParent->done("shutdown-message", GDBusCXX::makeDBusArray(1, (uint8_t *)""));
            m_msgToParent.reset();
        }
        if (m_status != FAILED) {
            m_status = CLOSED;
        }
    }

    /**
     * start sending message
     *
     * Memory must remain valid until reply is received or
     * message transmission is canceled.
     *
     * @param data      start address of data to send
     * @param len       number of bytes
     */
    virtual void send(const char *data, size_t len)
    {
        SE_LOG_DEBUG(NULL, NULL, "child local transport sending %ld bytes", (long)len);
        if (m_msgToParent) {
            m_status = ACTIVE;
            m_msgToParent->done(m_contentType, GDBusCXX::makeDBusArray(len, (uint8_t *)(data)));
            m_msgToParent.reset();
        } else {
            m_status = FAILED;
            SE_THROW("cannot send data to parent because parent is not waiting for message");
        }
    }

    /**
     * cancel an active message transmission
     *
     * Blocks until send buffer is no longer in use.
     * Returns immediately if nothing pending.
     */
    virtual void cancel() {}


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
    virtual Status wait(bool noReply = false)
    {
        while (m_status == ACTIVE) {
            step("waiting for next message");
        }
        return m_status;
    }

    /**
     * Tells the transport agent to stop the transmission the given
     * amount of seconds after send() was called. The transport agent
     * will then stop the message transmission and return a TIME_OUT
     * status in wait().
     *
     * @param seconds      number of seconds to wait before timing out, zero for no timeout
     */
    virtual void setTimeout(int seconds) {}

    /**
     * provides access to reply data
     *
     * Memory pointer remains valid as long as
     * transport agent is not deleted and no other
     * message is sent.
     */
    virtual void getReply(const char *&data, size_t &len, std::string &contentType)
    {
        SE_LOG_DEBUG(NULL, NULL, "processing %ld bytes in child", (long)m_message.size());
        if (m_status != GOT_REPLY) {
            SE_THROW("getReply() called in child when no reply available");
        }
        data = m_message.c_str();
        len = m_message.size();
        contentType = m_messageType;
    }
};

int LocalTransportMain(int argc, char **argv)
{
    // delay the client for debugging purposes
    const char *delay = getenv("SYNCEVOLUTION_LOCAL_CHILD_DELAY");
    if (delay) {
        sleep(atoi(delay));
    }

    SyncContext::initMain("syncevo-local-sync");

    // cannot be interrupted via signal directly,
    // signals must be directed to parent
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);

    try {
        if (getenv("SYNCEVOLUTION_DEBUG")) {
            LoggerBase::instance().setLevel(Logger::DEBUG);
        }
        // process name will be set to target config name once it is known
        Logger::setProcessName("syncevo-local-sync");

        boost::shared_ptr<LocalTransportAgentChild> child(new LocalTransportAgentChild);
        child->run();
        int ret = child->getReturnCode();
        child.reset();
        return ret;
    } catch ( const std::exception &ex ) {
        SE_LOG_ERROR(NULL, NULL, "%s", ex.what());
    } catch (...) {
        SE_LOG_ERROR(NULL, NULL, "unknown error");
    }

    return 1;
}

SE_END_CXX
