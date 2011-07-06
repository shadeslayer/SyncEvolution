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

# Another reason for gen-autotools.sh is that it generates
# the version in the configure script. This cannot be
# done inside the script because autoconf expects a
# literal string, not some kind of variable.
#
# To use the version specified in AC_INIT() unmodified,
# the following checks must pass:
# - SyncEvolution source is clean (git status reports
#   no "modified" files or "untracked" files, or the source
#   is not in git at all)
# - the source is tagged with the version of SyncEvolution
#   (git describe --tags HEAD reports something which matches,
#   for example syncevolution-1-0-beta-2a for 1.0beta2a)
# - same for libsynthesis, if the SYNTHESISSRC env variable
#   is set
#
# If these tests fail, the version is extended:
# +<yyyymmdd>+SE+<status>+SYSYNC+<status>
# <yyyymmdd> = date
# <status> = <hash>[+unclean]
# <hash> = shortened hash from describe (for example, 1040ffd)
# +unclean = source was dirty

set -e

version=`grep '^AC_INIT' configure-pre.in | sed -e 's/.*\[\(.*\)\])/\1/'`
checksource () {
    dir=$1
    force=$2
    dirty=
    if [ ! -d $dir/.git ]; then
        return
    fi

    cur=`pwd`
    cd $dir
    
    if git status | grep -e "modified:" -e "Untracked files:" -q; then
        dirty=+unclean
    fi
    describe=`git describe --tags`
    hash=`cat .git/HEAD | sed -e 's/ref: //'`
    if [ "`echo $hash | sed -e 's/[0-9a-fA-F]//g'`" ] ; then
        # contains other characters than simple hex, probably a reference:
        # convert to abbreviated hash
        hash=`git show-ref --abbrev --hash --verify $hash`
    else
        # already a hash, abbreviate
        hash=`echo $hash | sed -e 's/\(......\).*/\1/'`
    fi
    # detect -<number of changes>-g<hash> suffix added when tag is older than HEAD
    if perl -e "exit !('$describe' =~ m/-[0-9]+-[0-9a-g]{8}\$/);"; then
        # remove suffix to get tag (doesn't matter if we do not pick
        # the most recent one)
        exact=
        tag=`echo $describe | sed -e 's/-[0123456789]*-g.*//'`
    else
        # there is at least one tag matching HEAD;
        # pick the most recent one (based on lexical sorting)
        exact=1
        tag=`git show-ref --tags | grep $hash | sort | tail -1 | sed -e 's;.*refs/tags/;;'`
    fi
    simpletag=$tag
    # Hyphens between numbers in the tag are dots in the version
    # and all other hyphens can be removed.
    while true; do
        tmp=`echo $simpletag | sed -e 's/\([0123456789]\)-\([0123456789]\)/\1.\2/'`
        if [ $tmp = $simpletag ]; then
            break
        else
            simpletag=$tmp
        fi
    done
    simpletag=`echo $simpletag | sed -e 's/-//g'`
    if [ "$dirty" ] || [ "$force" ]; then
        # previous check failed, always print hash
        echo $hash$dirty
    elif [ "$exact" ] &&
        echo $simpletag | grep -q "syncevolution${version}\$"; then
        true
    else
        echo $hash$dirty
    fi
    cd $cur
}

versionsuffix=
syncevo=`checksource .`
if [ "$SYNTHESISSRC" ]; then
    sysync=`checksource $SYNTHESISSRC $syncevo`
fi
# run check again, to get hash when only libsynthesis failed
syncevo=`checksource . $sysync`
if [ "$syncevo" ]; then
    versionsuffix=+SE+$syncevo
fi
if [ "$sysync" ]; then
    versionsuffix=$versionsuffix+SYSYNC+$sysync
fi
if [ "$versionsuffix" ]; then
    versionsuffix=+`date +%Y%m%d`$versionsuffix
fi

# don't touch final output file unless new
# content is different
update () {
    if [ ! "$GEN_AUTOTOOLS_SET_VERSION" ]; then
        # ignore AC_INIT because it
        # contains a version number including git hashes,
        # thus changes on each commit and forces a recompile
        diffargs=--ignore-matching-lines=^AC_INIT
    else
        # a version change due to that must be set up
        # with autogen.sh, which disables this check
        diffargs=
    fi

    if [ -f $1 ] && diff $diffargs $1 $2; then
        rm $2
    else
        echo gen-autotools.sh: $1 updated
        mv $2 $1
    fi
}

# generate configure.in from main configure-*.in pieces
# and all backend configure-sub.in pieces
out=configure.in
tmpfile=configure.in.$$
rm -f $tmpfile
sed -e "s/^\\(AC_INIT.*\\)\\[\\(.*\\)\\]/\\1[\\2$versionsuffix]/" configure-pre.in >>$tmpfile

# Very simplistic detection of pre-releases:
# either the code isn't clean or properly tagged (versionsuffix non-empty)
# or the version contains "99" (part of the rpm-style versioning scheme).
if [ ! "$versionsuffix" ] && ! grep 'AC_INIT' $tmpfile | grep -q 99; then
    perl -pi -e 's/define\(\[STABLE_RELEASE\], \[no\]\)/define([STABLE_RELEASE], [yes])/' $tmpfile
fi

BACKENDS=
SUBS=
for sub in `find -L src -name configure-sub.in`; do
    case $sub in src/backends/*)
        BACKENDS="$BACKENDS `dirname $sub | sed -e 's;^src/;;'`";;
    esac
    SUBS="$SUBS $sub"
    echo "# vvvvvvvvvvvvvv $sub vvvvvvvvvvvvvv" >>$tmpfile
    cat $sub >>$tmpfile
    echo "AC_CONFIG_FILES(`echo $sub | sed -e s/configure-sub.in/Makefile/`)" >>$tmpfile
    echo "# ^^^^^^^^^^^^^^ $sub ^^^^^^^^^^^^^^" >>$tmpfile
    echo >>$tmpfile
done
cat configure-post.in >>$tmpfile
update $out $tmpfile

TEMPLATE_FILES=`cd src && find templates -type f \( -name README -o -name '*.png' -o -name '*.svg' -o -name '*.ini' \) | sort`
TEMPLATE_FILES=`echo $TEMPLATE_FILES`

# create src/Makefile.am file
sed -e "s;@BACKEND_REGISTRIES@;`echo src/backends/*/*Register.cpp | sed -e s%src/%%g`;" \
    -e "s;@BACKENDS@;$BACKENDS;" \
    -e "s;@TEMPLATE_FILES@;$TEMPLATE_FILES;" \
     src/Makefile-gen.am >src/Makefile.am.new.$$
update src/Makefile.am src/Makefile.am.new.$$

# create LINGUAS file: every .po is included
(cd po && ls -1 *.po | sort -u | sed -e 's/.po$//' > LINGUAS.new.$$ && update LINGUAS LINGUAS.new.$$)
