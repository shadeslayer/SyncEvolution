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

#ifndef CMD_LINE_WRAPPER_H
#define CMD_LINE_WRAPPER_H

#include <syncevo/Cmdline.h>

#include "dbus-sync.h"

SE_BEGIN_CXX

/**
 * A wrapper to maintain the execution of command line arguments from
 * dbus clients. It creates the DBusSync instance when required and
 * sets up the same environment as in the D-Bus client.
 */
class CmdlineWrapper
{
    class DBusCmdline : public Cmdline {
        Session &m_session;
    public:
        DBusCmdline(Session &session,
                    const vector<string> &args) :
            Cmdline(args),
            m_session(session)
        {}

        SyncContext* createSyncClient() {
            return new DBusSync(m_server, m_session);
        }
    };

    /** instance to run command line arguments */
    DBusCmdline m_cmdline;

    /** environment variables passed from client */
    map<string, string> m_envVars;

public:
    CmdlineWrapper(Session &session,
                   const vector<string> &args,
                   const map<string, string> &vars) :
        m_cmdline(session, args),
        m_envVars(vars)
    {}

    bool parse() { return m_cmdline.parse(); }
    bool run(LogRedirect &redirect)
    {
        bool success = true;

        //temporarily set environment variables and restore them after running
        list<boost::shared_ptr<ScopedEnvChange> > changes;
        BOOST_FOREACH(const StringPair &var, m_envVars) {
            changes.push_back(boost::shared_ptr<ScopedEnvChange>(new ScopedEnvChange(var.first, var.second)));
        }
        // exceptions must be handled (= printed) before returning,
        // so that our client gets the output
        try {
            success = m_cmdline.run();
        } catch (...) {
            redirect.flush();
            throw;
        }
        // always forward all currently pending redirected output
        // before closing the session
        redirect.flush();

        return success;
    }

    bool configWasModified() { return m_cmdline.configWasModified(); }
};

SE_END_CXX

#endif // CMD_LINE_WRAPPER_H
