#! /usr/bin/python -u
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
import string
import difflib

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import dbus.service
import gobject
import sys
import re
import atexit

# introduced in python-gobject 2.16, not available
# on all Linux distros => make it optional
try:
    import glib
    have_glib = True
except ImportError:
    have_glib = False

DBusGMainLoop(set_as_default=True)

debugger = "" # "gdb"
server = ["syncevo-dbus-server"]
monitor = ["dbus-monitor"]
# primarily for XDG files, but also other temporary files
xdg_root = "temp-test-dbus"
configName = "dbus_unittest"

def usingValgrind():
    return 'valgrind' in os.environ.get("TEST_DBUS_PREFIX", "")

def GrepNotifications(dbuslog):
    '''finds all Notify calls and returns their parameters as list of line lists'''
    return re.findall(r'^method call .* dest=.* .*interface=org.freedesktop.Notifications; member=Notify\n((?:^   .*\n)*)',
                      dbuslog,
                      re.MULTILINE)

# See notification-daemon.py for a stand-alone version.
#
# Embedded here to avoid issues with setting up the environment
# in such a way that the stand-alone version can be run
# properly.
class Notifications (dbus.service.Object):
    '''fake org.freedesktop.Notifications implementation,'''
    '''used when there is none already registered on the session bus'''

    @dbus.service.method(dbus_interface='org.freedesktop.Notifications', in_signature='', out_signature='ssss')
    def GetServerInformation(self):
        return ('test-dbus', 'SyncEvolution', '0.1', '1.1')

    @dbus.service.method(dbus_interface='org.freedesktop.Notifications', in_signature='', out_signature='as')
    def GetCapabilities(self):
        return ['actions', 'body', 'body-hyperlinks', 'body-markup', 'icon-static', 'sound']

    @dbus.service.method(dbus_interface='org.freedesktop.Notifications', in_signature='susssasa{sv}i', out_signature='u')
    def Notify(self, app, replaces, icon, summary, body, actions, hints, expire):
        return random.randint(1,100)

# fork before connecting to the D-Bus daemon
child = os.fork()
if child == 0:
    bus = dbus.SessionBus()
    loop = gobject.MainLoop()
    name = dbus.service.BusName("org.freedesktop.Notifications", bus)
    # start dummy notification daemon, if possible;
    # if it fails, ignore (probably already one running)
    notifications = Notifications(bus, "/org/freedesktop/Notifications")
    loop.run()
    sys.exit(0)

# testing continues in parent process
atexit.register(os.kill, child, 9)
bus = dbus.SessionBus()
loop = gobject.MainLoop()

# Bluez default adapter
bt_adaptor = "/org/bluez/1036/hci0"

# handles D-Bus messages for '/' object in both net.connman and
# org.bluez; must be the same object because D-Bus cannot switch based
# on the bus name (not necessarily included in message)
class RootObject (dbus.service.Object):
    # ConnMan state
    state = "online"
    getPropertiesCalled = False
    waitingForGetProperties = False

    def __init__(self):
        self.bluez_name = dbus.service.BusName('org.bluez', bus)
        self.conn_name = dbus.service.BusName("net.connman", bus)
        dbus.service.Object.__init__(self, bus, '/')

    @dbus.service.method(dbus_interface='org.bluez.Manager', in_signature='', out_signature='o')
    def DefaultAdapter(self):
        return bt_adaptor

    @dbus.service.signal(dbus_interface='org.bluez.Manager', signature='o')
    def DefaultAdapterChanged(self, obj):
        return bt_adaptor

    @dbus.service.method(dbus_interface='net.connman.Manager', in_signature='', out_signature='a{sv}')
    def GetProperties(self):
        # notify TestDBusServerPresence.setUp()?
        if self.waitingForGetProperties:
            loop.quit()
        self.getPropertiesCalled = True
        return { "State" : self.state }

    @dbus.service.signal(dbus_interface='net.connman.Manager', signature='sv')
    def PropertyChanged(self, key, value):
        pass

    def setState(self, state):
        if self.state != state:
            self.state = state
            self.PropertyChanged("State", state)
            # race condition: it happened that method calls
            # reached syncevo-dbus-server before the state change,
            # thus breaking the test
            time.sleep(1)

    def reset(self):
        self.state = "online"
        self.getPropertiesCalled = False
        self.waitingForGetProperties = False

root = RootObject()

def property(key, value):
    """Function decorator which sets an arbitrary property of a test.
    Use like this:
         @property("foo", "bar")
         def testMyTest:
             ...

             print self.getTestProperty("foo", "default")
    """
    def __setProperty(func):
        if not "properties" in dir(func):
            func.properties = {}
        func.properties[key] = value
        return func
    return __setProperty

def timeout(seconds):
    """Function decorator which sets a non-default timeout for a test.
    The default timeout, enforced by DBusTest.runTest(), are 20 seconds.
    Use like this:
        @timeout(60)
        def testMyTest:
            ...
    """
    return property("timeout", seconds)

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
        """testOneTimeout - OneTimeout"""
        self.called = False
        start = time.time()
        def callback():
            self.called = True
        Timeout.addTimeout(2, callback, use_glib=False)
        time.sleep(10)
        end = time.time()
        self.assertTrue(self.called)
        self.assertFalse(end - start < 2)
        self.assertFalse(end - start >= 3)

    def testEmptyTimeout(self):
        """testEmptyTimeout - EmptyTimeout"""
        self.called = False
        start = time.time()
        def callback():
            self.called = True
        Timeout.addTimeout(0, callback, use_glib=False)
        if not self.called:
            time.sleep(10)
        end = time.time()
        self.assertTrue(self.called)
        self.assertFalse(end - start < 0)
        self.assertFalse(end - start >= 1)

    def testTwoTimeouts(self):
        """testTwoTimeouts - TwoTimeouts"""
        self.called = False
        start = time.time()
        def callback():
            self.called = True
        Timeout.addTimeout(2, callback, use_glib=False)
        Timeout.addTimeout(5, callback, use_glib=False)
        time.sleep(10)
        end = time.time()
        self.assertTrue(self.called)
        self.assertFalse(end - start < 2)
        self.assertFalse(end - start >= 3)
        time.sleep(10)
        end = time.time()
        self.assertTrue(self.called)
        self.assertFalse(end - start < 5)
        self.assertFalse(end - start >= 6)

    def testTwoReversedTimeouts(self):
        """testTwoReversedTimeouts - TwoReversedTimeouts"""
        self.called = False
        start = time.time()
        def callback():
            self.called = True
        Timeout.addTimeout(5, callback, use_glib=False)
        Timeout.addTimeout(2, callback, use_glib=False)
        time.sleep(10)
        end = time.time()
        self.assertTrue(self.called)
        self.assertFalse(end - start < 2)
        self.assertFalse(end - start >= 3)
        time.sleep(10)
        end = time.time()
        self.assertTrue(self.called)
        self.assertFalse(end - start < 5)
        self.assertFalse(end - start >= 6)

def TryKill(pid, signal):
    try:
        os.kill(pid, signal)
    except OSError, ex:
        # might have quit in the meantime, deal with the race
        # condition
        if ex.errno != 3:
            raise ex

