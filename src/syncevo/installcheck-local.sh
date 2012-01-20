#!/bin/sh
#
# usage: PKG_CONFIG_PATH=... installcheck-local.sh <path to syncevo header files> <DESTDIR>

set -ex

TMPFILE=`mktemp`
TMPFILE_CXX=`mktemp`.cxx
TMPFILE_O=`mktemp`.o

rmtmp () {
    rm -f $TMPFILE $TMPFILE_CXX $TMPFILE_O
}
trap rmtmp EXIT

# check that c++ works, whatever it is
cat >$TMPFILE_CXX <<EOF
#include <iostream>

int main(int argc, char **argv)
{
    std::cout << "hello world\n";
    return 0;
}
EOF

for CXX in "c++ -Wall -Werror" "g++ -Wall -Werror" "c++" "g++" ""; do
    if [ ! "$CXX" ]; then
        echo "no usable compiler, skipping tests"
        exit 0
    fi
    if $CXX $TMPFILE_CXX -o $TMPFILE; then
        break
    fi
done

for header in `cd $1 && echo  *`; do
    cat >$TMPFILE_CXX <<EOF
#include <syncevo/$header>

int main(int argc, char **argv)
{
    return 0;
}
EOF
    # header must be usable stand-alone
    $CXX "-I$2" $TMPFILE_CXX -c -o $TMPFILE_O
done

# link once to check that the libs are found;
# must take DESTDIR into account by adding -L<libdir> (skipped when equal to /usr/lib)
# and modifying any additional paths including that
pkg-config --libs syncevolution
env LD_LIBRARY_PATH=$3:$3/syncevolution $LD_LIBRARY_PATH $CXX -v $TMPFILE_O -o $TMPFILE "-L$3" `pkg-config --libs syncevolution | sed -e "s;/usr/lib;$3;g"`
