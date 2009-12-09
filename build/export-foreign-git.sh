#! /bin/sh
#
# Exports changes made to files so that the upstream
# maintainers can import the changes into their own
# git repo. All changes which are not marked as
# being from the remote git repo with a "commit ID"
# comment are exported.
#
# Result are numbered .patch files, as with "git format-patch".
#
# Run this inside the top level of a clean
# syncevolution git repository with the following
# parameters:
# - file system path for foreign git repository
# - a remote directory into which the source file(s) are to be placed,
#   preserving all remaining directories after stripping
# - common local directory to be stripped from source file(s)
# - one or more source file names, with paths relative to the
#   local repository

set -e
set -x

FOREIGN="$1"
shift
TARGET_DIR="$1"
shift
SOURCE_DIR="$1"
shift
SOURCE="$@"

FOREIGN_NAME=`basename $FOREIGN`

# iterate over commits involving the relevant files,
# starting with oldest one
counter=1
for commit in `(git log --format=format:%H $SOURCE; echo) | perl -e 'print reverse(<>)'`; do
    if git log -n 1 $commit | grep -q "$FOREIGN_NAME commit ID"; then
        # nothing to do, is in original git repo
        true
    else
        file=`printf %03d $counter`-`git log -n 1 --format=format:%f $commit`.patch
        counter=`expr $counter + 1`
        git log -n 1 -p --stat --format=email $commit $SOURCE | perl -p -e "s;$SOURCE_DIR;$TARGET_DIR;g;" -e "s/^index [0-9a-f]*\.\.[0-9a-f]* [0-9]*\n$//;" >$file
    fi
done
