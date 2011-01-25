#! /bin/bash
#
# Copyright (C) 2011 Intel Corporation
#
# Does DNS SRV and TXT queries to find the full URL, including method
# (HTTP or HTTPS), host name, port and path (currently hard-coded to
# .well-known/<type>, should use TXT, except that none of the current
# services seem to use that either, so couldn't test).
#
# See http://tools.ietf.org/html/draft-daboo-srv-caldav-10
#
# Works with a variety of underlying utilities:
# - adnshost
# - host
# - nslookup
#
# Usage: syncevo-webdav-lookup.sh carddav|caldav <domain>
# Stdout: http[s]://<domain>:<port>/<path>
# Stderr: error messages indicating failure, empty for success
# Return codes:
# 1 general error
# 2 no DNS utility found
# 3 no result for domain
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301  USA
#

set -o pipefail

TYPE=$1
DOMAIN=$2

# Dump diagnostics on exit, if it still exists.
LOG=`mktemp`
trap "[ -s $LOG ] && ( echo $0 failed to find '$TYPE $DOMAIN':; cat $LOG >&2 ); rm -f $LOG" EXIT

# find one of the supported tools for DNS queries
TOOL=
ALTERNATIVES="adnshost host nslookup"
for i in $ALTERNATIVES; do
    if which $i >/dev/null; then
        TOOL=$i
        break
    fi
done
if [ ! "$TOOL" ]; then
    echo "need one of: $ALTERNATIVES" >&2
    exit 2
fi

# redirect tool errors and commands executed into log;
# try HTTPS first
for type in ${TYPE}s ${TYPE}; do
    (
        case $type in
            *s) METHOD=https;;
            *) METHOD=http;;
        esac
        HOSTNAME=
        PORT=
        # should be looked up via TXT, currently hard-coded
        URLPATH=.well-known/$TYPE

        set -xeo pipefail

        case $TOOL in
            adnshost)
                res=`$TOOL -Fi -tsrv _$type._tcp.$DOMAIN | tee -a $LOG | grep ^_$type._tcp.$DOMAIN`
                # _carddavs._tcp.yahoo.com.cn SRV 1 1 443 carddav.address.yahoo.com misconfig 101 prohibitedcname "DNS alias found where canonical name wanted" ( )
                PORT=`echo $res | sed -e 's;.* SRV [^ ]* [^ ]* \([^ ]*\) \([^ ]*\).*;\1;'`
                HOSTNAME=`echo $res | sed -e 's;.* SRV [^ ]* [^ ]* \([^ ]*\) \([^ ]*\).*;\2;'`
                ;;
            nslookup)
                res=`$TOOL -type=srv _$type._tcp.$DOMAIN | tee -a $LOG | grep "service =" | head -1`
                # _caldavs._tcp.yahoo.com	service = 1 1 443 caldav.calendar.yahoo.com.
                PORT=`echo $res | sed -e 's;.*service = [^ ]* [^ ]* \([^ ]*\) \([^ ]*\)\.;\1;'`
                HOSTNAME=`echo $res | sed -e 's;.*service = [^ ]* [^ ]* \([^ ]*\) \([^ ]*\)\.;\2;'`
                ;;
            host)
                res=`$TOOL -t srv _$type._tcp.$DOMAIN | tee -a $LOG | grep "has SRV record" | head -1`
                # _caldavs._tcp.yahoo.com has SRV record 1 1 443 caldav.calendar.yahoo.com.
                PORT=`echo $res | sed -e 's;.* \([^ ]*\) \([^ ]*\)\.;\1;'`
                HOSTNAME=`echo $res | sed -e 's;.* \([^ ]*\) \([^ ]*\)\.;\2;'`
                ;;
            *)
                echo "unsupported tool $TOOL"
                exit 1
                ;;
        esac

        # print result
        echo $METHOD://$HOSTNAME:$PORT/$URLPATH
    ) 2>>$LOG
    if [ $? -eq 0 ]; then
        # success, discard errrors
        rm -f $LOG
        exit 0
    fi
done

# nothing worked
exit 2
