#! /bin/sh
#
# Run this inside the top level of a clean
# syncevolution git repository with the following
# parameters:
# - file system path for foreign git repository
# - name of local branch for importing changes
# - a local directory into which the source file(s) is to be placed,
#   preserving all remaining directories after stripping
# - number of directory levels to strip from source file(s)
# - one or more source file names, with paths relative to the
#   foreign repository

set -e
set -x

FOREIGN="$1"
shift
TARGET_BRANCH="$1"
shift
TARGET_DIR="$1"
shift
SOURCE_LEVELS="$1"
shift
SOURCE="$@"

FOREIGN_NAME=`basename $FOREIGN`
TARGET=`for i in $SOURCE; do echo $i | perl -p -e "s;([^/]*/){$SOURCE_LEVELS};$TARGET_DIR/;"; done`
PATCH=`mktemp`
MSG=`mktemp`

git checkout $TARGET_BRANCH

# find lastest imported commit:
# import everything unless one of the files already exists,
# in which case we assume that all of the others also exist
revisions=master
for i in $TARGET; do 
    if [ -f $i ]; then
        revisions="`git log -n 1 -- $TARGET | tail -1`..master"
        break
    fi
done

count=`(cd "$FOREIGN" && git log -p $revisions -- $SOURCE) | grep '^commit' | wc -l`

# iterate over all commits from oldest to newest
i=1
while [ $i -le $count ]; do
    # get complete patch
    (cd "$FOREIGN" && git log -p --max-count=1 --skip=`expr $count - $i` $revisions -- $SOURCE) >$PATCH
    # get just the commit message
    (cd "$FOREIGN" && git log --max-count=1 --skip=`expr $count - $i` $revisions -- $SOURCE) >$MSG
    # apply patch to file: enter directory and skip pathname from patch
    if ! (cd $TARGET_DIR && patch -p`expr $SOURCE_LEVELS + 1` <$PATCH); then
        echo "patch failed in $TARGET_DIR: patch -p`expr $SOURCE_LEVELS + 1` <$PATCH"
        echo "continue? yes/no [no]"
        read yesno
        if [ "$yesno" != "yes" ]; then
            exit 1
        fi
    fi

    # now commit it (can't use commit because we want to preserve date):
    # - add to index
    for t in $TARGET; do
        [ -f $t ] && git add $t
    done
    # - write index
    id=`git write-tree`
    # - find information for commit and commit
    parent=`git show-ref --heads --hash $TARGET_BRANCH`
    origid=`grep ^commit $MSG | sed -e 's/commit //'`
    GIT_AUTHOR_NAME="`grep ^Author: $MSG | sed -e 's/Author: \(.*\) <.*/\1/'`"
    GIT_AUTHOR_EMAIL="`grep ^Author: $MSG | sed -e 's/Author: [^<]*<\([^>]*\)>/\1/'`"
    GIT_AUTHOR_DATE="`grep ^Date: $MSG | sed -e 's/Date: *//'`"
    export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE
    id=`(grep '^ ' $MSG | sed -e 's/^ *//' && echo && echo "$FOREIGN_NAME commit ID:" && echo $origid) | git commit-tree $id -p $parent`
    # - update branch and check it out
    git update-ref refs/heads/$TARGET_BRANCH $id
    git reset --hard $TARGET_BRANCH
    # next patch
    i=`expr $i + 1`
done
