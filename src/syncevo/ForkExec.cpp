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

#include "ForkExec.h"

#if defined(HAVE_GLIB)

#include <pcrecpp.h>
#include "test.h"

SE_BEGIN_CXX

static const std::string ForkExecEnvVar("SYNCEVOLUTION_FORK_EXEC=");

ForkExec::ForkExec()
{
}

ForkExecParent::ForkExecParent(const std::string &helper) :
    m_helper(helper),
    m_childPid(0),
    m_hasConnected(false),
    m_hasQuit(false),
    m_status(0),
    m_sigIntSent(false),
    m_sigTermSent(false),
    m_mergedStdoutStderr(false),
    m_out(NULL),
    m_err(NULL),
    m_outID(0),
    m_errID(0),
    m_watchChild(NULL)
{
}

boost::shared_ptr<ForkExecParent> ForkExecParent::create(const std::string &helper)
{
    boost::shared_ptr<ForkExecParent> forkexec(new ForkExecParent(helper));
    return forkexec;
}

ForkExecParent::~ForkExecParent()
{
    if (m_outID) {
        g_source_remove(m_outID);
    }
    if (m_errID) {
        g_source_remove(m_errID);
    }
    if (m_out) {
        g_io_channel_unref(m_out);
    }
    if (m_err) {
        g_io_channel_unref(m_err);
    }
    if (m_watchChild) {
        // stop watching
        g_source_destroy(m_watchChild);
        g_source_unref(m_watchChild);
    }
    if (m_childPid) {
        g_spawn_close_pid(m_childPid);
    }
}

/**
 * Redirect stdout to stderr.
 *
 * Child setup function, called insided forked process before exec().
 * only async-signal-safe functions allowed according to http://developer.gnome.org/glib/2.30/glib-Spawning-Processes.html#GSpawnChildSetupFunc
 */
static void setStdoutToStderr(gpointer /* user_data */) throw()
{
    dup2(STDERR_FILENO, STDOUT_FILENO);
}

void ForkExecParent::start()
{
    if (m_watchChild) {
        SE_THROW("child already started");
    }

    // boost::shared_ptr<ForkExecParent> me = ...;
    GDBusCXX::DBusErrorCXX dbusError;

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: preparing for child process %s", m_helper.c_str());
    m_server = GDBusCXX::DBusServerCXX::listen("", &dbusError);
    if (!m_server) {
        dbusError.throwFailure("starting server");
    }
    m_server->setNewConnectionCallback(boost::bind(&ForkExecParent::newClientConnection, this, _2));

    // look for helper binary
    std::string helper;
    GSpawnFlags flags = G_SPAWN_DO_NOT_REAP_CHILD;
    if (m_helper.find('/') == m_helper.npos) {
        helper = getEnv("SYNCEVOLUTION_LIBEXEC_DIR", "");
        if (helper.empty()) {
            // env variable not set, look in libexec dir
            helper = SYNCEVO_LIBEXEC;
            helper += "/";
            helper += m_helper;
            if (access(helper.c_str(), R_OK)) {
                // some error, try PATH
                flags = (GSpawnFlags)(flags | G_SPAWN_SEARCH_PATH);
                helper = m_helper;
            }
        } else {
            // use env variable without further checks, must work
            helper += "/";
            helper += m_helper;
        }
    } else {
        // absolute path, use it
        helper = m_helper;
    }

    m_argvStrings.push_back(helper);
    m_argv.reset(AllocStringArray(m_argvStrings));
    for (char **env = environ;
         *env;
         env++) {
        if (!boost::starts_with(*env, ForkExecEnvVar)) {
            m_envStrings.push_back(*env);
        }
    }

    // pass D-Bus address via env variable
    m_envStrings.push_back(ForkExecEnvVar + m_server->getAddress());
    m_env.reset(AllocStringArray(m_envStrings));

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: running %s with D-Bus address %s",
                 helper.c_str(), m_server->getAddress().c_str());

    // Check which kind of output redirection is wanted.
    m_mergedStdoutStderr = !m_onOutput.empty();
    if (!m_onOutput.empty()) {
        m_mergedStdoutStderr = true;
    }

    GErrorCXX gerror;
    int err = -1, out = -1;
    if (!g_spawn_async_with_pipes(NULL, // working directory
                                  static_cast<gchar **>(m_argv.get()),
                                  static_cast<gchar **>(m_env.get()),
                                  flags,
                                  // child setup function: redirect stdout to stderr
                                  m_mergedStdoutStderr ? setStdoutToStderr : NULL,
                                  NULL, // child setup user data
                                  &m_childPid,
                                  NULL, // set stdin to /dev/null
                                  (m_mergedStdoutStderr || m_onStdout.empty()) ? NULL : &out,
                                  (m_mergedStdoutStderr || !m_onStderr.empty()) ? &err : NULL,
                                  gerror)) {
        m_childPid = 0;
        gerror.throwError("spawning child");
    }
    // set up output redirection, ignoring failures
    setupPipe(m_err, m_errID, err);
    setupPipe(m_out, m_outID, out);

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: child process for %s has pid %ld",
                 helper.c_str(), (long)m_childPid);

    // TODO: introduce C++ wrapper around GSource
    m_watchChild = g_child_watch_source_new(m_childPid);
    g_source_set_callback(m_watchChild, (GSourceFunc)watchChildCallback, this, NULL);
    g_source_attach(m_watchChild, NULL);
}

