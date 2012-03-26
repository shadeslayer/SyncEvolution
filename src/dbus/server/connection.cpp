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

#include "server.h"
#include "connection.h"
#include "client.h"

#include <synthesis/san.h>
#include <syncevo/TransportAgent.h>
#include <syncevo/SyncContext.h>

using namespace GDBusCXX;

SE_BEGIN_CXX

void Connection::failed(const std::string &reason)
{
    SE_LOG_DEBUG(NULL, NULL, "connection failed: %s", reason.c_str());
    if (m_failure.empty()) {
        m_failure = reason;
        if (m_session) {
            m_session->setStubConnectionError(reason);
        }
    }
    // notify client
    abort();
    // ensure that state is failed
    m_state = SessionCommon::FAILED;
    // tell helper (again)
    m_statusSignal(reason);
}

std::string Connection::buildDescription(const StringMap &peer)
{
    StringMap::const_iterator
        desc = peer.find("description"),
        id = peer.find("id"),
        trans = peer.find("transport"),
        trans_desc = peer.find("transport_description");
    std::string buffer;
    buffer.reserve(256);
    if (desc != peer.end()) {
        buffer += desc->second;
    }
    if (id != peer.end() || trans != peer.end()) {
        if (!buffer.empty()) {
            buffer += " ";
        }
        buffer += "(";
        if (id != peer.end()) {
            buffer += id->second;
            if (trans != peer.end()) {
                buffer += " via ";
            }
        }
        if (trans != peer.end()) {
            buffer += trans->second;
            if (trans_desc != peer.end()) {
                buffer += " ";
                buffer += trans_desc->second;
            }
        }
        buffer += ")";
    }
    return buffer;
}

