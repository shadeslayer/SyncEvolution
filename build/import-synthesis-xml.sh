#! /bin/sh
#
# Run this inside the top level of a clean
# syncevolution git repository. Pass the path
# to a synthesis repository.
#
# The script switches to the "synthesis" branch
# in the syncevolution repo and then merges all
# patches committed to the "master" branch in the
# synthesis repo, updating the "synthesis" branch
# as it goes along.
#
# The original commit IDs are recorded
# at the end of each commit message.

set -e
set -x

SOURCE=src/sysync_SDK/configs/syncclient_sample_config.xml
SOURCE_LEVELS=4
TARGET=src/syncclient_sample_config.xml
TARGET_DIR=`dirname $TARGET`
SYNTHESIS="${1:-../libsynthesis}"
PATCH=`mktemp`
MSG=`mktemp`

git checkout synthesis

# find lastest imported commit
if [ -f $TARGET ]; then
    # 
    revisions="`git log -n 1 $TARGET | tail -1`..master"
else
    # import everything
    revisions=master
fi

count=`(cd "$SYNTHESIS" && git log -p $revisions $SOURCE) | grep '^commit' | wc -l`

# iterate over all commits from oldest to newest
i=1
while [ $i -le $count ]; do
    # get complete patch
    (cd "$SYNTHESIS" && git log -p --max-count=1 --skip=`expr $count - $i` $revisions $SOURCE) >$PATCH
    # get just the commit message
    (cd "$SYNTHESIS" && git log --max-count=1 --skip=`expr $count - $i` $revisions $SOURCE) >$MSG
    # apply patch to file: enter directory and skip pathname from patch
    (cd $TARGET_DIR && patch -p$SOURCE_LEVELS <$PATCH)
    # now commit it (can't use commit because we want to preserve date):
    # - add to index
    git add $TARGET
    # - write index
    id=`git write-tree`
    # - find information for commit and commit
    parent=`git show-ref --hash synthesis`
    origid=`grep ^commit $MSG | sed -e 's/commit //'`
    GIT_AUTHOR_NAME="`grep ^Author: $MSG | sed -e 's/Author: \(.*\) <.*/\1/'`"
    GIT_AUTHOR_EMAIL="`grep ^Author: $MSG | sed -e 's/Author: [^<]*<\([^>]*\)>/\1/'`"
    GIT_AUTHOR_DATE="`grep ^Date: $MSG | sed -e 's/Date: *//'`"
    export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE
    id=`(grep '^ ' $MSG | sed -e 's/^ *//' && echo && echo "libsynthesis commit ID:" && echo $origid) | git commit-tree $id -p $parent`
    # - update branch and check it out
    git update-ref refs/heads/synthesis $id
    git reset --hard synthesis
    # next patch
    i=`expr $i + 1`
done