void ForkExecParent::setupPipe(GIOChannel *&channel, guint &sourceID, int fd)
{
    if (fd == -1) {
        // nop
        return;
    }

    channel = g_io_channel_unix_new(fd);
    if (!channel) {
        // failure
        SE_LOG_DEBUG(NULL, NULL, "g_io_channel_unix_new() returned NULL");
        close(fd);
        return;
    }
    // Close fd when freeing the channel (done by caller).
    g_io_channel_set_close_on_unref(channel, true);
    // Don't block in outputReady().
    GErrorCXX error;
    g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, error);
    // We assume that the helper is writing data in the same encoding
    // and thus avoid any kind of conversion. Necessary to avoid
    // buffering.
    error.clear();
    g_io_channel_set_encoding(channel, NULL, error);
    g_io_channel_set_buffered(channel, true);
    sourceID = g_io_add_watch(channel, (GIOCondition)(G_IO_IN|G_IO_ERR|G_IO_HUP), outputReady, this);
}

gboolean ForkExecParent::outputReady(GIOChannel *source,
                                     GIOCondition condition,
                                     gpointer data) throw ()
{
    bool cont = true;

    try {
        ForkExecParent *me = static_cast<ForkExecParent *>(data);
        gchar *buffer = NULL;
        gsize length = 0;
        GErrorCXX error;
        // Try reading, even if the condition wasn't G_IO_IN.
        GIOStatus status = g_io_channel_read_to_end(source, &buffer, &length, error);
        if (buffer && length) {
            if (source == me->m_out) {
                me->m_onStdout(buffer, length);
            } else if (me->m_mergedStdoutStderr) {
                me->m_onOutput(buffer, length);
            } else {
                me->m_onStderr(buffer, length);
            }
        }
        if (status == G_IO_STATUS_EOF ||
            (condition & (G_IO_HUP|G_IO_ERR)) ||
            error) {
            SE_LOG_DEBUG(NULL, NULL, "reading helper %s done: %s",
                         source == me->m_out ? "stdout" :
                         me->m_mergedStdoutStderr ? "combined stdout/stderr" :
                         "stderr",
                         (const char *)error);

            // Will remove event source from main loop.
            cont = false;

            // Free channel.
            if (source == me->m_out) {
                me->m_out = NULL;
            } else {
                me->m_err = NULL;
            }
            g_io_channel_unref(source);

            // Send delayed OnQuit signal now?
            me->checkCompletion();
        }
        // If an exception skips this, we are going to die, in
        // which case we don't care about the leak.
        g_free(buffer);
    } catch (...) {
        Exception::handle(HANDLE_EXCEPTION_FATAL);
    }

    return cont;
}