void Connection::process(const Caller_t &caller,
                         const GDBusCXX::DBusArray<uint8_t> &message,
                         const std::string &message_type)
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s sends %lu bytes via connection %s, %s",
                 caller.c_str(),
                 (unsigned long)message.first,
                 getPath(),
                 message_type.c_str());

    boost::shared_ptr<Client> client(m_server.findClient(caller));
    if (!client) {
        throw runtime_error("unknown client");
    }

    boost::shared_ptr<Connection> myself =
        boost::static_pointer_cast<Connection, Resource>(client->findResource(this));
    if (!myself) {
        throw runtime_error("client does not own connection");
    }

    // any kind of error from now on terminates the connection
    try {
        switch (m_state) {
        case SessionCommon::SETUP: {
            std::string config;
            std::string peerDeviceID;
            bool serverMode = false;
            bool serverAlerted = false;
            // check message type, determine whether we act
            // as client or server, choose config
            if (message_type == "HTTP Config") {
                // type used for testing, payload is config name
                config.assign(reinterpret_cast<const char *>(message.second),
                              message.first);
            } else if (message_type == TransportAgent::m_contentTypeServerAlertedNotificationDS) {
                serverAlerted = true;
                sysync::SanPackage san;
                if (san.PassSan(const_cast<uint8_t *>(message.second), message.first, 2) || san.GetHeader()) {
                    // We are very tolerant regarding the content of the message.
                    // If it doesn't parse, try to do something useful anyway.
                    // only for SAN 1.2, for SAN 1.0/1.1 we can not be sure
                    // whether it is a SAN package or a normal sync pacakge
                    if (message_type == TransportAgent::m_contentTypeServerAlertedNotificationDS) {
                        config = "default";
                        SE_LOG_DEBUG(NULL, NULL, "SAN parsing failed, falling back to 'default' config");
                    }
                } else { //Server alerted notification case
                    // Extract server ID and match it against a server
                    // configuration.  Multiple different peers might use the
                    // same serverID ("PC Suite"), so check properties of the
                    // of our configs first before going back to the name itself.
                    std::string serverID = san.fServerID;
                    SyncConfig::ConfigList servers = SyncConfig::getConfigs();
                    BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server,
                            servers) {
                        SyncConfig conf(server.first);
                        vector<string> urls = conf.getSyncURL();
                        BOOST_FOREACH (const string &url, urls) {
                            if (url == serverID) {
                                config = server.first;
                                break;
                            }
                        }
                        if (!config.empty()) {
                            break;
                        }
                    }

                    // for Bluetooth transports match against mac address.
                    StringMap::const_iterator id = m_peer.find("id"),
                        trans = m_peer.find("transport");
                    if (trans != m_peer.end() && id != m_peer.end()) {
                        if (trans->second == "org.openobex.obexd") {
                            m_peerBtAddr = id->second.substr(0, id->second.find("+"));
                            BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server,
                                    servers) {
                                SyncConfig conf(server.first);
                                vector<string> urls = conf.getSyncURL();
                                BOOST_FOREACH (string &url, urls){
                                    url = url.substr (0, url.find("+"));
                                    SE_LOG_DEBUG (NULL, NULL, "matching against %s",url.c_str());
                                    if (url.find ("obex-bt://") ==0 && url.substr(strlen("obex-bt://"), url.npos) == m_peerBtAddr) {
                                        config = server.first;
                                        break;
                                    }
                                }
                                if (!config.empty()){
                                    break;
                                }
                            }
                        }
                    }

                    if (config.empty()) {
                        BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server,
                                      servers) {
                            if (server.first == serverID) {
                                config = serverID;
                                break;
                            }
                        }
                    }

                    // create a default configuration name if none matched
                    if (config.empty()) {
                        config = serverID+"_"+getCurrentTime();
                        SE_LOG_DEBUG(NULL,
                                     NULL,
                                     "SAN Server ID '%s' unknown, falling back to automatically created '%s' config",
                                     serverID.c_str(), config.c_str());
                    }


                    SE_LOG_DEBUG(NULL, NULL, "SAN sync with config %s", config.c_str());

                    m_SANContent.reset (new SANContent ());
                    // extract number of sources
                    int numSources = san.fNSync;
                    int syncType;
                    uint32_t contentType;
                    std::string serverURI;
                    if (!numSources) {
                        SE_LOG_DEBUG(NULL, NULL, "SAN message with no sources, using selected modes");
                        // Synchronize all known sources with the default mode.
                        if (san.GetNthSync(0, syncType, contentType, serverURI)) {
                            SE_LOG_DEBUG(NULL, NULL, "SAN invalid header, using default modes");
                        } else if (syncType < SYNC_FIRST || syncType > SYNC_LAST) {
                            SE_LOG_DEBUG(NULL, NULL, "SAN invalid sync type %d, using default modes", syncType);
                        } else {
                            m_syncMode = PrettyPrintSyncMode(SyncMode(syncType), true);
                            SE_LOG_DEBUG(NULL, NULL, "SAN sync mode for all configured sources: %s", m_syncMode.c_str());
                        }
                    } else {
                        for (int sync = 1; sync <= numSources; sync++) {
                            if (san.GetNthSync(sync, syncType, contentType, serverURI)) {
                                SE_LOG_DEBUG(NULL, NULL, "SAN invalid sync entry #%d", sync);
                            } else if (syncType < SYNC_FIRST || syncType > SYNC_LAST) {
                                SE_LOG_DEBUG(NULL, NULL, "SAN invalid sync type %d for entry #%d, ignoring entry", syncType, sync);
                            } else {
                                std::string syncMode = PrettyPrintSyncMode(SyncMode(syncType), true);
                                m_SANContent->m_syncType.push_back (syncMode);
                                m_SANContent->m_serverURI.push_back (serverURI);
                                m_SANContent->m_contentType.push_back (contentType);
                            }
                        }
                    }
                }
                // TODO: use the session ID set by the server if non-null
            } else if (// relaxed checking for XML: ignore stuff like "; CHARSET=UTF-8"
                       message_type.substr(0, message_type.find(';')) == TransportAgent::m_contentTypeSyncML ||
                       message_type == TransportAgent::m_contentTypeSyncWBXML) {
                // run a new SyncML session as server
                serverMode = true;
                if (m_peer.find("config") == m_peer.end() &&
                    !m_peer["config"].empty()) {
                    SE_LOG_DEBUG(NULL, NULL, "ignoring pre-chosen config '%s'",
                                 m_peer["config"].c_str());
                }

                // peek into the data to extract the locURI = device ID,
                // then use it to find the configuration
                SyncContext::SyncMLMessageInfo info;
                info = SyncContext::analyzeSyncMLMessage(reinterpret_cast<const char *>(message.second),
                                                         message.first,
                                                         message_type);
                if (info.m_deviceID.empty()) {
                    // TODO: proper exception
                    throw runtime_error("could not extract LocURI=deviceID from initial message");
                }
                BOOST_FOREACH(const SyncConfig::ConfigList::value_type &entry,
                              SyncConfig::getConfigs()) {
                    SyncConfig peer(entry.first);
                    if (info.m_deviceID == peer.getRemoteDevID()) {
                        config = entry.first;
                        SE_LOG_INFO(NULL, NULL, "matched %s against config %s (%s)",
                                    info.toString().c_str(),
                                    entry.first.c_str(),
                                    entry.second.c_str());
                        // Stop searching. Other peer configs might have the same remoteDevID.
                        // We go with the first one found, which because of the sort order
                        // of getConfigs() ensures that "foo" is found before "foo.old".
                        break;
                    }
                }
                if (config.empty()) {
                    // TODO: proper exception
                    throw runtime_error(string("no configuration found for ") +
                                        info.toString());
                }

                // identified peer, still need to abort previous sessions below
                peerDeviceID = info.m_deviceID;
            } else {
                throw runtime_error(StringPrintf("message type '%s' not supported for starting a sync", message_type.c_str()));
            }

            // run session as client or server
            m_state = SessionCommon::PROCESSING;
            m_session = Session::createSession(m_server,
                                               peerDeviceID,
                                               config,
                                               m_sessionID);
            if (serverMode) {
                m_session->initServer(SharedBuffer(reinterpret_cast<const char *>(message.second),
                                                   message.first),
                                      message_type);
            }
            m_session->setServerAlerted(serverAlerted);
            m_session->setPriority(Session::PRI_CONNECTION);
            m_session->setStubConnection(myself);
            // this will be reset only when the connection shuts down okay
            // or overwritten with the error given to us in
            // Connection::close()
            m_session->setStubConnectionError("closed prematurely");

            // Now abort all earlier sessions, if necessary.  The new
            // session will be enqueued below and thus won't get
            // killed. It also won't run unless all other sessions
            // before it terminate, therefore we don't need to check
            // for success.
            if (!peerDeviceID.empty()) {
                // TODO: On failure we should kill the connection (beware,
                // it might go away before killing completes and/or
                // fails - need to use shared pointer tracking).
                //
                // boost::shared_ptr<Connection> c = m_me.lock();
                // if (!c) {
                //     SE_THROW("internal error: Connection::process() cannot lock its own instance");
                // }
                m_server.killSessionsAsync(peerDeviceID,
                                           SimpleResult(SuccessCb_t(),
                                                        ErrorCb_t()));
            }
            m_server.enqueue(m_session);
            break;
        }
        case SessionCommon::PROCESSING:
            throw std::runtime_error("protocol error: already processing a message");
            break;
        case SessionCommon::WAITING:
            m_incomingMsg = SharedBuffer(reinterpret_cast<const char *>(message.second),
                                         message.first);
            m_incomingMsgType = message_type;
            m_messageSignal(DBusArray<uint8_t>(m_incomingMsg.size(),
                                               reinterpret_cast<uint8_t *>(m_incomingMsg.get())),
                            m_incomingMsgType);
            m_state = SessionCommon::PROCESSING;
            m_timeout.deactivate();
            break;
        case SessionCommon::FINAL:
            throw std::runtime_error("protocol error: final reply sent, no further message processing possible");
        case SessionCommon::DONE:
            throw std::runtime_error("protocol error: connection closed, no further message processing possible");
            break;
        case SessionCommon::FAILED:
            throw std::runtime_error(m_failure);
            break;
        default:
            throw std::runtime_error("protocol error: unknown internal state");
            break;
        }
    } catch (const std::exception &error) {
        failed(error.what());
        throw;
    } catch (...) {
        failed("unknown exception in Connection::process");
        throw;
    }
}

