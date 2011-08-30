#! /bin/sh

# This scripts takes current version as a parameter, generates the version
# and prints it to standard output - this way it is usable with m4_esyscmd,
# which can be used inside AC_INIT. The script will print the passed version if
# the following checks pass:
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

version="$1"
checksource ()
{
  dir=$1
  force=$2
  dirty=
  if [ ! -d $dir/.git ]; then
    return
  fi

  cur=`pwd`
  cd $dir

  if git status | grep -e "modified:" -e "Untracked files:" -q; then
    dirty='+unclean'
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

versionsuffix=''
syncevo=`checksource .`
if [ "$SYNTHESISSRC" ]; then
    sysync=`checksource $SYNTHESISSRC $syncevo`
fi
# run check again, to get hash when only libsynthesis failed
syncevo=`checksource . $sysync`
if [ "$syncevo" ]; then
    versionsuffix="+SE+$syncevo"
fi
if [ "$sysync" ]; then
    versionsuffix="$versionsuffix+SYSYNC+$sysync"
fi
if [ "$versionsuffix" ]; then
    versionsuffix="+`date +%Y%m%d`$versionsuffix"
fi

# using printf, because echo -n is not portable. hopefully printf is.
printf %s "$version$versionsuffix"
