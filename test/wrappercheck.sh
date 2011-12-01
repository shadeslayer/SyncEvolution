#! /bin/bash
#
# wrappercheck.sh background command args ... -- command args ...
#
# Wrapper script which runs one command in the background and the
# other in the foreground. Once that second command completes, the
# first one is send a SIGINT, then a SIGTERM until it terminates.
#
# Overall return code of this script is the return code of the
# foreground command or, if that is 0, the background command.

set -e
set -x

PIDS=

trap "kill -TERM $PIDS" TERM
trap "kill -INT $PIDS" INT

declare -a BACKGROUND
while [ $# -gt 1 ] && [ "$1" != "--" ] ; do
    BACKGROUND[${#BACKGROUND[*]}]="$1"
    shift
done
shift

( set +x; echo "*** starting background daemon" )
( set -x; exec "${BACKGROUND[@]}" ) &
BACKGROUND_PID=$!
PIDS+="$BACKGROUND_PID"

set +e
(set -x; "$@")
RET=$?
set -e

( set +x; echo "*** killing and waiting for ${BACKGROUND[0]}" )
kill -INT $BACKGROUND_PID || true
set +e
wait $BACKGROUND_PID
SUBRET=$?
set -e
if [ $RET = 0 ]; then
    RET=$SUBRET
fi

exit $RET
