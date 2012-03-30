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

#include "ForkExec.h"

#if defined(HAVE_GLIB)

SE_BEGIN_CXX

static const std::string ForkExecEnvVar("SYNCEVOLUTION_FORK_EXEC=");

ForkExec::ForkExec()
{
}

ForkExecParent::ForkExecParent(const std::string &helper) :
    m_helper(helper),
    m_childPid(0),
    m_hasConnected(false),
    m_hasQuit(false),
    m_sigIntSent(false),
    m_sigTermSent(false),
    m_watchChild(NULL)
{
}

boost::shared_ptr<ForkExecParent> ForkExecParent::create(const std::string &helper)
{
    boost::shared_ptr<ForkExecParent> forkexec(new ForkExecParent(helper));
    return forkexec;
}

ForkExecParent::~ForkExecParent()
{
    if (m_watchChild) {
        // stop watching
        g_source_destroy(m_watchChild);
        g_source_unref(m_watchChild);
    }
    if (m_childPid) {
        g_spawn_close_pid(m_childPid);
    }
}

/**
 * Redirect stdout to stderr.
 *
 * Child setup function, called insided forked process before exec().
 * only async-signal-safe functions allowed according to http://developer.gnome.org/glib/2.30/glib-Spawning-Processes.html#GSpawnChildSetupFunc
 */
static void setStdoutToStderr(gpointer /* user_data */) throw()
{
    // dup2(STDERR_FILENO, STDOUT_FILENO);
}

void ForkExecParent::start()
{
    if (m_watchChild) {
        SE_THROW("child already started");
    }

    // boost::shared_ptr<ForkExecParent> me = ...;
    GDBusCXX::DBusErrorCXX dbusError;

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: preparing for child process %s", m_helper.c_str());
    m_server = GDBusCXX::DBusServerCXX::listen("", &dbusError);
    if (!m_server) {
        dbusError.throwFailure("starting server");
    }
    m_server->setNewConnectionCallback(boost::bind(&ForkExecParent::newClientConnection, this, _2));

    // look for helper binary
    std::string helper;
    GSpawnFlags flags = G_SPAWN_DO_NOT_REAP_CHILD;
    if (m_helper.find('/') == m_helper.npos) {
        helper = getEnv("SYNCEVOLUTION_LIBEXEC_DIR", "");
        if (helper.empty()) {
            // env variable not set, look in libexec dir
            helper = SYNCEVO_LIBEXEC;
            helper += "/";
            helper += m_helper;
            if (access(helper.c_str(), R_OK)) {
                // some error, try PATH
                flags = (GSpawnFlags)(flags | G_SPAWN_SEARCH_PATH);
                helper = m_helper;
            }
        } else {
            // use env variable without further checks, must work
            helper += "/";
            helper += m_helper;
        }
    } else {
        // absolute path, use it
        helper = m_helper;
    }

    m_argvStrings.push_back(helper);
    m_argv.reset(AllocStringArray(m_argvStrings));
    for (char **env = environ;
         *env;
         env++) {
        if (!boost::starts_with(*env, ForkExecEnvVar)) {
            m_envStrings.push_back(*env);
        }
    }

    // pass D-Bus address via env variable
    m_envStrings.push_back(ForkExecEnvVar + m_server->getAddress());
    m_env.reset(AllocStringArray(m_envStrings));

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: running %s with D-Bus address %s",
                 helper.c_str(), m_server->getAddress().c_str());
    GErrorCXX gerror;
    if (!g_spawn_async_with_pipes(NULL, // working directory
                                  static_cast<gchar **>(m_argv.get()),
                                  static_cast<gchar **>(m_env.get()),
                                  flags,
                                  setStdoutToStderr, // child setup function: redirect stdout to stderr where it will be caught by our own output redirection code
                                  // TODO: avoid logging child errors as "[ERROR] stderr: [ERROR] onConnect not implemented"
                                  // TODO: log child INFO messages?
                                  NULL, // child setup user data
                                  &m_childPid,
                                  NULL, NULL, NULL, // stdin/out/error pipes
                                  gerror)) {
        m_childPid = 0;
        gerror.throwError("spawning child");
    }

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: child process for %s has pid %ld",
                 helper.c_str(), (long)m_childPid);

    // TODO: introduce C++ wrapper around GSource
    m_watchChild = g_child_watch_source_new(m_childPid);
    g_source_set_callback(m_watchChild, (GSourceFunc)watchChildCallback, this, NULL);
    g_source_attach(m_watchChild, NULL);
}

