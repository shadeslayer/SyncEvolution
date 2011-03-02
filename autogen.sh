#!/bin/sh

set -e

# wipe out temporary autotools files, necessary
# when switching between distros
rm -rf aclocal.m4 m4 autom4te.cache config.guess config.sub config.h.in configure depcomp install-sh ltmain.sh missing 

# intltoolize fails to copy its macros unless m4 exits
mkdir m4

env GEN_AUTOTOOLS_SET_VERSION=1 sh ./gen-autotools.sh

libtoolize -c
glib-gettextize --force --copy
intltoolize --force --copy --automake
aclocal -I m4 -I m4-repo
autoheader
automake -a -c -Wno-portability
autoconf

# This hack is required for the autotools on Debian Etch.
# Without it, configure expects a po/Makefile where
# only po/Makefile.in is available. This patch fixes
# configure so that it uses po/Makefile.in, like more
# recent macros do.
perl -pi -e 's;test ! -f "po/Makefile";test ! -f "po/Makefile.in";; s;mv "po/Makefile" "po/Makefile.tmp";cp "po/Makefile.in" "po/Makefile.tmp";;' configure
