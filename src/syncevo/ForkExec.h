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

#ifndef INCL_FORK_EXEC
# define INCL_FORK_EXEC

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#if defined(HAVE_GLIB)

#include <syncevo/util.h>
#include <syncevo/GLibSupport.h>
#include <syncevo/SmartPtr.h>

#include "gdbus-cxx-bridge.h"

#include <boost/signals2.hpp>

SE_BEGIN_CXX

/**
 * Utility class which starts a specific helper binary in a second
 * process. The helper binary is identified via its base name like
 * "syncevo-dbus-helper", exact location is then determined
 * automatically, or via an absolute path.
 *
 * Direct D-Bus communication is set up automatically. For this to
 * work, the helper must use ForkExecChild::connect(). To debug this
 * when using GIO DBus, set G_DBUS_DEBUG=message.
 *
 * There are more options mentioned here:
 * http://developer.gnome.org/gio/unstable/ch03.html
 *
 * Progress (like "client connected") and failures ("client disconnected")
 * are reported via boost::signal2 signals. To make progess, the user of
 * this class must run a glib event loop in the default context.
 *
 * Note that failures encountered inside the class methods themselves
 * will be reported via exceptions. Only asynchronous errors encountered
 * inside the event loop are reported via the failure signal.
 *
 * Slots connected to the signals may throw exceptions. They will be
 * propagated up to the caller when ForkExec methods were called directly.
 * When thrown inside the event loop, the exception will be logged and
 * then reported via the onFailure signal.
 */

class ForkExec : private boost::noncopyable {
 public:
    /**
     * Called when the D-Bus connection is up and running. It is ready
     * to register objects that the peer might need. It is
     * guaranteed that any objects registered now will be ready before
     * the helper gets a chance to make D-Bus calls.
     */
    typedef boost::signals2::signal<void (const GDBusCXX::DBusConnectionPtr &)> OnConnect;
    OnConnect m_onConnect;

    /**
     * Called when an exception cannot be propagated up to the caller
     * because it was thrown inside the event loop, or when some other
     * kind of failure is encountered which cannot be reported via some
     * other means. The original problem is already logged when
     * onFailure is invoked, so further logging should not be done unless
     * it adds new information.
     *
     * Bad results of asynchronous method calls are reported via the
     * result callback of the method, not via onFailure.
     *
     * When a failure occurs, the peer should be considered dead and
     * the connection to it should be shut down (if any had been
     * established at all).
     *
     * When the child quits before establishing a connection or quits
     * with a non-zero return code, onFailure will be called. That way
     * a user of ForkExecParent doesn't have to connect to onQuit.
     */
    typedef boost::signals2::signal<void (SyncMLStatus, const std::string &)> OnFailure;
    OnFailure m_onFailure;

 protected:
    ForkExec();
};

/**
 * The parent side of a fork/exec.
 */
class ForkExecParent : public ForkExec
{
 public:
    ~ForkExecParent();

    /**
     * A ForkExecParent instance must be created via this factory
     * method and then be tracked in a shared pointer. This method
     * will not start the helper yet: first connect your slots, then
     * call start().
     */
    static boost::shared_ptr<ForkExecParent> create(const std::string &helper);

    /**
     * the helper string passed to create()
     */
    std::string getHelper() const { return m_helper; }

    /**
     * run the helper executable in the parent process
     */
    void start();

    /**
     * request that the child process terminates by sending it a
     * SIGINT
     */
    void stop();

    /**
     * kill the child process without giving it a chance to shut down
     * by sending it a SIGKILL
     */
    void kill();

    /**
     * Called when the helper has quit. The parameter of the signal is
     * the return status of the helper (see waitpid()).
     */
    typedef boost::signals2::signal<void (int)> OnQuit;
    OnQuit m_onQuit;

    enum State {
        IDLE,           /**< instance constructed, but start() not called yet */
        STARTING,       /**< start() called */
        CONNECTED,      /**< child has connected, D-Bus connection established */
        TERMINATED      /**< child has quit */
    };
    State getState()
    {
        return m_hasQuit ? TERMINATED :
            m_hasConnected ? CONNECTED :
            m_watchChild ? STARTING :
            IDLE;
    }

    /**
     * Get the childs pid. This can be used as a unique id common to
     * both parent and child.
     */
    int getChildPid() { return static_cast<int>(m_childPid); }

    /**
     * Simply pushes a new environment variable onto m_envStrings.
     */
    void addEnvVar(const std::string &name, const std::string &value);

 private:
    ForkExecParent(const std::string &helper);

    std::string m_helper;
    boost::shared_ptr<GDBusCXX::DBusServerCXX> m_server;
    boost::scoped_array<char *> m_argv;
    std::list<std::string> m_argvStrings;
    boost::scoped_array<char *> m_env;
    std::list<std::string> m_envStrings;
    GPid m_childPid;
    bool m_hasConnected;
    bool m_hasQuit;
    bool m_sigIntSent;
    bool m_sigTermSent;

    GSource *m_watchChild;
    static void watchChildCallback(GPid pid,
                                   gint status,
                                   gpointer data) throw();

    void newClientConnection(GDBusCXX::DBusConnectionPtr &conn) throw();
};

/**
 * The child side of a fork/exec.
 *
 * At the moment, the child cannot monitor the parent or kill it.
 * Might be added (if needed), in which case the corresponding
 * ForkExecParent members should be moved to the common ForkExec.
 */
class ForkExecChild : public ForkExec
{
 public:
    /**
     * A ForkExecChild instance must be created via this factory
     * method and then be tracked in a shared pointer. The process
     * must have been started by ForkExecParent (directly or indirectly)
     * and any environment variables set by ForkExecParent must still
     * be set.
     */
    static boost::shared_ptr<ForkExecChild> create();

    /**
     * initiates connection to parent, connect to ForkExec::m_onConnect
     * before calling this function to be notified of success and
     * ForkExec::m_onFailure for failures
     */
    void connect();

    /**
     * true if the current process was created by ForkExecParent
     */
    static bool wasForked();

 private:
    ForkExecChild();

    static const char *getParentDBusAddress();
};

SE_END_CXX

#endif // HAVE_GLIB
#endif // INCL_FORK_EXEC

