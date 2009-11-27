#! /bin/sh
#
# This script generates the autotools configure.in and
# Makefile.am files from the information provided by
# SyncEvolution and backends in src/backends. The
# motivation for this non-standard approach was that
# it allows adding new backends without touching core
# files, which should have simplified the development of
# out-of-tree backends. Now git pretty much removes
# the need for such tricks, but it's still around.

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

TEMPLATE_FILES=`cd src && find templates -type f \( -name README -o -name '*.png' -o -name '*.svg' -o -name '*.ini' \)`
TEMPLATE_FILES=`echo $TEMPLATE_FILES`

# create Makefile.am files
sed -e "s;@BACKEND_REGISTRIES@;`echo src/backends/*/*Register.cpp | sed -e s%src/%%g`;" \
    -e "s;@BACKENDS@;$BACKENDS;" \
    -e "s;@TEMPLATE_FILES@;$TEMPLATE_FILES;" \
     src/Makefile-gen.am >src/Makefile.am

sed -e "s;@CONFIG_SUBS@;$SUBS;" \
    Makefile-gen.am >Makefile.am

# create LINGUAS file: every .po is included
(cd po && ls -1 *.po | sort -u | sed -e 's/.po$//' > LINGUAS)
