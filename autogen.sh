#!/bin/sh

set -e

# generate configure.in from main configure-*.in pieces
# and all backend configure-sub.in pieces
rm -f configure.in
cat configure-pre.in >>configure.in
BACKENDS=
SUBS=
for sub in src/backends/*/configure-sub.in; do
    BACKENDS="$BACKENDS `dirname $sub | sed -e 's;^src/;;'`"
    SUBS="$SUBS $sub"
    echo "# vvvvvvvvvvvvvv $sub vvvvvvvvvvvvvv" >>configure.in
    cat $sub >>configure.in
    echo "AC_CONFIG_FILES(`echo $sub | sed -e s/configure-sub.in/Makefile/`)" >>configure.in
    echo "# ^^^^^^^^^^^^^^ $sub ^^^^^^^^^^^^^^" >>configure.in
    echo >>configure.in
done
cat configure-post.in >>configure.in

sed -e "s;@BACKEND_REGISTRIES@;`echo src/backends/*/*Register.cpp | sed -e s%src/%%g`;" \
    -e "s;@BACKENDS@;$BACKENDS;" \
    -e "s;@TEMPLATE_FILES@;`cd src && find default/syncevolution -type f \( -name '*.png' -o -name '*.svg' -o -name '*.ini' \) -printf '%p '`;" \
     src/Makefile-gen.am >src/Makefile.am

sed -e "s;@CONFIG_SUBS@;$SUBS;" \
    Makefile-gen.am >Makefile.am

libtoolize -c
glib-gettextize --force --copy
intltoolize --force --copy --automake
aclocal -I m4
autoheader
automake -a -c -Wno-portability
autoconf

# This hack is required for the autotools on Debian Etch.
# Without it, configure expects a po/Makefile where
# only po/Makefile.in is available. This patch fixes
# configure so that it uses po/Makefile.in, like more
# recent macros do.
perl -pi -e 's;test ! -f "po/Makefile";test ! -f "po/Makefile.in";; s;mv "po/Makefile" "po/Makefile.tmp";cp "po/Makefile.in" "po/Makefile.tmp";;' configure
