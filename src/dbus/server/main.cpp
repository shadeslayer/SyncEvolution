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

#include <syncevo/SyncContext.h>
#include <syncevo/SuspendFlags.h>

using namespace SyncEvo;
using namespace GDBusCXX;

namespace {
    GMainLoop *loop = NULL;
    bool shutdownRequested = false;
}

void niam(int sig)
{
    shutdownRequested = true;
    SuspendFlags::getSuspendFlags().handleSignal(sig);
    g_main_loop_quit (loop);
}

static bool parseDuration(int &duration, const char* value)
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
        SyncContext::initMain("syncevo-dbus-server");

        loop = g_main_loop_new (NULL, FALSE);

        setvbuf(stderr, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IONBF, 0);

        LogRedirect redirect(true);

        // make daemon less chatty - long term this should be a command line option
        LoggerBase::instance().setLevel(getenv("SYNCEVOLUTION_DEBUG") ?
                                        LoggerBase::DEBUG :
                                        LoggerBase::INFO);

        SE_LOG_DEBUG(NULL, NULL, "syncevo-dbus-server: catch SIGINT/SIGTERM in our own shutdown function");
        signal(SIGTERM, niam);
        signal(SIGINT, niam);

        DBusErrorCXX err;
        DBusConnectionPtr conn = dbus_get_bus_connection("SESSION",
                                                         "org.syncevolution",
                                                         true,
                                                         &err);
        if (!conn) {
            err.throwFailure("dbus_get_bus_connection()", " failed - server already running?");
        }
        // make this object the main owner of the connection
        DBusObject obj(conn, "foo", "bar", true);

        SyncEvo::Server server(loop, shutdownRequested, restart, conn, duration);
        server.activate();

        SE_LOG_INFO(NULL, NULL, "%s: ready to run",  argv[0]);
        server.run(redirect);
        SE_LOG_INFO(NULL, NULL, "%s: terminating",  argv[0]);
        return 0;
    } catch ( const std::exception &ex ) {
        SE_LOG_ERROR(NULL, NULL, "%s", ex.what());
    } catch (...) {
        SE_LOG_ERROR(NULL, NULL, "unknown error");
    }

    return 1;
}
