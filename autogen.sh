#!/bin/sh

libtoolize -c
automake -a -c
aclocal
autoheader
autoconf
