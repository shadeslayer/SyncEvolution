#! /bin/sh
#
# Wrapper script which starts a new D-Bus session before
# running a program and kills the D-Bus daemon when done.

# start D-Bus session
eval `dbus-launch`
export DBUS_SESSION_BUS_ADDRESS
trap "kill $DBUS_SESSION_BUS_PID" EXIT

# run program
"$@"
