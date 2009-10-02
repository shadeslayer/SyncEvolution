#!/bin/sh
#
# usage: PKG_CONFIG_PATH=... installcheck-local.sh <path to syncevo header files>

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
    $CXX `pkg-config --cflags syncevolution` $TMPFILE_CXX -c -o $TMPFILE_O
done

# link once to check that the lib is found
$CXX `pkg-config --libs syncevolution` $TMPFILE_O -o $TMPFILE
