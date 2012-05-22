#! /usr/bin/python

'''Usage: syncevo-http-server.py <URL>
Runs a SyncML HTTP server under the given base URL.'''

# use the same glib main loop in D-Bus and twisted
from dbus.mainloop.glib import DBusGMainLoop
from twisted.internet import glib2reactor # for non-GUI apps
DBusGMainLoop(set_as_default=True)
glib2reactor.install()

import dbus
import gobject
import sys
import urlparse
import optparse
import os
import atexit
import signal
import time
import subprocess
import logging
import logging.config

import twisted.web
import twisted.python.log
import twisted.web.error
from twisted.web import server, resource, http
from twisted.internet import ssl, reactor
from OpenSSL import SSL

# for output from this script itself
logger = logging.getLogger("syncevo-http")

# for output from core SyncEvolution
loggerCore = logging.getLogger("sync")

class ChainedOpenSSLContextFactory(ssl.DefaultOpenSSLContextFactory):
    def __init__(self, privateKeyFileName, certificateChainFileName,
                 sslmethod = SSL.SSLv3_METHOD):
        """
        @param privateKeyFileName: Name of a file containing a private key
        @param certificateChainFileName: Name of a file containing a certificate chain
        @param sslmethod: The SSL method to use
        """
        self.privateKeyFileName = privateKeyFileName
        self.certificateChainFileName = certificateChainFileName
        self.sslmethod = sslmethod
        self.cacheContext()
    
    def cacheContext(self):
        ctx = SSL.Context(self.sslmethod)
        ctx.use_certificate_chain_file(self.certificateChainFileName)
        ctx.use_privatekey_file(self.privateKeyFileName)
        self._context = ctx

# cached information about previous POST and reply,
# in case that we need to resend
class OldRequest:
    sessionid = None
    data = None
    reply = None
    type = None

# holds global variables which will be set later:
# - bus = D-Bus session bus
# - dbusserver = path to syncevo-dbus-server if it needs to be started, None if using system
class Context:
    bus = None
    dbusserver = None
    _server = None

    @staticmethod
    def checkServer():
        if Context._server:
            retcode = Context._server.poll()
            if retcode == None:
                return
            elif retcode < 0:
                logger.error("syncevo-dbus-server was terminated by signal %d", -retcode)
            elif retcode > 0:
                logger.error("syncevo-dbus-server failed, return code %d", retcode)
            else:
                logger.debug("syncevo-dbus-server shut down normally")
            Context._server = None

    @staticmethod
    def getDBusServer():
        if not Context.dbusserver:
            # standard D-Bus activation
            return dbus.Interface(Context.bus.get_object('org.syncevolution',
                                                         '/org/syncevolution/Server'),
                                  'org.syncevolution.Server')

        # check status of previously started server
        Context.checkServer()

        # start executable
        if not Context._server:
            logger.debug("starting %s", Context.dbusserver)
            Context._server = subprocess.Popen(Context.dbusserver, stdout=open("/dev/null", "w"))

        # wait until the daemon shows up on D-Bus (slightly racy)
        while Context._server.poll() == None:
            time.sleep(0.5)
            if 'org.syncevolution' in Context.bus.list_names():
                return dbus.Interface(Context.bus.get_object('org.syncevolution',
                                                             '/org/syncevolution/Server'),
                                      'org.syncevolution.Server')

        # Startup failed?! Clean up, then give up and return nothing (probably triggers further errors).
        Context.checkServer()
        return None

