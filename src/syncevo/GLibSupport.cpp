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

#include <syncevo/GLibSupport.h>
#include <syncevo/util.h>

#include <string.h>

SE_BEGIN_CXX

#ifdef HAVE_GLIB

class Select {
    GMainLoop *m_loop;
    GMainContext *m_context;
    struct FDSource;
    FDSource *m_source;
    Timespec m_deadline;
    GPollFD m_pollfd;
    GLibSelectResult m_result;

    struct FDSource
    {
        GSource m_source;
        Select *m_select;

        static gboolean prepare(GSource *source,
                                gint *timeout)
        {
            FDSource *fdsource = (FDSource *)source;
            if (!fdsource->m_select->m_deadline) {
                *timeout = -1;
                return FALSE;
            }

            Timespec now = Timespec::monotonic();
            if (now < fdsource->m_select->m_deadline) {
                Timespec delta = fdsource->m_select->m_deadline - now;
                *timeout = delta.tv_sec * 1000 + delta.tv_nsec / 1000000;
                return FALSE;
            } else {
                fdsource->m_select->m_result = GLIB_SELECT_TIMEOUT;
                *timeout = 0;
                return TRUE;
            }
        }

        static gboolean check(GSource *source)
        {
            FDSource *fdsource = (FDSource *)source;
            if (fdsource->m_select->m_pollfd.revents) {
                fdsource->m_select->m_result = GLIB_SELECT_READY;
                return TRUE;
            } else {
                return FALSE;
            }
        }

        static gboolean dispatch(GSource *source,
                                 GSourceFunc callback,
                                 gpointer user_data)
        {
            FDSource *fdsource = (FDSource *)source;
            g_main_loop_quit(fdsource->m_select->m_loop);
            return FALSE;
        }

        static GSourceFuncs m_funcs;
    };

public:
    Select(GMainLoop *loop, int fd, int direction, Timespec *timeout) :
        m_loop(loop),
        m_context(g_main_loop_get_context(loop)),
        m_result(GLIB_SELECT_QUIT)
    {
        if (timeout) {
            m_deadline = Timespec::monotonic() + *timeout;
        }

        memset(&m_pollfd, 0, sizeof(m_pollfd));
        m_source = (FDSource *)g_source_new(&FDSource::m_funcs, sizeof(FDSource));
        if (!m_source) {
            SE_THROW("no FDSource");
        }
        m_source->m_select = this;
        m_pollfd.fd = fd;
        if (fd >= 0 &&
            direction != GLIB_SELECT_NONE) {
            if (direction & GLIB_SELECT_READ) {
                m_pollfd.events |= G_IO_IN | G_IO_HUP | G_IO_ERR;
            }
            if (direction & GLIB_SELECT_WRITE) {
                m_pollfd.events |= G_IO_OUT | G_IO_ERR;
            }
            g_source_add_poll(&m_source->m_source, &m_pollfd);
        }
        g_source_attach(&m_source->m_source, m_context);
    }

    ~Select()
    {
        if (m_source) {
            g_source_destroy(&m_source->m_source);
        }
    }

    GLibSelectResult run()
    {
        g_main_loop_run(m_loop);
        return m_result;
    }
};

GSourceFuncs Select::FDSource::m_funcs = {
    Select::FDSource::prepare,
    Select::FDSource::check,
    Select::FDSource::dispatch
};

GLibSelectResult GLibSelect(GMainLoop *loop, int fd, int direction, Timespec *timeout)
{
    Select instance(loop, fd, direction, timeout);
    return instance.run();
}

#else // HAVE_GLIB

GLibSelectResult GLibSelect(GMainLoop *loop, int fd, int direction, Timespec *timeout)
{
    SE_THROW("GLibSelect() not implemented without glib support");
    return GLIB_SELECT_QUIT;
}

#endif // HAVE_GLIB

SE_END_CXX
