#!/bin/sh

libtoolize -c
aclocal
autoheader
automake -a -c
autoconf
