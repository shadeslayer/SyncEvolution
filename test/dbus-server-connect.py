#! /usr/bin/python

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import gobject

DBusGMainLoop(set_as_default=True)

bus = dbus.SessionBus()

object = dbus.Interface(bus.get_object('org.syncevolution',
                                       '/org/syncevolution/Server'),
                        'org.syncevolution.Server')

conpath = object.Connect({'description': 'dbus-server-connection.py',
                          'transport': 'dummy'},
                         False,
                         0)
print conpath

connection = dbus.Interface(bus.get_object('org.syncevolution',
                                           conpath),
                            'org.syncevolution.Connection')
connection.Close(False, 'die, connection, die')


loop = gobject.MainLoop()

conpath = object.Connect({'description': 'dbus-server-connection.py',
                          'transport': 'dummy'},
                         False,
                         0)
connection = dbus.Interface(bus.get_object('org.syncevolution',
                                           conpath),
                            'org.syncevolution.Connection')

def Reply(data, type, meta, final, session):
    print "Reply:", data, type, meta, final, session
    connection.Close(True, '')

sessionpath = None
def SessionChanged(object, ready):
    print "SessionChanged:", object, ready
    if not ready or sessionpath == object:
        loop.quit()

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
connection.Process([ 1, 2, 3, 4 ], "dummy message type")
loop.run()

sessionpath = object.StartSession('no_such_server')
session = dbus.Interface(bus.get_object('org.syncevolution',
                                        sessionpath),
                         'org.syncevolution.Session')
# wait for session ready
loop.run()
session.Close()
# wait for session gone
loop.run()
