#! /bin/sh
#
# Run this inside the top level of a clean
# syncevolution git repository. Pass the path
# to a gdbus repository (default: ../libgdbus).
#
# The script switches to the "gdbus" branch
# in the syncevolution repo and then merges all
# patches committed to the "master" branch in the
# gdbus repo, updating the "gdbus" branch
# as it goes along.
#
# The original commit IDs are recorded
# at the end of each commit message.


set -e
set -x

`dirname $0`/import-foreign-git.sh "${1:-../libgdbus}" gdbus src/gdbus 1 \
    src/debug.c \
    src/debug.h \
    src/gdbus.h \
    src/mainloop.c \
    src/Makefile.am \
    src/object.c \
    src/watch.c