void ForkExecParent::watchChildCallback(GPid pid,
                                        gint status,
                                        gpointer data) throw()
{
    ForkExecParent *me = static_cast<ForkExecParent *>(data);
    me->m_hasQuit = true;
    me->m_status = status;
    me->checkCompletion();
}

void ForkExecParent::checkCompletion() throw ()
{
    if (m_hasQuit &&
        !m_out &&
        !m_err) {
        try {
            m_onQuit(m_status);
            if (!m_hasConnected ||
                m_status != 0) {
                SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: child was signaled %s, signal %d, int %d, term %d, int sent %s, term sent %s",
                             WIFSIGNALED(m_status) ? "yes" : "no",
                             WTERMSIG(m_status), SIGINT, SIGTERM,
                             m_sigIntSent ? "yes" : "no",
                             m_sigTermSent ? "yes" : "no");
                if (WIFSIGNALED(m_status) &&
                    ((WTERMSIG(m_status) == SIGINT && m_sigIntSent) ||
                     (WTERMSIG(m_status) == SIGTERM && m_sigTermSent))) {
                    // not an error when the child dies because we killed it
                    return;
                }
                std::string error = "child process quit";
                if (!m_hasConnected) {
                    error += " unexpectedly";
                }
                if (WIFEXITED(m_status)) {
                    error += StringPrintf(" with return code %d", WEXITSTATUS(m_status));
                } else if (WIFSIGNALED(m_status)) {
                    error += StringPrintf(" because of signal %d\n", WTERMSIG(m_status));
                } else {
                    error += " for unknown reasons";
                }
                SE_LOG_ERROR(NULL, NULL, "%s", error.c_str());
                m_onFailure(STATUS_FATAL, error);
            }
        } catch (...) {
            std::string explanation;
            SyncMLStatus status = Exception::handle(explanation);
            try {
                m_onFailure(status, explanation);
            } catch (...) {
                Exception::handle();
            }
        }
    }
}

void ForkExecParent::newClientConnection(GDBusCXX::DBusConnectionPtr &conn) throw()
{
    try {
        SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: child %s has connected",
                     m_helper.c_str());
        m_hasConnected = true;
        m_onConnect(conn);
    } catch (...) {
        std::string explanation;
        SyncMLStatus status = Exception::handle(explanation);
        try {
            m_onFailure(status, explanation);
        } catch (...) {
            Exception::handle();
        }
    }
}

void ForkExecParent::addEnvVar(const std::string &name, const std::string &value)
{
    if(!name.empty()) {
        m_envStrings.push_back(name + "=" + value);
    }
}

void ForkExecParent::stop(int signal)
{
    if (!m_childPid || m_hasQuit) {
        // not running, nop
        return;
    }

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: killing %s with signal %d (%s %s)",
                 m_helper.c_str(),
                 signal,
                 (!signal || signal == SIGINT) ? "SIGINT" : "",
                 (!signal || signal == SIGTERM) ? "SIGTERM" : "");
    if (!signal || signal == SIGINT) {
        ::kill(m_childPid, SIGINT);
        m_sigIntSent = true;
    }
    if (!signal || signal == SIGTERM) {
        ::kill(m_childPid, SIGTERM);
        m_sigTermSent = true;
    }
    if (signal && signal != SIGINT && signal != SIGTERM) {
        ::kill(m_childPid, signal);
    }
}

void ForkExecParent::kill()
{
    if (!m_childPid || m_hasQuit) {
        // not running, nop
        return;
    }

    SE_LOG_DEBUG(NULL, NULL, "ForkExecParent: killing %s with SIGKILL",
                 m_helper.c_str());
    ::kill(m_childPid, SIGKILL);
}

ForkExecChild::ForkExecChild()
{
}

boost::shared_ptr<ForkExecChild> ForkExecChild::create()
{
    boost::shared_ptr<ForkExecChild> forkexec(new ForkExecChild);
    return forkexec;
}

