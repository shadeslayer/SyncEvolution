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

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import dbus.service
import gobject
import random

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

DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
loop = gobject.MainLoop()
name = dbus.service.BusName("org.freedesktop.Notifications", bus)
# start dummy notification daemon, if possible;
# if it fails, ignore (probably already one running)
notifications = Notifications(bus, "/org/freedesktop/Notifications")

loop.run()
