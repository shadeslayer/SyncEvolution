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
 * a wrapper to maintain the execution of command line
 * arguments from dbus clients. It is in charge of
 * redirecting output of cmd line to logging system.
 */
class CmdlineWrapper
{
    /**
     * inherit from stream buf to redirect the output.
     * Set a log until we gets a '\n' separator since we know
     * the command line message often ends with '\n'. The reason
     * is to avoid setting less characters in one log and thus
     * sending many signals to dbus clients.
     */
    class CmdlineStreamBuf : public std::streambuf
    {
    public:
        virtual ~CmdlineStreamBuf()
        {
            //flush cached characters
            if(!m_str.empty()) {
                SE_LOG(LoggerBase::SHOW, NULL, NULL, "%s", m_str.c_str());
            }
        }
    protected:
        /**
         * inherit from std::streambuf, all characters are cached in m_str
         * until a character '\n' is reached.
         */
        virtual int_type overflow (int_type ch) {
            if(ch == '\n') {
                //don't append this character for logging system will append it
                SE_LOG(LoggerBase::SHOW, NULL, NULL, "%s", m_str.c_str());
                m_str.clear();
            } else if (ch != EOF) {
                m_str += ch;
            }
            return ch;
        }

        /** the cached output characters */
        string m_str;
    };

    /** streambuf used for m_cmdlineOutStream */
    CmdlineStreamBuf m_outStreamBuf;

    /** stream for command line out and err arguments */
    std::ostream m_cmdlineOutStream;

    /**
     * implement factory method to create DBusSync instances
     * This can check 'abort' and 'suspend' command from clients.
     */
    class DBusCmdline : public Cmdline {
        Session &m_session;
    public:
        DBusCmdline(Session &session,
                    const vector<string> &args,
                    ostream &out,
                    ostream &err)
            :Cmdline(args, out, err), m_session(session)
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
    /**
     * constructor to create cmdline instance.
     * Here just one stream is used and error message in
     * command line is output to this stream for it is
     * different from Logger::ERROR.
     */
    CmdlineWrapper(Session &session,
                   const vector<string> &args,
                   const map<string, string> &vars)
        : m_cmdlineOutStream(&m_outStreamBuf),
        m_cmdline(session, args, m_cmdlineOutStream, m_cmdlineOutStream),
        m_envVars(vars)
    {}

    bool parse() { return m_cmdline.parse(); }
    void run(LogRedirect &redirect)
    {
        //temporarily set environment variables and restore them after running
        list<boost::shared_ptr<ScopedEnvChange> > changes;
        BOOST_FOREACH(const StringPair &var, m_envVars) {
            changes.push_back(boost::shared_ptr<ScopedEnvChange>(new ScopedEnvChange(var.first, var.second)));
        }
        // exceptions must be handled (= printed) before returning,
        // so that our client gets the output
        try {
            if (!m_cmdline.run()) {
                SE_THROW_EXCEPTION(DBusSyncException, "command line execution failure");
            }

        } catch (...) {
            redirect.flush();
            throw;
        }
        // always forward all currently pending redirected output
        // before closing the session
        redirect.flush();
    }

    bool configWasModified() { return m_cmdline.configWasModified(); }
};

SE_END_CXX

#endif // CMD_LINE_WRAPPER_H