void ForkExecParent::watchChildCallback(GPid pid,
                                        gint status,
                                        gpointer data) throw()
{
    ForkExecParent *me = static_cast<ForkExecParent *>(data);
    try {
        me->m_hasQuit = true;
        me->m_onQuit(status);
        if (!me->m_hasConnected ||
            status != 0) {
            SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: child was signaled %s, signal %d, int %d, term %d, int sent %s, term sent %s",
                         WIFSIGNALED(status) ? "yes" : "no",
                         WTERMSIG(status), SIGINT, SIGTERM,
                         me->m_sigIntSent ? "yes" : "no",
                         me->m_sigTermSent ? "yes" : "no");
            if (WIFSIGNALED(status) &&
                ((WTERMSIG(status) == SIGINT && me->m_sigIntSent) ||
                 (WTERMSIG(status) == SIGTERM && me->m_sigTermSent))) {
                // not an error when the child dies because we killed it
                return;
            }
            std::string error = "child process quit";
            if (!me->m_hasConnected) {
                error += " unexpectedly";
            }
            if (WIFEXITED(status)) {
                error += StringPrintf(" with return code %d", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                error += StringPrintf(" because of signal %d\n", WTERMSIG(status));
            } else {
                error += " for unknown reasons";
            }
            SE_LOG_ERROR(NULL, NULL, "%s", error.c_str());
            me->m_onFailure(STATUS_FATAL, error);
        }
    } catch (...) {
        std::string explanation;
        SyncMLStatus status = Exception::handle(explanation);
        try {
            me->m_onFailure(status, explanation);
        } catch (...) {
            Exception::handle();
        }
    }
}

void ForkExecParent::newClientConnection(GDBusCXX::DBusConnectionPtr &conn) throw()
{
    try {
        SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: child %s has connected",
                     m_helper.c_str());
        m_hasConnected = true;
        m_onConnect(conn);
    } catch (...) {
        std::string explanation;
        SyncMLStatus status = Exception::handle(explanation);
        try {
            m_onFailure(status, explanation);
        } catch (...) {
            Exception::handle();
        }
    }
}

void ForkExecParent::addEnvVar(const std::string &name, const std::string &value)
{
    if(!name.empty()) {
        m_envStrings.push_back(name + "=" + value);
    }
}

void ForkExecParent::stop(int signal)
{
    if (!m_childPid || m_hasQuit) {
        // not running, nop
        return;
    }

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: killing %s with signal %d (%s %s)",
                 m_helper.c_str(),
                 signal,
                 (!signal || signal == SIGINT) ? "SIGINT" : "",
                 (!signal || signal == SIGTERM) ? "SIGTERM" : "");
    if (!signal || signal == SIGINT) {
        ::kill(m_childPid, SIGINT);
        m_sigIntSent = true;
    }
    if (!signal || signal == SIGTERM) {
        ::kill(m_childPid, SIGTERM);
        m_sigTermSent = true;
    }
    if (signal && signal != SIGINT && signal != SIGTERM) {
        ::kill(m_childPid, signal);
    }
}

void ForkExecParent::kill()
{
    if (!m_childPid || m_hasQuit) {
        // not running, nop
        return;
    }

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: killing %s with SIGKILL",
                 m_helper.c_str());
    ::kill(m_childPid, SIGKILL);
}

ForkExecChild::ForkExecChild()
{
}

boost::shared_ptr<ForkExecChild> ForkExecChild::create()
{
    boost::shared_ptr<ForkExecChild> forkexec(new ForkExecChild);
    return forkexec;
}

void ForkExecChild::connect()
{
    const char *address = getParentDBusAddress();
    if (!address) {
        SE_THROW("cannot connect to parent, was not forked");
    }

    SE_LOG_DEBUG(NULL, NULL, "ForkExecChild: connecting to parent with D-Bus address %s",
                 address);
    GDBusCXX::DBusErrorCXX dbusError;
    GDBusCXX::DBusConnectionPtr conn = dbus_get_bus_connection(address,
                                                               &dbusError,
                                                               true /* always delay message processing */);
    if (!conn) {
        dbusError.throwFailure("connecting to server");
    }
    m_onConnect(conn);
    dbus_bus_connection_undelay(conn);
}

bool ForkExecChild::wasForked()
{
    return getParentDBusAddress() != NULL;
}

const char *ForkExecChild::getParentDBusAddress()
{
    return getenv(ForkExecEnvVar.substr(0, ForkExecEnvVar.size() - 1).c_str());
}

SE_END_CXX

#endif // HAVE_GLIB
