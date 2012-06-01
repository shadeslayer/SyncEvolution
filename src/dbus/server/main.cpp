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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <iostream>

#include "server.h"
#include "restart.h"
#include "session-common.h"

#include <syncevo/SyncContext.h>
#include <syncevo/SuspendFlags.h>
#include <syncevo/LogRedirect.h>
#include <syncevo/LogSyslog.h>

using namespace SyncEvo;
using namespace GDBusCXX;

namespace {
    GMainLoop *loop = NULL;
    bool shutdownRequested = false;
    const char * const execName = "syncevo-dbus-server";
    const char * const debugEnv = "SYNCEVOLUTION_DEBUG";

void niam(int sig)
{
    shutdownRequested = true;
    SuspendFlags::getSuspendFlags().handleSignal(sig);
    g_main_loop_quit (loop);
}

bool parseDuration(int &duration, const char* value)
{
    if(value == NULL) {
        return false;
    } else if (boost::iequals(value, "unlimited")) {
        duration = -1;
        return true;
    } else if ((duration = atoi(value)) > 0) {
        return true;
    } else {
        return false;
    }
}

} // anonymous namespace

int main(int argc, char **argv, char **envp)
{
    // remember environment for restart
    boost::shared_ptr<Restart> restart;
    restart.reset(new Restart(argv, envp));

    int duration = 600;
    int opt = 1;
    while(opt < argc) {
        if(argv[opt][0] != '-') {
            break;
        }
        if (boost::iequals(argv[opt], "--duration") ||
            boost::iequals(argv[opt], "-d")) {
            opt++;
            if(!parseDuration(duration, opt== argc ? NULL : argv[opt])) {
                std::cout << argv[opt-1] << ": unknown parameter value or not set" << std::endl;
                return false;
            }
        } else {
            std::cout << argv[opt] << ": unknown parameter" << std::endl;
            return false;
        }
        opt++;
    }
    try {
        // Temporarily set G_DBUS_DEBUG. Hopefully GIO will read and
        // remember it, because we don't want to keep it set
        // permanently, lest it gets passed on to other processes.
        const char *gdbus = getenv("SYNCEVOLUTION_DBUS_SERVER_GDBUS");
        if (gdbus) {
            setenv("G_DBUS_DEBUG", gdbus, 1);
        }

        SyncContext::initMain(execName);

        loop = g_main_loop_new (NULL, FALSE);

        setvbuf(stderr, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IONBF, 0);

        const char *debugVar(getenv(debugEnv));
        const bool debugEnabled(debugVar && *debugVar);

        // TODO: redirect output *and* log it via syslog?!
        boost::shared_ptr<LoggerBase> logger;
        if (!gdbus) {
            logger.reset((true ||  debugEnabled) ?
                         static_cast<LoggerBase *>(new LogRedirect(true)) :
                         static_cast<LoggerBase *>(new LoggerSyslog(execName)));
        }

        // make daemon less chatty - long term this should be a command line option
        LoggerBase::instance().setLevel(debugEnabled ?
                                        LoggerBase::DEBUG :
                                        LoggerBase::INFO);

        // syncevo-dbus-server should hardly ever produce output that
        // is relevant for end users, so include the somewhat cryptic
        // process name for developers in this process, and not in
        // syncevo-dbus-helper.
        Logger::setProcessName("syncevo-dbus-server");

        SE_LOG_DEBUG(NULL, NULL, "syncevo-dbus-server: catch SIGINT/SIGTERM in our own shutdown function");
        signal(SIGTERM, niam);
        signal(SIGINT, niam);
        boost::shared_ptr<SuspendFlags::Guard> guard = SuspendFlags::getSuspendFlags().activate();

        DBusErrorCXX err;
        DBusConnectionPtr conn = dbus_get_bus_connection("SESSION",
                                                         SessionCommon::SERVICE_NAME,
                                                         true,
                                                         &err);
        if (!conn) {
            err.throwFailure("dbus_get_bus_connection()", " failed - server already running?");
        }
        // make this object the main owner of the connection
        boost::scoped_ptr<DBusObject> obj(new DBusObject(conn, "foo", "bar", true));

        boost::scoped_ptr<SyncEvo::Server> server(new SyncEvo::Server(loop, shutdownRequested, restart, conn, duration));
        server->activate();

        if (gdbus) {
            unsetenv("G_DBUS_DEBUG");
        }

        server->run();
        SE_LOG_DEBUG(NULL, NULL, "cleaning up");
        server.reset();
        conn.reset();
        obj.reset();
        guard.reset();
        SE_LOG_INFO(NULL, NULL, "terminating");
        return 0;
    } catch ( const std::exception &ex ) {
        SE_LOG_ERROR(NULL, NULL, "%s", ex.what());
    } catch (...) {
        SE_LOG_ERROR(NULL, NULL, "unknown error");
    }

    return 1;
}
