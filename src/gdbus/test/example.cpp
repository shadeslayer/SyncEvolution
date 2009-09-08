/*
 *
 *  Library for simple D-Bus integration with GLib
 *
 *  Copyright (C) 2009  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/signal.h>

#include "gdbus-cxx-bridge.h"
#include <memory>
#include <iostream>

class Test {
    typedef Result1<const std::string&> string_result;
    struct async
    {
        async(Watch *watch, string_result *result):
            m_watch(watch),
            m_result(result)
        {}
        ~async()
        {
            delete m_watch;
            delete m_result;
        }

        Watch *m_watch;
        string_result *m_result;
    };
        

    static gboolean method_idle(gpointer data)
    {
        std::auto_ptr<async> mydata(static_cast<async *>(data));
        mydata->m_result->done("Hello World, asynchronous and delayed");
        return false;
    }

    static void disconnect(const std::string &peer)
    {
        std::cout << peer << " has disconnected." << std::endl;
    }

public:
    void method(std::string &text)
    {
        text = "Hello World";
    }

    void method_async(int32_t secs, string_result *r)
    {
        Watch *watch = r->createWatch(boost::bind(disconnect,
                                                  "caller of method_async"));
        g_timeout_add_seconds(secs, method_idle, new async(watch, r));
    }

    void method2(int32_t arg, int32_t &ret)
    {
        ret = arg * 2;
    }

    int32_t method3(int32_t arg)
    {
        return arg * 3;
    }

    void method10(int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5,
                  int32_t a6, int32_t a7, int32_t a8, int32_t a9, int32_t a10)
    {
    }

    void method10_async(Result10<int32_t, int32_t, int32_t, int32_t, int32_t,
                        int32_t, int32_t, int32_t, int32_t, int32_t> *r)
    {
        r->done(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
        delete r;
    }

    int32_t method9(int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5,
                    int32_t a6, int32_t a7, int32_t a8, int32_t a9)
    {
        return 0;
    }


    void hash(const std::map<int8_t, int32_t> &in, std::map<int16_t, int32_t> &out)
    {
        for (std::map<int8_t, int32_t>::const_iterator it = in.begin();
             it != in.end();
             ++it) {
            out.insert(std::make_pair((int16_t)it->first, it->second * it->second));
        }
    }

    void array(const std::vector<int32_t> &in, std::vector<int32_t> &out)
    {
        for (std::vector<int32_t>::const_iterator it = in.begin();
             it != in.end();
             ++it) {
            out.push_back(*it * *it);
        }
    }

    void error()
    {
        throw dbus_error("org.example.error.Invalid", "error");
    }
};

class DBusTest : public Test
{
    DBusObjectHelper m_object;
    DBusObjectHelper m_secondary;

public:
    DBusTest(DBusConnection *conn) :
        m_object(conn, "/test", "org.example.Test"),
        // same path!
        m_secondary(conn, m_object.getPath(), "org.example.Secondary"),
        signal(m_object, "Signal")
    {}

    ~DBusTest()
    {
    }

    EmitSignal3<int32_t, const std::string &, const std::map<int32_t, int32_t> &>signal;

    void hello() {}

    void activate()
    {
        static GDBusMethodTable methods[] = {
            makeMethodEntry<Test,
                            int32_t, int32_t, int32_t, int32_t, int32_t,
                            int32_t, int32_t, int32_t, int32_t, int32_t,
                            typeof(&Test::method10), &Test::method10>("Method10"),
            makeMethodEntry<Test,
                            int32_t, int32_t, int32_t, int32_t, int32_t,
                            int32_t, int32_t, int32_t, int32_t, int32_t,
                            typeof(&Test::method10_async), &Test::method10_async>
                            ("Method10Async", G_DBUS_METHOD_FLAG_ASYNC),
            makeMethodEntry<Test,
                            int32_t, int32_t, int32_t, int32_t, int32_t,
                            int32_t, int32_t, int32_t, int32_t, int32_t,
                            &Test::method9>("Method9"),

            makeMethodEntry<Test, int32_t, int32_t &,
                            typeof(&Test::method2), &Test::method2>("Method2"),
            makeMethodEntry<Test, int32_t, int32_t,
                            &Test::method3>("Method3"),
            makeMethodEntry<Test, std::string &,
                            typeof(&Test::method), &Test::method>("Test"),
            makeMethodEntry<Test, int32_t, std::string &,
                            typeof(&Test::method_async), &Test::method_async>
                            ("TestAsync", G_DBUS_METHOD_FLAG_ASYNC),
            makeMethodEntry<Test,
                            const std::map<int8_t, int32_t> &,
                            std::map<int16_t, int32_t> &,
                            typeof(&Test::hash), &Test::hash>
                            ("Hash"),
            makeMethodEntry<Test,
                            const std::vector<int32_t> &,
                            std::vector<int32_t> &,
                            typeof(&Test::array), &Test::array>
                            ("Array"),
            makeMethodEntry<Test, typeof(&Test::error), &Test::error>("Error"),
            { },
        };

        static GDBusSignalTable signals[] = {
            signal.makeSignalEntry("Signal"),
            { },
        };

        m_object.activate(methods,
                          signals,
                          NULL,
                          this);

        static GDBusMethodTable secondary_methods[] = {
            makeMethodEntry<DBusTest, typeof(&DBusTest::hello), &DBusTest::hello>("Hello"),
            {}
        };
        m_secondary.activate(secondary_methods,
                             NULL,
                             NULL,
                             this);
    }

    void deactivate()
    {
        m_object.deactivate();
        m_secondary.deactivate();
    }
};

static GMainLoop *main_loop = NULL;

static void sig_term(int sig)
{
    g_main_loop_quit(main_loop);
}

int main(int argc, char *argv[])
{
    DBusConnection *conn;
    DBusError err;
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_term;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);

    main_loop = g_main_loop_new(NULL, FALSE);

    dbus_error_init(&err);

    conn = g_dbus_setup_bus(DBUS_BUS_SESSION, "org.example", &err);
    if (conn == NULL) {
        if (dbus_error_is_set(&err) == TRUE) {
            fprintf(stderr, "%s\n", err.message);
            dbus_error_free(&err);
        } else
            fprintf(stderr, "Can't register with session bus\n");
        exit(1);
    }

    std::auto_ptr<DBusTest> test(new DBusTest(conn));
    test->activate();
    test->signal(42, "hello world", std::map<int32_t, int32_t>());
    test->deactivate();
    test->activate();
    test->signal(123, "here I am again", std::map<int32_t, int32_t>());

    g_main_loop_run(main_loop);

    test.reset();

    g_dbus_cleanup_connection(conn);

    g_main_loop_unref(main_loop);

    return 0;
}
