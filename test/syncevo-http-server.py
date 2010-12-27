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
import logging
import logging.config

import twisted.web
import twisted.python.log
from twisted.web import server, resource, http
from twisted.internet import reactor

bus = dbus.SessionBus()
loop = gobject.MainLoop()

# for output from this script itself
logger = logging.getLogger("syncevo-http")

# for output from core SyncEvolution
loggerCore = logging.getLogger("sync")

# cached information about previous POST and reply,
# in case that we need to resend
class OldRequest:
    sessionid = None
    data = None
    reply = None
    type = None

def session_changed(object, ready):
    logger.debug("SessionChanged: %s %s", object, ready)

bus.add_signal_receiver(session_changed,
                        'SessionChanged',
                        'org.syncevolution.Server',
                        'org.syncevolution',
                        None,
                        byte_arrays=True)

class SyncMLSession:
    sessions = []

    def __init__(self):
        self.sessionid = None
        self.request = None
        self.conpath = None
        self.connection = None

    def destruct(self, code, message=""):
        '''Tell both HTTP client and D-Bus server that we are shutting down,
        then remove the session'''
        if self.request:
            self.request.setResponseCode(code, message)
            self.request.finish()
            self.request = None
        if self.connection:
            self.connection.Close(False, message)
            self.connection = None
        if self in SyncMLSession.sessions:
            SyncMLSession.sessions.remove(self)

    def abort(self):
        '''D-Bus server requests to close connection, so cancel everything'''
        logger.debug("connection %s went down", self.conpath)
        self.destruct(http.INTERNAL_SERVER_ERROR, "lost connection to SyncEvolution")

    def reply(self, data, type, meta, final, session):
        '''sent reply to HTTP client and/or close down normally'''
        logger.debug("reply session %s final %s data len %d %s", session, final, len(data), meta)
        # When the D-Bus server sends an empty array, Python binding
        # puts the four chars in 'None' into the data array?!
        if data and len(data) > 0 and data != 'None':
            request = self.request
            self.request = None
            OldRequest.reply = data
            OldRequest.type = type
            if request:
                request.setHeader('Content-Type', type)
                request.setResponseCode(http.OK)
                request.write(data)
                request.finish()
                self.sessionid = session
            else:
                self.connection.Close(False, "could not deliver reply")
                self.connection = None
        if final:
            logger.debug("closing connection for connection %s session %s", self.conpath, session)
            if self.connection:
                self.connection.Close(True, "")
                self.connection = None
            self.destruct(http.GONE, "D-Bus server done")

    def done(self, error):
        '''lost connection to HTTP client, either normally or in error'''
        if error and self.connection:
            self.connection.Close(False, error)
            self.connection = None

    def start(self, request, config, url):
        '''start a new session based on the incoming message'''
        logger.debug("requesting new session")
        self.object = dbus.Interface(bus.get_object('org.syncevolution',
                                                    '/org/syncevolution/Server'),
                                     'org.syncevolution.Server')
        deferred = request.notifyFinish()
        deferred.addCallback(self.done)
        self.conpath = self.object.Connect({'description': 'syncevo-server-http.py',
                                            'transport': 'HTTP',
                                            'config': config,
                                            'URL': url},
                                           True,
                                           '')
        self.connection = dbus.Interface(bus.get_object('org.syncevolution',
                                                        self.conpath),
                                         'org.syncevolution.Connection')

        bus.add_signal_receiver(self.abort,
                                'Abort',
                                'org.syncevolution.Connection',
                                'org.syncevolution',
                                self.conpath,
                                utf8_strings=True,
                                byte_arrays=True)
        bus.add_signal_receiver(self.reply,
                                'Reply',
                                'org.syncevolution.Connection',
                                'org.syncevolution',
                                self.conpath,
                                utf8_strings=True,
                                byte_arrays=True)

        # feed new data into SyncEvolution and wait for reply
        request.content.seek(0, 0)
        self.connection.Process(request.content.read(),
                                request.getHeader('content-type'))
        self.request = request
        SyncMLSession.sessions.append(self)

    def process(self, request, data):
        '''process next message by client in running session'''
        if self.request:
            # message resend?! Ignore old request.
            logger.debug("message resend?!")
            self.request.finish()
            self.request = None
        deferred = request.notifyFinish()
        deferred.addCallback(self.done)
        self.connection.Process(data,
                                request.getHeader('content-type'))
        self.request = request

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
            logger.error("unknown session %s => 404 error", sessionid)
            raise twisted.web.Error(http.NOT_FOUND)


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
    loggerCore.log(evo2python.get(level, logging.ERROR), "%s", output)

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

    # catch output from syncevo-dbus-server
    bus.add_signal_receiver(logSyncEvoOutput,
                            "LogOutput",
                            "org.syncevolution.Server",
                            "org.syncevolution",
                            None)

    if len(args) != 1:
        logger.error("need exactly on URL as command line parameter")
        exit(1)

    url = urlparse.urlparse(args[0])
    root = resource.Resource()
    root.putChild(url.path[1:], SyncMLPost(url))
    site = server.Site(root)
    reactor.listenTCP(url.port, site)
    reactor.run()

if __name__ == '__main__':
    main()
