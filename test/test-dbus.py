#! /usr/bin/python
#
# Copyright (C) 2009 Intel Corporation
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) version 3.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301  USA

import random
import unittest
import subprocess
import time
import os
import signal
import shutil
import copy
import heapq

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import gobject
# introduced in python-gobject 2.16, not available
# on all Linux distros => make it optional
try:
    import glib
    have_glib = True
except ImportError:
    have_glib = False

DBusGMainLoop(set_as_default=True)

bus = dbus.SessionBus()
loop = gobject.MainLoop()

debugger = "" # "gdb"
server = ["syncevo-dbus-server"]
monitor = ["dbus-monitor"]
xdg_root = "test-dbus"
config = "scheduleworld_1"

def timeout(seconds):
    """Function decorator which sets a non-default timeout for a test.
    The default timeout, enforced by DBusTest.runTest(), are 5 seconds.
    Use like this:
        @timeout(10)
        def testMyTest:
            ...
    """
    def __setTimeout(func):
        func.timeout = seconds
        return func
    return __setTimeout

class Timeout:
    """Implements global time-delayed callbacks."""
    alarms = []
    next_alarm = None
    previous_handler = None
    debugTimeout = False

    @classmethod
    def addTimeout(cls, delay_seconds, callback, use_glib=True):
        """Call function after a certain delay, specified in seconds.
        If possible and use_glib=True, then it will only fire inside
        glib event loop. Otherwise it uses signals. When signals are
        used it is a bit uncertain what kind of Python code can
        be executed. It was observed that trying to append to
        DBusUtil.quit_events before calling loop.quit() caused
        a KeyboardInterrupt"""
        if have_glib and use_glib:
            glib.timeout_add(delay_seconds, callback)
            # TODO: implement removal of glib timeouts
            return None
        else:
            now = time.time()
            if cls.debugTimeout:
                print "addTimeout", now, delay_seconds, callback, use_glib
            timeout = (now + delay_seconds, callback)
            heapq.heappush(cls.alarms, timeout)
            cls.__check_alarms()
            return timeout

    @classmethod
    def removeTimeout(cls, timeout):
        """Remove a timeout returned by a previous addTimeout call.
        None and timeouts which have already fired are acceptable."""
        try:
            cls.alarms.remove(timeout)
        except ValueError:
            pass
        else:
            heapq.heapify(cls.alarms)
            cls.__check_alarms()

    @classmethod
    def __handler(cls, signum, stack):
        """next_alarm has fired, check for expired timeouts and reinstall"""
        if cls.debugTimeout:
            print "fired", time.time()
        cls.next_alarm = None
        cls.__check_alarms()

    @classmethod
    def __check_alarms(cls):
        now = time.time()
        while cls.alarms and cls.alarms[0][0] <= now:
            timeout = heapq.heappop(cls.alarms)
            if cls.debugTimeout:
                print "invoking", timeout
            timeout[1]()

        if cls.alarms:
            if not cls.next_alarm or \
                    cls.next_alarm > cls.alarms[0][0]:
                if cls.previous_handler == None:
                    cls.previous_handler = signal.signal(signal.SIGALRM, cls.__handler)
                cls.next_alarm = cls.alarms[0][0]
                delay = int(cls.next_alarm - now + 0.5)
                if not delay:
                    delay = 1
                if cls.debugTimeout:
                    print "next alarm", cls.next_alarm, delay
                signal.alarm(delay)
        elif cls.next_alarm:
            if cls.debugTimeout:
                print "disarming alarm"
            signal.alarm(0)
            cls.next_alarm = None

# commented out because running it takes time
#class TestTimeout(unittest.TestCase):
class TimeoutTest:
    """unit test for Timeout mechanism"""

    def testOneTimeout(self):
        """simple timeout of two seconds"""
        self.called = False
        start = time.time()
        def callback():
            self.called = True
        Timeout.addTimeout(2, callback, use_glib=False)
        time.sleep(10)
        end = time.time()
        self.failUnless(self.called)
        self.failIf(end - start < 2)
        self.failIf(end - start >= 3)

    def testEmptyTimeout(self):
        """called immediately because of zero timeout"""
        self.called = False
        start = time.time()
        def callback():
            self.called = True
        Timeout.addTimeout(0, callback, use_glib=False)
        if not self.called:
            time.sleep(10)
        end = time.time()
        self.failUnless(self.called)
        self.failIf(end - start < 0)
        self.failIf(end - start >= 1)

    def testTwoTimeouts(self):
        """two timeouts after 2 and 5 seconds, installed in order"""
        self.called = False
        start = time.time()
        def callback():
            self.called = True
        Timeout.addTimeout(2, callback, use_glib=False)
        Timeout.addTimeout(5, callback, use_glib=False)
        time.sleep(10)
        end = time.time()
        self.failUnless(self.called)
        self.failIf(end - start < 2)
        self.failIf(end - start >= 3)
        time.sleep(10)
        end = time.time()
        self.failUnless(self.called)
        self.failIf(end - start < 5)
        self.failIf(end - start >= 6)

    def testTwoReversedTimeouts(self):
        """two timeouts after 2 and 5 seconds, installed in reversed order"""
        self.called = False
        start = time.time()
        def callback():
            self.called = True
        Timeout.addTimeout(5, callback, use_glib=False)
        Timeout.addTimeout(2, callback, use_glib=False)
        time.sleep(10)
        end = time.time()
        self.failUnless(self.called)
        self.failIf(end - start < 2)
        self.failIf(end - start >= 3)
        time.sleep(10)
        end = time.time()
        self.failUnless(self.called)
        self.failIf(end - start < 5)
        self.failIf(end - start >= 6)

