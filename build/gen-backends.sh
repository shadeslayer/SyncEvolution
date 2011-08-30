#! /bin/sh
#
# This script prints all configure-sub.in in src/backends
# directory to standard output. This is meant to be used
# from m4_esyscmd inside configure.ac.
#
# The motivation for this non-standard approach was that
# it allows adding new backends without touching core
# files, which should have simplified the development of
# out-of-tree backends. Now git pretty much removes
# the need for such tricks, but it's still around.

tmpfile="configure.in.$$"
rm -f "$tmpfile"

for sub in src/backends/*/configure-sub.in
do
  echo "# vvvvvvvvvvvvvv $sub vvvvvvvvvvvvvv" >>"$tmpfile"
  cat "$sub" >>"$tmpfile"
  echo "# ^^^^^^^^^^^^^^ $sub ^^^^^^^^^^^^^^" >>"$tmpfile"
  echo >>"$tmpfile"
done
cat "$tmpfile"
rm -f "$tmpfile"
