#! /bin/sh
#
# Run this inside the top level of a clean
# syncevolution git repository. Pass the path
# to a synthesis repository (default: ../libsynthesis).
#
# The script switches to the "synthesis-xml-fragments" branch
# in the syncevolution repo and then merges all
# patches committed to the "master" branch in the
# synthesis repo, updating the "synthesis" branch
# as it goes along.
#
# The original commit IDs are recorded
# at the end of each commit message.


set -e
set -x

path="${1:-../libsynthesis}"
files="`cd $path && find src/sysync_SDK/configs/ \( -name '*.xml' -o -name 'update-samples.pl' -o -name README \) -a \! \( -name 'sync*_sample_config.xml' -o -name sunbird_client.xml \)`"

`dirname $0`/import-foreign-git.sh "${1:-../libsynthesis}" synthesis-xml-fragments src/syncevo/configs 3 $files
