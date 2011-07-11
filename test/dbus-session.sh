#! /bin/sh
#
# Wrapper script which starts a new D-Bus session before
# running a program and kills the D-Bus daemon when done.

# start D-Bus session
eval `dbus-launch`
export DBUS_SESSION_BUS_ADDRESS
trap "kill $DBUS_SESSION_BUS_PID" EXIT

# Work-around for GNOME keyring daemon not started
# when accessed via org.freedesktop.secrets: start it
# explicitly.
# See https://launchpad.net/bugs/525642 and
# https://bugzilla.redhat.com/show_bug.cgi?id=572137
/usr/bin/gnome-keyring-daemon --start --foreground --components=secrets &
KEYRING_PID=$!
trap "kill $KEYRING_PID" EXIT

# run program
"$@"