class DBusUtil(Timeout):
    """Contains the common run() method for all D-Bus test suites
    and some utility functions."""

    # Use class variables because that way it is ensured that there is
    # only one set of them. Previously instance variables were used,
    # which had the effect that D-Bus signal handlers from test A
    # wrote into variables which weren't the ones used by test B.
    # Unfortunately it is impossible to remove handlers when
    # completing test A.
    events = []
    quit_events = []
    reply = None

    def runTest(self, result, own_xdg=True):
        """Starts the D-Bus server and dbus-monitor before the test
        itself. After the test run, the output of these two commands
        are added to the test's failure, if any. Otherwise the output
        is ignored. A non-zero return code of the D-Bus server is
        logged as separate failure.

        The D-Bus server must print at least one line of output
        before the test is allowed to start.
        
        The commands are run with XDG_DATA_HOME, XDG_CONFIG_HOME,
        XDG_CACHE_HOME pointing towards local dirs
        test-dbus/[data|config|cache] which are removed before each
        test."""

        DBusUtil.events = []
        DBusUtil.quit_events = []
        DBusUtil.reply = None

        kill = subprocess.Popen("sh -c 'killall -9 syncevo-dbus-server dbus-monitor >/dev/null 2>&1'", shell=True)
        kill.communicate()

        env = copy.deepcopy(os.environ)
        if own_xdg:
            shutil.rmtree(xdg_root, True)
            env["XDG_DATA_HOME"] = xdg_root + "/data"
            env["XDG_CONFIG_HOME"] = xdg_root + "/config"
            env["XDG_CACHE_HOME"] = xdg_root + "/cache"

        dbuslog = "dbus.log"
        syncevolog = "syncevo.log"
        pmonitor = subprocess.Popen(monitor,
                                    stdout=open(dbuslog, "w"),
                                    stderr=subprocess.STDOUT)
        if debugger:
            print "\n%s: %s\n" % (self.id(), self.shortDescription())
            pserver = subprocess.Popen([debugger] + server,
                                       env=env)

            while True:
                check = subprocess.Popen("ps x | grep %s | grep -w -v -e %s -e grep -e ps" % \
                                             (server[0], debugger),
                                         env=env,
                                         shell=True,
                                         stdout=subprocess.PIPE)
                out, err = check.communicate()
                if out:
                    # process exists, but might still be loading,
                    # so give it some more time
                    time.sleep(2)
                    break
        else:
            pserver = subprocess.Popen(server,
                                       env=env,
                                       stdout=open(syncevolog, "w"),
                                       stderr=subprocess.STDOUT)
            while os.path.getsize(syncevolog) == 0:
                time.sleep(1)

        numerrors = len(result.errors)
        numfailures = len(result.failures)
        if debugger:
            print "\nrunning\n"

        # Find out what test function we run and look into
        # the function definition to see whether it comes
        # with a non-default timeout, otherwise use a 5 second
        # timeout.
        test = eval(self.id().replace("__main__.", ""))
        if "timeout" in dir(test):
            timeout = test.timeout
        else:
            timeout = 5
        handle = None
        if timeout and not debugger:
            def timedout():
                error = "%s timed out after %d seconds" % (self.id(), timeout)
                if Timeout.debugTimeout:
                    print error
                raise Exception(error)
            timeout_handle = self.addTimeout(timeout, timedout, use_glib=False)
        try:
            self.running = True
            unittest.TestCase.run(self, result)
        except KeyboardInterrupt:
            # somehow this happens when timedout() above raises the exception
            # while inside glib main loop
            result.errors.append((self,
                                  "interrupted by timeout or CTRL-C or Python signal handler problem"))
        self.running = False
        self.removeTimeout(timeout_handle)
        if debugger:
            print "\ndone, quit gdb now\n"
        hasfailed = numerrors + numfailures != len(result.errors) + len(result.failures)

        if not debugger:
            os.kill(pserver.pid, signal.SIGTERM)
        pserver.communicate()
        serverout = open(syncevolog).read()
        if pserver.returncode and pserver.returncode != -15:
            hasfailed = True
        if hasfailed:
            # give D-Bus time to settle down
            time.sleep(1)
        os.kill(pmonitor.pid, signal.SIGTERM)
        pmonitor.communicate()
        monitorout = open(dbuslog).read()
        report = "\n\nD-Bus traffic:\n%s\n\nserver output:\n%s\n" % \
            (monitorout, serverout)
        if pserver.returncode and pserver.returncode != -15:
            # create a new failure specifically for the server
            result.errors.append((self,
                                  "server terminated with error code %d%s" % (pserver.returncode, report)))
        elif numerrors != len(result.errors):
            # append report to last error
            result.errors[-1] = (result.errors[-1][0], result.errors[-1][1] + report)
        elif numfailures != len(result.failures):
            # same for failure
            result.failures[-1] = (result.failures[-1][0], result.failures[-1][1] + report)

    def setUpServer(self):
        self.server = dbus.Interface(bus.get_object('org.syncevolution',
                                                    '/org/syncevolution/Server'),
                                     'org.syncevolution.Server')

    def createSession(self, config, wait):
        """Return sessionpath and session object for session using 'config'.
        A signal handler calls loop.quit() when this session becomes ready.
        If wait=True, then this call blocks until the session is ready.
        """
        sessionpath = self.server.StartSession(config)

        def session_ready(object, ready):
            if self.running and ready and object == sessionpath:
                DBusUtil.quit_events.append("session " + object + " ready")
                loop.quit()

        signal = bus.add_signal_receiver(session_ready,
                                         'SessionChanged',
                                         'org.syncevolution.Server',
                                         'org.syncevolution',
                                         None,
                                         byte_arrays=True,
                                         utf8_strings=True)
        session = dbus.Interface(bus.get_object('org.syncevolution',
                                                sessionpath),
                                 'org.syncevolution.Session')
        status, error, sources = session.GetStatus(utf8_strings=True)
        if wait and status == "queuing":
            # wait for signal
            loop.run()
            self.failUnlessEqual(DBusUtil.quit_events, ["session " + sessionpath + " ready"])
        elif DBusUtil.quit_events:
            # signal was processed inside D-Bus call?
            self.failUnlessEqual(DBusUtil.quit_events, ["session " + sessionpath + " ready"])
        if wait:
            # signal no longer needed, remove it because otherwise it
            # might record unexpected "session ready" events
            signal.remove()
        DBusUtil.quit_events = []
        return (sessionpath, session)

    def setUpSession(self, config):
        """stores ready session in self.sessionpath and self.session"""
        self.sessionpath, self.session = self.createSession(config, True)

    def setUpListeners(self, sessionpath):
        """records progress and status changes in DBusUtil.events and
        quits the main loop when the session is done"""

        def progress(*args):
            if self.running:
                DBusUtil.events.append(("progress", args))

        def status(*args):
            if self.running:
                DBusUtil.events.append(("status", args))
                if args[0] == "done":
                    if sessionpath:
                        DBusUtil.quit_events.append("session " + sessionpath + " done")
                    else:
                        DBusUtil.quit_events.append("session done")
                    loop.quit()

        bus.add_signal_receiver(progress,
                                'ProgressChanged',
                                'org.syncevolution.Session',
                                'org.syncevolution',
                                sessionpath,
                                byte_arrays=True, 
                                utf8_strings=True)
        bus.add_signal_receiver(status,
                                'StatusChanged',
                                'org.syncevolution.Session',
                                'org.syncevolution',
                                sessionpath,
                                byte_arrays=True, 
                                utf8_strings=True)

    def setUpConnectionListeners(self, conpath):
        """records connection signals (abort and reply), quits when
        getting an abort"""

        def abort():
            if self.running:
                DBusUtil.events.append(("abort",))
                DBusUtil.quit_events.append("connection " + conpath + " aborted")
                loop.quit()

        def reply(*args):
            if self.running:
                DBusUtil.reply = args
                if args[3]:
                    DBusUtil.quit_events.append("connection " + conpath + " got final reply")
                else:
                    DBusUtil.quit_events.append("connection " + conpath + " got reply")
                loop.quit()

        bus.add_signal_receiver(abort,
                                'Abort',
                                'org.syncevolution.Connection',
                                'org.syncevolution',
                                conpath,
                                byte_arrays=True, 
                                utf8_strings=True)
        bus.add_signal_receiver(reply,
                                'Reply',
                                'org.syncevolution.Connection',
                                'org.syncevolution',
                                conpath,
                                byte_arrays=True, 
                                utf8_strings=True)

