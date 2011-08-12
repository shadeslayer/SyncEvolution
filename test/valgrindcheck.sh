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

trap "if [ -f $LOGFILE ]; then cat $LOGFILE >&2; rm $LOGFILE; fi" EXIT

killvalgrind () {
    # killall did not always find valgrind when evolution-data-server-2.22 forked?!
    # This will kill *all* running valgrind instances, even those not started
    # by this script. That's better than missing some processes which were started
    # directly or indirectly and now no longer are associated with our process.
    killall -q $1 valgrind
    for i in `ps x | grep -v grep | grep -e " valgrind " -e valgrind.bin | sed -e 's/^ *//' | cut -f1 -d " "`; do kill $1 $i; done
}

( set -x; env GLIBCXX_FORCE_NEW=1 G_SLICE=always-malloc G_DEBUG=gc-friendly valgrind $VALGRIND_ARGS --gen-suppressions=all --log-file=$LOGFILE "$@" ) &
VALGRIND_PID=$!

intvalgrind () {
    kill -INT $VALGRIND_PID
}
termvalgrind () {
    kill -TERM $VALGRIND_PID
}

trap "kill -TERM $VALGRIND_PID" TERM
trap "kill -INT $VALGRIND_PID" INT

wait $VALGRIND_PID
RET=$?
echo valgrindcheck: "$@": returned $RET

# give other valgrind instances some time to settle down, then kill them
sleep 1
killvalgrind -15
# let valgrind chew on leak checking for up to 30 seconds before killing it
# for good
i=0
while ps x | grep -v grep | grep -q -e " valgrind " -e valgrind.bin && [ $i -lt 30 ]; do
    sleep 1
    i=`expr $i + 1`
done
killvalgrind -9
# Filter out leaks in forked processes if VALGRIND_LEAK_CHECK_ONLY_FIRST is set,
# detect if unfiltered errors were reported by valgrind. Unfiltered errors
# are detected because valgrind will produce a suppression for us, which can
# found easily by looking for "insert_a_suppression_name_here".
#
# Here is some example output:
#
# ==13044== Memcheck, a memory error detector
# ==13044== Copyright (C) 2002-2010, and GNU GPL'd, by Julian Seward et al.
# ==13044== Using Valgrind-3.6.0.SVN-Debian and LibVEX; rerun with -h for copyright info
# ==13044== Command: /tmp/test-fork
# ==13044== Parent PID: 13043
# ==13044== 
# ==13044== Conditional jump or move depends on uninitialised value(s)
# ==13044==    at 0x400555: main (test-fork.c:9)
# ==13044== 
# {
#    <insert_a_suppression_name_here>
#    Memcheck:Cond
#    fun:main
# }
# ==13044== 
# ==13044== HEAP SUMMARY:
# ==13044==     in use at exit: 0 bytes in 0 blocks
# ==13044==   total heap usage: 0 allocs, 0 frees, 0 bytes allocated
# ==13044== 
# ==13044== All heap blocks were freed -- no leaks are possible
# ==13044== 
# ==13044== For counts of detected and suppressed errors, rerun with: -v
# ==13044== Use --track-origins=yes to see where uninitialised values come from
# ==13044== ERROR SUMMARY: 1 errors from 1 contexts (suppressed: 4 from 4)
# ==13047== 
# ==13047== HEAP SUMMARY:
# ==13047==     in use at exit: 100 bytes in 1 blocks
# ==13047==   total heap usage: 1 allocs, 0 frees, 100 bytes allocated
# ==13047== 
# ==13047== 100 bytes in 1 blocks are definitely lost in loss record 1 of 1
# ==13047==    at 0x4C244E8: malloc (vg_replace_malloc.c:236)
# ==13047==    by 0x40056E: main (test-fork.c:15)
# ==13047== 
# {
#    <insert_a_suppression_name_here>
#    Memcheck:Leak
#    fun:malloc
#    fun:main
# }
# ==13047== LEAK SUMMARY:
# ==13047==    definitely lost: 100 bytes in 1 blocks
# ==13047==    indirectly lost: 0 bytes in 0 blocks
# ==13047==      possibly lost: 0 bytes in 0 blocks
# ==13047==    still reachable: 0 bytes in 0 blocks
# ==13047==         suppressed: 0 bytes in 0 blocks
# ==13047== 
# ==13047== For counts of detected and suppressed errors, rerun with: -v
# ==13047== ERROR SUMMARY: 1 errors from 1 contexts (suppressed: 4 from 4)
#
# This output is meant to be printed completely when VALGRIND_LEAK_CHECK_ONLY_FIRST
# is not set. Otherwise the "definitely lost" error including its suppression
# shall be filtered out.
#
# 100 is returned by Perl either way because of the Cond error in the main process.
#

perl \
    -e '$onlyfirst = $ENV{VALGRIND_LEAK_CHECK_ONLY_FIRST};' \
    -e '$ret = 0;' \
    -e '$pid = 0;' \
    -e '$skipping = 0;' \
    -e 'while (<>) {' \
    -e '   if (/^==(\d*)==/) {'\
    -e '      $newpid = $1;' \
    -e '   } else {' \
    -e '      $newpid = 0;' \
    -e '   }' \
    -e '   if ($skipping && $pid == $newpid) {' \
    -e '      $skipping = 0;' \
    -e '   }' \
    -e '   if (!$skipping) {' \
    -e '      $pid = $newpid unless $pid;' \
    -e '      if ($onlyfirst && $newpid && $pid != $newpid && /(possibly|indirectly) lost in loss record/) {' \
    -e '         $skipping = 1;' \
    -e '      } else {' \
    -e '         print;' \
    -e '         if (/insert_a_suppression_name_here/) {' \
    -e '            $ret = 100;' \
    -e '         }' \
    -e '      }' \
    -e '   }' \
    -e '}' \
    -e 'exit $ret;' \
    $LOGFILE
SUBRET=$?

# bad valgrind log result overrides successful completion or being killed by SIGTERM (143) or SIGINT (130)
if ( [ $RET -eq 0 ] || [ $RET -eq 130 ] || [ $RET -eq 143 ] ) && [ $SUBRET -ne 0 ]; then
    RET=$SUBRET
    echo valgrindcheck: "$@": log analysis overrides return code with $SUBRET
fi
rm $LOGFILE

echo valgrindcheck: "$@": final result $RET
exit $RET
