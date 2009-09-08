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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gdbus-cxx-bridge.h"
#include <syncevo/Logging.h>
#include <syncevo/util.h>

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <list>
#include <map>
#include <memory>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

#include <glib-object.h>

using namespace SyncEvo;

static GMainLoop *loop = NULL;

/**
 * Anything that can be owned by a client, like a connection
 * or session.
 */
class Resource {
public:
    virtual ~Resource() {}
};

class Session;
class Connection;
class Client;

/**
 * Implements the main org.syncevolution.Server interface.
 *
 * All objects created by it get a reference to the creating
 * DBusServer instance so that they can call some of its
 * methods. Because that instance holds references to all
 * of these objects and deletes them before destructing itself,
 * that reference is guaranteed to remain valid.
 */
class DBusServer : public DBusObjectHelper
{
    uint32_t m_lastSession;
    typedef std::list< std::pair< boost::shared_ptr<Watch>, boost::shared_ptr<Client> > > Clients_t;
    Clients_t m_clients;

    /**
     * The session which currently holds the main lock on the server.
     * To avoid issues with concurrent modification of data or configs,
     * only one session may make such modifications at a time. A
     * plain pointer which is reset by the session's deconstructor.
     *
     * A weak pointer did not work because it does not provide access
     * to the underlying pointer after the last corresponding shared
     * pointer is gone (which triggers the deconstructing of the session).
     */
    Session *m_activeSession;

    /**
     * The running sync session. Having a separate reference to it
     * ensures that the object won't go away prematurely, even if all
     * clients disconnect.
     */
    boost::shared_ptr<Session> m_syncSession;

    typedef std::list< boost::weak_ptr<Session> > WorkQueue_t;
    /**
     * A queue of pending, idle Sessions. Sorted by priority, most
     * important one first. Currently this is used to give client
     * requests a boost over remote connections and (in the future)
     * automatic syncs.
     *
     * Active sessions are removed from this list and then continue
     * to exist as long as a client in m_clients references it or
     * it is the currently running sync session (m_syncSession).
     */
    WorkQueue_t m_workQueue;

    /**
     * Watch callback for a specific client or connection.
     */
    void clientGone(Client *c);

    /**
     * Returns new session number. Checks for overflow, but not
     * currently for active sessions.
     */
    uint32_t getNextSession();

    /**
     * Implements org.syncevolution.Server.Connect.
     * Needs a Result object so that it can create the
     * watch on the connecting client.
     */
    void connect(const Caller_t &caller,
                 const boost::shared_ptr<Watch> &watch,
                 const std::map<std::string, std::string> &peer,
                 bool must_authenticate,
                 uint32_t session,
                 DBusObject_t &object);

    void startSession(const Caller_t &caller,
                      const boost::shared_ptr<Watch> &watch,
                      const std::string &server,
                      DBusObject_t &object);

    EmitSignal2<const DBusObject_t &,
                bool> sessionChanged;

public:
    DBusServer(const DBusConnectionPtr &conn);
    ~DBusServer();

    void activate();

    /**
     * look up client by its ID
     */
    boost::shared_ptr<Client> findClient(const Caller_t &ID);

    /**
     * find client by its ID or create one anew
     */
    boost::shared_ptr<Client> addClient(const DBusConnectionPtr &conn,
                                        const Caller_t &ID,
                                        const boost::shared_ptr<Watch> &watch);

    /**
     * Enqueue a session. Might also make it ready immediately,
     * if nothing else is first in the queue. To be called
     * by the creator of the session, *after* the session is
     * ready to run.
     */
    void enqueue(const boost::shared_ptr<Session> &session);

    /**
     * Remove a session from the work queue. If it is running a sync,
     * it will keep running and nothing will change. Otherwise, if it
     * is "ready" (= holds a lock on its configuration), then release
     * that lock.
     */
    void dequeue(Session *session);

    /**
     * Checks whether the server is ready to run another session
     * and if so, activates the first one in the queue.
     */
    void checkQueue();
};


/**
 * Tracks a single client and all sessions and connections that it is
 * connected to. Referencing them ensures that they stay around as
 * long as needed.
 */
class Client
{
    typedef std::list< boost::shared_ptr<Resource> > Resources_t;
    Resources_t m_resources;

public:
    const Caller_t m_ID;

