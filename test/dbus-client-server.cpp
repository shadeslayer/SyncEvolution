#include "gdbus-cxx-bridge.h"
#include <syncevo/GLibSupport.h>
#include <syncevo/SmartPtr.h>
#include <syncevo/ForkExec.h>

#include <iostream>
#include <signal.h>

#include <boost/bind.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/tuple/tuple_io.hpp>

SyncEvo::GMainLoopCXX loop;

// closes child connection
boost::scoped_ptr<GDBusCXX::DBusObject> guard;

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
        m_server.add(this, &Test::getstrings, "GetStrings");
        m_server.add(this, &Test::getmixed, "GetMixed");
        m_server.add(this, &Test::kill, "Kill");
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

    void getstrings(std::string &first, std::string &second)
    {
        first = "hello";
        second = "world";
    }

    void getmixed(std::string &first, int &second, std::string &third)
    {
        first = "hello";
        second = 1;
        third = "world";
    }

    void kill()
    {
        std::cout << "killing myself as requested" << std::endl;
        abort();
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
    std::cout << "new connection" << std::endl;
    testptr.reset(new Test(conn.get()));
    testptr->activate();
}

static void onChildConnect(const GDBusCXX::DBusConnectionPtr &conn,
                           boost::scoped_ptr<Test> &testptr)
{
    std::cout << "child is ready" << std::endl;
    testptr.reset(new Test(conn.get()));
    testptr->activate();
}

static void onQuit(int status)
{
    std::cout << "child has quit, status " << status << std::endl;
    // always quit the process, not just on failure
    g_main_loop_quit(loop.get());
}

static void onFailure(const std::string &error)
{
    std::cout << "failure, quitting now: " << error << std::endl;
    g_main_loop_quit(loop.get());
}

class TestProxy : public GDBusCXX::DBusRemoteObject
{
public:
    TestProxy(const GDBusCXX::DBusConnectionPtr &conn) :
        GDBusCXX::DBusRemoteObject(conn.get(), "/test", "org.example.Test", "direct.peer"),
        m_hello(*this, "Hello"),
        m_kill(*this, "Kill")
    {
    }

    GDBusCXX::DBusClientCall1<std::string> m_hello;
    GDBusCXX::DBusClientCall0 m_kill;
};

static void onChildConnectKill(const GDBusCXX::DBusConnectionPtr &conn,
                               boost::scoped_ptr<Test> &testptr)
{
    std::cout << "child is ready, kill it" << std::endl;
    testptr.reset(new Test(conn.get()));
    testptr->activate();

    // process messages already before returning from this onConnect callback
    dbus_bus_connection_undelay(conn);

    TestProxy proxy(conn);
    try {
        proxy.m_kill();
    } catch (const std::runtime_error &ex) {
        std::cout << "caught exception, as expected: " << ex.what() << std::endl;
        std::cout << "aborting..." << std::endl;
        abort();
    }
    std::cout << "did not get the expected exception" << std::endl;
    abort();
}

static void helloCB(GMainLoop *loop, const std::string &res, const std::string &error)
{
    if (!error.empty()) {
        std::cout << "call failed: " << error << std::endl;
    } else {
        std::cout << "hello('hello') = " << res << std::endl;
    }
    g_main_loop_quit(loop);
}

static void callServer(const GDBusCXX::DBusConnectionPtr &conn)
{
    TestProxy proxy(conn);
    Test test(conn.get());
    test.activate();

    // process messages already before returning from this onConnect callback
    dbus_bus_connection_undelay(conn);

    std::cout << "blocking call to server without callback" << std::endl;
    std::cout << proxy.m_hello(std::string("blocking world, II")) << std::endl;

    try {
        GDBusCXX::DBusClientCall1<std::string> nosuchcall(proxy, "nosuchcall");
        std::cout << nosuchcall(std::string("ignoreme")) << std::endl;
    } catch (const std::runtime_error &ex) {
        std::cout << "caught exception, as expected: " << ex.what() << std::endl;
    }

    GDBusCXX::DBusClientCall2<std::string, std::string> getstrings(proxy, "GetStrings");
    std::pair<std::string, std::string> r = getstrings();
    std::cout << "Got pair: (" << r.first << ", " << r.second << ")" << std::endl;

    GDBusCXX::DBusClientCall3<std::string, int, std::string> getmixed(proxy, "GetMixed");
    std::cout << "Got tuple: " << getmixed() << std::endl;

    std::cout << "calling server" << std::endl;
    proxy.m_hello.start(std::string("world"), boost::bind(helloCB, loop.get(), _1, _2));
    // keep connection open until child quits
    guard.reset(new  GDBusCXX::DBusObject(conn, "foo", "bar", true));
}

