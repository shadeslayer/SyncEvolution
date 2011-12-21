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

SE_BEGIN_CXX

static const std::string ForkExecEnvVar("SYNCEVOLUTION_FORK_EXEC=");

ForkExec::ForkExec()
{
}

ForkExecParent::ForkExecParent(const std::string &helper) :
    m_helper(helper),
    m_childPid(0),
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

void ForkExecParent::start()
{
    if (m_watchChild) {
        SE_THROW("child already started");
    }

    // boost::shared_ptr<ForkExecParent> me = ...;
    GDBusCXX::DBusErrorCXX dbusError;

    m_server = GDBusCXX::DBusServerCXX::listen("", &dbusError);
    if (!m_server) {
        dbusError.throwFailure("starting server");
    }
    m_server->setNewConnectionCallback(boost::bind(&ForkExecParent::newClientConnection, this, _2));

    m_argvStrings.push_back(m_helper);
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

    GErrorCXX gerror;
    if (!g_spawn_async_with_pipes(NULL, // working directory
                                  static_cast<gchar **>(m_argv.get()),
                                  static_cast<gchar **>(m_env.get()),
                                  (GSpawnFlags)((m_helper.find('/') == m_helper.npos ? G_SPAWN_SEARCH_PATH : 0) |
                                                G_SPAWN_DO_NOT_REAP_CHILD),
                                  NULL, // child setup function TODO: redirect stdout to stderr where it will be caught by our own output redirection code
                                  NULL, // child setup user data
                                  &m_childPid,
                                  NULL, NULL, NULL, // stdin/out/error pipes
                                  gerror)) {
        m_childPid = 0;
        gerror.throwError("spawning child");
    }

    // TODO: introduce C++ wrapper around GSource
    m_watchChild = g_child_watch_source_new(m_childPid);
    g_source_set_callback(m_watchChild, (GSourceFunc)watchChildCallback, this, NULL);
    g_source_attach(m_watchChild, NULL);
}

void ForkExecParent::watchChildCallback(GPid pid,
                                        gint status,
                                        gpointer data)
{
    ForkExecParent *me = static_cast<ForkExecParent *>(data);
    me->m_onQuit(status);
}

void ForkExecParent::newClientConnection(GDBusCXX::DBusConnectionPtr &conn)
{
    m_onConnect(conn);
}


void ForkExecParent::stop()
{
}

void ForkExecParent::kill()
{
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

    GDBusCXX::DBusErrorCXX dbusError;
    GDBusCXX::DBusConnectionPtr conn = dbus_get_bus_connection(address,
                                                               &dbusError);
    if (!conn) {
        dbusError.throwFailure("connecting to server");
    }
    m_onConnect(conn);
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