    Client(const Caller_t &ID) :
        m_ID(ID)
    {}

    ~Client()
    {
        SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s is destructing", m_ID.c_str());
    }
        

    /**
     * Attach a specific resource to this client. As long as the
     * resource is attached, it cannot be freed. Can be called
     * multiple times, which means that detach() also has to be called
     * the same number of times to finally detach the resource.
     */
    void attach(boost::shared_ptr<Resource> resource)
    {
        m_resources.push_back(resource);
    }

    /**
     * Detach once from the given resource. Has to be called as
     * often as attach() to really remove all references to the
     * session. It's an error to call detach() more often than
     * attach().
     */
    void detach(Resource *resource)
    {
        for (Resources_t::iterator it = m_resources.begin();
             it != m_resources.end();
             ++it) {
            if (it->get() == resource) {
                // got it
                m_resources.erase(it);
                return;
            }
        }

        throw std::runtime_error("cannot detach from resource that client is not attached to");
    }
    void detach(boost::shared_ptr<Resource> resource)
    {
        detach(resource.get());
    }

    /**
     * return corresponding smart pointer for a certain resource,
     * empty pointer if not found
     */
    boost::shared_ptr<Resource> findResource(Resource *resource)
    {
        for (Resources_t::iterator it = m_resources.begin();
             it != m_resources.end();
             ++it) {
            if (it->get() == resource) {
                // got it
                return *it;
            }
        }
        return boost::shared_ptr<Resource>();
    }
};

/**
 * Represents and implements the Session interface.  Use
 * boost::shared_ptr to track it and ensure that there are references
 * to it as long as the connection is needed.
 */
class Session : public DBusObjectHelper, public Resource
{
    DBusServer &m_server;
    boost::weak_ptr<Connection> m_connection;

    bool m_active;
    int m_priority;

    void close(const Caller_t &caller);

public:
    Session(DBusServer &server,
            uint32_t session);
    ~Session();

    enum {
        PRI_DEFAULT = 0,
        PRI_CONNECTION = 10
    };

    /**
     * Default priority is 0. Higher means less important.
     */
    void setPriority(int priority) { m_priority = priority; }
    int getPriority() const { return m_priority; }

    void setConnection(const boost::weak_ptr<Connection> c) { m_connection = c; }
    boost::weak_ptr<Connection> getConnection() { return m_connection; }

    /**
     * activate D-Bus object, session itself not ready yet
     */
    void activate();

    /**
     * called when the session is ready to run (true) or
     * lost the right to make changes (false)
     */
    void setActive(bool active);
};


/**
 * Represents and implements the Connection interface.
 *
 * The connection interacts with a Session by creating the Session and
 * exchanging data with it. For that, the connection registers itself
 * with the Session and unregisters again when it goes away.
 *
 * In contrast to clients, the Session only keeps a weak_ptr, which
 * becomes invalid when the referenced object gets deleted. Typically
 * this means the Session has to abort, unless reconnecting is
 * supported.
 */
class Connection : public DBusObjectHelper, public Resource
{
    DBusServer &m_server;
    std::map<std::string, std::string> m_peer;
    bool m_mustAuthenticate;
    enum {
        SETUP,          /**< ready for first message */
        PROCESSING,     /**< received message, waiting for engine's reply */
        WAITING,        /**< waiting for next follow-up message */
        FINAL,          /**< engine has sent final reply, wait for ACK by peer */
        DONE,           /**< peer has closed normally after the final reply */
        FAILED          /**< in a failed state, no further operation possible */
    } m_state;
    std::string m_failure;

    const uint32_t m_sessionNum;
    boost::shared_ptr<Session> m_session;

    /**
     * records the reason for the failure, sends Abort signal and puts
     * the connection into the FAILED state.
     */
    void failed(const std::string &reason);

    /**
     * returns "<description> (<ID> via <transport> <transport_description>)"
     */
    static std::string buildDescription(const std::map<std::string, std::string> &peer);

    void process(const Caller_t &caller,
                 const std::pair<size_t, const uint8_t *> &message,
                 const std::string &message_type);
    void close(const Caller_t &caller,
               bool normal,
               const std::string &error);
    EmitSignal0 abort;
    EmitSignal5<const std::pair<size_t, const uint8_t *> &,
                const std::string &,
                const std::map<std::string, std::string> &,
                bool,
                uint32_t> reply;

public:
    const std::string m_description;