def ShutdownSubprocess(popen, timeout):
    start = time.time()
    if popen.poll() == None:
        # kill process and process group, in case that process has
        # forked children (valgrindcheck.sh + syncevo-dbus-server
        # case or syncevo-dbus-server + local sync)
        TryKill(popen.pid, signal.SIGTERM)
        TryKill(-popen.pid, signal.SIGTERM)
    while popen.poll() == None and start + timeout >= time.time():
        time.sleep(0.01)
    if popen.poll() == None:
        TryKill(popen.pid, signal.SIGKILL)
        TryKill(-popen.pid, signal.SIGKILL)
        while popen.poll() == None and start + timeout + 1 >= time.time():
            time.sleep(0.01)
        return False
    else:
        # there shouldn't be any processes in the process group left now
        # because the parent process has quit normally, but make sure anyway
        TryKill(-popen.pid, signal.SIGKILL)
        return True

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
    pserver = None

    def getTestProperty(self, key, default):
        """retrieve values set with @property()"""
        test = eval(self.id().replace("__main__.", ""))
        if "properties" in dir(test):
            return test.properties.get(key, default)
        else:
            return default

    def runTest(self, result, own_xdg=True, serverArgs=[] ):
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

        """own_xdg is saved in self for we use this flag to check whether
        to copy the reference directory tree."""
        self.own_xdg = own_xdg
        env = copy.deepcopy(os.environ)
        if own_xdg:
            shutil.rmtree(xdg_root, True)
            env["XDG_DATA_HOME"] = xdg_root + "/data"
            env["XDG_CONFIG_HOME"] = xdg_root + "/config"
            env["XDG_CACHE_HOME"] = xdg_root + "/cache"

        # set additional environment variables for the test run,
        # as defined by @property("ENV", "foo=bar x=y")
        for assignment in self.getTestProperty("ENV", "").split():
            var, value = assignment.split("=")
            env[var] = value

        # always print all debug output directly (no output redirection),
        # and increase log level
        if self.getTestProperty("debug", True):
            env["SYNCEVOLUTION_DEBUG"] = "1"

        # can be set by a test to run additional tests on the content
        # of the D-Bus log
        self.runTestDBusCheck = None

        # testAutoSyncFailure (__main__.TestSessionAPIsDummy) => testAutoSyncFailure_TestSessionAPIsDummy
        testname = str(self).replace(" ", "_").replace("__main__.", "").replace("(", "").replace(")", "")
        dbuslog = testname + ".dbus.log"
        syncevolog = testname + ".syncevo.log"

        pmonitor = subprocess.Popen(monitor,
                                    stdout=open(dbuslog, "w"),
                                    stderr=subprocess.STDOUT)
        
        if debugger:
            print "\n%s: %s\n" % (self.id(), self.shortDescription())
            DBusUtil.pserver = subprocess.Popen([debugger] + server,
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
            logfile = open(syncevolog, "w")
            prefix = os.environ.get("TEST_DBUS_PREFIX", "")
            args = []
            if prefix:
                args.append(prefix)
            args.extend(server)
            args.extend(serverArgs)
            logfile.write("env:\n%s\n\nargs:\n%s\n\n" % (env, args))
            logfile.flush()
            size = os.path.getsize(syncevolog)
            DBusUtil.pserver = subprocess.Popen(args,
                                                preexec_fn=lambda: os.setpgid(0, 0),
                                                env=env,
                                                stdout=logfile,
                                                stderr=subprocess.STDOUT)
            while self.isServerRunning():
                newsize = os.path.getsize(syncevolog)
                if newsize != size:
                    if "syncevo-dbus-server: ready to run" in open(syncevolog).read():
                        break
                size = newsize
                time.sleep(1)

        numerrors = len(result.errors)
        numfailures = len(result.failures)
        if debugger:
            print "\nrunning\n"

        # Find out what test function we run and look into
        # the function definition to see whether it comes
        # with a non-default timeout, otherwise use a 20 second
        # timeout.
        timeout = self.getTestProperty("timeout", 20)
        timeout_handle = None
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

        if debugger:
            # allow debugger to run as long as it is needed
            DBusUtil.pserver.communicate()
        else:
            # Force shutdown after a certain delay: how much time we grant
            # the process depends on how much work it still needs to do
            # after being asked to quit. valgrind leak checking can take
            # a while.
            if usingValgrind():
                delay = 60
            else:
                delay = 20
            if not ShutdownSubprocess(DBusUtil.pserver, delay):
                print "   syncevo-dbus-server had to be killed with SIGKILL"
                result.errors.append((self,
                                      "syncevo-dbus-server had to be killed with SIGKILL"))
        serverout = open(syncevolog).read()
        if DBusUtil.pserver is not None and DBusUtil.pserver.returncode != -15:
            hasfailed = True
        if hasfailed:
            # give D-Bus time to settle down
            time.sleep(1)
        if not ShutdownSubprocess(pmonitor, 5):
            print "   dbus-monitor had to be killed with SIGKILL"
            result.errors.append((self,
                                  "dbus-monitor had to be killed with SIGKILL"))
        monitorout = open(dbuslog).read()
        report = "\n\nD-Bus traffic:\n%s\n\nserver output:\n%s\n" % \
            (monitorout, serverout)
        if self.runTestDBusCheck:
            try:
                self.runTestDBusCheck(self, monitorout)
            except:
                # only append report if not part of some other error below
                result.errors.append((self,
                                      "D-Bus log failed check: %s\n%s" % (sys.exc_info()[1], (not hasfailed and report) or "")))
        # detect the expected "killed by signal TERM" both when
        # running syncevo-dbus-server directly (negative value) and
        # when valgrindcheck.sh returns the error code 128 + 15 = 143
        if DBusUtil.pserver.returncode and \
           DBusUtil.pserver.returncode != 128 + 15 and \
           DBusUtil.pserver.returncode != -15:
            # create a new failure specifically for the server
            result.errors.append((self,
                                  "server terminated with error code %d%s" % (DBusUtil.pserver.returncode, report)))
        elif numerrors != len(result.errors):
            # append report to last error
            result.errors[-1] = (result.errors[-1][0], result.errors[-1][1] + report)
        elif numfailures != len(result.failures):
            # same for failure
            result.failures[-1] = (result.failures[-1][0], result.failures[-1][1] + report)

    def isServerRunning(self):
        """True while the syncevo-dbus-server executable is still running"""
        return DBusUtil.pserver and DBusUtil.pserver.poll() == None

    def serverExecutableHelper(self, pid):
        self.assertTrue(self.isServerRunning())
        maps = open("/proc/%d/maps" % pid, "r")
        regex = re.compile(r'[0-9a-f]*-[0-9a-f]* r-xp [0-9a-f]* [^ ]* \d* *(.*)\n')
        parentre = re.compile(r'^PPid:\s+(\d+)', re.MULTILINE)
        for line in maps:
            match = regex.match(line)
            if match:
                # must be syncevo-dbus-server
                res = match.group(1)
                if 'syncevo-dbus-server' in res:
                    return res
                # not found, try children
                for process in os.listdir('/proc'):
                    try:
                        status = open('/proc/%s/status' % process).read()
                        parent = parentre.search(status)
                        if parent and int(parent.group(1)) == pid:
                            res = self.serverExecutableHelper(int(process))
                            if res:
                                return res
                    except:
                        # ignore all errors
                        pass
        # no result
        return ""

    def serverExecutable(self):
        """returns full path of currently running syncevo-dbus-server binary"""
        res = self.serverExecutableHelper(DBusUtil.pserver.pid)
        self.assertTrue(res)
        return res

    def setUpServer(self):
        self.server = dbus.Interface(bus.get_object('org.syncevolution',
                                                    '/org/syncevolution/Server'),
                                     'org.syncevolution.Server')

    def createSession(self, config, wait, flags=[]):
        """Return sessionpath and session object for session using 'config'.
        A signal handler calls loop.quit() when this session becomes ready.
        If wait=True, then this call blocks until the session is ready.
        """
        if flags:
            sessionpath = self.server.StartSessionWithFlags(config, flags)
        else:
            sessionpath = self.server.StartSession(config)

        def session_ready(object, ready):
            if self.running and ready and object == sessionpath:
                DBusUtil.quit_events.append("session " + object + " ready")
                loop.quit()

        signal = bus.add_signal_receiver(session_ready,
                                         'SessionChanged',
                                         'org.syncevolution.Server',
                                         self.server.bus_name,
                                         None,
                                         byte_arrays=True,
                                         utf8_strings=True)
        session = dbus.Interface(bus.get_object(self.server.bus_name,
                                                sessionpath),
                                 'org.syncevolution.Session')
        status, error, sources = session.GetStatus(utf8_strings=True)
        if wait and status == "queuing":
            # wait for signal
            loop.run()
            self.assertEqual(DBusUtil.quit_events, ["session " + sessionpath + " ready"])
        elif DBusUtil.quit_events:
            # signal was processed inside D-Bus call?
            self.assertEqual(DBusUtil.quit_events, ["session " + sessionpath + " ready"])
        if wait:
            # signal no longer needed, remove it because otherwise it
            # might record unexpected "session ready" events
            signal.remove()
        DBusUtil.quit_events = []
        return (sessionpath, session)

    def setUpSession(self, config, flags=[]):
        """stores ready session in self.sessionpath and self.session"""
        self.sessionpath, self.session = self.createSession(config, True, flags)

    def progressChanged(self, *args):
        '''subclasses override this method to write specified callbacks for ProgressChanged signals
           It is called by progress signal receivers in setUpListeners'''
        pass

    def statusChanged(self, *args):
        '''subclasses override this method to write specified callbacks for StatusChanged signals
           It is called by status signal receivers in setUpListeners'''
        pass

    def setUpListeners(self, sessionpath):
        """records progress and status changes in DBusUtil.events and
        quits the main loop when the session is done"""

        def progress(*args):
            if self.running:
                DBusUtil.events.append(("progress", args))
                self.progressChanged(args)

        def status(*args):
            if self.running:
                DBusUtil.events.append(("status", args))
                self.statusChanged(args)
                if args[0] == "done":
                    if sessionpath:
                        DBusUtil.quit_events.append("session " + sessionpath + " done")
                    else:
                        DBusUtil.quit_events.append("session done")
                    loop.quit()

        bus.add_signal_receiver(progress,
                                'ProgressChanged',
                                'org.syncevolution.Session',
                                self.server.bus_name,
                                sessionpath,
                                byte_arrays=True, 
                                utf8_strings=True)
        bus.add_signal_receiver(status,
                                'StatusChanged',
                                'org.syncevolution.Session',
                                self.server.bus_name,
                                sessionpath,
                                byte_arrays=True, 
                                utf8_strings=True)

    def setUpConfigListeners(self):
        """records ConfigChanged signal and records it in DBusUtil.events, then quits the loop"""

        def config():
            if self.running:
                DBusUtil.events.append("ConfigChanged")
                DBusUtil.quit_events.append("ConfigChanged")
                loop.quit()

        bus.add_signal_receiver(config,
                                'ConfigChanged',
                                'org.syncevolution.Server',
                                self.server.bus_name,
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
                                self.server.bus_name,
                                conpath,
                                byte_arrays=True, 
                                utf8_strings=True)
        bus.add_signal_receiver(reply,
                                'Reply',
                                'org.syncevolution.Connection',
                                self.server.bus_name,
                                conpath,
                                byte_arrays=True, 
                                utf8_strings=True)

    def setUpFiles(self, snapshot):
        """ Copy reference directory trees from
        test/test-dbus/<snapshot> to own xdg_root (=./test-dbus). To
        be used only in tests which called runTest() with
        own_xdg=True."""
        self.assertTrue(self.own_xdg)
        # Get the absolute path of the current python file.
        scriptpath = os.path.abspath(os.path.expanduser(os.path.expandvars(sys.argv[0])))
        # reference directory 'test-dbus' is in the same directory as the current python file
        sourcedir = os.path.join(os.path.dirname(scriptpath), 'test-dbus', snapshot)
        """ Directories in test/test-dbus are copied to xdg_root, but
        maybe with different names, mappings are:
                  'test/test-dbus/<snapshot>'   './test-dbus'"""
        pairs = { 'sync4j'                    : '.sync4j',
                  'config'                    : 'config' ,
                  'cache'                     : 'cache'  ,
                  'data'                      : 'data'   }
        for src, dest in pairs.items():
            destpath = os.path.join(xdg_root, dest)
            # make sure the dest directory does not exist, which is required by shutil.copytree
            shutil.rmtree(destpath, True)
            sourcepath = os.path.join(sourcedir, src)
            # if source exists and could be accessed, then copy them
            if os.access(sourcepath, os.F_OK):
                shutil.copytree(sourcepath, destpath)

    def checkSync(self, expectedError=0, expectedResult=0):
        # check recorded events in DBusUtil.events, first filter them
        statuses = []
        progresses = []
        # Dict is used to check status order.  
        statusPairs = {"": 0, "idle": 1, "running" : 2, "aborting" : 3, "done" : 4}
        for item in DBusUtil.events:
            if item[0] == "status":
                statuses.append(item[1])
            elif item[0] == "progress":
                progresses.append(item[1])

        # check statuses
        lastStatus = ""
        lastSources = {}
        lastError = 0
        for status, error, sources in statuses:
            # consecutive entries should not be equal
            self.assertNotEqual((lastStatus, lastError, lastSources), (status, error, sources))
            # no error, unless expected
            if expectedError:
                if error:
                    self.assertEqual(expectedError, error)
            else:
                self.assertEqual(error, 0)
            # keep order: session status must be unchanged or the next status 
            seps = status.split(';')
            lastSeps = lastStatus.split(';')
            self.assertTrue(statusPairs.has_key(seps[0]))
            self.assertTrue(statusPairs[seps[0]] >= statusPairs[lastSeps[0]])
            # check specifiers
            if len(seps) > 1:
                self.assertEqual(seps[1], "waiting")
            for sourcename, value in sources.items():
                # no error
                self.assertEqual(value[2], 0)
                # keep order: source status must also be unchanged or the next status
                if lastSources.has_key(sourcename):
                    lastValue = lastSources[sourcename]
                    self.assertTrue(statusPairs[value[1]] >= statusPairs[lastValue[1]])

            lastStatus = status
            lastSources = sources
            lastError = error

        # check increasing progress percentage
        lastPercent = 0
        for percent, sources in progresses:
            self.assertFalse(percent < lastPercent)
            lastPercent = percent

        status, error, sources = self.session.GetStatus(utf8_strings=True)
        self.assertEqual(status, "done")
        self.assertEqual(error, expectedError)

        # now check that report is sane
        reports = self.session.GetReports(0, 100, utf8_strings=True)
        self.assertEqual(len(reports), 1)
        if expectedResult:
            self.assertEqual(int(reports[0]["status"]), expectedResult)
        else:
            self.assertEqual(int(reports[0]["status"]), 200)
            self.assertFalse("error" in reports[0])
        return reports[0]

    def assertEqualDiff(self, expected, res):
        '''Like assertEqual(), but raises an error which contains a
        diff of the two parameters. Useful when they are long strings
        (will be split at newlines automatically) or lists (compared
        as-is). Very similar to Python's 2.7 unittest, but also works
        for older Python releases and allows comparing strings against lists.'''
        def splitlines(str):
            '''split any object which looks like a string == has splitlines'''
            if 'splitlines' in dir(str):
                return str.splitlines(True)
            else:
                return str
        expected = splitlines(expected)
        res = splitlines(res)
        if expected != res:
            diff = ''.join(difflib.Differ().compare(expected, res))
            self.fail('differences between expected and actual text\n\n' + diff)

    def assertRegexpMatchesCustom(self, text, regex, msg=None):
        if isinstance(regex, str):
            regex = re.compile(regex)
        if not regex.search(text):
            if msg != None:
                self.fail(msg)
            else:
                self.fail('text does not match regex\n\nText:\n%s\n\nRegex:\n%s' % \
                              (text, regex.pattern))


    # reimplement Python 2.7 assertions only in older Python
    if True or not 'assertRegexpMatches' in dir(self):
        assertRegexpMatches = assertRegexpMatchesCustom

class TestDBusServer(unittest.TestCase, DBusUtil):
    """Tests for the read-only Server API."""

    def setUp(self):
        self.setUpServer()

    def run(self, result):
        self.runTest(result)

    def testCapabilities(self):
        """TestDBusServer.testCapabilities - Server.Capabilities()"""
        capabilities = self.server.GetCapabilities()
        capabilities.sort()
        self.assertEqual(capabilities, ['ConfigChanged', 'DatabaseProperties', 'GetConfigName', 'NamedConfig', 'Notifications', 'SessionAttach', 'SessionFlags', 'Version'])

    def testVersions(self):
        """TestDBusServer.testVersions - Server.GetVersions()"""
        versions = self.server.GetVersions()
        self.assertNotEqual(versions["version"], "")
        self.assertNotEqual(versions["system"], None)
        self.assertNotEqual(versions["backends"], None)

    def testGetConfigsEmpty(self):
        """TestDBusServer.testGetConfigsEmpty - Server.GetConfigsEmpty()"""
        configs = self.server.GetConfigs(False, utf8_strings=True)
        self.assertEqual(configs, [])

    @property("ENV", "DBUS_TEST_BLUETOOTH=none")
    def testGetConfigsTemplates(self):
        """TestDBusServer.testGetConfigsTemplates - Server.GetConfigsTemplates()"""
        configs = self.server.GetConfigs(True, utf8_strings=True)
        configs.sort()
        self.assertEqual(configs, ["Funambol",
                                   "Google_Calendar",
                                   "Google_Contacts",
                                   "Goosync",
                                   "Memotoo",
                                   "Mobical",
                                   "Oracle",
                                   "Ovi",
                                   "ScheduleWorld",
                                   "SyncEvolution",
                                   "Synthesis",
                                   "WebDAV",
                                   "Yahoo",
                                   "eGroupware"])

    def testGetConfigScheduleWorld(self):
        """TestDBusServer.testGetConfigScheduleWorld - Server.GetConfigScheduleWorld()"""
        config1 = self.server.GetConfig("scheduleworld", True, utf8_strings=True)
        config2 = self.server.GetConfig("ScheduleWorld", True, utf8_strings=True)
        self.assertNotEqual(config1[""]["deviceId"], config2[""]["deviceId"])
        config1[""]["deviceId"] = "foo"
        config2[""]["deviceId"] = "foo"
        self.assertEqual(config1, config2)

    def testInvalidConfig(self):
        """TestDBusServer.testInvalidConfig - Server.NoSuchConfig exception"""
        try:
            config1 = self.server.GetConfig("no-such-config", False, utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: No configuration 'no-such-config' found")
        else:
            self.fail("no exception thrown")

class TestDBusServerTerm(unittest.TestCase, DBusUtil):
    def setUp(self):
        self.setUpServer()

    def run(self, result):
        self.runTest(result, True, ["-d", "10"])

    @timeout(100)
    def testNoTerm(self):
        """TestDBusServerTerm.testNoTerm - D-Bus server must stay around during calls"""

        """The server should stay alive because we have dbus call within
        the duration. The loop is to make sure the total time is longer 
        than duration and the dbus server still stays alive for dbus calls.""" 
        for i in range(0, 4):
            time.sleep(4)
            try:
                self.server.GetConfigs(True, utf8_strings=True)
            except dbus.DBusException:
                self.fail("dbus server should work correctly")

    @timeout(100)
    def testTerm(self):
        """TestDBusServerTerm.testTerm - D-Bus server must auto-terminate"""
        #sleep a duration and wait for syncevo-dbus-server termination
        time.sleep(16)
        try:
            self.server.GetConfigs(True, utf8_strings=True)
        except dbus.DBusException:
            pass
        else:
            self.fail("no exception thrown")

    @timeout(100)
    def testTermConnection(self):
        """TestDBusServerTerm.testTermConnection - D-Bus server must terminate after closing connection and not sooner"""
        conpath = self.server.Connect({'description': 'test-dbus.py',
                                       'transport': 'dummy'},
                                      False,
                                      "")
        time.sleep(16)
        try:
            self.server.GetConfigs(True, utf8_strings=True)
        except dbus.DBusException:
            self.fail("dbus server should not terminate")

        connection = dbus.Interface(bus.get_object(self.server.bus_name,
                                                   conpath),
                                    'org.syncevolution.Connection')
        connection.Close(False, "good bye", utf8_strings=True)
        time.sleep(16)
        try:
            self.server.GetConfigs(True, utf8_strings=True)
        except dbus.DBusException:
            pass
        else:
            self.fail("no exception thrown")

    @timeout(100)
    def testTermAttachedClients(self):
        """TestDBusServerTerm.testTermAttachedClients - D-Bus server must not terminate while clients are attached"""

        """Also it tries to test the dbus server's behavior when a client 
        attaches the server many times"""
        self.server.Attach()
        self.server.Attach()
        time.sleep(16)
        try:
            self.server.GetConfigs(True, utf8_strings=True)
        except dbus.DBusException:
            self.fail("dbus server should not terminate")
        self.server.Detach()
        time.sleep(16)

        try:
            self.server.GetConfigs(True, utf8_strings=True)
        except dbus.DBusException:
            self.fail("dbus server should not terminate")

        self.server.Detach()
        time.sleep(16)
        try:
            self.server.GetConfigs(True, utf8_strings=True)
        except dbus.DBusException:
            pass
        else:
            self.fail("no exception thrown")

    @timeout(100)
    def testAutoSyncOn(self):
        """TestDBusServerTerm.testAutoSyncOn - D-Bus server must not terminate while auto syncing is enabled"""
        self.setUpSession("scheduleworld")
        # enable auto syncing with a very long delay to prevent accidentally running it
        config = self.session.GetConfig(True, utf8_strings=True)
        config[""]["autoSync"] = "1"
        config[""]["autoSyncInterval"] = "60m"
        self.session.SetConfig(False, False, config)
        self.session.Detach()

        time.sleep(16)

        try:
            self.server.GetConfigs(True, utf8_strings=True)
        except dbus.DBusException:
            self.fail("dbus server should not terminate")

    @timeout(100)
    def testAutoSyncOff(self):
        """TestDBusServerTerm.testAutoSyncOff - D-Bus server must terminate after auto syncing was disabled"""
        self.setUpSession("scheduleworld")
        # enable auto syncing with a very long delay to prevent accidentally running it
        config = self.session.GetConfig(True, utf8_strings=True)
        config[""]["autoSync"] = "1"
        config[""]["autoSyncInterval"] = "60m"
        self.session.SetConfig(False, False, config)
        self.session.Detach()

        self.setUpSession("scheduleworld")
        config[""]["autoSync"] = "0"
        self.session.SetConfig(True, False, config)
        self.session.Detach()

        time.sleep(16)
        try:
            self.server.GetConfigs(True, utf8_strings=True)
        except dbus.DBusException:
            pass
        else:
            self.fail("no exception thrown")

    @timeout(100)
    def testAutoSyncOff2(self):
        """TestDBusServerTerm.testAutoSyncOff2 - D-Bus server must terminate after auto syncing was disabled after a while"""
        self.setUpSession("scheduleworld")
        # enable auto syncing with a very long delay to prevent accidentally running it
        config = self.session.GetConfig(True, utf8_strings=True)
        config[""]["autoSync"] = "1"
        config[""]["autoSyncInterval"] = "60m"
        self.session.SetConfig(False, False, config)
        self.session.Detach()

        # wait until -d 10 second timeout has triggered in syncevo-dbus-server
        time.sleep(11)

        self.setUpSession("scheduleworld")
        config[""]["autoSync"] = "0"
        self.session.SetConfig(True, False, config)
        self.session.Detach()

        # should shut down after the 10 second idle period
        time.sleep(16)

        try:
            self.server.GetConfigs(True, utf8_strings=True)
        except dbus.DBusException:
            pass
        else:
            self.fail("no exception thrown")

class TestNamedConfig(unittest.TestCase, DBusUtil):
    """Tests for Set/GetNamedConfig"""

    def setUp(self):
        self.setUpServer()

    def run(self, result):
        self.runTest(result)

    def testSetNamedConfigError(self):
        """TestDBusSession.testSetNamedConfigError - SetNamedConfig() only allowed in 'all-configs' sessions"""
        self.setUpSession("")
        try:
            self.session.SetNamedConfig("foobar", False, False, {})
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                             "org.syncevolution.InvalidCall: SetNameConfig() only allowed in 'all-configs' sessions")
        else:
            self.fail("no exception thrown")

    def testSetNamedConfigErrorTemporary(self):
        """TestDBusSession.testSetNamedConfigErrorTemporary - SetNamedConfig() only implemented for session config"""
        self.setUpSession("foo", [ "all-configs" ])
        try:
            self.session.SetNamedConfig("foobar", False, True, {})
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                             "org.syncevolution.InvalidCall: SetNameConfig() with temporary config change only supported for config named when starting the session")
        else:
            self.fail("no exception thrown")
        self.session.Detach()

        self.setUpSession("")
        self.session.SetNamedConfig("", False, True, {})

    def testSetNamedConfig(self):
        """TestDBusSession.testSetNamedConfig - create two different configs in one session"""
        self.setUpSession("", [ "all-configs" ])

        fooConfig = {"": {"username": "foo", "configName": "foo"}}
        barConfig = {"": {"username": "bar", "configName": "bar"}}

        self.session.SetNamedConfig("foo", False, False, fooConfig)
        self.session.SetNamedConfig("bar", False, False, barConfig)

        self.assertEqual(fooConfig, self.server.GetConfig("foo", False))
        self.assertEqual(barConfig, self.server.GetConfig("bar", False))

        self.assertEqual(fooConfig, self.session.GetNamedConfig("foo", False))
        self.assertEqual(barConfig, self.session.GetNamedConfig("bar", False))

