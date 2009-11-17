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

class DBusUtil:
    """Contains the common run() method for all D-Bus test suites
    and some utility functions."""

    def __init__(self):
        self.events = []
        self.quit_events = []
        self.reply = None

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
        unittest.TestCase.run(self, result)
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

    def setUpListeners(self, sessionpath):
        """records progress and status changes in self.events and
        quits the main loop when the session is done"""
        def progress(*args):
            self.events.append(("progress", args))
        def status(*args):
            self.events.append(("status", args))
            if args[0] == "done":
                if sessionpath:
                    self.quit_events.append("session " + sessionpath + " done")
                else:
                    self.quit_events.append("session done")
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
            self.events.append(("abort",))
            self.quit_events.append("connection " + conpath + " aborted")
            loop.quit()
        def reply(*args):
            self.reply = args
            if args[3]:
                self.quit_events.append("connection " + conpath + " got final reply")
            else:
                self.quit_events.append("connection " + conpath + " got reply")

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

    def testSecondSession(self):
        """a second session should not run unless the first one stops"""
        self.failUnless(have_glib)
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


class TestDBusSessionConfig(unittest.TestCase, DBusUtil):
    """Tests that work for GetConfig/SetConfig in Session."""

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
                                                }
                       }
        # update config
        self.updateConfig = { 
                               "" : { "password" : "nosecret"},
                               "source/addressbook" : { "sync" : "slow"}
                            }

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

class TestDBusSync(unittest.TestCase, DBusUtil):
    """Executes a real sync."""

    def setUp(self):
        DBusUtil.__init__(self)
        self.setUpServer()
        self.setUpSession(config)

    def run(self, result):
        self.runTest(result, own_xdg=False)

    def testSync(self):
        self.setUpListeners(self.sessionpath)
        self.session.Sync("", {})
        loop.run()
        # TODO: check recorded events in self.events
        status, error, sources = self.session.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "done")
        self.failUnlessEqual(error, 0)

class TestDBusSyncError(unittest.TestCase, DBusUtil):
    """Executes a real sync with no corresponding config."""

    def setUp(self):
        DBusUtil.__init__(self)
        self.setUpServer()
        self.setUpSession(config)

    def run(self, result):
        self.runTest(result, own_xdg=True)

    def testSync(self):
        self.setUpListeners(self.sessionpath)
        self.session.Sync("", {})
        loop.run()
        # TODO: check recorded events in self.events
        status, error, sources = self.session.GetStatus(utf8_strings=True)
        self.failUnlessEqual(status, "done")
        self.failUnlessEqual(error, 500)