void ForkExecChild::connect()
{
    const char *address = getParentDBusAddress();
    if (!address) {
        SE_THROW("cannot connect to parent, was not forked");
    }

    SE_LOG_DEBUG(NULL, NULL, "ForkExecChild: connecting to parent with D-Bus address %s",
                 address);
    GDBusCXX::DBusErrorCXX dbusError;
    GDBusCXX::DBusConnectionPtr conn = dbus_get_bus_connection(address,
                                                               &dbusError,
                                                               true /* always delay message processing */);
    if (!conn) {
        dbusError.throwFailure("connecting to server");
    }
    m_onConnect(conn);
    dbus_bus_connection_undelay(conn);
}

bool ForkExecChild::wasForked()
{
    return getParentDBusAddress() != NULL;
}

const char *ForkExecChild::getParentDBusAddress()
{
    return getenv(ForkExecEnvVar.substr(0, ForkExecEnvVar.size() - 1).c_str());
}

#ifdef ENABLE_UNIT_TESTS
/**
 * Assumes that /bin/[false/true/echo] exist and that "env" is in the
 * path. Currently this does not cover actual D-Bus connection
 * handling and usage.
 */
class ForkExecTest : public CppUnit::TestFixture
{
public:
    void setUp()
    {
        m_statusValid = false;
        m_status = 0;
    }

private:
    CPPUNIT_TEST_SUITE(ForkExecTest);
    CPPUNIT_TEST(testTrue);
    CPPUNIT_TEST(testFalse);
    CPPUNIT_TEST(testPath);
    CPPUNIT_TEST(testNotFound);
    CPPUNIT_TEST(testEnv1);
    CPPUNIT_TEST(testEnv2);
    CPPUNIT_TEST(testOutErr);
    CPPUNIT_TEST(testMerged);
    CPPUNIT_TEST_SUITE_END();

    bool m_statusValid;
    int m_status;

    void hasQuit(int status)
    {
        m_status = status;
        m_statusValid = true;
    }

    static void append(const char *buffer, size_t length, std::string &all)
    {
        all.append(buffer, length);
    }

    boost::shared_ptr<ForkExecParent> create(const std::string &helper)
    {
        boost::shared_ptr<ForkExecParent> parent(ForkExecParent::create(helper));
        parent->m_onQuit.connect(boost::bind(&ForkExecTest::hasQuit, this, _1));
        return parent;
    }

    void testTrue()
    {
        boost::shared_ptr<ForkExecParent> parent(create("/bin/true"));
        parent->start();
        while (!m_statusValid) {
            g_main_context_iteration(NULL, true);
        }
        CPPUNIT_ASSERT(WIFEXITED(m_status));
        CPPUNIT_ASSERT_EQUAL(0, WEXITSTATUS(m_status));
    }

    void testFalse()
    {
        boost::shared_ptr<ForkExecParent> parent(create("/bin/false"));
        parent->start();
        while (!m_statusValid) {
            g_main_context_iteration(NULL, true);
        }
        CPPUNIT_ASSERT(WIFEXITED(m_status));
        CPPUNIT_ASSERT_EQUAL(1, WEXITSTATUS(m_status));
    }

    void testPath()
    {
        boost::shared_ptr<ForkExecParent> parent(create("true"));
        parent->start();
        while (!m_statusValid) {
            g_main_context_iteration(NULL, true);
        }
        CPPUNIT_ASSERT(WIFEXITED(m_status));
        CPPUNIT_ASSERT_EQUAL(0, WEXITSTATUS(m_status));
    }

    void testNotFound()
    {
        boost::shared_ptr<ForkExecParent> parent(create("no-such-binary"));
        std::string out;
        std::string err;
        parent->m_onStdout.connect(boost::bind(append, _1, _2, boost::ref(out)));
        parent->m_onStderr.connect(boost::bind(append, _1, _2, boost::ref(err)));
        try {
            parent->start();
        } catch (const SyncEvo::Exception &ex) {
            if (strstr(ex.what(), "spawning child: ")) {
                // glib itself detected that binary wasn't found. This
                // is what normally happens, but there's no guarantee,
                // thus the code below...
                return;
            }
            throw;
        }
        while (!m_statusValid) {
            g_main_context_iteration(NULL, true);
        }
        CPPUNIT_ASSERT(WIFEXITED(m_status));
        CPPUNIT_ASSERT_EQUAL(1, WEXITSTATUS(m_status));
        CPPUNIT_ASSERT_EQUAL(std::string(""), out);
        CPPUNIT_ASSERT_MESSAGE(err, err.find("no-such-binary") != err.npos);
    }

