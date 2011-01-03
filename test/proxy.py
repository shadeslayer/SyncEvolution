from twisted.internet import reactor
from twisted.web import http
from twisted.web.proxy import Proxy, ProxyRequest, ProxyClient, ProxyClientFactory
from twisted.python import log
import sys

class ContentWrapper:
    def __init__(self, content, parent):
        self.content = content
        self.parent = parent

    def seek(self, a, b):
        self.content.seek(a,b)

    def read(self):
        s = self.content.read()
        if True:
            log.msg("interrupt before send")
            # This exception will abort contacting the server and appear as
            # "Unhandled Error" in the proxy log. The proxy client will get
            # an empty reply.
            raise EOFError()
        return s

    def close(self):
        self.content.close()

class MyProxyClient(ProxyClient):
    def __init__(self, command, rest, version, headers, data, father):
        ProxyClient.__init__(self, command, rest, version, headers, data, father)

    def connectionMade(self):
        ProxyClient.connectionMade(self)
        log.msg("message sent")
        # interrupt now before server can reply?
        if self.father.mode == MyProxyRequest.INTERRUPT_AFTER_SEND:
            log.msg("interrupt after sending")
            # finish writing, but never read
            self.transport.loseConnection()
            # Be nice and report a real error back to the proxy client.
            self.father.setResponseCode(501, "Gateway error")
            self.father.responseHeaders.addRawHeader("Content-Type", "text/plain")
            self.father.write("connection intentionally interrupted after sending and before receiving")

class MyProxyClientFactory(ProxyClientFactory):
    protocol = MyProxyClient

    def __init__(self, command, rest, version, headers, data, father):
        ProxyClientFactory.__init__(self, command, rest, version, headers, data, father)

class MyProxyRequest(ProxyRequest):
    protocols = {"http": MyProxyClientFactory}
    INTERRUPT_BEFORE_SEND = 1
    INTERRUPT_AFTER_SEND = 2
    INTERRUPT_AFTER_RECEIVE = 3
    baseport = 10000

    def __init__(self, channel, queued, reactor=reactor):
        ProxyRequest.__init__(self, channel, queued, reactor)
        self.mode = channel.transport.server.port - self.baseport

    def process(self):
        log.msg("mode is", self.mode)

        # override read() method so that we can influence the original
        # process() without having to copy it; just replacing
        # the read method inside the existing content instance
        # would be easier, but turned out to be impossible (read-only
        # attribute)
        if self.mode == self.INTERRUPT_BEFORE_SEND:
            # ContentWrapper will raise exception instead of delivering data
            self.content = ContentWrapper(self.content, self)
        ProxyRequest.process(self)

    def write(self, content):
        log.msg("reply:", content)
        if self.mode == self.INTERRUPT_AFTER_RECEIVE:
            # TODO: suppress original headers
            # Original headers already sent to proxy client, but we
            # can still suppress the actual data and close the
            # connection to simulate a failure.
            log.msg("interrupt after receive")
            ProxyRequest.write(self, "")
            self.transport.loseConnection()
        else:
            ProxyRequest.write(self, content)

class MyProxy(Proxy):
    requestFactory = MyProxyRequest

if __name__ == '__main__':
    log.startLogging(sys.stdout)

    f = http.HTTPFactory()
    f.protocol = MyProxy
    reactor.listenTCP(MyProxyRequest.baseport + 0, f)
    reactor.listenTCP(MyProxyRequest.baseport + MyProxyRequest.INTERRUPT_BEFORE_SEND, f)
    reactor.listenTCP(MyProxyRequest.baseport + MyProxyRequest.INTERRUPT_AFTER_SEND, f)
    reactor.listenTCP(MyProxyRequest.baseport + MyProxyRequest.INTERRUPT_AFTER_RECEIVE, f)

    reactor.run()