void Connection::send(const DBusArray<uint8_t> buffer,
                      const std::string &type,
                      const std::string &url)
{
    if (m_state != SessionCommon::PROCESSING) {
        SE_THROW_EXCEPTION(TransportException,
                           "cannot send to our D-Bus peer");
    }

    // Change state in advance. If we fail while replying, then all
    // further resends will fail with the error above.
    m_state = SessionCommon::WAITING;
    activateTimeout();
    m_incomingMsg = SharedBuffer();

    // TODO: turn D-Bus exceptions into transport exceptions
    StringMap meta;
    meta["URL"] = url;
    reply(buffer, type, meta, false, m_sessionID);
}

void Connection::sendFinalMsg()
{
    if (m_state != SessionCommon::FAILED) {
        // send final, empty message and wait for close
        m_state = SessionCommon::FINAL;
        reply(GDBusCXX::DBusArray<uint8_t>(0, 0),
              "", StringMap(),
              true, m_sessionID);
    }
}

void Connection::close(const Caller_t &caller,
                       bool normal,
                       const std::string &error)
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s closes connection %s %s%s%s",
                 caller.c_str(),
                 getPath(),
                 normal ? "normally" : "with error",
                 error.empty() ? "" : ": ",
                 error.c_str());

    boost::shared_ptr<Client> client(m_server.findClient(caller));
    if (!client) {
        throw runtime_error("unknown client");
    }

    if (!normal ||
        m_state != SessionCommon::FINAL) {
        std::string err = error.empty() ?
            "connection closed unexpectedly" :
            error;
        m_statusSignal(err);
        if (m_session) {
            m_session->setStubConnectionError(err);
        }
        failed(err);
    } else {
        m_state = SessionCommon::DONE;
        m_statusSignal("");
        if (m_session) {
            m_session->setStubConnectionError("");
        }
    }

    // remove reference to us from client, will destruct *this*
    // instance!
    client->detach(this);
}