class TestConnection(unittest.TestCase, DBusUtil):
    """Tests Server.Connect(). Tests depend on getting one Abort signal to terminate."""

    """a real message sent to our own server, DevInf stripped, username/password foo/bar"""
    message1 = '''<?xml version="1.0" encoding="UTF-8"?><SyncML xmlns='SYNCML:SYNCML1.2'><SyncHdr><VerDTD>1.2</VerDTD><VerProto>SyncML/1.2</VerProto><SessionID>255</SessionID><MsgID>1</MsgID><Target><LocURI>http://127.0.0.1:9000/syncevolution</LocURI></Target><Source><LocURI>sc-api-nat</LocURI><LocName>test</LocName></Source><Cred><Meta><Format xmlns='syncml:metinf'>b64</Format><Type xmlns='syncml:metinf'>syncml:auth-md5</Type></Meta><Data>kHzMn3RWFGWSKeBpXicppQ==</Data></Cred><Meta><MaxMsgSize xmlns='syncml:metinf'>20000</MaxMsgSize><MaxObjSize xmlns='syncml:metinf'>4000000</MaxObjSize></Meta></SyncHdr><SyncBody><Alert><CmdID>1</CmdID><Data>200</Data><Item><Target><LocURI>addressbook</LocURI></Target><Source><LocURI>./addressbook</LocURI></Source><Meta><Anchor xmlns='syncml:metinf'><Last>20091105T092757Z</Last><Next>20091105T092831Z</Next></Anchor><MaxObjSize xmlns='syncml:metinf'>4000000</MaxObjSize></Meta></Item></Alert><Final/></SyncBody></SyncML>'''

    def setUp(self):
        DBusUtil.__init__(self)
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
        self.failUnlessEqual(self.events, [('abort',)])

    def testInvalidConnect(self):
        """get connection, send invalid initial message"""
        conpath, connection = self.getConnection()
        try:
            connection.Process('1234', 'invalid message type')
        except dbus.DBusException, ex:
            self.failUnlessEqual(str(ex),
                                 'org.syncevolution.Exception: message type not supported for starting a sync')
        loop.run()
        self.failUnlessEqual(self.events, [('abort',)])

    def testStartSync(self):
        """send a valid initial SyncML message"""
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        self.failUnlessEqual(self.quit_events, ["connection " + conpath + " got reply"])
        self.quit_events = []
        # TODO: check events
        self.failIfEqual(self.reply, None)
        self.failUnlessEqual(self.reply[1], 'application/vnd.syncml+xml')
        # credentials should have been accepted because must_authenticate=False
        # in Connect(); 508 = "refresh required" is normal
        self.failUnless('<Status><CmdID>2</CmdID><MsgRef>1</MsgRef><CmdRef>1</CmdRef><Cmd>Alert</Cmd><TargetRef>addressbook</TargetRef><SourceRef>./addressbook</SourceRef><Data>508</Data>' in self.reply[0])
        self.failIf('<Chal>' in self.reply[0])
        self.failUnlessEqual(self.reply[3], False)
        self.failIfEqual(self.reply[4], '')
        connection.Close(False, 'good bye')
        loop.run()
        loop.run()
        self.failUnlessEqual(self.quit_events, ["connection " + conpath + " aborted",
                                                "session done"])

    def testCredentialsWrong(self):
        """send invalid credentials"""
        conpath, connection = self.getConnection(must_authenticate=True)
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        self.failUnlessEqual(self.quit_events, ["connection " + conpath + " got reply"])
        self.quit_events = []
        # TODO: check events
        self.failIfEqual(self.reply, None)
        self.failUnlessEqual(self.reply[1], 'application/vnd.syncml+xml')
        # credentials should have been rejected because of wrong Nonce
        self.failUnless('<Chal>' in self.reply[0])
        self.failUnlessEqual(self.reply[3], False)
        self.failIfEqual(self.reply[4], '')
        connection.Close(False, 'good bye')
        # when the login fails, the server also ends the session
        loop.run()
        loop.run()
        loop.run()
        self.quit_events.sort()
        self.failUnlessEqual(self.quit_events, ["connection " + conpath + " aborted",
                                                "connection " + conpath + " got final reply",
                                                "session done"])

    def testCredentialsRight(self):
        """send correct credentials"""
        conpath, connection = self.getConnection(must_authenticate=True)
        plain_auth = TestConnection.message1.replace("<Type xmlns='syncml:metinf'>syncml:auth-md5</Type></Meta><Data>kHzMn3RWFGWSKeBpXicppQ==</Data>",
                                                     "<Type xmlns='syncml:metinf'>syncml:auth-basic</Type></Meta><Data>dGVzdDp0ZXN0</Data>")
        connection.Process(plain_auth, 'application/vnd.syncml+xml')
        loop.run()
        self.failUnlessEqual(self.quit_events, ["connection " + conpath + " got reply"])
        self.quit_events = []
        self.failIfEqual(self.reply, None)
        self.failUnlessEqual(self.reply[1], 'application/vnd.syncml+xml')
        # credentials should have been accepted because with basic auth,
        # credentials can be replayed; 508 = "refresh required" is normal
        self.failUnless('<Status><CmdID>2</CmdID><MsgRef>1</MsgRef><CmdRef>1</CmdRef><Cmd>Alert</Cmd><TargetRef>addressbook</TargetRef><SourceRef>./addressbook</SourceRef><Data>508</Data>' in self.reply[0])
        self.failUnlessEqual(self.reply[3], False)
        self.failIfEqual(self.reply[4], '')
        connection.Close(False, 'good bye')
        loop.run()
        loop.run()
        self.failUnlessEqual(self.quit_events, ["connection " + conpath + " aborted",
                                                "session done"])

    def testStartSyncTwice(self):
        """send the same SyncML message twice, starting two sessions"""
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        # TODO: check events
        self.failUnlessEqual(self.quit_events, ["connection " + conpath + " got reply"])
        self.failIfEqual(self.reply, None)
        self.failUnlessEqual(self.reply[1], 'application/vnd.syncml+xml')
        self.failUnlessEqual(self.reply[3], False)
        self.failIfEqual(self.reply[4], '')
        self.reply = None
        self.quit_events = []

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
        self.quit_events.sort()
        expected = [ "connection " + conpath + " aborted",
                     "session done",
                     "connection " + conpath2 + " got reply" ]
        expected.sort()
        self.failUnlessEqual(self.quit_events, expected)
        self.failIfEqual(self.reply, None)
        self.failUnlessEqual(self.reply[1], 'application/vnd.syncml+xml')
        self.failUnlessEqual(self.reply[3], False)
        self.failIfEqual(self.reply[4], '')
        self.quit_events = []

        # now quit for good
        connection2.Close(False, 'good bye')
        loop.run()
        loop.run()
        self.failUnlessEqual(self.quit_events, ["connection " + conpath2 + " aborted",
                                                "session done"])

    def testKillInactive(self):
        """block server with client A, then let client B connect twice"""
        conpath, connection = self.getConnection()
        connection.Process(TestConnection.message1, 'application/vnd.syncml+xml')
        loop.run()
        # TODO: check events
        self.failUnlessEqual(self.quit_events, ["connection " + conpath + " got reply"])
        self.failIfEqual(self.reply, None)
        self.failUnlessEqual(self.reply[1], 'application/vnd.syncml+xml')
        self.failUnlessEqual(self.reply[3], False)
        self.failIfEqual(self.reply[4], '')
        self.reply = None
        self.quit_events = []

        # Now start two more sessions with the second client *without*
        # closing the first one. The server should remove only the
        # first connection of client B.
        message1_clientB = TestConnection.message1.replace("sc-api-nat", "sc-pim-ppc")
        conpath2, connection2 = self.getConnection()
        connection2.Process(message1_clientB, 'application/vnd.syncml+xml')
        conpath3, connection3 = self.getConnection()
        connection3.Process(message1_clientB, 'application/vnd.syncml+xml')
        loop.run()
        self.failUnlessEqual(self.quit_events, [ "connection " + conpath2 + " aborted" ])
        self.quit_events = []

        # now quit for good
        connection3.Close(False, 'good bye client B')
        loop.run()
        self.failUnlessEqual(self.quit_events, [ "connection " + conpath3 + " aborted" ])
        self.quit_events = []
        connection.Close(False, 'good bye client A')
        loop.run()
        loop.run()
        self.failUnlessEqual(self.quit_events, ["connection " + conpath + " aborted",
                                                "session done"])

if __name__ == '__main__':
    unittest.main()