class SyncMLSession:
    sessions = []

    def __init__(self):
        self.sessionid = None
        self.request = None
        self.conpath = None
        self.abort_match = None
        self.reply_match = None
        self.connection = None

    def destruct(self, code, message=""):
        '''Tell both HTTP client and D-Bus server that we are shutting down,
        then remove the session'''
        logger.debug("destructing connection %s with code %s message %s", self.conpath, code, message)
        if self.request:
            self.request.setResponseCode(code, message)
            self.request.finish()
            self.request = None
        if self.connection:
            try:
                self.connection.Close(False, message)
            except dbus.exceptions.DBusException, ex:
                if ex.get_dbus_name() == "org.freedesktop.DBus.Error.UnknownMethod":
                    # triggered if connection instance is already gone, hide from user
                    logger.debug("self.connection.Close() failed, connection probably already gone: %s", ex)
                else:
                    raise
            self.connection = None
        if self.abort_match:
            Context.bus.remove_signal_receiver(self.abort_match)
        if self.reply_match:
            Context.bus.remove_signal_receiver(self.reply_match)
        if self in SyncMLSession.sessions:
            SyncMLSession.sessions.remove(self)
            logger.debug("removed SyncML session %s", self)

    def abort(self, **keywords):
        '''D-Bus server requests to close connection, so cancel everything'''
        conpath = keywords['conpath']
        logger.debug("connection %s went down, active connection %s", conpath, self.conpath)
        if conpath == self.conpath:
            self.destruct(http.INTERNAL_SERVER_ERROR, "lost connection to SyncEvolution")
        else:
            logger.debug("ignore shutdown of obsolete connection")

    def reply(self, data, type, meta, final, session, **keywords):
        '''sent reply to HTTP client and/or close down normally'''
        conpath = keywords['conpath']
        logger.debug("reply session %s connection %s (active %s) final %s data len %d %s",
                     session, conpath, self.conpath, final, len(data), meta)
        if conpath != self.conpath:
            logger.debug("ignore reply via obsolete connection")
            return
        self.logMessage("outgoing", self.request, data, type)
        # When the D-Bus server sends an empty array, Python binding
        # puts the four chars in 'None' into the data array?!
        if data and len(data) > 0 and data != 'None':
            request = self.request
            self.request = None
            OldRequest.reply = data
            OldRequest.type = type
            if request:
                request.setHeader('Content-Type', type)
                request.setHeader('Content-Length', len(data))
                request.setResponseCode(http.OK)
                request.write(data)
                request.finish()
                self.sessionid = session
            else:
                # syncevo-dbus-server does not need to know about lost connection
                # to client, because client might still resend
                logger.debug("could not send reply immediately, buffering it")
        if final:
            logger.debug("closing connection for connection %s session %s", self.conpath, session)
            if self.connection:
                self.connection.Close(True, "")
                self.connection = None
            self.destruct(http.GONE, "D-Bus server done")

    def done(self, error):
        '''lost connection to HTTP client, either normally or in error'''
        logger.debug("done with request in session %s, error %s", self.sessionid, error)
        # keep connection to syncevo-dbus-server, client might still
        # retry the request
        self.request = None

    def start(self, request, config, url):
        '''start a new session based on the incoming message'''
        data = request.content.read()
        type = request.getHeader('content-type')
        self.logMessage("incoming", request, data, type)
        logger.debug("requesting new session")
        self.object = Context.getDBusServer()
        self.request = request
        deferred = request.notifyFinish()
        deferred.addCallback(self.done)
        deferred.addErrback(self.done)
        self.conpath = self.object.Connect({'description': 'syncevo-server-http.py',
                                            'transport': 'HTTP',
                                            'config': config,
                                            'URL': url},
                                           True,
                                           '')
        logger.debug("started new connection %s" % self.conpath)
        self.connection = dbus.Interface(Context.bus.get_object('org.syncevolution',
                                                                self.conpath),
                                         'org.syncevolution.Connection')

        if self.abort_match:
            Context.bus.remove_signal_receiver(self.abort_match)
        if self.reply_match:
            Context.bus.remove_signal_receiver(self.reply_match)

        self.abort_match = \
        Context.bus.add_signal_receiver(self.abort,
                                        'Abort',
                                        'org.syncevolution.Connection',
                                        'org.syncevolution',
                                        self.conpath,
                                        path_keyword='conpath',
                                        utf8_strings=True,
                                        byte_arrays=True)
        self.reply_match = \
        Context.bus.add_signal_receiver(self.reply,
                                        'Reply',
                                        'org.syncevolution.Connection',
                                        'org.syncevolution',
                                        self.conpath,
                                        path_keyword='conpath',
                                        utf8_strings=True,
                                        byte_arrays=True)

        # feed new data into SyncEvolution and wait for reply
        request.content.seek(0, 0)
        self.connection.Process(data, type)
        SyncMLSession.sessions.append(self)
        logger.debug("added new SyncML session %s", self)

    def process(self, request, data):
        '''process next message by client in running session'''
        type = request.getHeader('content-type')
        self.logMessage("incoming", request, data, type)
        if self.request:
            # message resend?! Ignore old request.
            logger.debug("message resend?!")
            self.request.finish()
            self.request = None
        deferred = request.notifyFinish()
        deferred.addCallback(self.done)
        deferred.addErrback(self.done)
        self.request = request
        self.connection.Process(data, type)

    def logMessage(self, direction, request, data, type):
        if 'plain' in type or "+xml" in type:
            logger.debug("processing %s message of type %s and length %d:\n%s" % (direction, type, len(data), data))
        else:
            logger.debug("processing %s message of type %s and length %d, binary data" % (direction, type, len(data)))