void Connection::abort()
{
    if (!m_abortSent) {
        sendAbort();
        m_abortSent = true;
    }
}


void Connection::shutdown()
{
    // trigger removal of this connection by removing all
    // references to it
    m_server.detach(this);
}

Connection::Connection(Server &server,
                       const DBusConnectionPtr &conn,
                       const std::string &sessionID,
                       const StringMap &peer,
                       bool must_authenticate) :
    DBusObjectHelper(conn,
                     std::string("/org/syncevolution/Connection/") + sessionID,
                     "org.syncevolution.Connection",
                     boost::bind(&Server::autoTermCallback, &server)),
    m_server(server),
    m_peer(peer),
    m_mustAuthenticate(must_authenticate),
    m_state(SessionCommon::SETUP),
    m_sessionID(sessionID),
    m_timeoutSeconds(-1),
    sendAbort(*this, "Abort"),
    m_abortSent(false),
    reply(*this, "Reply"),
    m_description(buildDescription(peer))
{
    add(this, &Connection::process, "Process");
    add(this, &Connection::close, "Close");
    add(sendAbort);
    add(reply);
    m_server.autoTermRef();
}

boost::shared_ptr<Connection> Connection::createConnection(Server &server,
                                                           const DBusConnectionPtr &conn,
                                                           const std::string &sessionID,
                                                           const StringMap &peer,
                                                           bool must_authenticate)
{
    boost::shared_ptr<Connection> c(new Connection(server, conn, sessionID, peer, must_authenticate));
    c->m_me = c;
    return c;
}

