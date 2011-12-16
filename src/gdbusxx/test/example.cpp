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

namespace GDBusCXX {

struct args {
    int a;
    std::string b;
    std::map<std::string, std::string> c;
};

static void hello_global() {}

class Test {
    typedef boost::shared_ptr< Result1<const std::string&> > string_result;
    struct async
    {
        async(const boost::shared_ptr<Watch> &watch, Watch *watch2, const string_result &result):
            m_watch(watch),
            m_watch2(watch2),
            m_result(result)
        {}
        ~async()
        {
            delete m_watch2;
        }

        boost::shared_ptr<Watch> m_watch;
        Watch *m_watch2;
        string_result m_result;
    };


    static gboolean method_idle(gpointer data)
    {
        std::auto_ptr<async> mydata(static_cast<async *>(data));
        std::cout << "replying to method_async" << std::endl;
        mydata->m_result->done("Hello World, asynchronous and delayed");
        return false;
    }

    static void disconnect(const std::string &id, const std::string &peer)
    {
        std::cout << id << ": " << peer << " has disconnected." << std::endl;
    }

public:

    static void hello_static() {}
    void hello_const() const {}
    static void hello_world(const char *msg) { puts(msg); }
    void hello_base() {}

    void method(std::string &text)
    {
        text = "Hello World";
    }

    void method_async(const Caller_t &caller,
                      const boost::shared_ptr<Watch> &watch,
                      int32_t secs,
                      const string_result &r)
    {
        watch->setCallback(boost::bind(disconnect, "watch1", caller));
        Watch *watch2 = r->createWatch(boost::bind(disconnect, "watch2", caller));
        std::cout << "method_async called by " << caller << " delay " << secs << std::endl;
        g_timeout_add_seconds(secs, method_idle, new async(watch, watch2, r));
    }

    void method2(int32_t arg, int32_t &ret)
    {
        ret = arg * 2;
    }

    int32_t method3(int32_t arg)
    {
        return arg * 3;
    }

    void method8_simple(int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5,
                        int32_t a6, int32_t a7, int32_t a8)
    {
    }

    void method9_async(Result9<int32_t, int32_t, int32_t, int32_t, int32_t,
                               int32_t, int32_t, int32_t, int32_t> *r)
    {
        r->done(1, 2, 3, 4, 5, 6, 7, 8, 9);
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

    void argtest(const args &in, args &out)
    {
        out = in;
        out.a = in.a + 1;
    }
};

class Test2
{
public:
    void test2() {}
};

template<> struct dbus_traits<args> :
    public dbus_struct_traits<args, dbus_member<args, int, &args::a,
                                    dbus_member<args, std::string, &args::b,
                                    dbus_member_single<args, std::map<std::string, std::string>, &args::c> > > >
{};

class DBusTest : public Test, private Test2
{
    DBusObjectHelper m_object;
    DBusObjectHelper m_secondary;

public:
    DBusTest(const DBusConnectionPtr &conn) :
        m_object(conn, "/test", "org.example.Test"),
        // same path!
        m_secondary(conn, m_object.getPath(), "org.example.Secondary"),
        signal(m_object, "Signal")
    {
        m_object.add(this, &Test::method8_simple, "Method8Simple");
        // m_object.add(this, &Test::method10_async, "Method10Async", G_DBUS_METHOD_FLAG_ASYNC);
        // m_object.add(this, &Test::method9, "Method9");
        m_object.add(this, &Test::method2, "Method2");
        m_object.add(this, &Test::method3, "Method3");
        m_object.add(this, &Test::method, "Test");
        m_object.add(this, &Test::method_async, "TestAsync");
        m_object.add(this, &Test::argtest, "ArgTest");
        m_object.add(this, &Test::hash, "Hash");
        m_object.add(this, &Test::array, "Array");
        m_object.add(this, &Test::error, "Error");
        m_object.add(&hello_global, "Global");
        m_object.add(&DBusTest::hello_static, "Static");
        m_object.add(static_cast<Test2 *>(this), &Test2::test2, "Private");
        // The hello_const() method cannot be registered
        // because there is no matching MakeMethodEntry<>
        // specialization for it or DBusObjectHelper::add()
        // fails to determine the right function type,
        // depending how one wants to interpret the problem.
        // m_object.add2(this, &DBusTest::hello_const, "Const");

        m_object.add(signal);

        m_secondary.add(this, &DBusTest::hello, "Hello");
    }

    ~DBusTest()
    {
    }

    EmitSignal3<int32_t, const std::string &, const std::map<int32_t, int32_t> &>signal;

    void hello() {}
    static void hello_static() {}
    void hello_const() const {}

    void activate()
    {
        m_secondary.activate();
        m_object.activate();
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

} // namespace GDBusCXX

using namespace GDBusCXX;

int main(int argc, char *argv[])
{
    DBusConnectionPtr conn;
    DBusErrorCXX err;
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_term;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);

    main_loop = g_main_loop_new(NULL, FALSE);

    conn = dbus_get_bus_connection("SESSION", "org.example", false, &err);
    if (conn == NULL) {
        std::string message = err.getMessage();
        if (!message.empty()) {
            fprintf(stderr, "%s\n", message.c_str());
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

    // is this really necessary?
    // if(!g_dbus_connection_close_sync(conn, NULL, NULL)) {
    // fprintf(stderr, "Problem closing connection.\n");
    // }

    g_main_loop_unref(main_loop);

    return 0;
}