static void killServer(const GDBusCXX::DBusConnectionPtr &conn)
{
    TestProxy proxy(conn);

    // process messages already before returning from this onConnect callback
    dbus_bus_connection_undelay(conn);

    try {
        proxy.m_kill();
    } catch (const std::runtime_error &ex) {
        std::cout << "caught exception, as expected: " << ex.what() << std::endl;
        std::cout << "aborting..." << std::endl;
        abort();
    }
    std::cout << "did not get the expected exception" << std::endl;
    abort();
}

static void calledByServer(const GDBusCXX::DBusConnectionPtr &conn)
{
    // run until Test::kill() is invoked by server
    Test test(conn.get());
    test.activate();
    dbus_bus_connection_undelay(conn);
    g_main_loop_run(loop.get());
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
        gboolean opt_fork_exec;
        gboolean opt_fork_exec_failure;
        gchar *opt_address;
        gchar *opt_kill;
        GOptionContext *opt_context;
        // gboolean opt_allow_anonymous;
        SyncEvo::GErrorCXX gerror;
        GDBusCXX::DBusErrorCXX dbusError;
        GOptionEntry opt_entries[] = {
            { "server", 's', 0, G_OPTION_ARG_NONE, &opt_server, "Start a server instead of a client", NULL },
            { "forkexec", 'e', 0, G_OPTION_ARG_NONE, &opt_fork_exec, "Use fork+exec to start the client (implies --server)", NULL },
            { "forkfailure", 'f', 0, G_OPTION_ARG_NONE, &opt_fork_exec_failure, "Fork /bin/false to simulate a failure in the child (implies )", NULL },
            { "forkkill", 'a', 0, G_OPTION_ARG_STRING, &opt_kill, "'child/parent' call peer which kills itself before replying (implies --forkexec)", NULL },
            { "address", 'a', 0, G_OPTION_ARG_STRING, &opt_address, "D-Bus address to use", NULL },
            // { "allow-anonymous", 'n', 0, G_OPTION_ARG_NONE, &opt_allow_anonymous, "Allow anonymous authentication", NULL },
            { NULL}
        };

        g_type_init();

        opt_address = NULL;
        opt_kill = NULL;
        opt_server = FALSE;
        opt_fork_exec = FALSE;
        opt_fork_exec_failure = FALSE;
        // opt_allow_anonymous = FALSE;

        opt_context = g_option_context_new("peer-to-peer example");
        g_option_context_add_main_entries(opt_context, opt_entries, NULL);
        bool success = g_option_context_parse(opt_context, &argc, &argv, gerror);
        g_option_context_free(opt_context);
        if (!success) {
            gerror.throwError("parsing command line options");
        }
        // if (!opt_server && opt_allow_anonymous) {
        // throw stdruntime_error("The --allow-anonymous option only makes sense when used with --server.");
        // }

        loop = SyncEvo::GMainLoopCXX(g_main_loop_new (NULL, FALSE), false);
        if (!loop) {
            throw std::runtime_error("could not allocate main loop");
        }

        if (opt_fork_exec || opt_fork_exec_failure) {
            boost::scoped_ptr<Test> testptr;
            boost::shared_ptr<SyncEvo::ForkExecParent> forkexec =
                SyncEvo::ForkExecParent::create(opt_fork_exec_failure ? "/bin/false" : argv[0]);
            if (opt_kill) {
                forkexec->addEnvVar("DBUS_CLIENT_SERVER_KILL", opt_kill);
            }
            forkexec->m_onConnect.connect(g_strcmp0(opt_kill, "child") ?
                                          boost::bind(onChildConnect, _1, boost::ref(testptr)) :
                                          boost::bind(onChildConnectKill, _1, boost::ref(testptr)));
            forkexec->m_onQuit.connect(onQuit);
            forkexec->m_onFailure.connect(boost::bind(onFailure, _2));
            forkexec->start();
            g_main_loop_run(loop.get());
        } else if (opt_server) {
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
        } else if (SyncEvo::ForkExecChild::wasForked()) {
            boost::shared_ptr<SyncEvo::ForkExecChild> forkexec =
                SyncEvo::ForkExecChild::create();

            forkexec->m_onConnect.connect(!g_strcmp0(getenv("DBUS_CLIENT_SERVER_KILL"), "child") ? calledByServer :
                                          !g_strcmp0(getenv("DBUS_CLIENT_SERVER_KILL"), "server") ? killServer :
                                          callServer);
            forkexec->m_onFailure.connect(boost::bind(onFailure, _2));
            forkexec->connect();
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
            callServer(conn);
            g_main_loop_run(loop.get());
        }

        loop.reset();
    } catch (const std::exception &ex) {
        std::cout << ex.what() << std::endl;
        ret = 1;
        loop.reset();
    }

    std::cout << "server done" << std::endl;

    return ret;
}
