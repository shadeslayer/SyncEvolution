
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
