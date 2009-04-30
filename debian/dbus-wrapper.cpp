/*
 * Copyright (C) 2008 Patrick Ohly
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

#include <dlfcn.h>

/**
 * There are valid use cases where the (previously hard-coded) default
 * timeout was too short. This function replaces _DBUS_DEFAULT_TIMEOUT_VALUE
 * and - if set - interprets the content of DBUS_DEFAULT_TIMEOUT as
 * number of milliseconds.
 */
static int _dbus_connection_default_timeout(void)
{
    const char *def = getenv("DBUS_DEFAULT_TIMEOUT");
    int timeout = 0;

    if (def) {
        timeout = atoi(def);
    }
    if (timeout <= 0) {
        /* the traditional _DBUS_DEFAULT_TIMEOUT_VALUE */
        timeout = 25 * 1000;
    }
}

extern "C" int
dbus_connection_send_with_reply (void *connection,
                                 void *message,
                                 void **pending_return,
                                 int timeout_milliseconds)
{
    static typeof(dbus_connection_send_with_reply) *real_func;

    if (!real_func) {
        real_func = (typeof(dbus_connection_send_with_reply) *)dlsym(RTLD_NEXT, "dbus_connection_send_with_reply");
    }
    return real_func ?
        real_func(connection, message, pending_return,
                  timeout_milliseconds == -1 ? _dbus_connection_default_timeout() : timeout_milliseconds) :
        0;
}
