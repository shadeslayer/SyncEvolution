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

#ifndef TIMEOUT_H
#define TIMEOUT_H

#include <syncevo/SmartPtr.h>

#include <boost/utility.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Utility class which makes it easier to work with g_timeout_add_seconds().
 * Instantiate this class with a specific callback. Use boost::bind()
 * to attach specific parameters to that callback. Then activate
 * the timeout. Destructing this class will automatically remove
 * the timeout and thus ensure that it doesn't trigger without
 * valid parameters.
 */
class Timeout : boost::noncopyable
{
    guint m_tag;
    boost::function<bool ()> m_callback;

public:
    Timeout() :
        m_tag(0)
    {
    }

    ~Timeout()
    {
        if (m_tag) {
            g_source_remove(m_tag);
        }
    }

    /**
     * call the callback at regular intervals until it returns false
     */
    void activate(int seconds,
                  const boost::function<bool ()> &callback)
    {
        deactivate();

        m_callback = callback;
        m_tag = g_timeout_add_seconds(seconds, triggered, static_cast<gpointer>(this));
        if (!m_tag) {
            SE_THROW("g_timeout_add_seconds() failed");
        }
    }

    /**
     * stop calling the callback, drop callback
     */
    void deactivate()
    {
        if (m_tag) {
            g_source_remove(m_tag);
            m_tag = 0;
        }
        m_callback = 0;
    }

    /** true iff active */
    operator bool () const { return m_tag != 0; }

private:
    static gboolean triggered(gpointer data)
    {
        Timeout *me = static_cast<Timeout *>(data);
        return me->m_callback();
    }
};

SE_END_CXX

#endif // TIMEOUT_H
