#! /usr/bin/python

'''Usage: dbus-server-http.py <server name>
Runs a sync session with an HTTP SyncML server
configured in <server name> config.'''

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import gobject
import sys
import httplib, urlparse

DBusGMainLoop(set_as_default=True)

bus = dbus.SessionBus()

object = dbus.Interface(bus.get_object('org.syncevolution',
                                       '/org/syncevolution/Server'),
                        'org.syncevolution.Server')
conpath = object.Connect({'description': 'dbus-server-connection.py',
                          'transport': 'HTTP'},
                         False,
                         0)
connection = dbus.Interface(bus.get_object('org.syncevolution',
                                           conpath),
                            'org.syncevolution.Connection')

loop = gobject.MainLoop()

def Abort():
    print "connection went down"

def Reply(data, type, meta, final, session):
    try:
        if final:
            print "closing connection"
            connection.Close(True, "")
        else:
            print ("send %d bytes of type %s, " % (len(data), type)), meta
            url = urlparse.urlparse(meta["URL"])
        
            if url.scheme == "http":
                conn = httplib.HTTPConnection(url.netloc)
            elif url.scheme == "https":
                conn = httplib.HTTPSConnection(url.netloc)
            else:
                raise "invalid scheme " + url.scheme
            conn.request("POST",
                         url.path,
                         data,
                         {"Content-type": type})
            resp = conn.getresponse()
            reply = resp.read()
            print "received %d bytes of type %s" % (len(data), type)
            replytype = resp.getheader("Content-type")
            connection.Process(reply, replytype)
    except Exception as ex:
        print ex
        loop.quit()

def SessionChanged(object, ready):
    print "SessionChanged:", object, ready
    if not ready:
        loop.quit()

bus.add_signal_receiver(Abort,
                        'Abort',
                        'org.syncevolution.Connection',
                        'org.syncevolution',
                        conpath,
                        byte_arrays=True)
bus.add_signal_receiver(Reply,
                        'Reply',
                        'org.syncevolution.Connection',
                        'org.syncevolution',
                        conpath,
                        byte_arrays=True)
bus.add_signal_receiver(SessionChanged,
                        'SessionChanged',
                        'org.syncevolution.Server',
                        'org.syncevolution',
                        None,
                        byte_arrays=True)

# start a test session with an HTTP server
connection.Process(sys.argv[1], "HTTP Config")
loop.run()