class TestDBusServerPresence(unittest.TestCase, DBusUtil):
    """Tests Presence signal and checkPresence API"""

    # Our fake ConnMan implementation must be present on the
    # bus also outside of tests, because syncevo-dbus-server
    # will try to call it before setUp(). The implementation's
    # initialization and tearDown() below ensures that the state
    # is "online" outside of tests.
    conn = root

    def setUp(self):
        self.setUpServer()
        self.cbFailure = None
        # we don't know if the GetProperties() call was already
        # processed; if not, wait for it here
        if not self.conn.getPropertiesCalled:
            self.conn.waitingForGetProperties = True
            loop.run()

    def tearDown(self):
        self.conn.reset()
        self.conf = None

    def presenceCB(self,
                   server, status, transport,
                   expected):
        try:
            state = expected.pop(server, None)
            if not state:
                self.fail("unexpected presence signal for config " + server)
            self.failUnlessEqual(status, state[0])
            if not status:
                self.failUnlessEqual(transport, state[1])
        except Exception, ex:
            # tell test method about the problem
            loop.quit()
            self.cbFailure = ex
            # log exception
            raise ex

        if not expected:
            # got all expected signals
            loop.quit()

    def expect(self, expected):
        '''expected: hash from server config name to state+transport'''
        match = bus.add_signal_receiver(lambda x,y,z:
                                            self.presenceCB(x,y,z, expected), \
                                            'Presence',
                                            'org.syncevolution.Server',
                                            self.server.bus_name,
                                            None,
                                            byte_arrays=True,
                                            utf8_strings=True)
        return match

    @property("ENV", "DBUS_TEST_CONNMAN=session DBUS_TEST_NETWORK_MANAGER=none")
    @timeout(100)
    def testPresenceSignal(self):
        """TestDBusServerPresence.testPresenceSignal - check Server.Presence signal"""

        # creating a config does not trigger a Presence signal
        self.setUpSession("foo")
        self.session.SetConfig(False, False, {"" : {"syncURL": "http://http-only-1"}})
        self.session.Detach()

        # go offline
        match = self.expect({"foo" : ("no transport", "")})
        self.conn.setState("idle")
        loop.run()
        self.failIf(self.cbFailure)
        match.remove()

        # Changing the properties temporarily does change
        # the presence of the config although strictly speaking,
        # the presence of the config on disk hasn't changed.
        # Not sure whether we really want that behavior.
        match = self.expect({"foo" : ("", "obex-bt://temp-bluetooth-peer-changed-from-http")})
        self.setUpSession("foo")
        self.session.SetConfig(True, False, {"" : {"syncURL":
        "obex-bt://temp-bluetooth-peer-changed-from-http"}})
        # A ConnMan state change is needed to trigger the presence signal.
        # Definitely not the behavior that we want :-/
        self.conn.setState("failure")
        loop.run()
        self.failIf(self.cbFailure)
        match.remove()
        # remove temporary config change, back to using HTTP
        # BUG BMC #24648 in syncevo-dbus-server: after discarding the temporary
        # config change it keeps using the obex-bt syncURL.
        # Work around that bug in thus test here temporarily
        # by explicitly restoring the previous URL.
        self.session.SetConfig(True, False, {"" : {"syncURL": "http://http-only-1"}})
        self.session.Detach()

        # create second session
        self.setUpSession("bar")
        self.session.SetConfig(False, False, {"" : {"syncURL":
            "http://http-client-2"}})
        self.session.Detach()

        # go back to online mode
        match = self.expect({"foo" : ("", "http://http-only-1"),
                             "bar" : ("", "http://http-client-2")})
        self.conn.setState("online")
        loop.run()
        self.failIf(self.cbFailure)
        match.remove()

        # and offline
        match = self.expect({"foo" : ("no transport", ""),
                             "bar" : ("no transport", "")})
        self.conn.setState("idle")
        loop.run()
        self.failIf(self.cbFailure)
        match.remove()

    @property("ENV", "DBUS_TEST_CONNMAN=session DBUS_TEST_NETWORK_MANAGER=none")
    @timeout(100)
    def testServerCheckPresence(self):
        """TestDBusServerPresence.testServerCheckPresence - check Server.CheckPresence()"""
        self.setUpSession("foo")
        self.session.SetConfig(False, False, {"" : {"syncURL":
        "http://http-client"}})
        self.session.Detach()
        self.setUpSession("bar")
        self.session.SetConfig(False, False, {"" : {"syncURL":
            "obex-bt://bt-client"}})
        self.session.Detach()
        self.setUpSession("foobar")
        self.session.SetConfig(False, False, {"" : {"syncURL":
            "obex-bt://bt-client-mixed http://http-client-mixed"}})
        self.session.Detach()

        # online initially
        (status, transports) = self.server.CheckPresence ("foo")
        self.assertEqual (status, "")
        self.assertEqual (transports, ["http://http-client"])
        (status, transports) = self.server.CheckPresence ("bar")
        self.assertEqual (status, "")
        self.assertEqual (transports, ["obex-bt://bt-client"])
        (status, transports) = self.server.CheckPresence ("foobar")
        self.assertEqual (status, "")
        self.assertEqual (transports, ["obex-bt://bt-client-mixed",
        "http://http-client-mixed"])

        # go offline; Bluetooth remains on
        self.conn.setState("idle")
        # wait until server has seen the state change
        match = bus.add_signal_receiver(lambda x,y,z: loop.quit(),
                                        'Presence',
                                        'org.syncevolution.Server',
                                        self.server.bus_name,
                                        None,
                                        byte_arrays=True,
                                        utf8_strings=True)
        match.remove()
        (status, transports) = self.server.CheckPresence ("foo")
        self.assertEqual (status, "no transport")
        (status, transports) = self.server.CheckPresence ("bar")
        self.assertEqual (status, "")
        self.assertEqual (transports, ["obex-bt://bt-client"])
        (status, transports) = self.server.CheckPresence ("foobar")
        self.assertEqual (status, "")
        self.assertEqual (transports, ["obex-bt://bt-client-mixed"])

    @property("ENV", "DBUS_TEST_CONNMAN=session DBUS_TEST_NETWORK_MANAGER=none")
    @timeout(100)
    def testSessionCheckPresence(self):
        """TestDBusServerPresence.testSessionCheckPresence - check Session.CheckPresence()"""
        self.setUpSession("foobar")
        self.session.SetConfig(False, False, {"" : {"syncURL":
            "obex-bt://bt-client-mixed http://http-client-mixed"}})
        status = self.session.checkPresence()
        self.failUnlessEqual (status, "")

        # go offline; Bluetooth remains on
        self.conn.setState("idle")
        # wait until server has seen the state change
        match = bus.add_signal_receiver(lambda x,y,z: loop.quit(),
                                        'Presence',
                                        'org.syncevolution.Server',
                                        self.server.bus_name,
                                        None,
                                        byte_arrays=True,
                                        utf8_strings=True)
        match.remove()

        # config uses Bluetooth, so syncing still possible
        status = self.session.checkPresence()
        self.failUnlessEqual (status, "")

        # now the same without Bluetooth, while offline
        self.session.Detach()
        self.setUpSession("foo")
        self.session.SetConfig(False, False, {"" : {"syncURL": "http://http-only"}})
        status = self.session.checkPresence()
        self.assertEqual (status, "no transport")

        # go online
        self.conn.setState("online")
        # wait until server has seen the state change
        match = bus.add_signal_receiver(lambda x,y,z: loop.quit(),
                                        'Presence',
                                        'org.syncevolution.Server',
                                        self.server.bus_name,
                                        None,
                                        byte_arrays=True,
                                        utf8_strings=True)
        match.remove()
        status = self.session.checkPresence()
        self.failUnlessEqual (status, "")

        # temporary config change shall always affect the
        # Session.CheckPresence() result: go offline,
        # then switch to Bluetooth (still present)
        self.conn.setState("idle")
        match = bus.add_signal_receiver(lambda x,y,z: loop.quit(),
                                        'Presence',
                                        'org.syncevolution.Server',
                                        self.server.bus_name,
                                        None,
                                        byte_arrays=True,
                                        utf8_strings=True)
        match.remove()
        status = self.session.checkPresence()
        self.failUnlessEqual (status, "no transport")
        self.session.SetConfig(True, False, {"" : {"syncURL": "obex-bt://bt-client-mixed"}})
        status = self.session.checkPresence()
        self.failUnlessEqual (status, "")

    def run(self, result):
        self.runTest(result, True)