    void testEnv1()
    {
        boost::shared_ptr<ForkExecParent> parent(create("env"));
        parent->addEnvVar("FORK_EXEC_TEST_ENV", "foobar");
        std::string out;
        parent->m_onStdout.connect(boost::bind(append, _1, _2, boost::ref(out)));
        parent->start();
        while (!m_statusValid) {
            g_main_context_iteration(NULL, true);
        }
        CPPUNIT_ASSERT(WIFEXITED(m_status));
        CPPUNIT_ASSERT_EQUAL(0, WEXITSTATUS(m_status));
        CPPUNIT_ASSERT_MESSAGE(out, out.find("FORK_EXEC_TEST_ENV=foobar\n") != out.npos);
    }

    void testEnv2()
    {
        boost::shared_ptr<ForkExecParent> parent(create("env"));
        parent->addEnvVar("FORK_EXEC_TEST_ENV1", "foo");
        parent->addEnvVar("FORK_EXEC_TEST_ENV2", "bar");
        std::string out;
        parent->m_onStdout.connect(boost::bind(append, _1, _2, boost::ref(out)));
        parent->start();
        while (!m_statusValid) {
            g_main_context_iteration(NULL, true);
        }
        CPPUNIT_ASSERT(WIFEXITED(m_status));
        CPPUNIT_ASSERT_EQUAL(0, WEXITSTATUS(m_status));
        CPPUNIT_ASSERT_MESSAGE(out, out.find("FORK_EXEC_TEST_ENV1=foo\n") != out.npos);
        CPPUNIT_ASSERT_MESSAGE(out, out.find("FORK_EXEC_TEST_ENV2=bar\n") != out.npos);
    }

    void testOutErr()
    {
        // This tests uses a trick to get output via stdout (normal
        // env output) and stderr (from ld.so).
        boost::shared_ptr<ForkExecParent> parent(create("env"));
        parent->addEnvVar("FORK_EXEC_TEST_ENV", "foobar");
        parent->addEnvVar("LD_DEBUG", "files");

        std::string out;
        std::string err;
        parent->m_onStdout.connect(boost::bind(append, _1, _2, boost::ref(out)));
        parent->m_onStderr.connect(boost::bind(append, _1, _2, boost::ref(err)));
        parent->start();
        while (!m_statusValid) {
            g_main_context_iteration(NULL, true);
        }
        CPPUNIT_ASSERT(WIFEXITED(m_status));
        CPPUNIT_ASSERT_EQUAL(0, WEXITSTATUS(m_status));
        CPPUNIT_ASSERT_MESSAGE(out, out.find("FORK_EXEC_TEST_ENV=foobar\n") != out.npos);
        CPPUNIT_ASSERT_MESSAGE(err, err.find("transferring control: ") != err.npos);
    }

    void testMerged()
    {
        // This tests uses a trick to get output via stdout (normal
        // env output) and stderr (from ld.so).
        boost::shared_ptr<ForkExecParent> parent(create("env"));
        parent->addEnvVar("FORK_EXEC_TEST_ENV", "foobar");
        parent->addEnvVar("LD_DEBUG", "files");

        std::string output;
        parent->m_onOutput.connect(boost::bind(append, _1, _2, boost::ref(output)));
        parent->start();
        while (!m_statusValid) {
            g_main_context_iteration(NULL, true);
        }
        CPPUNIT_ASSERT(WIFEXITED(m_status));
        CPPUNIT_ASSERT_EQUAL(0, WEXITSTATUS(m_status));
        // output from ld.so directly followed by env output
        CPPUNIT_ASSERT_MESSAGE(output,
                               pcrecpp::RE("transferring control:.*\\n(\\s+\\d+:.*\\n)*[A-Za-z0-9_]+=.*\\n").PartialMatch(output));
    }
};
SYNCEVOLUTION_TEST_SUITE_REGISTRATION(ForkExecTest);

#endif

SE_END_CXX

#endif // HAVE_GLIB