class TestDBusServer(unittest.TestCase, DBusUtil):
    """Tests for the read-only Server API."""

    def setUp(self):
        self.setUpServer()

    def run(self, result):
        self.runTest(result)

    def testGetConfigsEmpty(self):
        """GetConfigs() with no configurations available"""
        configs = self.server.GetConfigs(False, utf8_strings=True)
        self.failUnlessEqual(configs, [])

    def testGetConfigsTemplates(self):
        """read templates"""
        configs = self.server.GetConfigs(True, utf8_strings=True)
        configs.sort()
        self.failUnlessEqual(configs, ["Funambol",
                                       "Google",
                                       "Memotoo",
                                       "Mobical",
                                       "ScheduleWorld",
                                       "Synthesis",
                                       "ZYB"])

    def testGetConfigScheduleWorld(self):
        """read ScheduleWorld template"""
        config1 = self.server.GetConfig("scheduleworld", True, utf8_strings=True)
        config2 = self.server.GetConfig("ScheduleWorld", True, utf8_strings=True)
        self.failIfEqual(config1[""]["deviceId"], config2[""]["deviceId"])
        config1[""]["deviceId"] = "foo"
        config2[""]["deviceId"] = "foo"
        self.failUnlessEqual(config1, config2)

    def testInvalidConfig(self):
        """check that the right error is reported for invalid config name"""
        try:
            config1 = self.server.GetConfig("no-such-config", False, utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: No server 'no-such-config' found")

class TestDBusSession(unittest.TestCase, DBusUtil):
    """Tests that work with an active session."""

    def setUp(self):
        self.setUpServer()
        self.setUpSession("")

    def run(self, result):
        self.runTest(result)

    def testCreateSession(self):
        """ask for session"""
        pass

    @timeout(20)
    def testSecondSession(self):
        """a second session should not run unless the first one stops"""
        sessionpath = self.server.StartSession("")

        def session_ready(object, ready):
            if self.running:
                DBusUtil.quit_events.append("session " + object + (ready and " ready" or " done"))
                loop.quit()

        bus.add_signal_receiver(session_ready,
                                'SessionChanged',
                                'org.syncevolution.Server',
                                'org.syncevolution',
                                None,
                                byte_arrays=True,
                                utf8_strings=True)
        session = dbus.Interface(bus.get_object('org.syncevolution',
                                                sessionpath),
                                 'org.syncevolution.Session')
        status, error, sources = session.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "queueing")
        # use hash so that we can write into it in callback()
        callback_called = {}
        def callback():
            callback_called[1] = "callback()"
            self.session.Detach()
        t1 = self.addTimeout(2, callback)
        # session 1 done
        loop.run()
        self.failUnless(callback_called)
        # session 2 ready
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, ["session " + self.sessionpath + " done",
                                                    "session " + sessionpath + " ready"])
        status, error, sources = session.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "idle")
        self.removeTimeout(t1)

