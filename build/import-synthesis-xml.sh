#! /bin/sh
#
# Run this inside the top level of a clean
# syncevolution git repository. Pass the path
# to a synthesis repository (default: ../libsynthesis).
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

`dirname $0`/import-foreign-git.sh "${1:-../libsynthesis}" synthesis src 3 src/sysync_SDK/configs/syncclient_sample_config.xml
