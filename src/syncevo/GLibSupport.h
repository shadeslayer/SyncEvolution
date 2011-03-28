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

#ifndef INCL_GLIB_SUPPORT
# define INCL_GLIB_SUPPORT

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <syncevo/util.h>

#ifdef HAVE_GLIB
# include <glib-object.h>
# include <gio/gio.h>
#else
typedef void *GMainLoop;
#endif

#include <boost/intrusive_ptr.hpp>
#include <boost/utility.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

enum {
    GLIB_SELECT_NONE = 0,
    GLIB_SELECT_READ = 1,
    GLIB_SELECT_WRITE = 2
};

enum GLibSelectResult {
    GLIB_SELECT_TIMEOUT,      /**< returned because not ready after given amount of time */
    GLIB_SELECT_READY,        /**< fd is ready */
    GLIB_SELECT_QUIT          /**< something else caused the loop to quit, return to caller immediately */
};

/**
 * Waits for one particular file descriptor to become ready for reading
 * and/or writing. Keeps the given loop running while waiting.
 *
 * @param  loop       loop to keep running; must not be NULL
 * @param  fd         file descriptor to watch, -1 for none
 * @param  direction  read, write, both, or none (then fd is ignored)
 * @param  timeout    timeout in seconds + nanoseconds from now, NULL for no timeout, empty value for immediate return
 * @return see GLibSelectResult
 */
GLibSelectResult GLibSelect(GMainLoop *loop, int fd, int direction, Timespec *timeout);

#ifdef HAVE_GLIB

/**
 * Defines a shared pointer for a GObject-based type, with intrusive
 * reference counting. Use *outside* of SyncEvolution namespace
 * (i.e. outside of SE_BEGIN/END_CXX. This is necessary because some
 * functions must be put into the boost namespace. The type itself is
 * *inside* the SyncEvolution namespace.
 *
 * Example:
 * SE_GOBJECT_TYPE(GFile);
 * SE_BEGIN_CXX
 * {
 *   // reference normally increased during construction,
 *   // steal() avoids that
 *   GFileCXX filecxx = GFileCXX::steal(g_file_new_for_path("foo"));
 *   GFile *filec = filecxx.get(); // does not increase reference count
 *   // file freed here as filecxx gets destroyed
 * }
 * SE_END_CXX
 */
#define SE_GOBJECT_TYPE(_x) \
    void inline intrusive_ptr_add_ref(_x *ptr) { g_object_ref(ptr); } \
    void inline intrusive_ptr_release(_x *ptr) { g_object_unref(ptr); } \
    SE_BEGIN_CXX \
    class _x ## CXX : public boost::intrusive_ptr<_x> { \
    public: \
         _x ## CXX(_x *ptr, bool add_ref = true) : boost::intrusive_ptr<_x>(ptr, add_ref) {} \
         _x ## CXX() {} \
         _x ## CXX(const _x ## CXX &other) : boost::intrusive_ptr<_x>(other) {} \
\
         static  _x ## CXX steal(_x *ptr) { return _x ## CXX(ptr, false); } \
    }; \
    SE_END_CXX \

SE_END_CXX

SE_GOBJECT_TYPE(GFile)
SE_GOBJECT_TYPE(GFileMonitor)

SE_BEGIN_CXX

/**
 * Wrapper around g_file_monitor_file().
 * Not copyable because monitor is tied to specific callback
 * via memory address.
 */
class GLibNotify : public boost::noncopyable
{
 public:
    typedef boost::function<void (GFile *, GFile *, GFileMonitorEvent)> callback_t;

    GLibNotify(const char *file, 
               const callback_t &callback);
 private:
    GFileMonitorCXX m_monitor;
    callback_t m_callback;
};

/**
 * always throws an exception, including information from GError if available:
 * <action>: <error message>|failure
 */
void GLibErrorException(const std::string &action, GError *error);

#endif

SE_END_CXX

#endif // INCL_GLIB_SUPPORT