class SyncMLPost(resource.Resource):
    isLeaf = True

    def __init__(self, url):
        self.url = url

    def render_GET(self, request):
        logger.info("GET %s from %s", self.url, request.getClientIP())
        return "<html>SyncEvolution SyncML Server</html>"

    def render_POST(self, request):
        config = request.postpath
        if config:
            config = config[0]
        else:
            config = ""
        type = request.getHeader('content-type')
        len = request.getHeader('content-length')
        sessionid = request.args.get('sessionid')
        if sessionid:
            sessionid = sessionid[0]
        logger.debug("POST from %s config %s type %s session %s args %s length %s",
                     request.getClientIP(), config, type, sessionid, request.args, len)
        if not sessionid:
            logger.info("new SyncML session for %s", request.getClientIP())
            session = SyncMLSession()
            session.start(request, config,
                          urlparse.urljoin(self.url.geturl(), request.path))
            return server.NOT_DONE_YET
        else:
            data = request.content.read()
            # Detect resent message. We support that for
            # independently from the session, because it
            # might already be gone (server sends last reply
            # in session, closes session, client doesn't
            # get reply, reposts).
            if sessionid == OldRequest.sessionid and \
                    OldRequest.data == data and \
                    OldRequest.reply:
                logger.debug("resend reply session %s", sessionid)
                request.setHeader('Content-Type', OldRequest.type)
                request.setResponseCode(http.OK)
                request.write(OldRequest.reply)
                request.finish()
                return server.NOT_DONE_YET
            else:
                # prepare resending, will be completed in
                # SyncSession.reply()
                OldRequest.sessionid = sessionid
                OldRequest.data = data
                OldRequest.reply = None
            for session in SyncMLSession.sessions:
                if session.sessionid == sessionid:
                    session.process(request, data)
                    return server.NOT_DONE_YET
            # fallback when session not found
            logger.error("unknown session %s => 404 error", sessionid)
            page = twisted.web.error.NoResource(message="The session %s was not found" % sessionid)
            return page.render(request)

class TwistedLogging(object):
    "same as Twisted's PythonLoggingObserver, except that it uses loglevels debug and error"
    def __init__(self):
        self.logger = logging.getLogger("twisted")

    def emit(self, eventDict):
        if 'logLevel' in eventDict:
            level = eventDict['logLevel']
        elif eventDict['isError']:
            if 'failure' in eventDict and \
                    eventDict['failure'].type == dbus.exceptions.DBusException and \
                    eventDict['failure'].value.get_dbus_name() == "org.syncevolution.Exception":
                # special case: errors inside the syncevo-dbus-server are better shown
                # to users as part of the syncevo-dbus-server output, so treat the
                # syncevo-http-server side of it as something for debugging.
                level = logging.DEBUG
            else:
                level = logging.ERROR
        else:
            level = logging.DEBUG
        text = twisted.python.log.textFromEventDict(eventDict)
        if text is None:
            return
        self.logger.log(level, text)

    def start(self):
        twisted.python.log.startLoggingWithObserver(self.emit, setStdout=False)

    def stop(self):
        twisted.python.log.removeObserver(self.emit)

evo2python = {
    "DEBUG": logging.DEBUG,
    "DEVELOPER": logging.DEBUG,
    "INFO": logging.INFO,
    "SHOW": logging.INFO,
    "ERROR": logging.ERROR,
    "WARNING": logging.WARNING
}

def logSyncEvoOutput(path, level, output):
    loggerCore.log(evo2python.get(level, logging.ERROR), "%s: %s", path, output)

usage =  """usage: %prog [options] http://localhost:<port>/<path>

Runs a HTTP server which listens on all network interfaces on
the given port and answers requests for the given path.
Configurations for clients must be created manually, see
http://syncevolution.org/development/http-server-howto"""

