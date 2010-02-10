#! /bin/sh
#
# Run this inside the top level of a clean
# syncevolution git repository. Pass the path
# to a gdbus repository (default: ../libgdbus).
#
# The script generates .patch files for all changes
# made in the current branch to files which are
# shared with gdbus. The resulting files can
# be imported with "git am".

set -e
set -x

`dirname $0`/export-foreign-git.sh "${1:-../libgdbus}" src src/gdbus \
    src/gdbus/debug.c \
    src/gdbus/debug.h \
    src/gdbus/gdbus.h \
    src/gdbus/mainloop.c \
    src/gdbus/object.c \
    src/gdbus/watch.c
