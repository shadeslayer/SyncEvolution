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
#else
typedef void *GMainLoop;
#endif

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

SE_END_CXX

#endif // INCL_GLIB_SUPPORT