def main():
    parser = optparse.OptionParser(usage=usage)
    parser.add_option("-d", "--debug",
                      action="store_true", dest="debug", default=False,
                      help="enables debug messages")
    parser.add_option("-q", "--quiet",
                      action="store_true", dest="quiet", default=False,
                      help="limits output to real error messages; ignored if --debug is given")
    parser.add_option("", "--log-config",
                      action="store", type="string", dest="logConfig", default=None,
                      help="configure logging via Python logging config file; --debug and --quiet override the log level in the root logger")
    parser.add_option("", "--server-certificate",
                      action="store", type="string", dest="cert", default=None,
                      help="certificate file used by the server to identify itself (required for https)")
    parser.add_option("", "--server-key",
                      action="store", type="string", dest="key", default=None,
                      help="key file used by the server to identify itself (optional, certificate file is used as fallback, which then must contain key and certificate)")
    parser.add_option("", "--start-dbus-session",
                      action="store_true", dest="startDBus", default=False,
                      help="""creates a new D-Bus session for
communication with syncevo-dbus-server and (inside that server) with
other D-Bus services like Evolution Data Server, removes it when
shutting down; should only be used if it is guaranteed that the
current user will not have another session running, because these
services can get confused when started multiple times; without this
option, syncevo-http-server a) uses the session specified in the
environment or b) looks for a session of the current user (depends on
ConsoleKit and might not always work)""")
    parser.add_option("", "--start-syncevolution",
                      action="store_true", dest="startSyncEvo", default=False,
                      help="""sets up the right environment for
syncevo-dbus-server and (re)starts it explicitly, instead of depending
on D-Bus auto-activation; to be used when SyncEvolution is not
installed at the location it was compiled for""")
    parser.add_option("", "--syncevolution-path",
                      action="store", type="string", dest="path", default=None,
                      help="""sets the installation path (the
directory which contains 'bin', 'libexec', etc.) to be used in
--start-syncevolution, default is the location where
syncevo-http-server itself is installed""")
    (options, args) = parser.parse_args()

    # determine level chosen via command line
    level = None
    if options.debug:
        level = logging.DEBUG
    elif options.quiet:
        level = logging.ERROR
    else:
        level = None

    # create logging infrastructure
    if options.logConfig:
        logging.config.fileConfig(options.logConfig)
        # only override level if explicitly set
        if level != None:
            logging.getLogger().setLevel(level)
    else:
        root = logging.getLogger()
        ch = logging.StreamHandler()
        formatter = logging.Formatter("[%(levelname)s] %(name)s: %(message)s")
        ch.setFormatter(formatter)
        root.addHandler(ch)
        if level == None:
            level = logging.INFO
        root.setLevel(level)

    # redirect output from Twisted
    observer = TwistedLogging()
    observer.start()

    # Set up D-Bus. First check for an existing session in env.
    #
    # Doing the check via ConsoleKit here would be nice, but that
    # led to the following problem with --start-dbus-session:
    # - dbus module is initialized in dbus.SystemBus()
    # - DISPLAY remains unset, dbus-launch creates new DBUS_SESSION_BUS_ADDRESS
    # - dbus.SessionBus() fails with "Autolaunch error: X11 initialization failed."
    # Presumably the session bus address is not checked again after setting it.
    # Not touching D-Bus before dbus-launch avoids that problem.
    havedbus = os.environ.get("DBUS_SESSION_BUS_ADDRESS") or os.environ.get("DISPLAY")
    if options.startDBus:
        if havedbus:
            logger.info("%s session found (DISPLAY=%s DBUS_SESSION_BUS_ADDRESS=%s), but starting a new D-Bus session as requested",
                        os.environ.get("DBUS_SESSION_BUS_ADDRESS") and "D-Bus" or "potential",
                        os.environ.get("DISPLAY", ""),
                        os.environ.get("DBUS_SESSION_BUS_ADDRESS", ""))
        p = subprocess.Popen("dbus-launch", stdout=subprocess.PIPE)
        output = p.communicate()[0]
        if p.wait():
            logger.error("dbus-launch failed")
            exit(1)
        for line in output.split():
            logger.debug("dbus-launch: %s", line)
            var, value = line.split("=", 1)
            if var == "DBUS_SESSION_BUS_PID":
                # kill that process when we quit
                atexit.register(os.kill, int(value), signal.SIGTERM)
            os.environ[var] = value
    else:
        if not havedbus:
            # code inspired by Ross Burton's http://burtonini.com/blog/computers/offlineimap-2008-11-04-20-00
            logger.debug("looking for X11 session via ConsoleKit")
            bus = dbus.SystemBus()
            try:
                manager_obj = bus.get_object('org.freedesktop.ConsoleKit', '/org/freedesktop/ConsoleKit/Manager')
                manager = dbus.Interface(manager_obj, 'org.freedesktop.ConsoleKit.Manager')

                for ssid in manager.GetSessionsForUnixUser(os.getuid()):
                    obj = bus.get_object('org.freedesktop.ConsoleKit', ssid)
                    session = dbus.Interface(obj, 'org.freedesktop.ConsoleKit.Session')
                    dpy = session.GetX11Display()
                    logger.debug("ConsoleKit session %s has DISPLAY=%s", session, dpy)
                    if dpy:
                        if havedbus:
                            # already found a session earlier, now what?!
                            logger.error("multiple X11 sessions found for current user, please set DISPLAY to the one to be used for syncing")
                            exit(1)
                        else:
                            # use this X11 session to find D-Bus session bus
                            os.environ["DISPLAY"] = dpy
                            havedbus = True
            except dbus.exceptions.DBusException, ex:
                if ex.get_dbus_name() == "org.freedesktop.DBus.Error.ServiceUnknown":
                    logger.debug("org.freedesktop.ConsoleKit service not available")
                else:
                    raise

        if not havedbus:
            logger.error("no D-Bus session, use --start-dbus-session")
            exit(1)

    # Now at least one of DISPLAY or DBUS_SESSION_BUS_ADDRESS should be set.
    # DISPLAY is sufficient for the D-Bus autolaunch mechanism in libdbus (which
    # reuses a session bus if one exists or creates a new one),
    # whereas DBUS_SESSION_BUS_ADDRESS explicitly selects an existing bus.
    logger.debug("connecting to D-Bus session with DISPLAY=%s DBUS_SESSION_BUS_ADDRESS=%s",
                 os.environ.get("DISPLAY", ""),
                 os.environ.get("DBUS_SESSION_BUS_ADDRESS", ""))
    Context.bus = dbus.SessionBus()

    # catch output from syncevo-dbus-server
    Context.bus.add_signal_receiver(logSyncEvoOutput,
                                    "LogOutput",
                                    "org.syncevolution.Server",
                                    "org.syncevolution",
                                    None)

    # start syncevo-dbus-server?
    if options.startSyncEvo:
        path = options.path
        if not path:
            path = os.path.dirname(os.path.dirname(sys.argv[0]))
        Context.dbusserver = os.path.join(path, "libexec/syncevo-dbus-server")
        logger.info("using SyncEvolution installation in %s", path)
        os.environ["SYNCEVOLUTION_BACKEND_DIR"] = os.path.join(path, "lib/syncevolution/backends")
        os.environ["LD_LIBRARY_PATH"] = ":".join((os.path.join(path, "lib/"),
                                                  os.path.join(path, "lib/syncevolution"),
                                                  os.environ.get("LD_LIBRARY_PATH", "")))
        os.environ["PATH"] = ":".join((os.path.join(path, "libexec/"),
                                       os.path.join(path, "bin"),
                                       os.environ.get("PATH", "")))
        os.environ["SYNCEVOLUTION_DATA_DIR"] = os.path.join(path, "share/syncevolution")
        os.environ["SYNCEVOLUTION_XML_CONFIG_DIR"] = os.path.join(path, "share/syncevolution/xml")
        os.environ["SYNCEVOLUTION_TEMPLATE_DIR"] = os.path.join(path, "share/syncevolution/templates")
        # try whether it can be started
        Context.getDBusServer()

    if len(args) != 1:
        logger.error("need exactly on URL as command line parameter")
        exit(1)

    url = urlparse.urlparse(args[0])
    root = resource.Resource()
    root.putChild(url.path[1:], SyncMLPost(url))
    site = server.Site(root)
    if url.scheme == "https":
        if not options.cert:
            logger.error("need server certificate for https")
            exit(1)
        reactor.listenSSL(url.port, site,
                          ChainedOpenSSLContextFactory(options.key or options.cert, options.cert))
    else:
        reactor.listenTCP(url.port, site)
    reactor.run()

if __name__ == '__main__':
    main()