Connection::~Connection()
{
    SE_LOG_DEBUG(NULL, NULL, "done with connection to '%s'%s%s%s",
                 m_description.c_str(),
                 m_state == SessionCommon::DONE ? ", normal shutdown" : " unexpectedly",
                 m_failure.empty() ? "" : ": ",
                 m_failure.c_str());
    try {
        if (m_state != SessionCommon::DONE) {
            abort();
            m_state = SessionCommon::FAILED;
        }
        // DBusTransportAgent waiting? Wake it up.
        m_statusSignal(m_failure);
        m_session.reset();
    } catch (...) {
        // log errors, but do not propagate them because we are
        // destructing
        Exception::handle();
    }
    m_server.autoTermUnref();
}

void Connection::ready()
{
    //if configuration not yet created
    std::string configName = m_session->getConfigName();
    SyncConfig config (configName);
    if (!config.exists() && m_SANContent) {
        SE_LOG_DEBUG (NULL, NULL, "Configuration %s not exists for a runnable session in a SAN context, create it automatically", configName.c_str());
        ReadOperations::Config_t from;
        const std::string templateName = "SyncEvolution";
        // TODO: support SAN from other well known servers
        ReadOperations ops(templateName, m_server);
        ops.getConfig(true , from);
        if (!m_peerBtAddr.empty()){
            from[""]["SyncURL"] = string ("obex-bt://") + m_peerBtAddr;
        }
        m_session->setConfig (false, false, from);
    }

    // As we cannot resend messages via D-Bus even if running as
    // client (API not designed for it), let's use the hard server
    // timeout from RetryDuration here.
    m_timeoutSeconds = config.getRetryDuration();

    const SyncContext context (configName);
    std::list<std::string> sources = context.getSyncSources();

    if (m_SANContent && !m_SANContent->m_syncType.empty()) {
        // check what the server wants us to synchronize
        // and only synchronize that
        m_syncMode = "disabled";
        for (size_t sync=0; sync<m_SANContent->m_syncType.size(); sync++) {
            std::string syncMode = m_SANContent->m_syncType[sync];
            std::string serverURI = m_SANContent->m_serverURI[sync];
            //uint32_t contentType = m_SANContent->m_contentType[sync];
            bool found = false;
            BOOST_FOREACH(const std::string &source, sources) {
                boost::shared_ptr<const PersistentSyncSourceConfig> sourceConfig(context.getSyncSourceConfig(source));
                // prefix match because the local
                // configuration might contain
                // additional parameters (like date
                // range selection for events)
                if (boost::starts_with(sourceConfig->getURINonEmpty(), serverURI)) {
                    SE_LOG_DEBUG(NULL, NULL,
                                 "SAN entry #%d = source %s with mode %s",
                                 (int)sync, source.c_str(), syncMode.c_str());
                    m_sourceModes[source] = syncMode;
                    found = true;
                    break;
                }
            }
            if (!found) {
                SE_LOG_DEBUG(NULL, NULL,
                             "SAN entry #%d with mode %s ignored because Server URI %s is unknown",
                             (int)sync, syncMode.c_str(), serverURI.c_str());
            }
        }
        if (m_sourceModes.empty()) {
            SE_LOG_DEBUG(NULL, NULL,
                    "SAN message with no known entries, falling back to default");
            m_syncMode = "";
        }
    }

    if (m_SANContent) {
        m_session->setRemoteInitiated(true);
    }
    // proceed with sync now that our session is ready
    m_session->sync(m_syncMode, m_sourceModes);
}

void Connection::activateTimeout()
{
    if (m_timeoutSeconds >= 0) {
        m_timeout.runOnce(m_timeoutSeconds,
                          boost::bind(&Connection::timeoutCb,
                                      this));
    } else {
        m_timeout.deactivate();
    }
}

void Connection::timeoutCb()
{
    failed(StringPrintf("timed out after %ds", m_timeoutSeconds));
    // Don't delete ourselves while some code of the Connection still
    // runs. Instead let server do that as part of its event loop.
    boost::shared_ptr<Connection> c = m_me.lock();
    if (c) {
        m_server.delayDeletion(c);
    }
    
}

SE_END_CXX