class TestDBusSession(unittest.TestCase, DBusUtil):
    """Tests that work with an active session."""

    def setUp(self):
        self.setUpServer()
        self.setUpSession("")

    def run(self, result):
        self.runTest(result)

    def testCreateSession(self):
        """TestDBusSession.testCreateSession - ask for session"""
        self.assertEqual(self.session.GetFlags(), [])
        self.assertEqual(self.session.GetConfigName(), "@default")

    def testAttachSession(self):
        """TestDBusSession.testAttachSession - attach to running session"""
        self.session.Attach()
        self.session.Detach()
        self.assertEqual(self.session.GetFlags(), [])
        self.assertEqual(self.session.GetConfigName(), "@default")

    @timeout(70)
    def testAttachOldSession(self):
        """TestDBusSession.testAttachOldSession - attach to session which no longer has clients"""
        self.session.Detach()
        time.sleep(5)
        # This used to be impossible with SyncEvolution 1.0 because it
        # removed the session right after the previous client
        # left. SyncEvolution 1.1 makes it possible by keeping
        # sessions around for a minute. However, the session is
        # no longer listed because it really should only be used
        # by clients which heard about it before.
        self.assertEqual(self.server.GetSessions(), [])
        self.session.Attach()
        self.assertEqual(self.session.GetFlags(), [])
        self.assertEqual(self.session.GetConfigName(), "@default")
        time.sleep(60)
        self.assertEqual(self.session.GetFlags(), [])

    @timeout(70)
    def testExpireSession(self):
        """TestDBusSession.testExpireSession - ensure that session stays around for a minute"""
        self.session.Detach()
        time.sleep(5)
        self.assertEqual(self.session.GetFlags(), [])
        self.assertEqual(self.session.GetConfigName(), "@default")
        time.sleep(60)
        try:
            self.session.GetFlags()
        except:
            pass
        else:
            self.fail("Session.GetFlags() should have failed")

    def testCreateSessionWithFlags(self):
        """TestDBusSession.testCreateSessionWithFlags - ask for session with some specific flags and config"""
        self.session.Detach()
        self.sessionpath, self.session = self.createSession("FooBar@no-such-context", True, ["foo", "bar"])
        self.assertEqual(self.session.GetFlags(), ["foo", "bar"])
        self.assertEqual(self.session.GetConfigName(), "foobar@no-such-context")

    def testSecondSession(self):
        """TestDBusSession.testSecondSession - a second session should not run unless the first one stops"""
        sessions = self.server.GetSessions()
        self.assertEqual(sessions, [self.sessionpath])
        sessionpath = self.server.StartSession("")
        sessions = self.server.GetSessions()
        self.assertEqual(sessions, [self.sessionpath, sessionpath])

        def session_ready(object, ready):
            if self.running:
                DBusUtil.quit_events.append("session " + object + (ready and " ready" or " done"))
                loop.quit()

        bus.add_signal_receiver(session_ready,
                                'SessionChanged',
                                'org.syncevolution.Server',
                                self.server.bus_name,
                                None,
                                byte_arrays=True,
                                utf8_strings=True)

        def status(*args):
            if self.running:
                DBusUtil.events.append(("status", args))
                if args[0] == "idle":
                    DBusUtil.quit_events.append("session " + sessionpath + " idle")
                    loop.quit()

        bus.add_signal_receiver(status,
                                'StatusChanged',
                                'org.syncevolution.Session',
                                self.server.bus_name,
                                sessionpath,
                                byte_arrays=True, 
                                utf8_strings=True)

        session = dbus.Interface(bus.get_object(self.server.bus_name,
                                                sessionpath),
                                 'org.syncevolution.Session')
        status, error, sources = session.GetStatus(utf8_strings=True)
        self.assertEqual(status, "queueing")
        # use hash so that we can write into it in callback()
        callback_called = {}
        def callback():
            callback_called[1] = "callback()"
            self.session.Detach()
        try:
            t1 = self.addTimeout(2, callback)
            # session 1 done
            loop.run()
            self.assertTrue(callback_called)
            # session 2 ready and idle
            loop.run()
            loop.run()
            expected = ["session " + self.sessionpath + " done",
                        "session " + sessionpath + " idle",
                        "session " + sessionpath + " ready"]
            expected.sort()
            DBusUtil.quit_events.sort()
            self.assertEqual(DBusUtil.quit_events, expected)
            status, error, sources = session.GetStatus(utf8_strings=True)
            self.assertEqual(status, "idle")
        finally:
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
        """TestSessionAPIsEmptyName.testGetConfigEmptyName - reading empty default config"""
        config = self.session.GetConfig(False, utf8_strings=True)

    def testGetTemplateEmptyName(self):
        """TestSessionAPIsEmptyName.testGetTemplateEmptyName - trigger error by getting template for empty server name"""
        try:
            config = self.session.GetConfig(True, utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: No template '' found")
        else:
            self.fail("no exception thrown")

    def testCheckSourceEmptyName(self):
        """TestSessionAPIsEmptyName.testCheckSourceEmptyName - Test the error is reported when the server name is empty for CheckSource"""
        try:
            self.session.CheckSource("", utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.NoSuchSource: '' has no '' source")
        else:
            self.fail("no exception thrown")

    def testGetDatabasesEmptyName(self):
        """TestSessionAPIsEmptyName.testGetDatabasesEmptyName - Test the error is reported when the server name is empty for GetDatabases"""
        try:
            self.session.GetDatabases("", utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.NoSuchSource: '' has no '' source")
        else:
            self.fail("no exception thrown")

    def testGetReportsEmptyName(self):
        """TestSessionAPIsEmptyName.testGetReportsEmptyName - Test reports from all peers are returned in order when the peer name is empty for GetReports"""
        self.setUpFiles('reports')
        reports = self.session.GetReports(0, 0xFFFFFFFF, utf8_strings=True)
        self.assertEqual(len(reports), 7)
        refPeers = ["dummy-test", "dummy", "dummy-test", "dummy-test",
                    "dummy-test", "dummy_test", "dummy-test"]
        for i in range(0, len(refPeers)):
            self.assertEqual(reports[i]["peer"], refPeers[i])

    def testGetReportsContext(self):
        """TestSessionAPIsEmptyName.testGetReportsContext - Test reports from a context are returned when the peer name is empty for GetReports"""
        self.setUpFiles('reports')
        self.session.Detach()
        self.setUpSession("@context")
        reports = self.session.GetReports(0, 0xFFFFFFFF, utf8_strings=True)
        self.assertEqual(len(reports), 1)
        self.assertTrue(reports[0]["dir"].endswith("dummy_+test@context-2010-01-20-10-10"))


class TestSessionAPIsDummy(unittest.TestCase, DBusUtil):
    """Tests that work for GetConfig/SetConfig/CheckSource/GetDatabases/GetReports in Session.
       This class is only working in a dummy config. Thus it can't do sync correctly. The purpose
       is to test some cleanup cases and expected errors. Also, some unit tests for some APIs 
       depend on a clean configuration so they are included here. For those unit tests depending
       on sync, another class is used """

    def setUp(self):
        self.setUpServer()
        # use 'dummy-test' as the server name
        self.setUpSession("dummy-test")
        # default config
        self.config = { 
                         "" : { "syncURL" : "http://impossible-syncurl-just-for-testing-to-avoid-conflict",
                                "username" : "unknown",
                                # the password request tests depend on not having a real password here
                                "password" : "-",
                                "deviceId" : "foo",
                                "RetryInterval" : "10",
                                "RetryDuration" : "20",
                                "ConsumerReady" : "1",
                                "configName" : "dummy-test"
                              },
                         "source/addressbook" : { "sync" : "slow",
                                                  "backend" : "file",
                                                  "database" : "file://" + xdg_root + "/addressbook",
                                                  "databaseFormat" : "text/vcard",
                                                  "uri" : "card"
                                                },
                         "source/calendar"    : { "sync" : "disabled",
                                                  "backend" : "file",
                                                  "database" : "file://" + xdg_root + "/calendar",
                                                  "databaseFormat" : "text/calendar",
                                                  "uri" : "cal"
                                                },
                         "source/todo"        : { "sync" : "disabled",
                                                  "backend" : "file",
                                                  "database" : "file://" + xdg_root + "/todo",
                                                  "databaseFormat" : "text/calendar",
                                                  "uri" : "task"
                                                },
                         "source/memo"        : { "sync" : "disabled",
                                                  "backend" : "file",
                                                  "database" : "file://" + xdg_root + "/memo",
                                                  "databaseFormat" : "text/calendar",
                                                  "uri" : "text"
                                                }
                       }
        # update config
        self.updateConfig = { 
                               "" : { "username" : "doe"},
                               "source/addressbook" : { "sync" : "slow"}
                            }
        self.sources = ['addressbook', 'calendar', 'todo', 'memo']

        # set by SessionReady signal handlers in some tests
        self.auto_sync_session_path = None

    def run(self, result):
        self.runTest(result)

    def clearAllConfig(self):
        """ clear a server config. All should be removed. Used internally. """
        emptyConfig = {}
        self.session.SetConfig(False, False, emptyConfig, utf8_strings=True)

    def setupConfig(self):
        """ create a server with full config. Used internally. """
        self.session.SetConfig(False, False, self.config, utf8_strings=True)

    def testTemporaryConfig(self):
        """TestSessionAPIsDummy.testTemporaryConfig - various temporary config changes"""
        ref = { "": { "loglevel": "2", "configName": "dummy-test" } }
        config = copy.deepcopy(ref)
        self.session.SetConfig(False, False, config, utf8_strings=True)
        # reset
        self.session.SetConfig(False, True, {}, utf8_strings=True)
        self.assertEqual(config, self.session.GetConfig(False, utf8_strings=True))
        # add sync prop
        self.session.SetConfig(True, True, { "": { "loglevel": "100" } }, utf8_strings=True)
        config[""]["loglevel"] = "100"
        self.assertEqual(config, self.session.GetConfig(False, utf8_strings=True))
        # add source
        self.session.SetConfig(True, True, { "source/foobar": { "sync": "two-way" } }, utf8_strings=True)
        config["source/foobar"] = { "sync": "two-way" }
        self.session.SetConfig(True, True, { "": { "loglevel": "100" } }, utf8_strings=True)
        # add source prop
        self.session.SetConfig(True, True, { "source/foobar": { "database": "xyz" } }, utf8_strings=True)
        config["source/foobar"]["database"] = "xyz"
        self.assertEqual(config, self.session.GetConfig(False, utf8_strings=True))
        # reset temporary settings
        self.session.SetConfig(False, True, { }, utf8_strings=True)
        config = copy.deepcopy(ref)
        self.assertEqual(config, self.session.GetConfig(False, utf8_strings=True))

    def testCreateGetConfig(self):
        """TestSessionAPIsDummy.testCreateGetConfig -  test the config is created successfully. """
        self.setUpConfigListeners()
        self.config[""]["username"] = "creategetconfig"
        self.config[""]["password"] = "112233445566778"
        self.setupConfig()
        """ get config and compare """
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config, self.config)
        # terminate session and check whether a "config changed" signal
        # was sent as required
        self.session.Detach()
        loop.run()
        self.assertEqual(DBusUtil.events, ["ConfigChanged"])

    def testUpdateConfig(self):
        """TestSessionAPIsDummy.testUpdateConfig -  test the config is permenantly updated correctly. """
        self.setupConfig()
        """ update the given config """
        self.session.SetConfig(True, False, self.updateConfig, utf8_strings=True)
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config[""]["username"], "doe")
        self.assertEqual(config["source/addressbook"]["sync"], "slow")

    def testUpdateConfigTemp(self):
        """TestSessionAPIsDummy.testUpdateConfigTemp -  test the config is just temporary updated but no effect in storage. """
        self.setupConfig()
        """ set config temporary """
        self.session.SetConfig(True, True, self.updateConfig, utf8_strings=True)
        self.session.Detach()
        """ creat a new session to lose the temporary configs """
        self.setUpSession("dummy-test")
        config = self.session.GetConfig(False, utf8_strings=True)
        """ no change of any properties """
        self.assertEqual(config, self.config)

    def testGetConfigUpdateConfigTemp(self):
        """TestSessionAPIsDummy.testGetConfigUpdateConfigTemp -  test the config is temporary updated and in effect for GetConfig in the current session. """
        self.setupConfig()
        """ set config temporary """
        self.session.SetConfig(True, True, self.updateConfig, utf8_strings=True)
        """ GetConfig is affected """
        config = self.session.GetConfig(False, utf8_strings=True)
        """ no change of any properties """
        self.assertEqual(config[""]["username"], "doe")
        self.assertEqual(config["source/addressbook"]["sync"], "slow")

    def testGetConfigWithTempConfig(self):
        """TestSessionAPIsDummy.testGetConfigWithTempConfig -  test the config is gotten for a new temporary config. """
        """ The given config doesn't exist on disk and it's set temporarily. Then GetConfig should
            return the configs temporarily set. """
        self.session.SetConfig(True, True, self.config, utf8_strings=True)
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config, self.config)

    def testUpdateConfigError(self):
        """TestSessionAPIsDummy.testUpdateConfigError -  test the right error is reported when an invalid property value is set """
        self.setupConfig()
        config = { 
                     "source/addressbook" : { "sync" : "invalid-value"}
                  }
        try:
            self.session.SetConfig(True, False, config, utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.InvalidCall: invalid value 'invalid-value' for "
                                 "property 'sync': 'not one of the valid values (two-way, slow, "
                                 "refresh-from-local, refresh-from-remote = refresh, one-way-from-local, "
                                 "one-way-from-remote = one-way, refresh-from-client = refresh-client, "
                                 "refresh-from-server = refresh-server, one-way-from-client = one-way-client, "
                                 "one-way-from-server = one-way-server, disabled = none)'")
        else:
            self.fail("no exception thrown")

    def testUpdateNoConfig(self):
        """TestSessionAPIsDummy.testUpdateNoConfig -  test the right error is reported when updating properties for a non-existing server """
        try:
            self.session.SetConfig(True, False, self.updateConfig, utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.NoSuchConfig: The configuration 'dummy-test' doesn't exist")
        else:
            self.fail("no exception thrown")

    def testUnknownConfigContent(self):
        """TestSessionAPIsDummy.testUnknownConfigContent - config with unkown must be rejected"""
        self.setupConfig()

        try:
            config1 = copy.deepcopy(self.config)
            config1[""]["no-such-sync-property"] = "foo"
            self.session.SetConfig(False, False, config1, utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.InvalidCall: unknown property 'no-such-sync-property'")
        else:
            self.fail("no exception thrown")

        try:
            config1 = copy.deepcopy(self.config)
            config1["source/addressbook"]["no-such-source-property"] = "foo"
            self.session.SetConfig(False, False, config1, utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.InvalidCall: unknown property 'no-such-source-property'")
        else:
            self.fail("no exception thrown")

        try:
            config1 = copy.deepcopy(self.config)
            config1["no-such-key"] = { "foo": "bar" }
            self.session.SetConfig(False, False, config1, utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.InvalidCall: invalid config entry 'no-such-key'")
        else:
            self.fail("no exception thrown")

    def testClearAllConfig(self):
        """TestSessionAPIsDummy.testClearAllConfig -  test all configs of a server are cleared correctly. """
        """ first set up config and then clear all configs and also check a non-existing config """
        self.setupConfig()
        self.clearAllConfig()
        try:
            config = self.session.GetConfig(False, utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                "org.syncevolution.NoSuchConfig: No configuration 'dummy-test' found")
        else:
            self.fail("no exception thrown")

    def testCheckSourceNoConfig(self):
        """TestSessionAPIsDummy.testCheckSourceNoConfig -  test the right error is reported when the server doesn't exist """
        try:
            self.session.CheckSource("", utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.NoSuchSource: 'dummy-test' has no '' source")
        else:
            self.fail("no exception thrown")

    def testCheckSourceNoSourceName(self):
        """TestSessionAPIsDummy.testCheckSourceNoSourceName -  test the right error is reported when the source doesn't exist """
        self.setupConfig()
        try:
            self.session.CheckSource("dummy", utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.NoSuchSource: 'dummy-test' "
                                 "has no 'dummy' source")
        else:
            self.fail("no exception thrown")

    def testCheckSourceInvalidDatabase(self):
        """TestSessionAPIsDummy.testCheckSourceInvalidEvolutionSource -  test the right error is reported when the evolutionsource is invalid """
        self.setupConfig()
        config = { "source/memo" : { "database" : "impossible-source"} }
        self.session.SetConfig(True, False, config, utf8_strings=True)
        try:
            self.session.CheckSource("memo", utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.SourceUnusable: The source 'memo' is not usable")
        else:
            self.fail("no exception thrown")

    def testCheckSourceInvalidBackend(self):
        """TestSessionAPIsDummy.testCheckSourceInvalidBackend -  test the right error is reported when the type is invalid """
        self.setupConfig()
        config = { "source/memo" : { "backend" : "no-such-backend"} }
        try:
            self.session.SetConfig(True, False, config, utf8_strings=True)
        except dbus.DBusException, ex:
            expected = "org.syncevolution.InvalidCall: invalid value 'no-such-backend' for property 'backend': "
            self.assertEqual(str(ex)[0:len(expected)], expected)
        else:
            self.fail("no exception thrown")

    def testCheckSourceNoBackend(self):
        """TestSessionAPIsDummy.testCheckSourceNoBackend -  test the right error is reported when the source is unusable"""
        self.setupConfig()
        config = { "source/memo" : { "backend" : "file",
                                     "databaseFormat" : "text/calendar",
                                     "database" : "file:///no/such/path" } }
        self.session.SetConfig(True, False, config, utf8_strings=True)
        try:
            self.session.CheckSource("memo", utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.SourceUnusable: The source 'memo' is not usable")
        else:
            self.fail("no exception thrown")

    def testCheckSource(self):
        """TestSessionAPIsDummy.testCheckSource - testCheckSource - test all sources are okay"""
        self.setupConfig()
        try:
            for source in self.sources:
                self.session.CheckSource(source, utf8_strings=True)
        except dbus.DBusException, ex:
            self.fail(ex)

    def testCheckSourceUpdateConfigTemp(self):
        """TestSessionAPIsDummy.testCheckSourceUpdateConfigTemp -  test the config is temporary updated and in effect for GetDatabases in the current session. """
        self.setupConfig()
        tempConfig = {"source/temp" : { "backend" : "calendar"}}
        self.session.SetConfig(True, True, tempConfig, utf8_strings=True)
        databases2 = self.session.CheckSource("temp", utf8_strings=True)

    def testGetDatabasesNoConfig(self):
        """TestSessionAPIsDummy.testGetDatabasesNoConfig -  test the right error is reported when the server doesn't exist """
        # make sure the config doesn't exist """
        try:
            self.session.GetDatabases("", utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.NoSuchSource: 'dummy-test' has no '' source")
        else:
            self.fail("no exception thrown")

    def testGetDatabasesEmpty(self):
        """TestSessionAPIsDummy.testGetDatabasesEmpty -  test the right error is reported for non-existing source"""
        self.setupConfig()
        try:
            databases = self.session.GetDatabases("never_use_this_source_name", utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.NoSuchSource: 'dummy-test' has no 'never_use_this_source_name' source")
        else:
            self.fail("no exception thrown")

    def testGetDatabases(self):
        """TestSessionAPIsDummy.testGetDatabases -  test the right way to get databases """
        self.setupConfig()

        # don't know actual databases, so compare results of two different times
        sources = ['addressbook', 'calendar', 'todo', 'memo']
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
        self.assertEqual(databases1, databases2)

    def testGetDatabasesUpdateConfigTemp(self):
        """TestSessionAPIsDummy.testGetDatabasesUpdateConfigTemp -  test the config is temporary updated and in effect for GetDatabases in the current session. """
        self.setupConfig()
        # file backend: reports a short help text instead of a real database list
        databases1 = self.session.GetDatabases("calendar", utf8_strings=True)
        # databaseFormat is required for file backend, otherwise it
        # cannot be instantiated and even simple operations as reading
        # the (in this case fixed) list of databases fail
        tempConfig = {"source/temp" : { "backend" : "file", "databaseFormat" : "text/calendar" }}
        self.session.SetConfig(True, True, tempConfig, utf8_strings=True)
        databases2 = self.session.GetDatabases("temp", utf8_strings=True)
        self.assertEqual(databases2, databases1)

    def testGetReportsNoConfig(self):
        """TestSessionAPIsDummy.testGetReportsNoConfig -  Test nothing is gotten when the given server doesn't exist. Also covers boundaries """
        reports = self.session.GetReports(0, 0, utf8_strings=True)
        self.assertEqual(reports, [])
        reports = self.session.GetReports(0, 1, utf8_strings=True)
        self.assertEqual(reports, [])
        reports = self.session.GetReports(0, 0xFFFFFFFF, utf8_strings=True)
        self.assertEqual(reports, [])
        reports = self.session.GetReports(0xFFFFFFFF, 0xFFFFFFFF, utf8_strings=True)
        self.assertEqual(reports, [])

    def testGetReportsNoReports(self):
        """TestSessionAPIsDummy.testGetReportsNoReports -  Test when the given server has no reports. Also covers boundaries """
        self.setupConfig()
        reports = self.session.GetReports(0, 0, utf8_strings=True)
        self.assertEqual(reports, [])
        reports = self.session.GetReports(0, 1, utf8_strings=True)
        self.assertEqual(reports, [])
        reports = self.session.GetReports(0, 0xFFFFFFFF, utf8_strings=True)
        self.assertEqual(reports, [])
        reports = self.session.GetReports(0xFFFFFFFF, 0xFFFFFFFF, utf8_strings=True)
        self.assertEqual(reports, [])

    def testGetReportsByRef(self):
        """TestSessionAPIsDummy.testGetReportsByRef -  Test the reports are gotten correctly from reference files. Also covers boundaries """
        """ This could be extractly compared since the reference files are known """
        self.setUpFiles('reports')
        report0 = { "peer" : "dummy-test",
                    "start" : "1258520955",
                    "end" : "1258520964",
                    "status" : "200",
                    "source-addressbook-mode" : "slow",
                    "source-addressbook-first" : "true",
                    "source-addressbook-resume" : "false",
                    "source-addressbook-status" : "0",
                    "source-addressbook-backup-before" : "0",
                    "source-addressbook-backup-after" : "0",
                    "source-addressbook-stat-local-any-sent" : "9168",
                    "source-addressbook-stat-remote-added-total" : "71",
                    "source-addressbook-stat-remote-updated-total" : "100",
                    "source-addressbook-stat-local-updated-total" : "632",
                    "source-addressbook-stat-remote-any-reject" : "100",
                    "source-addressbook-stat-remote-any-conflict_duplicated" : "5293487",
                    "source-addressbook-stat-remote-any-conflict_client_won" : "33",
                    "source-addressbook-stat-local-any-received" : "2",
                    "source-addressbook-stat-local-removed-total" : "4",
                    "source-addressbook-stat-remote-any-conflict_server_won" : "38",
                    "source-addressbook-stat-local-any-reject" : "77",
                    "source-addressbook-stat-local-added-total" : "84",
                    "source-addressbook-stat-remote-removed-total" : "66",
                    "source-calendar-mode" : "slow",
                    "source-calendar-first" : "true",
                    "source-calendar-resume" : "false",
                    "source-calendar-status" : "0",
                    "source-calendar-backup-before" : "17",
                    "source-calendar-backup-after" : "17",
                    "source-calendar-stat-local-any-sent" : "8619",
                    "source-calendar-stat-remote-added-total": "17",
                    "source-calendar-stat-remote-updated-total" : "10",
                    "source-calendar-stat-local-updated-total" : "6",
                    "source-calendar-stat-remote-any-reject" : "1",
                    "source-calendar-stat-remote-any-conflict_duplicated" : "5",
                    "source-calendar-stat-remote-any-conflict_client_won" : "3",
                    "source-calendar-stat-local-any-received" : "24",
                    "source-calendar-stat-local-removed-total" : "54",
                    "source-calendar-stat-remote-any-conflict_server_won" : "38",
                    "source-calendar-stat-local-any-reject" : "7",
                    "source-calendar-stat-local-added-total" : "42",
                    "source-calendar-stat-remote-removed-total" : "6",
                    "source-memo-mode" : "slow",
                    "source-memo-first" : "true",
                    "source-memo-resume" : "false",
                    "source-memo-status" : "0",
                    "source-memo-backup-before" : "3",
                    "source-memo-backup-after" : "4",
                    "source-memo-stat-local-any-sent" : "8123",
                    "source-memo-stat-remote-added-total" : "15",
                    "source-memo-stat-remote-updated-total" : "6",
                    "source-memo-stat-local-updated-total" : "8",
                    "source-memo-stat-remote-any-reject" : "16",
                    "source-memo-stat-remote-any-conflict_duplicated" : "27",
                    "source-memo-stat-remote-any-conflict_client_won" : "2",
                    "source-memo-stat-local-any-received" : "3",
                    "source-memo-stat-local-removed-total" : "4",
                    "source-memo-stat-remote-any-conflict_server_won" : "8",
                    "source-memo-stat-local-any-reject" : "40",
                    "source-memo-stat-local-added-total" : "34",
                    "source-memo-stat-remote-removed-total" : "5",
                    "source-todo-mode" : "slow",
                    "source-todo-first" : "true",
                    "source-todo-resume" : "false",
                    "source-todo-status" : "0",
                    "source-todo-backup-before" : "2",
                    "source-todo-backup-after" : "2",
                    "source-todo-stat-local-any-sent" : "619",
                    "source-todo-stat-remote-added-total" : "71",
                    "source-todo-stat-remote-updated-total" : "1",
                    "source-todo-stat-local-updated-total" : "9",
                    "source-todo-stat-remote-any-reject" : "10",
                    "source-todo-stat-remote-any-conflict_duplicated" : "15",
                    "source-todo-stat-remote-any-conflict_client_won" : "7",
                    "source-todo-stat-local-any-received" : "2",
                    "source-todo-stat-local-removed-total" : "4",
                    "source-todo-stat-remote-any-conflict_server_won" : "8",
                    "source-todo-stat-local-any-reject" : "3",
                    "source-todo-stat-local-added-total" : "24",
                    "source-todo-stat-remote-removed-total" : "80" }
        reports = self.session.GetReports(0, 0, utf8_strings=True)
        self.assertEqual(reports, [])
        # get only one report
        reports = self.session.GetReports(0, 1, utf8_strings=True)
        self.assertTrue(len(reports) == 1)
        del reports[0]["dir"]

        self.assertEqual(reports[0], report0)
        """ the number of reference sessions is totally 5. Check the returned count
        when parameter is bigger than 5 """
        reports = self.session.GetReports(0, 0xFFFFFFFF, utf8_strings=True)
        self.assertTrue(len(reports) == 5)
        # start from 2, this could check integer overflow
        reports2 = self.session.GetReports(2, 0xFFFFFFFF, utf8_strings=True)
        self.assertTrue(len(reports2) == 3)
        # the first element of reports2 should be the same as the third element of reports
        self.assertEqual(reports[2], reports2[0])
        # indexed from 5, nothing could be gotten
        reports = self.session.GetReports(5, 0xFFFFFFFF, utf8_strings=True)
        self.assertEqual(reports, [])

    def testRestoreByRef(self):
        """TestSessionAPIsDummy.testRestoreByRef - restore data before or after a given session"""
        self.setUpFiles('restore')
        self.setupConfig()
        self.setUpListeners(self.sessionpath)
        reports = self.session.GetReports(0, 1, utf8_strings=True)
        dir = reports[0]["dir"]
        sessionpath, session = self.createSession("dummy-test", False)
        #TODO: check restore result, how?
        #restore data before this session
        self.session.Restore(dir, True, [], utf8_strings=True)
        loop.run()
        self.session.Detach()

        # check recorded events in DBusUtil.events, first filter them
        statuses = []
        progresses = []
        for item in DBusUtil.events:
            if item[0] == "status":
                statuses.append(item[1])
            elif item[0] == "progress":
                progresses.append(item[1])

        lastStatus = ""
        lastSources = {}
        statusPairs = {"": 0, "idle": 1, "running" : 2, "done" : 3}
        for status, error, sources in statuses:
            self.assertFalse(status == lastStatus and lastSources == sources)
            # no error
            self.assertEqual(error, 0)
            for sourcename, value in sources.items():
                # no error
                self.assertEqual(value[2], 0)
                # keep order: source status must also be unchanged or the next status
                if lastSources.has_key(sourcename):
                    lastValue = lastSources[sourcename]
                    self.assertTrue(statusPairs[value[1]] >= statusPairs[lastValue[1]])

            lastStatus = status
            lastSources = sources

        # check increasing progress percentage
        lastPercent = 0
        for percent, sources in progresses:
            self.assertFalse(percent < lastPercent)
            lastPercent = percent

        session.SetConfig(False, False, self.config, utf8_strings=True)
        #restore data after this session
        session.Restore(dir, False, ["addressbook", "calendar"], utf8_strings=True)
        loop.run()

    def testSecondRestore(self):
        """TestSessionAPIsDummy.testSecondRestore - right error thrown when session is not active?"""
        self.setUpFiles('restore')
        self.setupConfig()
        self.setUpListeners(self.sessionpath)
        reports = self.session.GetReports(0, 1, utf8_strings=True)
        dir = reports[0]["dir"]
        sessionpath, session = self.createSession("dummy-test", False)
        try:
            session.Restore(dir, False, [], utf8_strings=True)
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                    "org.syncevolution.InvalidCall: session is not active, call not allowed at this time")
        else:
            self.fail("no exception thrown")

        self.session.Detach()
        session.SetConfig(False, False, self.config, utf8_strings=True)
        session.Restore(dir, False, [], utf8_strings=True)
        loop.run()

    @timeout(300)
    def testInteractivePassword(self):
        """TestSessionAPIsDummy.testInteractivePassword -  test the info request is correctly working for password """
        self.setupConfig()
        self.setUpListeners(self.sessionpath)
        self.lastState = "unknown"
        # define callback for InfoRequest signals and send corresponds response
        # to dbus server
        def infoRequest(id, session, state, handler, type, params):
            if state == "request":
                self.assertEqual(self.lastState, "unknown")
                self.lastState = "request"
                self.server.InfoResponse(id, "working", {}, utf8_strings=True)
            elif state == "waiting":
                self.assertEqual(self.lastState, "request")
                self.lastState = "waiting"
                self.server.InfoResponse(id, "response", {"password" : "123456"}, utf8_strings=True)
            elif state == "done":
                self.assertEqual(self.lastState, "waiting")
                self.lastState = "done"
            else:
                self.fail("state should not be '" + state + "'")

        signal = bus.add_signal_receiver(infoRequest,
                                         'InfoRequest',
                                         'org.syncevolution.Server',
                                         self.server.bus_name,
                                         None,
                                         byte_arrays=True,
                                         utf8_strings=True)

        # dbus server will be blocked by gnome-keyring-ask dialog, so we kill it, and then 
        # it can't get the password from gnome keyring and send info request for password
        def callback():
            kill = subprocess.Popen("sh -c 'killall -9 gnome-keyring-ask >/dev/null 2>&1'", shell=True)
            kill.communicate()
            return True

        timeout_handler = Timeout.addTimeout(1, callback)

        # try to sync and invoke password request
        self.session.Sync("", {})
        loop.run()
        Timeout.removeTimeout(timeout_handler)
        self.assertEqual(self.lastState, "done")

    @timeout(60)
    def testAutoSyncNetworkFailure(self):
        """TestSessionAPIsDummy.testAutoSyncNetworkFailure - test that auto-sync is triggered, fails due to (temporary?!) network error here"""
        self.setupConfig()
        # enable auto-sync
        config = copy.deepcopy(self.config)
        # Note that writing this config will modify the host's keyring!
        # Use a syncURL that is unlikely to conflict with the host
        # or any other D-Bus test.
        config[""]["syncURL"] = "http://no-such-domain.foobar"
        config[""]["autoSync"] = "1"
        config[""]["autoSyncDelay"] = "0"
        config[""]["autoSyncInterval"] = "10s"
        config[""]["password"] = "foobar"
        self.session.SetConfig(True, False, config, utf8_strings=True)

        def session_ready(object, ready):
            if self.running and object != self.sessionpath and \
                (self.auto_sync_session_path == None and ready or \
                 self.auto_sync_session_path == object):
                self.auto_sync_session_path = object
                DBusUtil.quit_events.append("session " + object + (ready and " ready" or " done"))
                loop.quit()

        signal = bus.add_signal_receiver(session_ready,
                                         'SessionChanged',
                                         'org.syncevolution.Server',
                                         self.server.bus_name,
                                         None,
                                         byte_arrays=True,
                                         utf8_strings=True)

        # shut down current session, will allow auto-sync
        self.session.Detach()

        # wait for start and end of auto-sync session
        loop.run()
        loop.run()
        end = time.time()
        self.assertEqual(DBusUtil.quit_events, ["session " + self.auto_sync_session_path + " ready",
                                                "session " + self.auto_sync_session_path + " done"])
        DBusUtil.quit_events = []
        # session must be around for a while after terminating, to allow
        # reading information about it by clients who didn't start it
        # and thus wouldn't know what the session was about otherwise
        session = dbus.Interface(bus.get_object(self.server.bus_name,
                                                self.auto_sync_session_path),
                                 'org.syncevolution.Session')
        reports = session.GetReports(0, 100, utf8_strings=True)
        self.assertEqual(len(reports), 1)
        self.assertEqual(reports[0]["status"], "20043")
        name = session.GetConfigName()
        self.assertEqual(name, "dummy-test")
        flags = session.GetFlags()
        self.assertEqual(flags, [])
        first_auto = self.auto_sync_session_path
        self.auto_sync_session_path = None

        # check that interval between auto-sync sessions is right
        loop.run()
        start = time.time()
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["session " + self.auto_sync_session_path + " ready",
                                                "session " + self.auto_sync_session_path + " done"])
        self.assertNotEqual(first_auto, self.auto_sync_session_path)
        delta = start - end
        # avoid timing checks when running under valgrind
        if not usingValgrind():
            self.assertTrue(delta < 13)
            self.assertTrue(delta > 7)

        # check that org.freedesktop.Notifications.Notify was not called
        # (network errors are considered temporary, can't tell in this case
        # that the name lookup error is permanent)
        def checkDBusLog(self, content):
            notifications = GrepNotifications(content)
            self.assertEqual(notifications, [])

        # done as part of post-processing in runTest()
        self.runTestDBusCheck = checkDBusLog

    @timeout(60)
    def testAutoSyncLocalConfigError(self):
        """TestSessionAPIsDummy.testAutoSyncLocalConfigError - test that auto-sync is triggered for local sync, fails due to permanent config error here"""
        self.setupConfig()
        # enable auto-sync
        config = copy.deepcopy(self.config)
        config[""]["syncURL"] = "local://@foobar" # will fail
        config[""]["autoSync"] = "1"
        config[""]["autoSyncDelay"] = "0"
        config[""]["autoSyncInterval"] = "10s"
        config[""]["password"] = "foobar"
        self.session.SetConfig(True, False, config, utf8_strings=True)

        def session_ready(object, ready):
            if self.running and object != self.sessionpath and \
                (self.auto_sync_session_path == None and ready or \
                 self.auto_sync_session_path == object):
                self.auto_sync_session_path = object
                DBusUtil.quit_events.append("session " + object + (ready and " ready" or " done"))
                loop.quit()

        signal = bus.add_signal_receiver(session_ready,
                                         'SessionChanged',
                                         'org.syncevolution.Server',
                                         self.server.bus_name,
                                         None,
                                         byte_arrays=True,
                                         utf8_strings=True)

        # shut down current session, will allow auto-sync
        self.session.Detach()

        # wait for start and end of auto-sync session
        loop.run()
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["session " + self.auto_sync_session_path + " ready",
                                                "session " + self.auto_sync_session_path + " done"])
        session = dbus.Interface(bus.get_object(self.server.bus_name,
                                                self.auto_sync_session_path),
                                 'org.syncevolution.Session')
        reports = session.GetReports(0, 100, utf8_strings=True)
        self.assertEqual(len(reports), 1)
        self.assertEqual(reports[0]["status"], "10500")
        name = session.GetConfigName()
        self.assertEqual(name, "dummy-test")
        flags = session.GetFlags()
        self.assertEqual(flags, [])

        # check that org.freedesktop.Notifications.Notify was called
        # once to report the failed attempt to start the sync
        def checkDBusLog(self, content):
            notifications = GrepNotifications(content)
            self.assertEqual(notifications,
                             ['   string "SyncEvolution"\n'
                              '   uint32 0\n'
                              '   string ""\n'
                              '   string "Sync problem."\n'
                              '   string "Sorry, there\'s a problem with your sync that you need to attend to."\n'
                              '   array [\n'
                              '      string "view"\n'
                              '      string "View"\n'
                              '      string "default"\n'
                              '      string "Dismiss"\n'
                              '   ]\n'
                              '   array [\n'
                              '   ]\n'
                              '   int32 -1\n'])

        # done as part of post-processing in runTest()
        self.runTestDBusCheck = checkDBusLog

    @timeout(120)
    def testAutoSyncLocalSuccess(self):
        """TestSessionAPIsDummy.testAutoSyncLocalSuccess - test that auto-sync is done successfully for local sync between file backends"""
        # create @foobar config
        self.session.Detach()
        self.setUpSession("target-config@foobar")
        config = copy.deepcopy(self.config)
        config[""]["remoteDeviceId"] = "foo"
        config[""]["deviceId"] = "bar"
        for i in ("addressbook", "calendar", "todo", "memo"):
            source = config["source/" + i]
            source["database"] = source["database"] + ".server"
        self.session.SetConfig(False, False, config, utf8_strings=True)
        self.session.Detach()

        # create dummy-test@default auto-sync config
        self.setUpSession("dummy-test")
        config = copy.deepcopy(self.config)
        config[""]["syncURL"] = "local://@foobar"
        config[""]["PeerIsClient"] = "1"
        config[""]["autoSync"] = "1"
        config[""]["autoSyncDelay"] = "0"
        # must be small enough (otherwise test runs a long time)
        # but not too small (otherwise the next sync already starts
        # before we can check the result and kill the daemon)
        config[""]["autoSyncInterval"] = usingValgrind() and "60s" or "10s"
        config["source/addressbook"]["uri"] = "addressbook"
        self.session.SetConfig(False, False, config, utf8_strings=True)

        def session_ready(object, ready):
            if self.running and object != self.sessionpath and \
                (self.auto_sync_session_path == None and ready or \
                 self.auto_sync_session_path == object):
                self.auto_sync_session_path = object
                DBusUtil.quit_events.append("session " + object + (ready and " ready" or " done"))
                loop.quit()

        signal = bus.add_signal_receiver(session_ready,
                                         'SessionChanged',
                                         'org.syncevolution.Server',
                                         self.server.bus_name,
                                         None,
                                         byte_arrays=True,
                                         utf8_strings=True)

        # shut down current session, will allow auto-sync
        self.session.Detach()

        # wait for start and end of auto-sync session
        loop.run()
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["session " + self.auto_sync_session_path + " ready",
                                                "session " + self.auto_sync_session_path + " done"])
        session = dbus.Interface(bus.get_object(self.server.bus_name,
                                                self.auto_sync_session_path),
                                 'org.syncevolution.Session')
        reports = session.GetReports(0, 100, utf8_strings=True)
        self.assertEqual(len(reports), 1)
        self.assertEqual(reports[0]["status"], "200")
        name = session.GetConfigName()
        self.assertEqual(name, "dummy-test")
        flags = session.GetFlags()
        self.assertEqual(flags, [])

        # check that org.freedesktop.Notifications.Notify was called
        # when starting and completing the sync
        def checkDBusLog(self, content):
            notifications = GrepNotifications(content)
            self.assertEqual(notifications,
                             ['   string "SyncEvolution"\n'
                              '   uint32 0\n'
                              '   string ""\n'
                              '   string "dummy-test is syncing"\n'
                              '   string "We have just started to sync your computer with the dummy-test sync service."\n'
                              '   array [\n'
                              '      string "view"\n'
                              '      string "View"\n'
                              '      string "default"\n'
                              '      string "Dismiss"\n'
                              '   ]\n'
                              '   array [\n'
                              '   ]\n'
                              '   int32 -1\n',

                              '   string "SyncEvolution"\n'
                              '   uint32 0\n'
                              '   string ""\n'
                              '   string "dummy-test sync complete"\n'
                              '   string "We have just finished syncing your computer with the dummy-test sync service."\n'
                              '   array [\n'
                              '      string "view"\n'
                              '      string "View"\n'
                              '      string "default"\n'
                              '      string "Dismiss"\n'
                              '   ]\n'
                              '   array [\n'
                              '   ]\n'
                              '   int32 -1\n'])

        # done as part of post-processing in runTest()
        self.runTestDBusCheck = checkDBusLog


class TestSessionAPIsReal(unittest.TestCase, DBusUtil):
    """ This class is used to test those unit tests of session APIs, depending on doing sync.
        Thus we need a real server configuration to confirm sync could be run successfully.
        Typically we need make sure that at least one sync has been done before testing our
        desired unit tests. Note that it also covers session.Sync API itself """
    """ All unit tests in this class have a dependency on a real sync
    config named 'dbus_unittest'. That config must have preventSlowSync=0,
    maxLogDirs=1, username, password set such that syncing succeeds
    for at least one source. It does not matter which data is synchronized.
    For example, the following config will work:
    syncevolution --configure --template <server of your choice> \
                  username=<your username> \
                  password=<your password> \
                  preventSlowSync=0 \
                  maxLogDirs=1 \
                  backend=file \
                  database=file:///tmp/test_dbus_data \
                  databaseFormat=text/vcard \
                  dbus_unittest@test-dbus addressbook
                  """

    def setUp(self):
        self.setUpServer()
        self.setUpSession(configName)
        self.operation = "" 

    def run(self, result):
        self.runTest(result, own_xdg=False)

    def setupConfig(self):
        """ Apply for user settings. Used internally. """
        configProps = { }
        # check whether 'dbus_unittest' is configured.
        try:
            configProps = self.session.GetConfig(False, utf8_strings=True)
        except dbus.DBusException, ex:
            self.fail(str(ex) + 
                      ". To test this case, please first set up a correct config named 'dbus_unittest'.")

    def doSync(self):
        self.setupConfig()
        self.setUpListeners(self.sessionpath)
        self.session.Sync("slow", {})
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["session " + self.sessionpath + " done"])

    def progressChanged(self, *args):
        # subclass specifies its own callback for ProgressChanged signals
        percentage = args[0]
        # make sure sync is really running
        if percentage > 20:
            if self.operation == "abort":
                self.session.Abort()
            if self.operation == "suspend":
                self.session.Suspend()

    @timeout(300)
    def testSync(self):
        """TestSessionAPIsReal.testSync - run a real sync with default server and test status list and progress number"""
        """ check events list is correct for StatusChanged and ProgressChanged """
        # do sync
        self.doSync()
        self.checkSync()
    
    @timeout(300)
    def testSyncStatusAbort(self):
        """TestSessionAPIsReal.testSyncStatusAbort -  test status is set correctly when the session is aborted """
        self.operation = "abort"
        self.doSync()
        hasAbortingStatus = False
        for item in DBusUtil.events:
            if item[0] == "status" and item[1][0] == "aborting":
                hasAbortingStatus = True
                break
        self.assertEqual(hasAbortingStatus, True)

    @timeout(300)
    def testSyncStatusSuspend(self):
        """TestSessionAPIsReal.testSyncStatusSuspend -  test status is set correctly when the session is suspended """
        self.operation = "suspend"
        self.doSync()
        hasSuspendingStatus = False
        for item in DBusUtil.events:
            if item[0] == "status" and "suspending" in item[1][0] :
                hasSuspendingStatus = True
                break
        self.assertEqual(hasSuspendingStatus, True)

    @timeout(300)
    def testSyncSecondSession(self):
        """TestSessionAPIsReal.testSyncSecondSession - ask for a second session that becomes ready after a real sync"""
        sessionpath2, session2 = self.createSession("", False)
        status, error, sources = session2.GetStatus(utf8_strings=True)
        self.assertEqual(status, "queueing")
        self.testSync()
        # now wait for second session becoming ready
        loop.run()
        status, error, sources = session2.GetStatus(utf8_strings=True)
        self.assertEqual(status, "idle")
        self.assertEqual(DBusUtil.quit_events, ["session " + self.sessionpath + " done",
                                                    "session " + sessionpath2 + " ready"])
        session2.Detach()

class TestDBusSyncError(unittest.TestCase, DBusUtil):
    def setUp(self):
        self.setUpServer()
        self.setUpSession(configName)

    def run(self, result):
        self.runTest(result, own_xdg=True)

    def testSyncNoConfig(self):
        """testDBusSyncError.testSyncNoConfig - Executes a real sync with no corresponding config."""
        self.setUpListeners(self.sessionpath)
        self.session.Sync("", {})
        loop.run()
        # TODO: check recorded events in DBusUtil.events
        status, error, sources = self.session.GetStatus(utf8_strings=True)
        self.assertEqual(status, "done")
        self.assertEqual(error, 10500)

class TestConnection(unittest.TestCase, DBusUtil):
    """Tests Server.Connect(). Tests depend on getting one Abort signal to terminate."""

    """a real message sent to our own server, DevInf stripped, username/password foo/bar"""
    message1 = '''<?xml version="1.0" encoding="UTF-8"?><SyncML xmlns='SYNCML:SYNCML1.2'><SyncHdr><VerDTD>1.2</VerDTD><VerProto>SyncML/1.2</VerProto><SessionID>255</SessionID><MsgID>1</MsgID><Target><LocURI>http://127.0.0.1:9000/syncevolution</LocURI></Target><Source><LocURI>sc-api-nat</LocURI><LocName>test</LocName></Source><Cred><Meta><Format xmlns='syncml:metinf'>b64</Format><Type xmlns='syncml:metinf'>syncml:auth-md5</Type></Meta><Data>kHzMn3RWFGWSKeBpXicppQ==</Data></Cred><Meta><MaxMsgSize xmlns='syncml:metinf'>20000</MaxMsgSize><MaxObjSize xmlns='syncml:metinf'>4000000</MaxObjSize></Meta></SyncHdr><SyncBody><Alert><CmdID>1</CmdID><Data>200</Data><Item><Target><LocURI>addressbook</LocURI></Target><Source><LocURI>./addressbook</LocURI></Source><Meta><Anchor xmlns='syncml:metinf'><Last>20091105T092757Z</Last><Next>20091105T092831Z</Next></Anchor><MaxObjSize xmlns='syncml:metinf'>4000000</MaxObjSize></Meta></Item></Alert><Final/></SyncBody></SyncML>'''

    def setUp(self):
        self.setUpServer()
        self.setUpListeners(None)
        # default config
        self.config = { 
                         "" : { "remoteDeviceId" : "sc-api-nat",
                                "password" : "test",
                                "username" : "test",
                                "PeerIsClient" : "1",
                                "RetryInterval" : "1",
                                "RetryDuration" : "10"
                              },
                         "source/addressbook" : { "sync" : "slow",
                                                  "backend" : "file",
                                                  "database" : "file://" + xdg_root + "/addressbook",
                                                  "databaseFormat" : "text/vcard",
                                                  "uri" : "card"
                                                },
                         "source/calendar"    : { "sync" : "disabled",
                                                  "backend" : "file",
                                                  "database" : "file://" + xdg_root + "/calendar",
                                                  "databaseFormat" : "text/calendar",
                                                  "uri" : "cal"
                                                },
                         "source/todo"        : { "sync" : "disabled",
                                                  "backend" : "file",
                                                  "database" : "file://" + xdg_root + "/todo",
                                                  "databaseFormat" : "text/calendar",
                                                  "uri" : "task"
                                                },
                         "source/memo"        : { "sync" : "disabled",
                                                  "backend" : "file",
                                                  "database" : "file://" + xdg_root + "/memo",
                                                  "databaseFormat" : "text/calendar",
                                                  "uri" : "text"
                                                }
                       }

    def setupConfig(self, name="dummy-test", deviceId="sc-api-nat"):
        self.setUpSession(name)
        self.config[""]["remoteDeviceId"] = deviceId
        self.session.SetConfig(False, False, self.config, utf8_strings=True)
        self.session.Detach()

    def run(self, result):
        self.runTest(result, own_xdg=True)

    def getConnection(self, must_authenticate=False):
        conpath = self.server.Connect({'description': 'test-dbus.py',
                                       'transport': 'dummy'},
                                      must_authenticate,
                                      "")
        self.setUpConnectionListeners(conpath)
        connection = dbus.Interface(bus.get_object(self.server.bus_name,
                                                   conpath),
                                    'org.syncevolution.Connection')
        return (conpath, connection)

    def testConnect(self):
        """TestConnection.testConnect - get connection and close it"""
        conpath, connection = self.getConnection()
        connection.Close(False, 'good bye')
        loop.run()
        self.assertEqual(DBusUtil.events, [('abort',)])

    def testInvalidConnect(self):
        """TestConnection.testInvalidConnect - get connection, send invalid initial message"""
        self.setupConfig()
        conpath, connection = self.getConnection()
        try:
            connection.Process('1234', 'invalid message type')
        except dbus.DBusException, ex:
            self.assertEqual(str(ex),
                                 "org.syncevolution.Exception: message type 'invalid message type' not supported for starting a sync")
        else:
            self.fail("no exception thrown")
        loop.run()
        # 'idle' status doesn't be checked
        self.assertTrue(('abort',) in DBusUtil.events)

    def testStartSync(self):
        """TestConnection.testStartSync - send a valid initial SyncML message"""
        self.setupConfig()
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        DBusUtil.quit_events = []
        # TODO: check events
        self.assertNotEqual(DBusUtil.reply, None)
        self.assertEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        # credentials should have been accepted because must_authenticate=False
        # in Connect(); 508 = "refresh required" is normal
        self.assertTrue('<Status><CmdID>2</CmdID><MsgRef>1</MsgRef><CmdRef>1</CmdRef><Cmd>Alert</Cmd><TargetRef>addressbook</TargetRef><SourceRef>./addressbook</SourceRef><Data>508</Data>' in DBusUtil.reply[0])
        self.assertFalse('<Chal>' in DBusUtil.reply[0])
        self.assertEqual(DBusUtil.reply[3], False)
        self.assertNotEqual(DBusUtil.reply[4], '')
        connection.Close(False, 'good bye')
        loop.run()
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " aborted",
                                                    "session done"])
        # start another session for the server (ensures that the previous one is done),
        # then check the server side report
        DBusUtil.quit_events = []
        self.setUpSession("dummy-test")
        sessions = self.session.GetReports(0, 100)
        self.assertEqual(len(sessions), 1)
        # transport failure, only addressbook active and later aborted
        self.assertEqual(sessions[0]["status"], "20043")
        self.assertEqual(sessions[0]["error"], "D-Bus peer has disconnected")
        self.assertEqual(sessions[0]["source-addressbook-status"], "20017")
        # The other three sources are disabled and should not be listed in the
        # report. Used to be listed with status 0 in the past, which would also
        # be acceptable, but here we use the strict check for "not present" to
        # ensure that the current behavior is preserved.
        self.assertFalse("source-calendar-status" in sessions[0])
        self.assertFalse("source-todo-status" in sessions[0])
        self.assertFalse("source-memo-status" in sessions[0])

    def testCredentialsWrong(self):
        """TestConnection.testCredentialsWrong - send invalid credentials"""
        self.setupConfig()
        conpath, connection = self.getConnection(must_authenticate=True)
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        DBusUtil.quit_events = []
        # TODO: check events
        self.assertNotEqual(DBusUtil.reply, None)
        self.assertEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        # credentials should have been rejected because of wrong Nonce
        self.assertTrue('<Chal>' in DBusUtil.reply[0])
        self.assertEqual(DBusUtil.reply[3], False)
        self.assertNotEqual(DBusUtil.reply[4], '')
        connection.Close(False, 'good bye')
        # when the login fails, the server also ends the session
        loop.run()
        loop.run()
        loop.run()
        DBusUtil.quit_events.sort()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " aborted",
                                                    "connection " + conpath + " got final reply",
                                                    "session done"])

    def testCredentialsRight(self):
        """TestConnection.testCredentialsRight - send correct credentials"""
        self.setupConfig()
        conpath, connection = self.getConnection(must_authenticate=True)
        plain_auth = TestConnection.message1.replace("<Type xmlns='syncml:metinf'>syncml:auth-md5</Type></Meta><Data>kHzMn3RWFGWSKeBpXicppQ==</Data>",
                                                     "<Type xmlns='syncml:metinf'>syncml:auth-basic</Type></Meta><Data>dGVzdDp0ZXN0</Data>")
        connection.Process(plain_auth, 'application/vnd.syncml+xml')
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        DBusUtil.quit_events = []
        self.assertNotEqual(DBusUtil.reply, None)
        self.assertEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        # credentials should have been accepted because with basic auth,
        # credentials can be replayed; 508 = "refresh required" is normal
        self.assertTrue('<Status><CmdID>2</CmdID><MsgRef>1</MsgRef><CmdRef>1</CmdRef><Cmd>Alert</Cmd><TargetRef>addressbook</TargetRef><SourceRef>./addressbook</SourceRef><Data>508</Data>' in DBusUtil.reply[0])
        self.assertEqual(DBusUtil.reply[3], False)
        self.assertNotEqual(DBusUtil.reply[4], '')
        connection.Close(False, 'good bye')
        loop.run()
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " aborted",
                                                    "session done"])

    def testStartSyncTwice(self):
        """TestConnection.testStartSyncTwice - send the same SyncML message twice, starting two sessions"""
        self.setupConfig()
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        # TODO: check events
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        self.assertNotEqual(DBusUtil.reply, None)
        self.assertEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        self.assertEqual(DBusUtil.reply[3], False)
        self.assertNotEqual(DBusUtil.reply[4], '')
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
        self.assertEqual(DBusUtil.quit_events, expected)
        self.assertNotEqual(DBusUtil.reply, None)
        self.assertEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        self.assertEqual(DBusUtil.reply[3], False)
        self.assertNotEqual(DBusUtil.reply[4], '')
        DBusUtil.quit_events = []

        # now quit for good
        connection2.Close(False, 'good bye')
        loop.run()
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath2 + " aborted",
                                                    "session done"])

    def testKillInactive(self):
        """TestConnection.testKillInactive - block server with client A, then let client B connect twice"""
        #set up 2 configs
        self.setupConfig()
        self.setupConfig("dummy", "sc-pim-ppc")
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        # TODO: check events
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        self.assertNotEqual(DBusUtil.reply, None)
        self.assertEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        self.assertEqual(DBusUtil.reply[3], False)
        self.assertNotEqual(DBusUtil.reply[4], '')
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
        self.assertEqual(DBusUtil.quit_events, [ "connection " + conpath2 + " aborted" ])
        DBusUtil.quit_events = []

        # now quit for good
        connection3.Close(False, 'good bye client B')
        loop.run()
        self.assertEqual(DBusUtil.quit_events, [ "connection " + conpath3 + " aborted" ])
        DBusUtil.quit_events = []
        connection.Close(False, 'good bye client A')
        loop.run()
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " aborted",
                                                    "session done"])

    def testTimeoutSync(self):
        """TestConnection.testTimeoutSync - start a sync, then wait for server to detect that we stopped replying"""

        # The server-side configuration for sc-api-nat must contain a retryDuration=10
        # because this test itself will time out with a failure after 20 seconds.
        self.setupConfig()
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " got reply"])
        DBusUtil.quit_events = []
        # TODO: check events
        self.assertNotEqual(DBusUtil.reply, None)
        self.assertEqual(DBusUtil.reply[1], 'application/vnd.syncml+xml')
        # wait for connection reset and "session done" due to timeout
        loop.run()
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["connection " + conpath + " aborted",
                                                    "session done"])

