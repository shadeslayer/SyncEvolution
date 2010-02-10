#! /bin/sh
#
# Run this inside the top level of a clean
# syncevolution git repository. Pass the path
# to a gdbus repository (default: ../libsynthesis).
#
# The script generates .patch files for all changes
# made in the current branch to files which are
# shared with gdbus. The resulting files can
# be imported with "git am".

set -e
set -x

path="${1:-../libsynthesis}"
files="`((cd $path/src/sysync_SDK && find configs \( -name '*.xml' -o -name 'update-samples.pl' -o -name README \) -a \! \( -name 'sync*_sample_config.xml' -o -name sunbird_client.xml \)) && (cd src/syncevo && find configs -name '*.xml' -o name README)) | sort -u | sed -e 's;^;src/syncevo/;'`"

`dirname $0`/export-foreign-git.sh "$path" src/sysync_SDK src/syncevo $files
