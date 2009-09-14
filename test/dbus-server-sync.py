#! /usr/bin/python

'''Runs a sync.
Usage: dbus-server-sync <server> <mode> <source modes>
<server> - configuration name
<mode> - "", "two-way", "disabled", ...
<source modes> - "{}" or Python hash (like {"addressbook": "refresh-from-server"})
'''

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import gobject
import sys

DBusGMainLoop(set_as_default=True)

bus = dbus.SessionBus()

object = dbus.Interface(bus.get_object('org.syncevolution',
                                       '/org/syncevolution/Server'),
                        'org.syncevolution.Server')

loop = gobject.MainLoop()

sessionpath = None
def SessionChanged(object, ready):
    print "SessionChanged:", object, ready
    if sessionpath == object:
        loop.quit()

bus.add_signal_receiver(SessionChanged,
                        'SessionChanged',
                        'org.syncevolution.Server',
                        'org.syncevolution',
                        None,
                        byte_arrays=True)

dummysessionpath = object.StartSession("")
sessionpath = object.StartSession(sys.argv[1])

# detach from dummy session so that real session can run
session = dbus.Interface(bus.get_object('org.syncevolution',
                                        dummysessionpath),
                         'org.syncevolution.Session')
session.Detach()
session = dbus.Interface(bus.get_object('org.syncevolution',
                                        sessionpath),
                         'org.syncevolution.Session')

print 'session created:', session.GetStatus(), session.GetProgress()
# wait for session ready
loop.run()
print 'session ready:', session.GetStatus(), session.GetProgress()
session.Sync(sys.argv[2], eval(sys.argv[3]))
print 'sync started:', session.GetStatus(), session.GetProgress()
# wait for session done
loop.run()
print 'done:', session.GetStatus(), session.GetProgress()
