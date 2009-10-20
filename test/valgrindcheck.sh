#! /bin/sh
#
# Wrapper script which runs a command under valgrind control,
# redirects valgrind output into a log file and then
# checks after the run whether valgrind errors or warnings
# were logged.
#
# The environment is set so that GNOME libs and libstdc++ are
# valgrind-friendly (for example, explicit alloc/free instead of
# pooling).
#
# Additional valgrind parameters can be passed in the VALGRIND_ARGS
# environment variable. The log file can be chosen via VALGRIND_LOG,
# with valgrind.<pid of shell>.out as default.


LOGFILE=${VALGRIND_LOG:-valgrind.$$.out}

trap "cat $LOGFILE >&2; rm $LOGFILE" EXIT

killvalgrind () {
    # killall did not always find valgrind when evolution-data-server-2.22 forked?!
    killall -q $1 valgrind
    for i in `ps x | grep " valgrind " | sed -e 's/^ *//' | cut -f1 -d " "`; do kill $1 $i; done
}

( set -x; env GLIBCXX_FORCE_NEW=1 G_SLICE=always-malloc G_DEBUG=gc-friendly valgrind $VALGRIND_ARGS --leak-check=yes --trace-children=no --quiet --gen-suppressions=all --log-file=$LOGFILE "$@" )
RET=$?

# give other valgrind instances some time to settle down, then kill them
sleep 1
killvalgrind -15
sleep 5
killvalgrind -9
if grep '^==[0-9][0-9]*==' $LOGFILE >/dev/null; then
    RET=100
fi

exit $RET
