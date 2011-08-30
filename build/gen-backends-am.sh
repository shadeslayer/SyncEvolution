#!/bin/sh

amfile='src/backends/backends.am'
tmpfile="$amfile.$$"

rm -f "$tmpfile"
touch "$tmpfile"

BACKENDS="`echo src/backends/*/configure-sub.in | sed -e 's%/configure-sub\.in%%g' | sort`"
BACKEND_REGISTRIES="`echo src/backends/*/*Register.cpp | sort`"

tf()
{
  echo "$1" >>"$tmpfile"
}

tf '# This is a stupid workaround for an absolute path in SYNCEVOLUTION_LIBS.'
tf '# See AUTOTOOLS-TODO for details.'
tf '@SYNCEVOLUTION_LIBS@: src/syncevo/libsyncevolution.la ; @true'
tf ''
tf "BACKENDS = $BACKENDS"
tf ''
tf "BACKEND_REGISTRIES = $BACKEND_REGISTRIES"
tf ''
tf '# backend includes'

for backend in $BACKENDS
do
  name=`echo "$backend" | sed -e 's%src/backends/%%'`
  tf "include \$(top_srcdir)/$backend/$name.am"
done

if test ! -f "$amfile" || ! cmp -s "$amfile" "$tmpfile"
then
  mv "$tmpfile" "$amfile"
else
  rm -f "$tmpfile"
fi
