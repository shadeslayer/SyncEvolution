#! /usr/bin/python

import random
import unittest
import subprocess
import time
import os
import signal
import shutil

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import gobject

DBusGMainLoop(set_as_default=True)

bus = dbus.SessionBus()

debugger = "" # "gdb"
server = ["syncevo-dbus-server"]
monitor = ["dbus-monitor"]
xdg_root = "test-dbus"

class TestDBusServer(unittest.TestCase):

    def run(self, result):
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

        shutil.rmtree(xdg_root, True)
        os.environ["XDG_DATA_HOME"] = xdg_root + "/data"
        os.environ["XDG_CONFIG_HOME"] = xdg_root + "/config"
        os.environ["XDG_CACHE_HOME"] = xdg_root + "/cache"

        pmonitor = subprocess.Popen(monitor,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
        if debugger:
            print "\n%s: %s\n" % (self.id(), self.shortDescription())
            pserver = subprocess.Popen([debugger] + server)
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

    def setUp(self):
        self.server = dbus.Interface(bus.get_object('org.syncevolution',
                                                    '/org/syncevolution/Server'),
                                     'org.syncevolution.Server')

    def testGetConfigsEmpty(self):
        """GetConfigs() with no configurations available"""
        configs = self.server.GetConfigs(False)
        failUnlessEqual(configs, [])

    def testGetConfigsTemplates(self):
        """read templates"""
        configs = self.server.GetConfigs(True)
        failUnlessEqual(configs, ["google"])

    def testGetConfigScheduleWorld(self):
        """read ScheduleWorld template"""
        config1 = self.server.GetConfig("scheduleworld", True)
        config2 = self.server.GetConfig("ScheduleWorld", True)
        self.failIfEqual(config1[""]["deviceId"], config2[""]["deviceId"])
        config1[""]["deviceId"] = "foo"
        config2[""]["deviceId"] = "foo"
        self.failUnlessEqual(config1, config2)

if __name__ == '__main__':
    unittest.main()
