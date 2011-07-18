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

#ifndef AUTOTERM_H
#define AUTOTERM_H

#include <syncevo/SmartPtr.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Automatic termination and track clients
 * The dbus server will automatic terminate once it is idle in a given time.
 * If any attached clients or connections, it never terminate.
 * Once no actives, timer is started to detect the time of idle.
 * Note that there will be less-than TERM_INTERVAL inaccuracy in seconds,
 * that's because we do check every TERM_INTERVAL seconds.
 */
class AutoTerm {
    GMainLoop *m_loop;
    bool &m_shutdownRequested;
    int m_refs;
    time_t m_interval;
    guint m_checkSource;
    time_t m_lastUsed;

    /**
     * This callback is called as soon as we might have to terminate.
     * If it finds that the server has been used in the meantime, it
     * will simply set another timeout and check again later.
     */
    static gboolean checkCallback(gpointer data) {
        AutoTerm *at = static_cast<AutoTerm*>(data);
        if (!at->m_refs) {
            // currently idle, but also long enough?
            time_t now = time(NULL);
            if (at->m_lastUsed + at->m_interval <= now) {
                // yes, shut down event loop and daemon
                SE_LOG_DEBUG(NULL, NULL, "terminating because not in use and idle for more than %ld seconds", (long)at->m_interval);
                at->m_shutdownRequested = true;
                g_main_loop_quit(at->getLoop());
            } else {
                // check again later
                SE_LOG_DEBUG(NULL, NULL, "not terminating because last used %ld seconds ago, check again in %ld seconds",
                             (long)(now - at->m_lastUsed),
                             (long)(at->m_lastUsed + at->m_interval - now));
                at->m_checkSource = g_timeout_add_seconds(at->m_lastUsed + at->m_interval - now,
                                                          checkCallback,
                                                          data);
            }
        } else {
            SE_LOG_DEBUG(NULL, NULL, "not terminating, not renewing timeout because busy");
        }
        // always remove the current timeout, its job is done
        return FALSE;
    }

public:
    /**
     * constructor
     * If interval is less than 0, it means 'unlimited' and never terminate
     */
    AutoTerm(GMainLoop *loop, bool &shutdownRequested, int interval) :
        m_loop(loop),
        m_shutdownRequested(shutdownRequested),
        m_refs(0),
        m_checkSource(0),
        m_lastUsed(0)
    {
        if (interval <= 0) {
            m_interval = 0;
            // increasing reference counts prevents shutdown forever
            ref();
        } else {
            m_interval = interval;
        }
        reset();
    }

    ~AutoTerm()
    {
        if (m_checkSource) {
            g_source_remove(m_checkSource);
        }
    }

    /** access to the GMainLoop. */
    GMainLoop *getLoop() { return m_loop; }

    //increase the actives objects
    void ref(int refs = 1) {
        m_refs += refs;
        reset();
    }

    //decrease the actives objects
    void unref(int refs = 1) {
        m_refs -= refs;
        if(m_refs <= 0) {
           m_refs = 0;
        }
        reset();
    }

    /**
     * To be called each time the server interacts with a client,
     * which includes adding or removing a client. If necessary,
     * this installs a timeout to stop the daemon when it has been
     * idle long enough.
     */
    void reset()
    {
        if (m_refs > 0) {
            // in use, don't need timeout
            if (m_checkSource) {
                SE_LOG_DEBUG(NULL, NULL, "deactivating idle termination because in use");
                g_source_remove(m_checkSource);
                m_checkSource = 0;
            }
        } else {
            // An already active timeout will trigger at the chosen time,
            // then notice that the server has been used in the meantime and
            // reset the timer. Therefore we don't have to remove it.
            m_lastUsed = time(NULL);
            if (!m_checkSource) {
                SE_LOG_DEBUG(NULL, NULL, "activating idle termination in %ld seconds because idle", m_interval);
                m_checkSource = g_timeout_add_seconds(m_interval,
                                                      checkCallback,
                                                      static_cast<gpointer>(this));
            }
        }
    }
};

SE_END_CXX

#endif // AUTOTERM_H