class TestSessionAPIsEmptyName(unittest.TestCase, DBusUtil):
    """Test session APIs that work with an empty server name. Thus, all of session APIs which
       need this kind of checking are put in this class. """

    def setUp(self):
        self.setUpServer()
        self.setUpSession("")

    def run(self, result):
        self.runTest(result)

    def testGetConfigEmptyName(self):
        """Test the error is reported when the server name is empty for GetConfig"""
        try:
            self.session.GetConfig(True, utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: Template or server name must be given")

    def testSetConfigEmptyName(self):
        """Test the error is reported when the server name is empty for SetConfig"""
        try:
            self.session.SetConfig(True, False, {}, utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: Server name must be given")

    def testCheckSourceEmptyName(self):
        """Test the error is reported when the server name is empty for CheckSource"""
        try:
            self.session.CheckSource("", utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: Server name must be given")

    def testGetDatabasesEmptyName(self):
        """Test the error is reported when the server name is empty for GetDatabases"""
        try:
            self.session.GetDatabases("", utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: Server name must be given")

    def testGetReportsEmptyName(self):
        """Test the error is reported when the server name is empty for GetReports"""
        try:
            self.session.GetReports(0, 0, utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: Server name must be given")

class TestSessionAPIsDummy(unittest.TestCase, DBusUtil):
    """Tests that work for GetConfig/SetConfig/CheckSource/GetDatabases/GetReports in Session.
       This class is only working in a dummy config. Thus it can't do sync correctly. The purpose
       is to test some cleanup cases and expected errors. Also, some unit tests for some APIs 
       depend on a clean configuration so they are included here. For those unit tests depending
       on sync, another class is used """

    def setUp(self):
        self.setUpServer()
        # use a long name to avoid conflicts with other configs
        self.setUpSession("dummy-test-for-config-purpose")
        # default config
        self.config = { 
                         "" : { "syncURL" : "http://my.funambol.com/sync",
                                "username" : "unknown",
                                "password" : "secret",
                                "deviceId" : "foo"
                              },
                         "source/addressbook" : { "sync" : "two-way",
                                                  "type" : "addressbook",
                                                  "uri" : "card"
                                                },
                         "source/calendar"    : { "sync" : "disabled",
                                                  "type" : "calendar",
                                                  "uri" : "cal"
                                                },
                         "source/todo"        : { "sync" : "two-way",
                                                  "type" : "todo",
                                                  "uri" : "task"
                                                },
                         "source/memo"        : { "sync" : "two-way",
                                                  "type" : "memo",
                                                  "uri" : "text"
                                                }
                       }
        # update config
        self.updateConfig = { 
                               "" : { "password" : "nosecret"},
                               "source/addressbook" : { "sync" : "slow"}
                            }
        self.sources = ['addressbook', 'calendar', 'todo', 'memo']

    def run(self, result):
        self.runTest(result)

    def clearAllConfig(self):
        """ clear a server config. All should be removed. Used internally. """
        emptyConfig = {}
        self.session.SetConfig(False, False, emptyConfig, utf8_strings=True)

    def setupConfig(self):
        """ create a server with full config. Used internally. """
        self.session.SetConfig(False, False, self.config, utf8_strings=True)

    def testSetConfigInvalidParam(self):
        """ test that the right error is reported when parameters are not correct """
        try:
            self.session.SetConfig(False, True, {}, utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.Exception: Clearing existing configuration "
                                 "and temporary configuration changes which only affects the "
                                 "duration of the session are mutually exclusive")

    def testCreateGetConfig(self):
        """ test the config is created successfully. """
        self.setupConfig()
        """ get config and compare """
        config = self.session.GetConfig(False, utf8_strings=True)
        self.failUnlessEqual(config, self.config)

    def testUpdateConfig(self):
        """ test the config is permenantly updated correctly. """
        self.setupConfig()
        """ update the given config """
        self.session.SetConfig(True, False, self.updateConfig, utf8_strings=True)
        config = self.session.GetConfig(False, utf8_strings=True)
        self.failUnlessEqual(config[""]["password"], "nosecret")
        self.failUnlessEqual(config["source/addressbook"]["sync"], "slow")

    def testUpdateConfigTemp(self):
        """ test the config is just temporary updated but no effect in storage. """
        self.setupConfig()
        """ set config temporary """
        self.session.SetConfig(True, True, self.updateConfig, utf8_strings=True)
        config = self.session.GetConfig(False, utf8_strings=True)
        """ no change of any properties """
        self.failUnlessEqual(config, self.config)

    def testUpdateConfigError(self):
        """ test the right error is reported when an invalid property value is set """
        self.setupConfig()
        config = { 
                     "source/addressbook" : { "sync" : "invalid-value"}
                  }
        try:
            self.session.SetConfig(True, False, config, utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.Exception: test-dbus/config/syncevolution/"
                                 "dummy-test-for-config-purpose/sources/addressbook/config.ini: "
                                 "sync = invalid-value: not one of the valid values (two-way, "
                                 "slow, refresh-from-client = refresh-client, refresh-from-server "
                                 "= refresh-server = refresh, one-way-from-client = one-way-client, "
                                 "one-way-from-server = one-way-server = one-way, disabled = none)")

    def testUpdateNoConfig(self):
        """ test the right error is reported when updating properties for a non-existing server """
        self.clearAllConfig()
        try:
            self.session.SetConfig(True, False, self.updateConfig, utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: The server 'dummy-test-for-config-purpose' doesn't exist")
        
    def testClearAllConfig(self):
        """ test all configs of a server are cleared correctly. """
        """ first set up config and then clear all configs and also check a non-existing config """
        self.setupConfig()
        self.clearAllConfig()
        try:
            config = self.session.GetConfig(False, utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: No server 'dummy-test-for-config-purpose' found")
    
    def testClearConfigSources(self):
        """ test sources related configs are cleared correctly. """
        self.setupConfig()
        config1 = { 
                     "" : { "syncURL" : "http://my.funambol.com/sync",
                            "username" : "unknown",
                            "password" : "secret",
                            "deviceId" : "foo"
                          }
                  }
        self.session.SetConfig(False, False, config1, utf8_strings=True)
        config2 = self.session.GetConfig(False, utf8_strings=True)
        self.failUnlessEqual(config2, config1)

    def testCheckSourceNoConfig(self):
        """ test the right error is reported when the server doesn't exist """
        # make sure the config doesn't exist 
        self.clearAllConfig();
        try:
            self.session.CheckSource("", utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: No server 'dummy-test-for-config-purpose' found")

    def testCheckSourceNoSourceName(self):
        """ test the right error is reported when the source doesn't exist """
        self.setupConfig()
        try:
            self.session.CheckSource("dummy", utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchSource: 'dummy-test-for-config-purpose' "
                                 "has no 'dummy' source")

    def testCheckSourceInvalidEvolutionSource(self):
        """ test the right error is reported when the evolutionsource is invalid """
        self.setupConfig()
        config = { "source/memo" : { "evolutionsource" : "impossible-source"} }
        self.session.SetConfig(True, False, config, utf8_strings=True)
        try:
            self.session.CheckSource("memo", utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.Exception: The source 'memo' configuration "
                                 "is not correct")

    def testCheckSource(self):
        """ test all are right """
        self.setupConfig()
        try:
            for source in self.sources:
                self.session.CheckSource(source, utf8_strings=True)
        except dbus.DBusException, ex:
            self.fail("check source is failed with exception: " + str(ex))

    def testGetDatabasesNoConfig(self):
        """ test the right error is reported when the server doesn't exist """
        # make sure the config doesn't exist """
        self.clearAllConfig()
        try:
            self.session.GetDatabases("", utf8_strings=True)
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: No server 'dummy-test-for-config-purpose' found")

    def testGetDatabasesEmpty(self):
        """ test the empty is gotten for non-existing source """
        self.setupConfig()
        databases = self.session.GetDatabases("never_use_this_source_name", utf8_strings=True)
        self.failUnlessEqual(databases, [])

    def testGetDatabases(self):
        """ test the right way to get databases """
        self.setupConfig()

        # don't know actual databases, so compare results of two different times
        sources = ['addressbook', 'calendar', 'task', 'memo']
        databases1 = []
        for source in sources:
            databases1.append(self.session.GetDatabases(source, utf8_strings=True))
        # reverse the list of sources and get databases again
        sources.reverse()
        databases2 = []
        for source in sources:
            databases2.append(self.session.GetDatabases(source, utf8_strings=True))
        # sort two arrays
        databases1.sort()
        databases2.sort()
        self.failUnlessEqual(databases1, databases2)

    def testGetReportsNoConfig(self):
        """ Test when the given server has no reports. Also covers boundaries """
        self.clearAllConfig()
        reports = self.session.GetReports(0, 0, utf8_strings=True)
        self.failUnlessEqual(reports, [])
        reports = self.session.GetReports(0, 1, utf8_strings=True)
        self.failUnlessEqual(reports, [])
        reports = self.session.GetReports(0, 0xFFFFFFFF, utf8_strings=True)
        self.failUnlessEqual(reports, [])
        reports = self.session.GetReports(0xFFFFFFFF, 0xFFFFFFFF, utf8_strings=True)
        self.failUnlessEqual(reports, [])

    def testGetReportsNoReports(self):
        """ Test when the given server has no reports. Also covers boundaries """
        self.clearAllConfig()
        self.setupConfig()
        reports = self.session.GetReports(0, 0, utf8_strings=True)
        self.failUnlessEqual(reports, [])
        reports = self.session.GetReports(0, 1, utf8_strings=True)
        self.failUnlessEqual(reports, [])
        reports = self.session.GetReports(0, 0xFFFFFFFF, utf8_strings=True)
        self.failUnlessEqual(reports, [])
        reports = self.session.GetReports(0xFFFFFFFF, 0xFFFFFFFF, utf8_strings=True)
        self.failUnlessEqual(reports, [])

class TestSessionAPIsReal(unittest.TestCase, DBusUtil):
    """ This class is used to test those unit tests of session APIs, depending on doing sync.
        Thus we need a real server configuration to confirm sync could be run successfully.
        Typically we need make sure that at least one sync has been done before testing our
        desired unit tests. Note that it also covers session.Sync API itself """

    def setUp(self):
        self.setUpServer()
        self.setUpSession(config)

    def run(self, result):
        self.runTest(result, own_xdg=False)

    def doSync(self):
        self.setUpListeners(self.sessionpath)
        self.session.Sync("", {})
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, ["session " + self.sessionpath + " done"])

    @timeout(300)
    def testSync(self):
        '''run a real sync with default server'''
        self.doSync()
        # TODO: check recorded events in DBusUtil.events
        status, error, sources = self.session.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "done")
        self.failUnlessEqual(error, 0)

    @timeout(300)
    def testSyncSecondSession(self):
        '''ask for a second session that becomes ready after a real sync'''
        sessionpath2, session2 = self.createSession("", False)
        status, error, sources = session2.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "queueing")
        self.testSync()
        # now wait for second session becoming ready
        loop.run()
        status, error, sources = session2.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "idle")
        self.failUnlessEqual(DBusUtil.quit_events, ["session " + self.sessionpath + " done",
                                                    "session " + sessionpath2 + " ready"])
        session2.Detach()

    # TODO: don't depend on running a real sync in this test,
    # then remove timeout
    @timeout(300)
    def testGetReports(self):
        """ Test when the given server exists and reports are returned correctly. Also covers boundaries """
        # one sync, so reports could be generated at least one time """
        self.doSync()

        reports = self.session.GetReports(0, 0, utf8_strings=True)
        self.failUnlessEqual(reports, [])
        # GetReports should return one report starting from index 0
        reports = self.session.GetReports(0, 1, utf8_strings=True)
        self.assertTrue(len(reports) == 1)
        # each source contains 13 stat items, so the total number should be multiples of 13 
        self.assertTrue(len(reports[0]) % 13 == 0)

        # test the returned reports should be less than maximum and greater than 1
        reports = self.session.GetReports(0, 0xFFFFFFFF, utf8_strings=True)
        self.assertTrue(len(reports) >= 1)
        self.assertTrue(len(reports) <= 0xFFFFFFFF)

        reports = self.session.GetReports(0xFFFFFFFF, 0xFFFFFFFF, utf8_strings=True)
        self.failUnlessEqual(reports, [])

class TestDBusSyncError(unittest.TestCase, DBusUtil):
    def setUp(self):
        self.setUpServer()
        self.setUpSession(config)

    def run(self, result):
        self.runTest(result, own_xdg=True)

    def testSyncNoConfig(self):
        """Executes a real sync with no corresponding config."""
        self.setUpListeners(self.sessionpath)
        self.session.Sync("", {})
        loop.run()
        # TODO: check recorded events in DBusUtil.events
        status, error, sources = self.session.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "done")
        self.failUnlessEqual(error, 500)

class TestConnection(unittest.TestCase, DBusUtil):
    """Tests Server.Connect(). Tests depend on getting one Abort signal to terminate."""

    """a real message sent to our own server, DevInf stripped, username/password foo/bar"""
    message1 = '''<?xml version="1.0" encoding="UTF-8"?><SyncML xmlns='SYNCML:SYNCML1.2'><SyncHdr><VerDTD>1.2</VerDTD><VerProto>SyncML/1.2</VerProto><SessionID>255</SessionID><MsgID>1</MsgID><Target><LocURI>http://127.0.0.1:9000/syncevolution</LocURI></Target><Source><LocURI>sc-api-nat</LocURI><LocName>test</LocName></Source><Cred><Meta><Format xmlns='syncml:metinf'>b64</Format><Type xmlns='syncml:metinf'>syncml:auth-md5</Type></Meta><Data>kHzMn3RWFGWSKeBpXicppQ==</Data></Cred><Meta><MaxMsgSize xmlns='syncml:metinf'>20000</MaxMsgSize><MaxObjSize xmlns='syncml:metinf'>4000000</MaxObjSize></Meta></SyncHdr><SyncBody><Alert><CmdID>1</CmdID><Data>200</Data><Item><Target><LocURI>addressbook</LocURI></Target><Source><LocURI>./addressbook</LocURI></Source><Meta><Anchor xmlns='syncml:metinf'><Last>20091105T092757Z</Last><Next>20091105T092831Z</Next></Anchor><MaxObjSize xmlns='syncml:metinf'>4000000</MaxObjSize></Meta></Item></Alert><Final/></SyncBody></SyncML>'''

    def setUp(self):
        self.setUpServer()
        self.setUpListeners(None)

    def run(self, result):
        self.runTest(result, own_xdg=False)

    def getConnection(self, must_authenticate=False):
        conpath = self.server.Connect({'description': 'test-dbus.py',
                                       'transport': 'dummy'},
                                      must_authenticate,
                                      "")
        self.setUpConnectionListeners(conpath)
        connection = dbus.Interface(bus.get_object('org.syncevolution',
                                                   conpath),
                                    'org.syncevolution.Connection')
        return (conpath, connection)

    def testConnect(self):
        """get connection and close it"""
        conpath, connection = self.getConnection()
        connection.Close(False, 'good bye')
        loop.run()
        self.failUnlessEqual(DBusUtil.events, [('abort',)])

    def testInvalidConnect(self):
        """get connection, send invalid initial message"""
        conpath, connection = self.getConnection()
        try:
            connection.Process('1234', 'invalid message type')
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 'org.syncevolution.Exception: message type not supported for starting a sync')
        loop.run()
        self.failUnlessEqual(DBusUtil.events, [('abort',)])

    def testStartSync(self):
        """send a valid initial SyncML message"""
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        DBusUtil.quit_events = []
        # TODO: check events
        self.failIfEqual(DBusUtil.reply, None)
        self.failUnlessEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        # credentials should have been accepted because must_authenticate=False
        # in Connect(); 508 = "refresh required" is normal
        self.failUnless('<Status><CmdID>2</CmdID><MsgRef>1</MsgRef><CmdRef>1</CmdRef><Cmd>Alert</Cmd><TargetRef>addressbook</TargetRef><SourceRef>./addressbook</SourceRef><Data>508</Data>' in DBusUtil.reply[0])
        self.failIf('<Chal>' in DBusUtil.reply[0])
        self.failUnlessEqual(DBusUtil.reply[3], False)
        self.failIfEqual(DBusUtil.reply[4], '')
        connection.Close(False, 'good bye')
        loop.run()
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath + " aborted",
                                                    "session done"])

    def testCredentialsWrong(self):
        """send invalid credentials"""
        conpath, connection = self.getConnection(must_authenticate=True)
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        DBusUtil.quit_events = []
        # TODO: check events
        self.failIfEqual(DBusUtil.reply, None)
        self.failUnlessEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        # credentials should have been rejected because of wrong Nonce
        self.failUnless('<Chal>' in DBusUtil.reply[0])
        self.failUnlessEqual(DBusUtil.reply[3], False)
        self.failIfEqual(DBusUtil.reply[4], '')
        connection.Close(False, 'good bye')
        # when the login fails, the server also ends the session
        loop.run()
        loop.run()
        loop.run()
        DBusUtil.quit_events.sort()
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath + " aborted",
                                                    "connection " + conpath + " got final reply",
                                                    "session done"])

    def testCredentialsRight(self):
        """send correct credentials"""
        conpath, connection = self.getConnection(must_authenticate=True)
        plain_auth = TestConnection.message1.replace("<Type xmlns='syncml:metinf'>syncml:auth-md5</Type></Meta><Data>kHzMn3RWFGWSKeBpXicppQ==</Data>",
                                                     "<Type xmlns='syncml:metinf'>syncml:auth-basic</Type></Meta><Data>dGVzdDp0ZXN0</Data>")
        connection.Process(plain_auth, 'application/vnd.syncml+xml')
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        DBusUtil.quit_events = []
        self.failIfEqual(DBusUtil.reply, None)
        self.failUnlessEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        # credentials should have been accepted because with basic auth,
        # credentials can be replayed; 508 = "refresh required" is normal
        self.failUnless('<Status><CmdID>2</CmdID><MsgRef>1</MsgRef><CmdRef>1</CmdRef><Cmd>Alert</Cmd><TargetRef>addressbook</TargetRef><SourceRef>./addressbook</SourceRef><Data>508</Data>' in DBusUtil.reply[0])
        self.failUnlessEqual(DBusUtil.reply[3], False)
        self.failIfEqual(DBusUtil.reply[4], '')
        connection.Close(False, 'good bye')
        loop.run()
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath + " aborted",
                                                    "session done"])

    def testStartSyncTwice(self):
        """send the same SyncML message twice, starting two sessions"""
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        # TODO: check events
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        self.failIfEqual(DBusUtil.reply, None)
        self.failUnlessEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        self.failUnlessEqual(DBusUtil.reply[3], False)
        self.failIfEqual(DBusUtil.reply[4], '')
        DBusUtil.reply = None
        DBusUtil.quit_events = []

        # Now start another session with the same client *without*
        # closing the first one. The server should detect this
        # and forcefully close the first one.
        conpath2, connection2 = self.getConnection()
        connection2.Process(TestConnection.message1, 'application/vnd.syncml+xml')

        # reasons for leaving the loop, in random order:
        # - abort of first connection
        # - first session done
        # - reply for second one
        loop.run()
        loop.run()
        loop.run()
        DBusUtil.quit_events.sort()
        expected = [ "connection " + conpath + " aborted",
                     "session done",
                     "connection " + conpath2 + " got reply" ]
        expected.sort()
        self.failUnlessEqual(DBusUtil.quit_events, expected)
        self.failIfEqual(DBusUtil.reply, None)
        self.failUnlessEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        self.failUnlessEqual(DBusUtil.reply[3], False)
        self.failIfEqual(DBusUtil.reply[4], '')
        DBusUtil.quit_events = []

        # now quit for good
        connection2.Close(False, 'good bye')
        loop.run()
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath2 + " aborted",
                                                    "session done"])

    def testKillInactive(self):
        """block server with client A, then let client B connect twice"""
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        # TODO: check events
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        self.failIfEqual(DBusUtil.reply, None)
        self.failUnlessEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        self.failUnlessEqual(DBusUtil.reply[3], False)
        self.failIfEqual(DBusUtil.reply[4], '')
        DBusUtil.reply = None
        DBusUtil.quit_events = []

        # Now start two more sessions with the second client *without*
        # closing the first one. The server should remove only the
        # first connection of client B.
        message1_clientB = TestConnection.message1.replace("sc-api-nat", "sc-pim-ppc")
        conpath2, connection2 = self.getConnection()
        connection2.Process(message1_clientB, 'application/vnd.syncml+xml')
        conpath3, connection3 = self.getConnection()
        connection3.Process(message1_clientB, 'application/vnd.syncml+xml')
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, [ "connection " + conpath2 + " aborted" ])
        DBusUtil.quit_events = []

        # now quit for good
        connection3.Close(False, 'good bye client B')
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, [ "connection " + conpath3 + " aborted" ])
        DBusUtil.quit_events = []
        connection.Close(False, 'good bye client A')
        loop.run()
        loop.run()
        self.failUnlessEqual(DBusUtil.quit_events, ["connection " + conpath + " aborted",
                                                    "session done"])

if __name__ == '__main__':
    unittest.main()
