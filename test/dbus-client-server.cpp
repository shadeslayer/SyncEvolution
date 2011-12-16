#include "gdbus-cxx-bridge.h"
#include <syncevo/GLibSupport.h>
#include <syncevo/SmartPtr.h>

#include <iostream>
#include <signal.h>

#include <boost/bind.hpp>
#include <boost/scoped_ptr.hpp>

SE_GOBJECT_TYPE(GMainLoop);

SyncEvo::GMainLoopPtr loop;

class Test
{
    GDBusCXX::DBusObjectHelper m_server;
    // GDBusCXX::DBusRemoteObject m_dbusAPI;
    // GDBusCXX::SignalWatch0 m_disconnected;

public:
    Test(const GDBusCXX::DBusConnectionPtr &conn) :
        // will close connection
        m_server(conn, "/test", "org.example.Test", GDBusCXX::DBusObjectHelper::Callback_t(), true)
        // m_dbusAPI(conn, DBUS_PATH_LOCAL, DBUS_INTERFACE_LOCAL, "" /* sender? */),
        // m_disconnected(m_dbusAPI, "Disconnected")
    {
        m_server.add(this, &Test::hello, "Hello");
    }

    void activate()
    {
        m_server.activate();
        // fails with an unspecific error inside libdbus, don't rely on "Disconnected"
        // m_disconnected.activate(boost::bind(&Test::disconnected, this));

        // not implemented either
        // b_dbus_set_disconnect_function(m_server.getConnection(),
        // staticDisconnected,
        // NULL,
        // NULL);
    }

    std::string hello(const std::string &in)
    {
        std::cout << "hello() called with " << in << std::endl;
        return "world";
    }

    void disconnected()
    {
        std::cout << "connection disconnected";
    }

    // static void staticDisconnected(DBusConnection*conn, void *data)
    // {
    // std::cout << "connection disconnected";
    // }
};

static void newClientConnection(GDBusCXX::DBusServerCXX &server, GDBusCXX::DBusConnectionPtr &conn,
                                boost::scoped_ptr<Test> &testptr)
{
    std::cout << "new connection, " <<
        (dbus_connection_get_is_authenticated(conn.get()) ? "authenticated" : "not authenticated") <<
        std::endl;
    testptr.reset(new Test(conn.get()));
    testptr->activate();
}

class TestProxy : public GDBusCXX::DBusRemoteObject
{
public:
    TestProxy(GDBusCXX::DBusConnectionPtr &conn) :
        GDBusCXX::DBusRemoteObject(conn.get(), "/test", "org.example.Test", "direct.peer"),
        m_hello(*this, "Hello") {
    }

    GDBusCXX::DBusClientCall1<std::string> m_hello;
};

static void helloCB(GMainLoop *loop, const std::string &res, const std::string &error)
{
    if (!error.empty()) {
        std::cout << "call failed: " << error << std::endl;
    } else {
        std::cout << "hello('hello') = " << res << std::endl;
    }
    g_main_loop_quit(loop);
}

void signalHandler (int sig)
{
    if (loop) {
        g_main_loop_quit(loop.get());
    }
}


int main(int argc, char **argv)
{
    int ret = 0;

    signal (SIGABRT, &signalHandler);
    signal (SIGTERM, &signalHandler);
    signal (SIGINT, &signalHandler);

    try {
        gboolean opt_server;
        gchar *opt_address;
        GOptionContext *opt_context;
        // gboolean opt_allow_anonymous;
        SyncEvo::GErrorCXX gerror;
        GDBusCXX::DBusErrorCXX dbusError;
        GOptionEntry opt_entries[] = {
            { "server", 's', 0, G_OPTION_ARG_NONE, &opt_server, "Start a server instead of a client", NULL },
            { "address", 'a', 0, G_OPTION_ARG_STRING, &opt_address, "D-Bus address to use", NULL },
            // { "allow-anonymous", 'n', 0, G_OPTION_ARG_NONE, &opt_allow_anonymous, "Allow anonymous authentication", NULL },
            { NULL}
        };

        g_type_init();

        opt_address = NULL;
        opt_server = FALSE;
        // opt_allow_anonymous = FALSE;

        opt_context = g_option_context_new("peer-to-peer example");
        g_option_context_add_main_entries(opt_context, opt_entries, NULL);
        if (!g_option_context_parse(opt_context, &argc, &argv, gerror)) {
            gerror.throwError("parsing command line options");
        }
        // if (!opt_server && opt_allow_anonymous) {
        // throw stdruntime_error("The --allow-anonymous option only makes sense when used with --server.");
        // }

        loop.set(g_main_loop_new (NULL, FALSE), "main loop");

        if (opt_server) {
            boost::shared_ptr<GDBusCXX::DBusServerCXX> server =
                GDBusCXX::DBusServerCXX::listen(opt_address ?
                                                opt_address : "",
                                                &dbusError);
            if (!server) {
                dbusError.throwFailure("starting server");
            }
            std::cout << "Server is listening at: " << server->getAddress() << std::endl;
            boost::scoped_ptr<Test> testptr;
            server->setNewConnectionCallback(boost::bind(newClientConnection, _1, _2, boost::ref(testptr)));

            g_main_loop_run(loop.get());
        } else {
            if (!opt_address) {
                throw std::runtime_error("need server address");
            }

            GDBusCXX::DBusConnectionPtr conn = dbus_get_bus_connection(opt_address,
                                                                       &dbusError);
            if (!conn) {
                dbusError.throwFailure("connecting to server");
            }
            // closes connection
            GDBusCXX::DBusObject guard(conn, "foo", "bar", true);
            TestProxy proxy(conn);
            proxy.m_hello(std::string("world"), boost::bind(helloCB, loop.get(), _1, _2));
            g_main_loop_run(loop.get());
        }

        loop.set(NULL);
    } catch (const std::exception &ex) {
        std::cout << ex.what() << std::endl;
        ret = 1;
        loop.set(NULL);
    }

    std::cout << "server done" << std::endl;

    return ret;
}
