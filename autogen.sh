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
     src/Makefile-gen.am >src/Makefile.am

sed -e "s;@CONFIG_SUBS@;$SUBS;" \
    Makefile-gen.am >Makefile.am

libtoolize -c
aclocal
autoheader
automake -a -c
autoconf