class TestMultipleConfigs(unittest.TestCase, DBusUtil):
    """ sharing of properties between configs

    Creates and tests the configs 'foo', 'bar', 'foo@other_context',
    '@default' and checks that 'defaultPeer' (global), 'syncURL' (per
    peer), 'database' (per source), 'uri' (per source and peer)
    are shared correctly.

    Runs with a the server ready, without session."""

    def setUp(self):
        self.setUpServer()

    def run(self, result):
        self.runTest(result)

    def setupEmpty(self):
        """Creates empty configs 'foo', 'bar', 'foo@other_context'.
        Updating non-existant configs is an error. Use this
        function before trying to update one of these configs."""
        self.setUpSession("foo")
        self.session.SetConfig(False, False, {"" : {}})
        self.session.Detach()
        self.setUpSession("bar")
        self.session.SetConfig(False, False, {"": {}})
        self.session.Detach()
        self.setUpSession("foo@other_CONTEXT")
        self.session.SetConfig(False, False, {"": {}})
        self.session.Detach()

    def setupConfigs(self):
        """Creates polulated configs 'foo', 'bar', 'foo@other_context'."""
        self.setupEmpty()

        # update normal view on "foo"
        self.setUpSession("foo")
        self.session.SetConfig(True, False,
                               { "" : { "defaultPeer" : "foobar_peer",
                                        "deviceId" : "shared-device-identifier",
                                        "syncURL": "http://scheduleworld" },
                                 "source/calendar" : { "uri" : "cal3" },
                                 "source/addressbook" : { "database": "Personal",
                                                          "sync" : "two-way",
                                                          "uri": "card3" } },
                               utf8_strings=True)
        self.session.Detach()

        # "bar" shares properties with "foo"
        self.setUpSession("bar")
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertEqual(config[""]["deviceId"], "shared-device-identifier")
        self.assertEqual(config["source/addressbook"]["database"], "Personal")
        self.session.SetConfig(True, False,
                               { "" : { "syncURL": "http://funambol" },
                                 "source/calendar" : { "uri" : "cal" },
                                 "source/addressbook" : { "database": "Work",
                                                          "sync" : "refresh-from-client",
                                                          "uri": "card" } },
                               utf8_strings=True)
        self.session.Detach()

    def testSharing(self):
        """TestMultipleConfigs.testSharing - set up configs and tests reading them"""
        self.setupConfigs()

        # check how view "foo" has been modified
        self.setUpSession("Foo@deFAULT")
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertEqual(config[""]["syncURL"], "http://scheduleworld")
        self.assertEqual(config["source/addressbook"]["database"], "Work")
        self.assertEqual(config["source/addressbook"]["uri"], "card3")
        self.session.Detach()

        # different ways of addressing this context
        self.setUpSession("")
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertTrue("source/addressbook" in config)
        self.assertFalse("uri" in config["source/addressbook"])
        self.session.Detach()

        self.setUpSession("@DEFAULT")
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertEqual(config[""]["deviceId"], "shared-device-identifier")
        self.assertTrue("source/addressbook" in config)
        self.assertFalse("uri" in config["source/addressbook"])
        self.session.Detach()

        # different context
        self.setUpSession("@other_context")
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertFalse("source/addressbook" in config)
        self.session.Detach()

    def testSharedTemplate(self):
        """TestMultipleConfigs.testSharedTemplate - templates must contain shared properties"""
        self.setupConfigs()

        config = self.server.GetConfig("scheduleworld", True, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertEqual(config[""]["deviceId"], "shared-device-identifier")
        self.assertEqual(config["source/addressbook"]["database"], "Work")

    def testSharedProperties(self):
        """TestMultipleConfigs.testSharedProperties - 'type' consists of per-peer and shared properties"""
        self.setupConfigs()

        # writing for peer modifies properties in "foo" and context
        self.setUpSession("Foo@deFAULT")
        config = self.session.GetConfig(False, utf8_strings=True)
        config["source/addressbook"]["syncFormat"] = "text/vcard"
        config["source/addressbook"]["backend"] = "file"
        config["source/addressbook"]["databaseFormat"] = "text/x-vcard"
        self.session.SetConfig(True, False,
                               config,
                               utf8_strings=True)
        config = self.server.GetConfig("Foo", False, utf8_strings=True)
        self.assertEqual(config["source/addressbook"]["syncFormat"], "text/vcard")
        config = self.server.GetConfig("@default", False, utf8_strings=True)
        self.assertEqual(config["source/addressbook"]["backend"], "file")
        self.assertEqual(config["source/addressbook"]["databaseFormat"], "text/x-vcard")
        self.session.Detach()

    def testSharedPropertyOther(self):
        """TestMultipleConfigs.testSharedPropertyOther - shared backend properties must be preserved when adding peers"""
        # writing peer modifies properties in "foo" and creates context "@other"
        self.setUpSession("Foo@other")
        config = self.server.GetConfig("ScheduleWorld@other", True, utf8_strings=True)
        config["source/addressbook"]["backend"] = "file"
        config["source/addressbook"]["databaseFormat"] = "text/x-vcard"
        self.session.SetConfig(False, False,
                               config,
                               utf8_strings=True)
        config = self.server.GetConfig("Foo", False, utf8_strings=True)
        self.assertEqual(config["source/addressbook"]["backend"], "file")
        config = self.server.GetConfig("@other", False, utf8_strings=True)
        self.assertEqual(config["source/addressbook"]["databaseFormat"], "text/x-vcard")
        self.session.Detach()

        # adding second client must preserve backend value
        self.setUpSession("bar@other")
        config = self.server.GetConfig("Funambol@other", True, utf8_strings=True)
        self.assertEqual(config["source/addressbook"]["backend"], "file")
        self.session.SetConfig(False, False,
                               config,
                               utf8_strings=True)
        config = self.server.GetConfig("bar", False, utf8_strings=True)
        self.assertEqual(config["source/addressbook"]["backend"], "file")
        self.assertEqual(config["source/addressbook"].get("syncFormat"), None)
        config = self.server.GetConfig("@other", False, utf8_strings=True)
        self.assertEqual(config["source/addressbook"]["databaseFormat"], "text/x-vcard")

    def testOtherContext(self):
        """TestMultipleConfigs.testOtherContext - write into independent context"""
        self.setupConfigs()

        # write independent "foo@other_context" config
        self.setUpSession("foo@other_context")
        config = self.session.GetConfig(False, utf8_strings=True)
        config[""]["syncURL"] = "http://scheduleworld2"
        config["source/addressbook"] = { "database": "Play",
                                         "uri": "card30" }
        self.session.SetConfig(True, False,
                               config,
                               utf8_strings=True)
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertEqual(config[""]["syncURL"], "http://scheduleworld2")
        self.assertEqual(config["source/addressbook"]["database"], "Play")
        self.assertEqual(config["source/addressbook"]["uri"], "card30")
        self.session.Detach()

        # "foo" modified?
        self.setUpSession("foo")
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertEqual(config[""]["syncURL"], "http://scheduleworld")
        self.assertEqual(config["source/addressbook"]["database"], "Work")
        self.assertEqual(config["source/addressbook"]["uri"], "card3")
        self.session.Detach()

    def testSourceRemovalLocal(self):
        """TestMultipleConfigs.testSourceRemovalLocal - remove 'addressbook' source in 'foo'"""
        self.setupConfigs()
        self.setUpSession("foo")
        config = self.session.GetConfig(False, utf8_strings=True)
        del config["source/addressbook"]
        self.session.SetConfig(False, False, config, utf8_strings=True)
        self.session.Detach()

        # "addressbook" still exists in "foo" but only with default values
        config = self.server.GetConfig("foo", False, utf8_strings=True)
        self.assertFalse("uri" in config["source/addressbook"])
        self.assertFalse("sync" in config["source/addressbook"])

        # "addressbook" unchanged in "bar"
        config = self.server.GetConfig("bar", False, utf8_strings=True)
        self.assertEqual(config["source/addressbook"]["uri"], "card")
        self.assertEqual(config["source/addressbook"]["sync"], "refresh-from-client")

    def testSourceRemovalGlobal(self):
        """TestMultipleConfigs.testSourceRemovalGlobal - remove "addressbook" everywhere"""
        self.setupConfigs()
        self.setUpSession("")
        config = self.session.GetConfig(False, utf8_strings=True)
        del config["source/addressbook"]
        self.session.SetConfig(False, False, config, utf8_strings=True)
        self.session.Detach()

        # "addressbook" gone in "foo" and "bar"
        config = self.server.GetConfig("foo", False, utf8_strings=True)
        self.assertFalse("source/addressbook" in config)
        config = self.server.GetConfig("bar", False, utf8_strings=True)
        self.assertFalse("source/addressbook" in config)

    def testRemovePeer(self):
        """TestMultipleConfigs.testRemovePeer - check listing of peers while removing 'bar'"""
        self.setupConfigs()
        self.testOtherContext()
        self.setUpSession("bar")
        peers = self.session.GetConfigs(False, utf8_strings=True)
        self.assertEqual(peers,
                             [ "bar", "foo", "foo@other_context" ])
        peers2 = self.server.GetConfigs(False, utf8_strings=True)
        self.assertEqual(peers, peers2)
        # remove "bar"
        self.session.SetConfig(False, False, {}, utf8_strings=True)
        peers = self.server.GetConfigs(False, utf8_strings=True)
        self.assertEqual(peers,
                             [ "foo", "foo@other_context" ])
        self.session.Detach()

        # other configs should not have been affected
        config = self.server.GetConfig("foo", False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertEqual(config[""]["syncURL"], "http://scheduleworld")
        self.assertEqual(config["source/calendar"]["uri"], "cal3")
        config = self.server.GetConfig("foo@other_context", False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertEqual(config[""]["syncURL"], "http://scheduleworld2")
        self.assertEqual(config["source/addressbook"]["database"], "Play")
        self.assertEqual(config["source/addressbook"]["uri"], "card30")

    def testRemoveContext(self):
        """TestMultipleConfigs.testRemoveContext - remove complete config"""
        self.setupConfigs()
        self.setUpSession("")
        self.session.SetConfig(False, False, {}, utf8_strings=True)
        config = self.session.GetConfig(False, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        peers = self.server.GetConfigs(False, utf8_strings=True)
        self.assertEqual(peers, ['foo@other_context'])
        self.session.Detach()

    def testTemplates(self):
        """TestMultipleConfigs.testTemplates - templates reuse common properties"""
        self.setupConfigs()

        # deviceID must be shared and thus be reused in templates
        self.setUpSession("")
        config = self.session.GetConfig(False, utf8_strings=True)
        config[""]["DEVICEID"] = "shared-device-identifier"
        self.session.SetConfig(True, False, config, utf8_strings=True)
        config = self.server.GetConfig("", False, utf8_strings=True)
        self.assertEqual(config[""]["deviceId"], "shared-device-identifier")

        # get template for default context
        config = self.server.GetConfig("scheduleworld", True, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertEqual(config[""]["deviceId"], "shared-device-identifier")

        # now for @other_context - different device ID!
        config = self.server.GetConfig("scheduleworld@other_context", True, utf8_strings=True)
        self.assertEqual(config[""]["defaultPeer"], "foobar_peer")
        self.assertNotEqual(config[""]["deviceId"], "shared-device-identifier")

class TestLocalSync(unittest.TestCase, DBusUtil):
    """Tests involving local sync."""

    def setUp(self):
        self.setUpServer()

    def setUpConfigs(self, childPassword=None):
        # create file<->file configs
        self.setUpSession("target-config@client")
        addressbook = { "sync": "two-way",
                        "backend": "file",
                        "databaseFormat": "text/vcard",
                        "database": "file://" + xdg_root + "/client" }
        if childPassword:
            addressbook["databaseUser"] = "foo-user"
            addressbook["databasePassword"] = childPassword
        self.session.SetConfig(False, False,
                               {"" : { "loglevel": "4" },
                                "source/addressbook": addressbook })
        self.session.Detach()
        self.setUpSession("server")
        self.session.SetConfig(False, False,
                               {"" : { "loglevel": "4",
                                       "syncURL": "local://@client",
                                       "RetryDuration": self.getTestProperty("resendDuration", "60"),
                                       "peerIsClient": "1" },
                                "source/addressbook": { "sync": "two-way",
                                                        "uri": "addressbook",
                                                        "backend": "file",
                                                        "databaseFormat": "text/vcard",
                                                        "database": "file://" + xdg_root + "/server" } })

    @timeout(100)
    def testSync(self):
        """TestLocalSync.testSync - run a simple slow sync between local dirs"""
        self.setUpConfigs()
        os.makedirs(xdg_root + "/server")
        output = open(xdg_root + "/server/0", "w")
        output.write('''BEGIN:VCARD
VERSION:3.0
FN:John Doe
N:Doe;John
END:VCARD''')
        output.close()
        self.setUpListeners(self.sessionpath)
        self.session.Sync("slow", {})
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["session " + self.sessionpath + " done"])
        self.checkSync()
        input = open(xdg_root + "/server/0", "r")
        self.assertTrue("FN:John Doe" in input.read())

    @timeout(100)
    def testPasswordRequest(self):
        """TestLocalSync.testPassswordRequest - check that password request child->parent->us works"""
        self.setUpConfigs(childPassword="-")
        self.setUpListeners(self.sessionpath)
        self.lastState = "unknown"
        def infoRequest(id, session, state, handler, type, params):
            if state == "request":
                self.assertEqual(self.lastState, "unknown")
                self.lastState = "request"
                self.server.InfoResponse(id, "working", {}, utf8_strings=True)
            elif state == "waiting":
                self.assertEqual(self.lastState, "request")
                self.lastState = "waiting"
                self.server.InfoResponse(id, "response", {"password" : "123456"}, utf8_strings=True)
            elif state == "done":
                self.assertEqual(self.lastState, "waiting")
                self.lastState = "done"
            else:
                self.fail("state should not be '" + state + "'")

        signal = bus.add_signal_receiver(infoRequest,
                                         'InfoRequest',
                                         'org.syncevolution.Server',
                                         self.server.bus_name,
                                         None,
                                         byte_arrays=True,
                                         utf8_strings=True)

        try:
            self.session.Sync("slow", {})
            loop.run()
        finally:
            signal.remove()

        self.assertEqual(DBusUtil.quit_events, ["session " + self.sessionpath + " done"])
        self.assertEqual(self.lastState, "done")
        self.checkSync()

    @property("ENV", "SYNCEVOLUTION_LOCAL_CHILD_DELAY=5")
    @timeout(100)
    def testConcurrency(self):
        """TestLocalSync.testConcurrency - D-Bus server must remain responsive while sync runs"""
        self.setUpConfigs()
        self.setUpListeners(self.sessionpath)
        self.session.Sync("slow", {})
        time.sleep(2)
        status, error, sources = self.session.GetStatus(utf8_strings=True)
        self.assertEqual(status, "running")
        self.assertEqual(error, 0)
        self.session.Abort()
        loop.run()
        self.assertEqual(DBusUtil.quit_events, ["session " + self.sessionpath + " done"])
        report = self.checkSync(20017, 20017) # aborted
        self.assertFalse("error" in report) # ... but without error message
        self.assertEqual(report["source-addressbook-status"], "0") # unknown status for source (aborted early)

    def run(self, result):
        self.runTest(result)

class TestFileNotify(unittest.TestCase, DBusUtil):
    """syncevo-dbus-server must stop if one of its files mapped into
    memory (executable, libraries) change. Furthermore it must restart
    if automatic syncs are enabled. This class simulates such file changes
    by starting the server, identifying the location of the main executable,
    and renaming it back and forth."""

    def setUp(self):
        self.setUpServer()
        self.serverexe = self.serverExecutable()

    def tearDown(self):
        if os.path.isfile(self.serverexe + ".bak"):
            os.rename(self.serverexe + ".bak", self.serverexe)

    def run(self, result):
        self.runTest(result)

    def modifyServerFile(self):
        """rename server executable to trigger shutdown"""
        os.rename(self.serverexe, self.serverexe + ".bak")
        os.rename(self.serverexe + ".bak", self.serverexe)        

    @timeout(100)
    def testShutdown(self):
        """TestFileNotify.testShutdown - update server binary for 30 seconds, check that it shuts down at most 15 seconds after last mod"""
        self.assertTrue(self.isServerRunning())
        i = 0
        # Server must not shut down immediately, more changes might follow.
        # Simulate that.
        while i < 6:
            self.modifyServerFile()
            time.sleep(5)
            i = i + 1
        self.assertTrue(self.isServerRunning())
        time.sleep(10)
        self.assertFalse(self.isServerRunning())

    @timeout(30)
    def testSession(self):
        """TestFileNotify.testSession - create session, shut down directly after closing it"""
        self.assertTrue(self.isServerRunning())
        self.setUpSession("")
        self.modifyServerFile()
        time.sleep(15)
        self.assertTrue(self.isServerRunning())
        self.session.Detach()
        # should shut down almost immediately,
        # except when using valgrind
        if usingValgrind():
            time.sleep(10)
        else:
            time.sleep(1)
        self.assertFalse(self.isServerRunning())

    @timeout(30)
    def testSession2(self):
        """TestFileNotify.testSession2 - create session, shut down after quiesence period after closing it"""
        self.assertTrue(self.isServerRunning())
        self.setUpSession("")
        self.modifyServerFile()
        self.assertTrue(self.isServerRunning())
        self.session.Detach()
        time.sleep(8)
        self.assertTrue(self.isServerRunning())
        if usingValgrind():
            time.sleep(10)
        else:
            time.sleep(4)
        self.assertFalse(self.isServerRunning())

    @timeout(100)
    def testRestart(self):
        """TestFileNotify.testRestart - set up auto sync, then check that server restarts"""
        self.assertTrue(self.isServerRunning())
        self.setUpSession("memotoo")
        config = self.session.GetConfig(True, utf8_strings=True)
        config[""]["autoSync"] = "1"
        self.session.SetConfig(False, False, config)
        self.assertTrue(self.isServerRunning())
        self.session.Detach()
        self.modifyServerFile()
        bus_name = self.server.bus_name
        # give server time to restart
        if usingValgrind():
            time.sleep(40)
        else:
            time.sleep(15)
        self.setUpServer()
        self.assertNotEqual(bus_name, self.server.bus_name)
        # serverExecutable() will fail if the service wasn't properly
        # with execve() because then the old process is dead.
        self.assertEqual(self.serverexe, self.serverExecutable())

bt_mac         = "D4:5D:42:73:E4:6C"
bt_fingerprint = "Nokia 5230"
bt_name        = "My Nokia 5230"
bt_template    = "Bluetooth_%s_1" % (bt_mac)
bt_device      = "%s/dev_%s" % (bt_adaptor, string.replace(bt_mac, ':', '_'))

class BluezAdapter (dbus.service.Object):
    def __init__(self):
        self.SUPPORTS_MULTIPLE_OBJECT_PATHS = True
        bus_name = dbus.service.BusName('org.bluez', bus)
        dbus.service.Object.__init__(self, bus_name, bt_adaptor)

    @dbus.service.signal(dbus_interface='org.bluez.Adapter', signature='o')
    def DeviceCreated(self, obj):
        return bt_adaptor

    @dbus.service.signal(dbus_interface='org.bluez.Adapter', signature='o')
    def DeviceRemoved(self, obj):
        return bt_device

    @dbus.service.method(dbus_interface='org.bluez.Adapter', in_signature='', out_signature='ao')
    def ListDevices(self):
        return [bt_device]

class BluezDevice (dbus.service.Object):
    def __init__(self):
        self.SUPPORTS_MULTIPLE_OBJECT_PATHS = True
        bus_name = dbus.service.BusName('org.bluez', bus)
        dbus.service.Object.__init__(self, bus_name, bt_device)

    @dbus.service.method(dbus_interface='org.bluez.Device', in_signature='', out_signature='a{sv}')
    def GetProperties(self):
        return {"Name": bt_name,
                "Address": bt_mac,
                "UUIDs": ['00000002-0000-1000-8000-0002ee000002',
                          '00001000-0000-1000-8000-00805f9b34fb',
                          '00001101-0000-1000-8000-00805f9b34fb',
                          '00001103-0000-1000-8000-00805f9b34fb',
                          '00001105-0000-1000-8000-00805f9b34fb',
                          '00001106-0000-1000-8000-00805f9b34fb',
                          '0000110a-0000-1000-8000-00805f9b34fb',
                          '0000110c-0000-1000-8000-00805f9b34fb',
                          '0000110e-0000-1000-8000-00805f9b34fb',
                          '00001112-0000-1000-8000-00805f9b34fb',
                          '0000111b-0000-1000-8000-00805f9b34fb',
                          '0000111f-0000-1000-8000-00805f9b34fb',
                          '0000112d-0000-1000-8000-00805f9b34fb',
                          '0000112f-0000-1000-8000-00805f9b34fb',
                          '00001200-0000-1000-8000-00805f9b34fb',
                          '00005005-0000-1000-8000-0002ee000001',
                          '00005557-0000-1000-8000-0002ee000001',
                          '00005601-0000-1000-8000-0002ee000001']}

    @dbus.service.method(dbus_interface='org.bluez.Device', in_signature='s', out_signature='a{us}')
    def DiscoverServices(self, ignore):
        # This should be the last method to call. So, we need to quit the loop to exit.
        loop.quit()
        return { 65569L: '<?xml version="1.0" encoding="UTF-8" ?><record><attribute id="0x0000"><uint32 value="0x00010021" /></attribute><attribute id="0x0001"><sequence><uuid value="0x1200" /></sequence></attribute><attribute id="0x0005"><sequence><uuid value="0x1002" /></sequence></attribute><attribute id="0x0006"><sequence><uint16 value="0x454e" /><uint16 value="0x006a" /><uint16 value="0x0100" /></sequence></attribute><attribute id="0x0100"><text value="PnP Information" /></attribute><attribute id="0x0200"><uint16 value="0x0102" /></attribute><attribute id="0x0201"><uint16 value="0x0001" /></attribute><attribute id="0x0202"><uint16 value="0x00e7" /></attribute><attribute id="0x0203"><uint16 value="0x0000" /></attribute><attribute id="0x0204"><boolean value="true" /></attribute><attribute id="0x0205"><uint16 value="0x0001" /></attribute></record>'}

    @dbus.service.signal(dbus_interface='org.bluez.Device', signature='sv')
    def PropertyChanged(self, key, value):
        if(key == "Name"):
            bt_name = value

    def emitSignal(self):
        """ Change the device name. """
        self.PropertyChanged("Name", [string.replace(bt_name, "My", "Changed")])
        return

class TestBluetooth(unittest.TestCase, DBusUtil):
    """Tests that Bluetooth works properly."""

    def setUp(self):
        self.adp_conn = BluezAdapter()
        self.dev_conn = BluezDevice()
        loop.run()
        self.setUpServer()

    def tearDown(self):
        self.adp_conn.remove_from_connection()
        self.dev_conn.remove_from_connection()

    def run(self, result):
        self.runTest(result)

    @property("ENV", "DBUS_TEST_BLUETOOTH=session")
    @timeout(100)
    def testBluetoothTemplates(self):
        """TestBluetooth.testBluetoothTemplates - check for the bluetooth device's template"""
        configs = self.server.GetConfigs(True, utf8_strings=True)
        config = next((config for config in configs if config == bt_template), None)
        self.failUnless(config)

    @property("ENV", "DBUS_TEST_BLUETOOTH=session")
    @timeout(100)
    def testBluetoothNames(self):
        """TestBluetooth.testBluetoothNames - check that fingerPrint/peerName/deviceName/hardwareName are set correctly"""
        # This needs to be called before we can fetch the single config.
        configs = self.server.GetConfigs(True, utf8_strings=True)
        config  = self.server.GetConfig(bt_template, True, utf8_strings=True)
        # user-configurable name
        self.failUnlessEqual(config['']["deviceName"], bt_name)
        # must not be set
        self.failIf("peerName" in config[''])
        # all of the possible strings in the template, must include the hardware name of this example device
        self.failIf(string.find(config['']["fingerPrint"], bt_fingerprint) < 0)
        # real hardware information
        self.failUnlessEqual(config['']["hardwareName"], bt_fingerprint)

def createFiles(root, content, append = False):
    '''create directory hierarchy, overwriting previous content'''
    if not append:
        shutil.rmtree(root, True)

    entries = content.split("\n")
    outname = ''
    outfile = None
    for entry in entries:
        if not entry:
            continue
        parts = entry.split(":")
        newname = parts[0]
        line = parts[1]
        if newname != outname:
            fullpath = root + "/" + newname
            try:
                os.makedirs(fullpath[0:fullpath.rindex("/")])
            except:
                pass
            mode = "w"
            if append:
                mode = "a"
            outfile = open(fullpath, mode)
            outname = newname
        outfile.write(line + "\n")
    outfile.close()

isPropRegEx = re.compile(r'^([a-zA-Z]+) = ')
def isPropAssignment (line):
    '''true if "<word> = "'''
    m = isPropRegEx.search(line)
    if not m:
        return False
    # exclude some false positives
    if m.group(1) in ('KCalExtended', 'mkcal', 'QtContacts'):
        return False
    return True

def scanFiles(root, peer = '', onlyProps = True, directory = ''):
    '''turn directory hierarchy into string
    root      - root path in file system
    peer      - if non-empty, then ignore all <root>/peers/<foo>
                directories where <foo> != peer
    onlyProps - ignore lines which are comments
    directory - a subdirectory of root (used for recursion)'''
    newroot = root + '/' + directory
    out = ''

    for entry in sorted(os.listdir(newroot)):
        fullEntry = newroot + "/" + entry
        if os.path.isdir(fullEntry):
            if not (entry.endswith("/peers") and peer and entry != peer):
                if directory:
                    newdir = directory + '/' + entry
                else:
                    newdir = entry
                out += scanFiles(root, peer, onlyProps, newdir)
        else:
            infile = open (fullEntry)
            for line in infile:
                line = line.rstrip("\r\n")
                if (line):
                    takeIt = False
                    if (line.startswith("# ")):
                        takeIt = isPropAssignment(line[2:])
                    else:
                        takeIt = True
                    if (not onlyProps or takeIt):
                        if (directory):
                            out += directory + "/"
                        out += entry + ':' + line + "\n"
    return out

def sortConfig(config):
    '''sort lines by file, preserving order inside each line'''
    lines = config.splitlines()
    linenr = -1

    unsorted = []
    for line in lines:
        linenr += 1
        if not line:
            continue
        parts = line.split(":", 1)
        element = parts[0], linenr, parts[1]
        unsorted.append(element)

    # stable sort because of line number
    # probably it would be stable without it
    # but better be safe than sorry
    lines = sorted(unsorted)
    unsorted = []
    newconfig = ""
    for line in lines:
        newconfig += line[0] + ":" + line[2] + "\n"

    return newconfig

def lastLine(string):
    return string.splitlines(True)[-1]

def stripTime(string):
    matches = re.match("\[(\w+)\s+\d\d:\d\d:\d\d\] (.*)$", string, re.DOTALL)
    if matches != None:
        return "[" + matches.group(1) + "] " + matches.group(2)
    return string

class TestCmdline(unittest.TestCase, DBusUtil):
    """Tests cmdline by Session::Execute()."""

    def setUp(self):
        self.setUpServer()
        # All tests run with their own XDG root hierarchy.
        # Here are the config files.
        self.configdir = xdg_root + "/config/syncevolution"

    def run(self, result):
        self.runTest(result)

    def runCmdline(self, args, env=None, expectSuccess=True, preserveOutputOrder=False):
        '''Run the 'syncevolution' command line (from PATH) with the
        given arguments (list or tuple of strings). Uses the current
        environment unless one is set explicitly. Unless told
        otherwise, the result of the command is checked for
        success. Usually stdout and stderr are captured separately,
        in which case relative order of messages from different
        streams cannot be tested. When that is relevant, set preserveOutputOrder=True
        and look only at the stdout.

        Returns tuple with stdout, stderr and result code.'''
        a = [ 'syncevolution' ]
        a.extend(args)
        # Explicitly pass an environment. Otherwise subprocess.Popen()
        # from Python 2.6 uses not os.environ (which would be okay)
        # but rather the environment passed to a previous call to
        # subprocess.Popen() (which will fail if the previous test ran
        # with an environment which had SYNCEVOLUTION_DEBUG set).
        if env == None:
            env=os.environ
        if preserveOutputOrder:
            s = subprocess.Popen(a, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 env=env)
        else:
            s = subprocess.Popen(a, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 env=env)
        out, err = s.communicate()
        if expectSuccess and s.returncode != 0:
            result = 'syncevolution command failed.\nOutput:\n%s' % out
            if not preserveOutputOrder:
                result += '\nSeparate stderr:\n%s' % err
            self.fail(result)
        return (out, err, s.returncode)

    cachedSSLServerCertificates = None
    def getSSLServerCertificates(self):
        '''Default SSLServerCertificates path as compiled into the SyncEvolution
        binaries. Determined once by asking for a template.'''
        if TestCmdline.cachedSSLServerCertificates == None:
            out, err, code = self.runCmdline(['--template', 'default',
                                              '--print-config'])
            self.assertEqualDiff('', err)
            m = re.search(r'^# SSLServerCertificates = (.*)\n', out, re.MULTILINE)
            self.assertTrue(m)
            TestCmdline.cachedSSLServerCertificates = m.group(1)
        return TestCmdline.cachedSSLServerCertificates

    def ScheduleWorldConfig(self, peerMinVersion = 1, peerCurVersion = 1, contextMinVersion = 1, contextCurVersion = 1):
        return '''peers/scheduleworld/.internal.ini:peerMinVersion = {0}
peers/scheduleworld/.internal.ini:peerCurVersion = {1}
peers/scheduleworld/.internal.ini:# HashCode = 0
peers/scheduleworld/.internal.ini:# ConfigDate = 
peers/scheduleworld/.internal.ini:# lastNonce = 
peers/scheduleworld/.internal.ini:# deviceData = 
peers/scheduleworld/.internal.ini:# webDAVCredentialsOkay = 0
peers/scheduleworld/config.ini:syncURL = http://sync.scheduleworld.com/funambol/ds
peers/scheduleworld/config.ini:# username = 
peers/scheduleworld/config.ini:# password = 
.internal.ini:contextMinVersion = {2}
.internal.ini:contextCurVersion = {3}
config.ini:# logdir = 
peers/scheduleworld/config.ini:# loglevel = 0
peers/scheduleworld/config.ini:# printChanges = 1
peers/scheduleworld/config.ini:# dumpData = 1
config.ini:# maxlogdirs = 10
peers/scheduleworld/config.ini:# autoSync = 0
peers/scheduleworld/config.ini:# autoSyncInterval = 30M
peers/scheduleworld/config.ini:# autoSyncDelay = 5M
peers/scheduleworld/config.ini:# preventSlowSync = 1
peers/scheduleworld/config.ini:# useProxy = 0
peers/scheduleworld/config.ini:# proxyHost = 
peers/scheduleworld/config.ini:# proxyUsername = 
peers/scheduleworld/config.ini:# proxyPassword = 
peers/scheduleworld/config.ini:# clientAuthType = md5
peers/scheduleworld/config.ini:# RetryDuration = 5M
peers/scheduleworld/config.ini:# RetryInterval = 2M
peers/scheduleworld/config.ini:# remoteIdentifier = 
peers/scheduleworld/config.ini:# PeerIsClient = 0
peers/scheduleworld/config.ini:# SyncMLVersion = 
peers/scheduleworld/config.ini:PeerName = ScheduleWorld
config.ini:deviceId = fixed-devid
peers/scheduleworld/config.ini:# remoteDeviceId = 
peers/scheduleworld/config.ini:# enableWBXML = 1
peers/scheduleworld/config.ini:# maxMsgSize = 150000
peers/scheduleworld/config.ini:# maxObjSize = 4000000
peers/scheduleworld/config.ini:# SSLServerCertificates = {4}
peers/scheduleworld/config.ini:# SSLVerifyServer = 1
peers/scheduleworld/config.ini:# SSLVerifyHost = 1
peers/scheduleworld/config.ini:WebURL = http://www.scheduleworld.com
peers/scheduleworld/config.ini:IconURI = image://themedimage/icons/services/scheduleworld
peers/scheduleworld/config.ini:# ConsumerReady = 0
peers/scheduleworld/config.ini:# peerType = 
peers/scheduleworld/sources/addressbook/.internal.ini:# adminData = 
peers/scheduleworld/sources/addressbook/.internal.ini:# synthesisID = 0
peers/scheduleworld/sources/addressbook/config.ini:sync = two-way
peers/scheduleworld/sources/addressbook/config.ini:uri = card3
peers/scheduleworld/sources/addressbook/config.ini:syncFormat = text/vcard
peers/scheduleworld/sources/addressbook/config.ini:# forceSyncFormat = 0
peers/scheduleworld/sources/calendar/.internal.ini:# adminData = 
peers/scheduleworld/sources/calendar/.internal.ini:# synthesisID = 0
peers/scheduleworld/sources/calendar/config.ini:sync = two-way
peers/scheduleworld/sources/calendar/config.ini:uri = cal2
sources/calendar/config.ini:backend = calendar
peers/scheduleworld/sources/calendar/config.ini:# syncFormat = 
peers/scheduleworld/sources/calendar/config.ini:# forceSyncFormat = 0
sources/calendar/config.ini:# database = 
sources/calendar/config.ini:# databaseFormat = 
sources/calendar/config.ini:# databaseUser = 
sources/calendar/config.ini:# databasePassword = 
peers/scheduleworld/sources/memo/.internal.ini:# adminData = 
peers/scheduleworld/sources/memo/.internal.ini:# synthesisID = 0
peers/scheduleworld/sources/memo/config.ini:sync = two-way
peers/scheduleworld/sources/memo/config.ini:uri = note
sources/memo/config.ini:backend = memo
peers/scheduleworld/sources/memo/config.ini:# syncFormat = 
peers/scheduleworld/sources/memo/config.ini:# forceSyncFormat = 0
sources/memo/config.ini:# database = 
sources/memo/config.ini:# databaseFormat = 
sources/memo/config.ini:# databaseUser = 
sources/memo/config.ini:# databasePassword = 
peers/scheduleworld/sources/todo/.internal.ini:# adminData = 
peers/scheduleworld/sources/todo/.internal.ini:# synthesisID = 0
peers/scheduleworld/sources/todo/config.ini:sync = two-way
peers/scheduleworld/sources/todo/config.ini:uri = task2
sources/todo/config.ini:backend = todo
peers/scheduleworld/sources/todo/config.ini:# syncFormat = 
peers/scheduleworld/sources/todo/config.ini:# forceSyncFormat = 0
sources/addressbook/config.ini:backend = addressbook
sources/addressbook/config.ini:# database = 
sources/addressbook/config.ini:# databaseFormat = 
sources/addressbook/config.ini:# databaseUser = 
sources/addressbook/config.ini:# databasePassword = 
sources/todo/config.ini:# database = 
sources/todo/config.ini:# databaseFormat = 
sources/todo/config.ini:# databaseUser = 
sources/todo/config.ini:# databasePassword = '''.format(
           peerMinVersion, peerCurVersion,
           contextMinVersion, contextCurVersion,
           self.getSSLServerCertificates())

    def replaceLineInConfig(self, config, begin, to):
        index = config.find(begin)
        self.assertNotEqual(index, -1)
        newline = config.find("\n", index + len(begin))
        self.assertNotEqual(newline, -1)
        return config[:index] + to + config[newline:]

    def removeRandomUUID(self, config):
        return self.replaceLineInConfig(config,
                                        "deviceId = syncevolution-",
                                        "deviceId = fixed-devid")

    def removeSSLCertsPaths(self, config):
        return self.replaceLineInConfig(config,
                                        "SSLServerCertificates = ",
                                        "SSLServerCertificates = ")

    @property('debug', False)
    def testFramework(self):
        """TestCmdline.testFramework - tests whether utility functions work"""
        content = "baz:line\n" \
                  "caz/subdir:booh\n" \
                  "caz/subdir2/sub:# comment\n" \
                  "caz/subdir2/sub:# foo = bar\n" \
                  "caz/subdir2/sub:# empty = \n" \
                  "caz/subdir2/sub:# another comment\n" \
                  "foo:bar1\n" \
                  "foo:\n" \
                  "foo: \n" \
                  "foo:bar2\n"

        filtered = "baz:line\n" \
                   "caz/subdir:booh\n" \
                   "caz/subdir2/sub:# foo = bar\n" \
                   "caz/subdir2/sub:# empty = \n" \
                   "foo:bar1\n" \
                   "foo: \n" \
                   "foo:bar2\n"

        createFiles(self.configdir, content)
        res = scanFiles(self.configdir)
        self.assertEqualDiff(filtered, res)
        randomUUID = "deviceId = syncevolution-blabla\n"
        fixedUUID = "deviceId = fixed-devid\n"
        res = self.removeRandomUUID(randomUUID)
        self.assertEqual(fixedUUID, res)

        SSLCertPaths = "peers/scheduleworld/config.ini:# SSLServerCertificates = /etc/ssl/certs/ca-certificates.crt:/etc/pki/tls/certs/ca-bundle.crt:/usr/share/ssl/certs/ca-bundle.crt\n"
        clearSSLCertpaths = "peers/scheduleworld/config.ini:# SSLServerCertificates = \n"
        res = self.removeSSLCertsPaths(SSLCertPaths)
        self.assertEqual(clearSSLCertpaths, res)

        unsorted = "f:g\n" \
                   "f:j\n" \
                   "a:b\n" \
                   "f:a\n" \
                   "a/b:a\n"
        expected = "a:b\n" \
                   "a/b:a\n" \
                   "f:g\n" \
                   "f:j\n" \
                   "f:a\n"
        res = sortConfig(unsorted)
        self.assertEqualDiff(expected, res)

        # test DBusUtil.assertEqualDiff()
        try:
            self.assertEqualDiff('foo\nbar\n', 'foo\nxxx\nbar\n')
        except AssertionError, ex:
            expected = '''differences between expected and actual text

  foo
+ xxx
  bar
'''
            self.assertTrue(str(ex).endswith(expected), 'actual exception differs\n' + str(ex))
        else:
            self.fail('''DBusUtil.assertEqualDiff() did not detect diff''')

        self.assertEqualDiff('foo\nbar', [ 'foo\n', 'bar' ])
        self.assertEqualDiff([ 'foo\n', 'bar' ], 'foo\nbar')
        self.assertEqualDiff([ 'foo\n', 'bar' ], [ 'foo\n', 'bar' ])

        # test our own regex match
        self.assertRegexpMatchesCustom('foo\nbar\nend', 'bar')
        self.assertRegexpMatchesCustom('foo\nbar\nend', 'b.r')
        self.assertRegexpMatchesCustom('foo\nbar\nend', re.compile('^b.r$', re.MULTILINE))
        try:
            self.assertRegexpMatchesCustom('foo\nbar\nend', 'xxx')
        except AssertionError, ex:
            expected = '''text does not match regex\n\nText:\nfoo\nbar\nend\n\nRegex:\nxxx'''
            self.assertTrue(str(ex).endswith(expected), 'actual exception differs\n' + str(ex))
        else:
            self.fail('''DBusUtil.assertRegexpMatchesCustom() did not fail''')
        self.assertRegexpMatches('foo\nbar\nend', 'bar')

        lines = "a\nb\nc\n"
        lastline = "c\n"
        res = lastLine(lines)
        self.assertEqualDiff(lastline, res)

        message = "[ERROR 12:34:56] msg\n"
        stripped = "[ERROR] msg\n"
        res = stripTime(message)
        self.assertEqualDiff(stripped, res)

        # Run command without talking to server, separate streams.
        out, err, code = self.runCmdline(['--foo-bar'], expectSuccess=False)
        self.assertEqualDiff('[ERROR] --foo-bar: unknown parameter\n', err)
        self.assertRegexpMatches(out, '^List databases:\n')
        self.assertEqual(1, code)

        # Run command without talking to server, joined streams.
        out, err, code = self.runCmdline(['--foo-bar'], expectSuccess=False, preserveOutputOrder=True)
        self.assertEqual(err, None)
        self.assertRegexpMatches(out, r'^List databases:\n(.*\n)*\[ERROR\] --foo-bar: unknown parameter\n$')
        self.assertEqual(1, code)

    def assertSilent(self, out, err):
        if err != None:
            self.assertEqualDiff('', err)
        self.assertEqualDiff('', out)

    def doSetupScheduleWorld(self, shared):
        root = self.configdir + "/default"
        peer = ""

        if shared:
            peer = root + "/peers/scheduleworld"
        else:
            peer = root

        shutil.rmtree(peer, True)
        out, err, code = self.runCmdline(['--configure',
                                          '--sync-property', 'proxyHost = proxy',
                                          'scheduleworld', 'addressbook'])
        self.assertSilent(out, err)
        res = sortConfig(scanFiles(root))
        res = self.removeRandomUUID(res)
        expected = self.ScheduleWorldConfig()
        expected = sortConfig(expected)
        expected = expected.replace("# proxyHost = ",
                                    "proxyHost = proxy",
                                    1)
        expected = expected.replace("sync = two-way",
                                    "sync = disabled")
        expected = expected.replace("addressbook/config.ini:sync = disabled",
                                    "addressbook/config.ini:sync = two-way",
                                    1)
        self.assertEqualDiff(expected, res)

        shutil.rmtree(peer, True)
        out, err, code = self.runCmdline(['--configure',
                                          '--sync-property', 'deviceId = fixed-devid',
                                          'scheduleworld'])
        self.assertSilent(out, err)
        res = sortConfig(scanFiles(root))
        expected = self.ScheduleWorldConfig()
        expected = sortConfig(expected)
        self.assertEqualDiff(expected, res)

    @property('debug', False)
    def testSetupScheduleWorld(self):
        """TestCmdline.testSetupScheduleWorld - configure ScheduleWorld"""
        self.doSetupScheduleWorld(False)

if __name__ == '__main__':
    unittest.main()