    Connection(DBusServer &server,
               const DBusConnectionPtr &conn,
               uint32_t session_num,
               const std::map<std::string, std::string> &peer,
               bool must_authenticate);

    ~Connection();

    void activate();

    void ready();
};

/***************** Session implementation ***********************/

void Session::close(const Caller_t &caller)
{
    boost::shared_ptr<Client> client(m_server.findClient(caller));
    if (!client) {
        throw runtime_error("unknown client");
    }
    client->detach(this);
}

Session::Session(DBusServer &server,
                 uint32_t session) :
    DBusObjectHelper(server.getConnection(),
                     StringPrintf("/org/syncevolution/Session/%u", session),
                     "org.syncevolution.Session"),
    m_server(server),
    m_active(false),
    m_priority(PRI_DEFAULT)
{}

Session::~Session()
{
    m_server.dequeue(this);
}
    

void Session::activate()
{
    static GDBusMethodTable methods[] = {
        makeMethodEntry<Session,
                        const Caller_t &,
                        typeof(&Session::close), &Session::close>
                        ("Close"),
        {}
    };

    static GDBusSignalTable signals[] = {
        { },
    };

    DBusObjectHelper::activate(methods,
                               signals,
                               NULL,
                               this);
}

void Session::setActive(bool active)
{
    m_active = active;
    if (active) {
        boost::shared_ptr<Connection> c = m_connection.lock();
        if (c) {
            c->ready();
        }
    }
}

/************************ Connection implementation *****************/

void Connection::failed(const std::string &reason)
{
    if (m_failure.empty()) {
        m_failure = reason;
    }
    if (m_state != FAILED) {
        abort();
    }
    m_state = FAILED;
}

std::string Connection::buildDescription(const std::map<std::string, std::string> &peer)
{
    std::map<std::string, std::string>::const_iterator
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
             const std::pair<size_t, const uint8_t *> &message,
             const std::string &message_type)
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s sends %lu bytes, %s",
                 caller.c_str(),
                 message.first,
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

    switch (m_state) {
    case SETUP: {
        // TODO: check message type, determine whether we act
        // as client or server, choose config, create Session, ...

        // For the time being, request a session, then when it
        // is ready, send a dummy reply.
        m_session.reset(new Session(m_server,
                                    m_sessionNum));
        m_session->setPriority(Session::PRI_CONNECTION);
        m_session->setConnection(myself);
        m_server.enqueue(m_session);
        break;
    }
    case WAITING:
        throw std::runtime_error("not implemented yet");

        // TODO: pass message to session
        break;
    case FINAL:
    case DONE:
        throw std::runtime_error("protocol error: final reply sent, no further message processing possible");
        break;
    case FAILED:
        throw std::runtime_error(m_failure);
        break;
    default:
        throw std::runtime_error("protocol error: unknown internal state");
        break;
    }            
}

void Connection::close(const Caller_t &caller,
                       bool normal,
                       const std::string &error)
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s closes %s%s%s",
                 caller.c_str(),
                 normal ? "normally" : "with error",
                 error.empty() ? "" : ": ",
                 error.c_str());

    boost::shared_ptr<Client> client(m_server.findClient(caller));
    if (!client) {
        throw runtime_error("unknown client");
    }

    if (!normal ||
        m_state != FINAL) {
        failed(error.empty() ?
               "connection closed unexpectedly" :
               error);
    } else {
        m_state = DONE;
    }

    // remove reference to us from client, will destruct *this*
    // instance!
    client->detach(this);
}

Connection::Connection(DBusServer &server,
           const DBusConnectionPtr &conn,
           uint32_t session_num,
           const std::map<std::string, std::string> &peer,
           bool must_authenticate) :
    DBusObjectHelper(conn.get(),
                     StringPrintf("/org/syncevolution/Connection/%u", session_num),
                     "org.syncevolution.Connection"),
    m_server(server),
    m_peer(peer),
    m_mustAuthenticate(must_authenticate),
    m_state(SETUP),
    m_sessionNum(session_num),
    abort(*this, "Abort"),
    reply(*this, "Reply"),
    m_description(buildDescription(peer))
{}

