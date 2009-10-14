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

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import gobject
import glib

DBusGMainLoop(set_as_default=True)

bus = dbus.SessionBus()
loop = gobject.MainLoop()

debugger = "" # "gdb"
server = ["syncevo-dbus-server"]
monitor = ["dbus-monitor"]
xdg_root = "test-dbus"
config = "scheduleworld_1"

class DBusUtil:
    """Contains the common run() method for all D-Bus test suites
    and some utility functions."""

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

        env = copy.deepcopy(os.environ)
        if own_xdg:
            shutil.rmtree(xdg_root, True)
            env["XDG_DATA_HOME"] = xdg_root + "/data"
            env["XDG_CONFIG_HOME"] = xdg_root + "/config"
            env["XDG_CACHE_HOME"] = xdg_root + "/cache"

        pmonitor = subprocess.Popen(monitor,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
        if debugger:
            print "\n%s: %s\n" % (self.id(), self.shortDescription())
            pserver = subprocess.Popen([debugger] + server,
                                       env=env)

            while True:
                check = subprocess.Popen("ps x | grep %s | grep -w -v -e %s -e grep -e ps" % \
                                             (server[0], debugger),
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
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT)
            pserver.stdout.readline()

        numerrors = len(result.errors)
        numfailures = len(result.failures)
        if debugger:
            print "\nrunning\n"
        unittest.TestCase.run(self, result)
        if debugger:
            print "\ndone, quit gdb now\n"
        hasfailed = numerrors + numfailures != len(result.errors) + len(result.failures)

        if not debugger:
            pserver.terminate()
        serverout, dummy = pserver.communicate()
        if hasfailed:
            # give D-Bus time to settle down
            time.sleep(1)
        pmonitor.terminate()
        monitorout, dummy = pmonitor.communicate()
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

    def setUpSession(self, config):
        self.sessionpath = self.server.StartSession(config)
        bus.add_signal_receiver(lambda object, ready: loop.quit(),
                                'SessionChanged',
                                'org.syncevolution.Server',
                                'org.syncevolution',
                                self.sessionpath,
                                byte_arrays=True,
                                utf8_strings=True)
        self.session = dbus.Interface(bus.get_object('org.syncevolution',
                                                     self.sessionpath),
                                      'org.syncevolution.Session')
        status, error, sources = self.session.GetStatus(utf8_strings=True)
        if status == "queuing":
            loop.run()


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
        self.failUnlessEqual(configs, ["google"])

    def testGetConfigScheduleWorld(self):
        """read ScheduleWorld template"""
        config1 = self.server.GetConfig("scheduleworld", True, utf8_strings=True)
        config2 = self.server.GetConfig("ScheduleWorld", True, utf8_strings=True)
        self.failIfEqual(config1[""]["deviceId"], config2[""]["deviceId"])
        config1[""]["deviceId"] = "foo"
        config2[""]["deviceId"] = "foo"
        self.failUnlessEqual(config1, config2)

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

    def testSecondSession(self):
        """a second session should not run unless the first one stops"""
        sessionpath = self.server.StartSession("")
        bus.add_signal_receiver(lambda object, ready: loop.quit(),
                                'SessionChanged',
                                'org.syncevolution.Server',
                                'org.syncevolution',
                                sessionpath,
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
            callback_called[1] = True
            self.session.Detach()
        glib.timeout_add(2, callback)
        glib.timeout_add(5, lambda: loop.quit())
        loop.run()
        self.failUnless(callback_called)
        status, error, sources = session.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "idle")

class TestDBusSync(unittest.TestCase, DBusUtil):
    """Executes a real sync."""

    def setUp(self):
        self.setUpServer()
        self.setUpSession(config)

    def run(self, result):
        self.runTest(result, own_xdg=False)

    def testSync(self):
        events = []
        def progress(*args):
            events.append(("progress", args))
        def status(*args):
            events.append(("status", args))
            if args[0] == "done":
                loop.quit()
        bus.add_signal_receiver(progress,
                                'ProgressChanged',
                                'org.syncevolution.Session',
                                'org.syncevolution',
                                self.sessionpath,
                                byte_arrays=True, 
                                utf8_strings=True)
        bus.add_signal_receiver(status,
                                'StatusChanged',
                                'org.syncevolution.Session',
                                'org.syncevolution',
                                self.sessionpath,
                                byte_arrays=True, 
                                utf8_strings=True)
        self.session.Sync("", {})
        loop.run()
        # TODO: check recorded events
        status, error, sources = self.session.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "done")
        self.failUnlessEqual(error, 0)

if __name__ == '__main__':
    unittest.main()