Connection::~Connection()
{
    SE_LOG_DEBUG(NULL, NULL, "done with connection to '%s'%s%s%s",
                 m_description.c_str(),
                 m_state == DONE ? ", normal shutdown" : " unexpectedly",
                 m_failure.empty() ? "" : ": ",
                 m_failure.c_str());
    if (m_state != DONE) {
        abort();
    }
    m_session.use_count();
    m_session.reset();
}

void Connection::activate()
{
    static GDBusMethodTable methods[] = {
        makeMethodEntry<Connection,
                        const Caller_t &,
                        const std::pair<size_t, const uint8_t *> &,
                        const std::string &,
                        typeof(&Connection::process), &Connection::process>
                        ("Process"),
        makeMethodEntry<Connection,
                        const Caller_t &,
                        bool,
                        const std::string &,
                        typeof(&Connection::close), &Connection::close>
                        ("Close"),
        {}
    };

    static GDBusSignalTable signals[] = {
        abort.makeSignalEntry("Abort"),
        reply.makeSignalEntry("Reply"),
        { },
    };

    DBusObjectHelper::activate(methods,
                               signals,
                               NULL,
                               this);
}

void Connection::ready()
{
    // TODO: proceed with sync now that our session is ready

    // dummy reply
    m_state = WAITING;
    const char msg[] = "hello world";
    try {
        reply(std::make_pair(sizeof(msg) - 1, (const uint8_t *)msg),
              "dummy_type", std::map<std::string, std::string>(), true, m_sessionNum);
    } catch (...) {
        failed("sending reply failed");
        throw;
    }
}

/********************** DBusServer implementation ******************/

void DBusServer::clientGone(Client *c)
{
    for(Clients_t::iterator it = m_clients.begin();
        it != m_clients.end();
        ++it) {
        if (it->second.get() == c) {
            SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s has disconnected",
                         c->m_ID.c_str());
            m_clients.erase(it);
            return;
        }
    }
    SE_LOG_DEBUG(NULL, NULL, "unknown client has disconnected?!");
}

uint32_t DBusServer::getNextSession()
{
    m_lastSession++;
    if (!m_lastSession) {
        m_lastSession++;
    }
    return m_lastSession;
}

void DBusServer::connect(const Caller_t &caller,
                         const boost::shared_ptr<Watch> &watch,
                         const std::map<std::string, std::string> &peer,
                         bool must_authenticate,
                         uint32_t session,
                         DBusObject_t &object)
{
    if (session) {
        // reconnecting to old connection is not implemented yet
        throw std::runtime_error("not implemented");
    }
    uint32_t new_session = getNextSession();

    boost::shared_ptr<Connection> c(new Connection(*this,
                                                   getConnection(),
                                                   new_session,
                                                   peer,
                                                   must_authenticate));
    SE_LOG_DEBUG(NULL, NULL, "connecting D-Bus client %s with '%s'",
                 caller.c_str(),
                 c->m_description.c_str());
        
    boost::shared_ptr<Client> client = addClient(getConnection(),
                                                 caller,
                                                 watch);
    client->attach(c);
    c->activate();

    object = c->getPath();
}

void DBusServer::startSession(const Caller_t &caller,
                              const boost::shared_ptr<Watch> &watch,
                              const std::string &server,
                              DBusObject_t &object)
{
    boost::shared_ptr<Client> client = addClient(getConnection(),
                                                 caller,
                                                 watch);
    uint32_t new_session = getNextSession();   
    boost::shared_ptr<Session> session(new Session(*this,
                                                   new_session));
    client->attach(session);
    session->activate();
    enqueue(session);
    object = session->getPath();
}

DBusServer::DBusServer(const DBusConnectionPtr &conn) :
    DBusObjectHelper(conn.get(), "/org/syncevolution/Server", "org.syncevolution.Server"),
    m_lastSession(time(NULL)),
    m_activeSession(NULL),
    sessionChanged(*this, "SessionChanged")
{}

DBusServer::~DBusServer()
{
    // make sure all other objects are gone before destructing ourselves
    m_syncSession.reset();
    m_workQueue.clear();
    m_clients.clear();
}

void DBusServer::activate()
{
    static GDBusMethodTable methods[] = {
        makeMethodEntry<DBusServer,
                        const Caller_t &,
                        const boost::shared_ptr<Watch> &,
                        const std::map<std::string, std::string> &,
                        bool,
                        uint32_t,
                        DBusObject_t &,
                        typeof(&DBusServer::connect), &DBusServer::connect
                        >("Connect"),
        makeMethodEntry<DBusServer,
                        const Caller_t &,
                        const boost::shared_ptr<Watch> &,
                        const std::string &,
                        DBusObject_t &,
                        typeof(&DBusServer::startSession), &DBusServer::startSession
                        >("StartSession"),
        {}
    };

    static GDBusSignalTable signals[] = {
        sessionChanged.makeSignalEntry("SessionChanged"),
        { },
    };

    DBusObjectHelper::activate(methods,
                               signals,
                               NULL,
                               this);
}

/**
 * look up client by its ID
 */
boost::shared_ptr<Client> DBusServer::findClient(const Caller_t &ID)
{
    for(Clients_t::iterator it = m_clients.begin();
        it != m_clients.end();
        ++it) {
        if (it->second->m_ID == ID) {
            return it->second;
        }
    }
    return boost::shared_ptr<Client>();
}

boost::shared_ptr<Client> DBusServer::addClient(const DBusConnectionPtr &conn,
                                                const Caller_t &ID,
                                                const boost::shared_ptr<Watch> &watch)
{
    boost::shared_ptr<Client> client(findClient(ID));
    if (client) {
        return client;
    }
    client.reset(new Client(ID));
    // add to our list *before* checking that peer exists, so
    // that clientGone() can remove it if the check fails
    m_clients.push_back(std::make_pair(watch, client));
    watch->setCallback(boost::bind(&DBusServer::clientGone, this, client.get()));
    return client;
}

void DBusServer::enqueue(const boost::shared_ptr<Session> &session)
{
    WorkQueue_t::iterator it = m_workQueue.end();
    while (it != m_workQueue.begin()) {
        --it;
        if (it->lock()->getPriority() <= session->getPriority()) {
            ++it;
            break;
        }
    }
    m_workQueue.insert(it, session);

    checkQueue();
}

void DBusServer::dequeue(Session *session)
{
    if (m_syncSession.get() == session) {
        // This is the running sync session.
        // It's not in the work queue and we have to
        // keep it active, so nothing to do.
        return;
    }

    for (WorkQueue_t::iterator it = m_workQueue.begin();
         it != m_workQueue.end();
         ++it) {
        if (it->lock().get() == session) {
            // remove from queue
            m_workQueue.erase(it);
            // session was idle, so nothing else to do
            return;
        }
    }

    if (m_activeSession == session) {
        // The session is releasing the lock, so someone else might
        // run now.
        session->setActive(false);
        sessionChanged(session->getPath(), false);
        m_activeSession = NULL;
        checkQueue();
        return;
    }
}

void DBusServer::checkQueue()
{
    if (m_activeSession) {
        // still busy
        return;
    }

    while (!m_workQueue.empty()) {
        boost::shared_ptr<Session> session = m_workQueue.front().lock();
        m_workQueue.pop_front();
        if (session) {
            // activate the session
            m_activeSession = session.get();
            session->setActive(true);
            sessionChanged(session->getPath(), true);
            return;
        }
    }
}

/**************************** main *************************/

void niam(int sig)
{
    g_main_loop_quit (loop);
}

int main()
{
    try {
        g_type_init();
        g_thread_init(NULL);
        g_set_application_name("SyncEvolution");
        loop = g_main_loop_new (NULL, FALSE);

        signal(SIGTERM, niam);
        signal(SIGINT, niam);

        LoggerBase::instance().setLevel(LoggerBase::DEBUG);

        DBusErrorCXX err;
        DBusConnectionPtr conn = g_dbus_setup_bus(DBUS_BUS_SESSION,
                                                  "org.syncevolution",
                                                  &err);
        if (!conn) {
            err.throwFailure("g_dbus_setup_bus()");
        }

        DBusServer server(conn);
        server.activate();
        g_main_loop_run(loop);

	return 0;
    } catch ( const std::exception &ex ) {
        SE_LOG_ERROR(NULL, NULL, "%s", ex.what());
    } catch (...) {
        SE_LOG_ERROR(NULL, NULL, "unknown error");
    }

    return 1;
}
