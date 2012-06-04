#! /usr/bin/env perl
#
# Copyright (C) 2008 Funambol, Inc.
# Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
# Copyright (C) 2009 Intel Corporation
#
# Usage: <file>
#        <left file> <right file>
# Either normalizes a file or compares two of them in a side-by-side
# diff.
#
# Checks environment variables:
#
# CLIENT_TEST_SERVER=funambol|scheduleworld|egroupware|synthesis
#       Enables code which simplifies the text files just like
#       certain well-known servers do. This is useful for testing
#       to ignore the data loss introduced by these servers or (for
#       users) to simulate the effect of these servers on their data.
#
# CLIENT_TEST_CLIENT=evolution|addressbook (Mac OS X/iPhone)
#       Same as for servers this replicates the effect of storing
#       data in the clients.
#
# CLIENT_TEST_LEFT_NAME="before sync"
# CLIENT_TEST_RIGHT_NAME="after sync"
# CLIENT_TEST_REMOVED="removed during sync"
# CLIENT_TEST_ADDED="added during sync"
#       Setting these variables changes the default legend
#       print above the left and right file during a
#       comparison.
#
# CLIENT_TEST_COMPARISON_FAILED=1
#       Overrides the default error code when changes are found.
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


use strict;

# Various crashes have been encountered in the Perl interpreter
# executable when enabling UTF-8. It is only needed for nicer
# side-by-side comparison of changes (correct column width),
# so not much functionality is lost by disabling this.
# use encoding 'utf8';

# Instead enable writing the result as UTF-8. Input
# files are read as UTF-8 via PerlIO parameters in open().
binmode(STDOUT, ":utf8");

use Algorithm::Diff;
use MIME::Base64;
use Digest::MD5 qw(md5 md5_hex md5_base64);

# ignore differences caused by specific servers or local backends?
my $server = $ENV{CLIENT_TEST_SERVER};
my $client = $ENV{CLIENT_TEST_CLIENT} || "evolution";
my $scheduleworld = $server =~ /scheduleworld/;
my $synthesis = $server =~ /synthesis/;
my $zyb = $server =~ /zyb/;
my $mobical = $server =~ /mobical/;
my $memotoo = $server =~ /memotoo/;
my $nokia_7210c = $server =~ /nokia_7210c/;
my $ovi = $server =~ /Ovi/;
my $unique_uid = $ENV{CLIENT_TEST_UNIQUE_UID};
my $full_timezones = $ENV{CLIENT_TEST_FULL_TIMEZONES}; # do not simplify VTIMEZONE definitions

# TODO: this hack ensures that any synchronization is limited to
# properties supported by Synthesis. Remove this again.
# $synthesis = 1;

my $exchange = $server =~ /exchange/; # Exchange via ActiveSync
my $egroupware = $server =~ /egroupware/;
my $funambol = $server =~ /funambol/;
my $googlesyncml = $server eq "google";
my $googlecaldav = $server eq "googlecalendar";
my $googleeas = $server eq "googleeas";
my $google_valarm = $ENV{CLIENT_TEST_GOOGLE_VALARM};
my $yahoo = $server =~ /yahoo/;
my $davical = $server =~ /davical/;
my $apple = $server =~ /apple/;
my $oracle = $server =~ /oracle/;
my $radicale = $server =~ /radicale/;
my $evolution = $client =~ /evolution/;
my $addressbook = $client =~ /addressbook/;

sub Usage {
  print "$0 <vcards.vcf\n";
  print "   normalizes one file (stdin or single argument), prints to stdout\n";
  print "$0 vcards1.vcf vcards2.vcf\n";
  print "   compares the two files\n";
  print "Also works for iCalendar files.\n";
}

sub uppercase {
  my $text = shift;
  $text =~ tr/a-z/A-Z/;
  return $text;
}

sub sortlist {
  my $list = shift;
  return join(",", sort(split(/,/, $list)));
}

sub splitvalue {
  my $prop = shift;
  my $values = shift;
  my $eol = shift;

  my @res = ();
  foreach my $val (split (/;/, $values)) {
      push(@res, $prop, ":", $val, $eol);
  }
  return join("", @res);
}

# normalize the DATE-TIME duration unless the VALUE isn't a duration
sub NormalizeTrigger {
    my $value = shift;
    $value =~ /([+-]?)P(?:(\d*)D)?T(?:(\d*)H)?(?:(\d*)M)?(?:(\d*)S)?/;
    my ($sign, $days, $hours, $minutes, $seconds) = ($1, int($2), int($3), int($4), int($5));
    while ($seconds >= 60) {
        $minutes++;
        $seconds -= 60;
    }
    while ($minutes >= 60) {
        $hours++;
        $minutes -= 60;
    }
    while ($hours >= 24) {
        $days++;
        $hours -= 24;
    }
    $value = $sign;
    $value .= ($days . "D") if $days;
    $value .= ($hours . "H") if $hours;
    $value .= ($minutes . "M") if $minutes;
    $value .= ($seconds . "S") if $seconds;
    return $value;
}

# decode base64 string, return size and hash
sub describeBase64 {
    my $data = decode_base64($1);
    return sprintf("%d b64 characters = %d bytes, %s md5sum", length($1), length($data), md5_hex($data));
}

# called for one VCALENDAR (with single VEVENT/VTODO/VJOURNAL) or VCARD,
# returns normalized one
sub NormalizeItem {
    my $width = shift;
    $_ = shift;

    # undo line continuation
    s/\n\s//gs;
    # ignore charset specifications, assume UTF-8
    s/;CHARSET="?UTF-8"?//g;

    # UID may differ, but only in vCards and journal entries:
    # in calendar events the UID needs to be preserved to handle
    # meeting invitations/replies correctly
    s/((VCARD|VJOURNAL).*)^UID:[^\n]*\n/$1/msg;

    # intentional changes to UID are acceptable when running with CLIENT_TEST_UNIQUE_UID
    if ($unique_uid) {
        s/UID:UNIQUE-UID-\d+-/UID:/g;
    }

    # merge all CATEGORIES properties into one comma-separated one
    while ( s/^CATEGORIES:([^\n]*)\n(.*)^CATEGORIES:([^\n]*)\n/CATEGORIES:$1,$3\n$2/ms ) {}

    # exact order of categories is irrelevant
    s/^CATEGORIES:(\S+)/"CATEGORIES:" . sortlist($1)/mge;

    # expand <foo> shortcuts to TYPE=<foo>
    while (s/^(ADR|EMAIL|TEL)([^:\n]*);(HOME|OTHER|WORK|PARCEL|INTERNET|CAR|VOICE|CELL|PAGER)/$1;TYPE=$3/mg) {}

    # the distinction between an empty and a missing property
    # is vague and handled differently, so ignore empty properties
    s/^[^:\n]*:;*\n//mg;

    # use separate TYPE= fields
    while( s/^(\w*[^:\n]*);TYPE=(\w*),(\w*)/$1;TYPE=$2;TYPE=$3/mg ) {}

    # make TYPE uppercase (in vCard 3.0 at least those parameters are case-insensitive)
    while( s/^(\w*[^:\n]*);TYPE=(\w*?[a-z]\w*?)([;:])/ $1 . ";TYPE=" . uppercase($2) . $3 /mge ) {}

    # replace parameters with a sorted parameter list
    s!^([^;:\n]*);(.*?):!$1 . ";" . join(';',sort(split(/;/, $2))) . ":"!meg;

    # EXDATE;VALUE=DATE is the default, no need to show it
    s/^EXDATE;VALUE=DATE:/EXDATE:/mg;

    # default opacity is OPAQUE
    s/^TRANSP:OPAQUE\r?\n?//gm;

    # multiple EXDATEs may be joined into one, use separate properties as normal form
    s/^(EXDATE[^:]*):(.*)(\r?\n)/splitvalue($1, $2, $3)/mge;

    # sort value lists of specific properties
    s!^(RRULE.*):(.*)!$1 . ":" . join(';',sort(split(/;/, $2)))!meg;

    # INTERVAL=1 is the default and thus can be removed
    s/^RRULE(.*?);INTERVAL=1(;|$)/RRULE$1$2/mg;

    # Ignore remaining "other" email, address and telephone type - this is
    # an Evolution specific extension which might not be preserved.
    s/^(ADR|EMAIL|TEL)([^:\n]*);TYPE=OTHER/$1$2/mg;
    # TYPE=PREF on the other hand is not used by Evolution, but
    # might be sent back.
    s/^(ADR|EMAIL)([^:\n]*);TYPE=PREF/$1$2/mg;
    # Evolution does not need TYPE=INTERNET for email
    s/^(EMAIL)([^:\n]*);TYPE=INTERNET/$1$2/mg;
    # ignore TYPE=PREF in address, does not matter in Evolution
    s/^((ADR|LABEL)[^:\n]*);TYPE=PREF/$1/mg;
    # ignore extra separators in multi-value fields
    s/^((ORG|N|(ADR[^:\n]*?)):.*?);*$/$1/mg;
    # the type of certain fields is ignore by Evolution
    s/^X-(AIM|GROUPWISE|ICQ|YAHOO);TYPE=HOME/X-$1/gm;
    # Evolution ignores an additional pager type
    s/^TEL;TYPE=PAGER;TYPE=WORK/TEL;TYPE=PAGER/gm;
    # PAGER property is sent by Evolution, but otherwise ignored
    s/^LABEL[;:].*\n//mg;
    # TYPE=VOICE is the default in Evolution and may or may not appear in the vcard;
    # this simplification is a bit too agressive and hides the problematic
    # TYPE=PREF,VOICE combination which Evolution does not handle :-/
    s/^TEL([^:\n]*);TYPE=VOICE,([^:\n]*):/TEL$1;TYPE=$2:/mg;
    s/^TEL([^:\n]*);TYPE=([^;:\n]*),VOICE([^:\n]*):/TEL$1;TYPE=$2$3:/mg;
    s/^TEL([^:\n]*);TYPE=VOICE([^:\n]*):/TEL$1$2:/mg;
    # don't care about the TYPE property of PHOTOs
    s/^PHOTO;(.*)TYPE=[A-Z]*/PHOTO;$1/mg;
    # encoding is not case sensitive, skip white space in the middle of binary data
    if (s/^PHOTO;.*?ENCODING=(b|B|BASE64).*?:\s*/PHOTO;ENCODING=B: /mgi) {
        if ($memotoo) {
            # transcodes image data, can't compare it
            s/(^PHOTO.*:).*/$1<stripped by synccompare>/mg;
        } else {
            while (s/^PHOTO(.*?): (\S+)[\t ]+(\S+)/PHOTO$1: $2$3/mg) {}
        }
    }
    # Don't show base64 encoded PHOTO data (makes diff very long). Instead
    # decode and show size + hash.
    s/^PHOTO;ENCODING=B: (.*)$/"PHOTO: " . describeBase64($1)/mge;
    # special case for the inlining of the local test case PHOTO
    s!^PHOTO;;VALUE=uri:file://testcases/local.png$!PHOTO;;VALUE=uri:<local.png>!m;
    s!^PHOTO;ENCODING=B: iVBORw0KGgoAAAANSUh.*UQOVkeH/aKBSLM04QlMqAAFNBTl\+CjN9AAAAAElFTkSuQmCC$!PHOTO;;VALUE=uri:<local.png>!m;
    # ignore extra day factor in front of weekday
    s/^RRULE:(.*)BYDAY=\+?1(\D)/RRULE:$1BYDAY=$2/mg;
    # remove default VALUE=DATE-TIME
    s/^(DTSTART|DTEND)([^:\n]*);VALUE=DATE-TIME/$1$2/mg;

    # remove default LANGUAGE=en-US
    s/^([^:\n]*);LANGUAGE=en-US/$1/mg;

    # normalize values which look like a date to YYYYMMDD because the hyphen is optional
    s/:(\d{4})-(\d{2})-(\d{2})/:$1$2$3/g;

    # mailto is case insensitive
    s/^((ATTENDEE|ORGANIZER).*):[Mm][Aa][Ii][Ll][Tt][Oo]:/$1:mailto:/mg;

    # remove fields which may differ
    s/^(PRODID|CREATED|DTSTAMP|LAST-MODIFIED|REV)(;X-VOBJ-FLOATINGTIME-ALLOWED=(TRUE|FALSE))?:.*\r?\n?//gm;
    # remove optional fields
    s/^(METHOD|X-WSS-[A-Z]*|X-WR-[A-Z]*|CALSCALE):.*\r?\n?//gm;

    # trailing line break(s) in a DESCRIPTION may or may not be
    # removed or added by servers
    s/^DESCRIPTION:(.*?)(\\n)+$/DESCRIPTION:$1/gm;

    # use the shorter property name when there are alternatives,
    # but avoid duplicates
    foreach my $i ("SPOUSE", "MANAGER", "ASSISTANT", "ANNIVERSARY") {
        if (/^X-\Q$i\E:(.*?)$/m) {
            s/^X-EVOLUTION-\Q$i\E:\Q$1\E\n//m;
        }
    }
    s/^X-EVOLUTION-(SPOUSE|MANAGER|ASSISTANT|ANNIVERSARY)/X-$1/gm;

    # some properties are always lost because we don't transmit them
    if ($ENV{CLIENT_TEST_SERVER}) {
        s/^(X-FOOBAR-EXTENSION|X-TEST)(;[^:;\n]*)*:.*\r?\n?//gm;
    }

    # if there is no DESCRIPTION in a VJOURNAL, then use the
    # summary: that's what is done when exchanging such a
    # VJOURNAL as plain text
    if (/^BEGIN:VJOURNAL$/m && !/^DESCRIPTION/m) {
        s/^SUMMARY:(.*)$/SUMMARY:$1\nDESCRIPTION:$1/m;
    }

    # strip configurable X- parameters or properties
    my $strip = $ENV{CLIENT_TEST_STRIP_PROPERTIES};
    if ($strip) {
        s/^$strip(;[^:;\n]*)*:.*\r?\n?//gm;
    }
    $strip = $ENV{CLIENT_TEST_STRIP_PARAMETERS};
    if ($strip) {
        while (s/^(\w+)([^:\n]*);$strip=\d+/$1$2/mg) {}
    }

    # strip redundant VTIMEZONE definitions (happen to be
    # added by Google CalDAV server when storing an all-day event
    # which doesn't need any time zone definition)
    # http://code.google.com/p/google-caldav-issues/issues/detail?id=63
    while (m/(BEGIN:VTIMEZONE.*?TZID:([^\n]*)\n.*?END:VTIMEZONE\n)/gs) {
        my $def = $1;
        my $tzid = $2;
        # used as parameter?
        if (! m/;TZID="?\Q$tzid\E"?/) {
            # no, remove definition
            s!\Q$def\E!!s;
        }
    }

    if (!$full_timezones) {
        # Strip trailing digits from TZID. They are appended by
        # Evolution and SyncEvolution to distinguish VTIMEZONE
        # definitions which have the same TZID, but different rules.
        s/(^TZID:|;TZID=)([^;:]*?) \d+/$1$2/gm;

        # Strip trailing -(Standard) from TZID. Evolution 2.24.5 adds
        # that (not sure exactly where that comes from).
        s/(^TZID:|;TZID=)([^;:]*?)-\(Standard\)/$1$2/gm;

        # VTIMEZONE and TZID do not have to be preserved verbatim as long
        # as the replacement is still representing the same timezone.
        # Reduce TZIDs which specify a proper location
        # to their location part and strip the VTIMEZONE - makes the
        # diff shorter, too.
        my $location = "[^\n]*((?:Africa|America|Antarctica|Arctic|Asia|Atlantic|Australia|Brazil|Canada|Chile|Egypt|Eire|Europe|Hongkong|Iceland|India|Iran|Israel|Jamaica|Japan|Kwajalein|Libya|Mexico|Mideast|Navajo|Pacific|Poland|Portugal|Singapore|Turkey|Zulu)[-a-zA-Z0-9_/]*)";
        s;^BEGIN:VTIMEZONE.*?^TZID:$location.*^END:VTIMEZONE;BEGIN:VTIMEZONE\n  TZID:$1 [...]\nEND:VTIMEZONE;gms;
        s;TZID="?$location"?;TZID=$1;gm;
    }

    # normalize iCalendar 2.0
    if (/^BEGIN:(VEVENT|VTODO|VJOURNAL)$/m) {
        # CLASS=PUBLIC is the default, no need to show it
        s/^CLASS:PUBLIC\r?\n//m;
        # RELATED=START is the default behavior
        s/^TRIGGER([^\n:]*);RELATED=START/TRIGGER$1/mg;
        # VALUE=DURATION is the default behavior
        s/^TRIGGER([^\n:]*);VALUE=DURATION/TRIGGER$1/mg;
        s/^(TRIGGER.*):(\S*)/$1 . ":" . NormalizeTrigger($2)/mge;
    }

    # Added by EDS >= 2.32, presumably to cache some internal computation.
    # Because it can be recreated, it doesn't have to be preserved during
    # sync and such changes can be ignored:
    #
    # RRULE:BYDAY=SU;COUNT=10;FREQ=WEEKLY  |   RRULE;X-EVOLUTION-ENDDATE=20080608T 
    #                                      >    070000Z:BYDAY=SU;COUNT=10;FREQ=WEEK
    #                                      >    LY                                 
    s/^(\w+)([^:\n]*);X-EVOLUTION-ENDDATE=[0-9TZ]*/$1$2/mg;

    if ($scheduleworld || $egroupware || $synthesis || $addressbook || $funambol ||$googlesyncml || $googleeas || $mobical || $memotoo) {
      # does not preserve X-EVOLUTION-UI-SLOT=
      s/^(\w+)([^:\n]*);X-EVOLUTION-UI-SLOT=\d+/$1$2/mg;
    }

    if ($scheduleworld) {
      # cannot distinguish EMAIL types
      s/^EMAIL;TYPE=\w*/EMAIL/mg;
      # replaces certain TZIDs with more up-to-date ones
      s;TZID(=|:)/(scheduleworld.com|softwarestudio.org)/Olson_\d+_\d+/;TZID$1/foo.com/Olson_20000101_1/;mg;
    }

    if ($synthesis || $mobical) {
      # only preserves ORG "Company", but loses "Department" and "Office"
      s/^ORG:([^;:\n]+)(;[^\n]*)/ORG:$1/mg;
    }

    if ($funambol) {
      # only preserves ORG "Company";"Department", but loses "Office"
      s/^ORG:([^;:\n]+)(;[^;:\n]*)(;[^\n]*)/ORG:$1$2/mg;
      # drops the second address line
      s/^ADR(.*?):([^;]*?);[^;]*?;/ADR$1:$2;;/mg;
      # has no concept of "preferred" phone number
      s/^(TEL.*);TYPE=PREF/$1/mg;
    }

   if($googlesyncml) {
      # ignore the PHOTO encoding data 
      s/^PHOTO(.*?): .*\n/^PHOTO$1: [...]\n/mg; 
      # FN propertiey is not correct 
      s/^FN:.*\n/FN$1: [...]\n/mg;
      # Not support car type in telephone
      s!^TEL\;TYPE=CAR(.*)\n!TEL$1\n!mg;
      # some properties are lost
      s/^(X-EVOLUTION-FILE-AS|NICKNAME|BDAY|CATEGORIES|CALURI|FBURL|ROLE|URL|X-AIM|X-EVOLUTION-UI-SLOT|X-ANNIVERSARY|X-ASSISTANT|X-EVOLUTION-BLOG-URL|X-EVOLUTION-VIDEO-URL|X-GROUPWISE|X-ICQ|X-GADUGADU|X-JABBER|X-MSN|X-SIP|X-SKYPE|X-MANAGER|X-SPOUSE|X-MOZILLA-HTML|X-YAHOO)(;[^:;\n]*)*:.*\r?\n?//gm;
   }

   if ($googlecaldav) {
      #several properties are not preserved by Google in icalendar2.0 format
      s/^(SEQUENCE|X-EVOLUTION-ALARM-UID)(;[^:;\n]*)*:.*\r?\n?//gm;

      # Google adds calendar owner as attendee of meetings, regardless
      # whether it was on the original attendee list. Ignore this
      # during testing by removing all attendees with @googlemail.com
      # email address.
      s/^ATTENDEE.*googlemail.com\r?\n//gm;
    }

    if ($apple) {
        # remove some parameters added by Apple Calendar server in CalDAV
        s/^(ORGANIZER[^:]*);SCHEDULE-AGENT=NONE/$1/gm;
        s/^(ORGANIZER[^:]*);SCHEDULE-STATUS=5.3/$1/gm;
        # seems to require a fixed number of recurrences; hmm, okay...
        s/^RRULE:COUNT=400;FREQ=DAILY/RRULE:FREQ=DAILY/gm;
    }

    if ($oracle) {
        # remove extensions added by server
        s/^(X-S1CS-RECURRENCE-COUNT)(;[^:;\n]*)*:.*\r?\n?//gm;
        # ignore loss of LANGUAGE=xxx property in ATTENDEE
        s/^ATTENDEE([^\n:]*);LANGUAGE=([^\n;:]*)/ATTENDEE$1/mg;
    }

    if ($radicale) {
        # remove extensions added by server
        s/^(X-RADICALE-NAME)(;[^:;\n]*)*:.*\r?\n?//gm;
    }

    if ($googlecaldav || $yahoo) {
      # default status is CONFIRMED
      s/^STATUS:CONFIRMED\r?\n?//gm;
    }

    # Google randomly (?!) adds a standard alarm to events.
    if ($google_valarm) {
        s/BEGIN:VALARM\nDESCRIPTION:This is an event reminder\nACTION:DISPLAY\nTRIGGER:-PT10M\n(X-KDE-KCALCORE-ENABLED:TRUE\n)END:VALARM\n//s;
    }

    if ($yahoo) {
        s/^(X-MICROSOFT-[-A-Z0-9]*)(;[^:;\n]*)*:.*\r?\n?//gm;
        # some properties cannot be stored
        s/^(FN)(;[^:;\n]*)*:.*\r?\n?//gm;
    }

    if ($addressbook) {
      # some properties cannot be stored
      s/^(X-MOZILLA-HTML|X-EVOLUTION-FILE-AS|X-EVOLUTION-ANNIVERSARY|X-EVOLUTION-BLOG-URL|X-EVOLUTION-VIDEO-URL|X-GROUPWISE|ROLE|CATEGORIES|FBURL|CALURI|FN)(;[^:;\n]*)*:.*\r?\n?//gm;
      # only some parts of ADR are preserved
      my $type;
      s/^ADR(.*?)\:(.*)/$type=($1 || ""); @_ = split(\/(?<!\\);\/, $2); "ADR:;;" . ($_[2] || "") . ";" . ($_[3] || "") . ";" . ($_[4] || "") . ";" . ($_[5] || "") . ";" . ($_[6] || "")/gme;
      # TYPE=CAR not supported
      s/;TYPE=CAR//g;
    }

    if ($synthesis) {
      # does not preserve certain properties
      s/^(FN|BDAY|X-MOZILLA-HTML|X-EVOLUTION-FILE-AS|X-AIM|NICKNAME|UID|PHOTO|CALURI|SEQUENCE|TRANSP|ORGANIZER|ROLE|FBURL|X-ANNIVERSARY|X-ASSISTANT|X-EVOLUTION-BLOG-URL|X-EVOLUTION-VIDEO-URL|X-GADUGADU|X-GROUPWISE|X-ICQ|X-JABBER|X-MANAGER|X-MSN|X-SIP|X-SKYPE|X-SPOUSE|X-YAHOO)(;[^:;\n]*)*:.*\r?\n?//gm;
      # default ADR is HOME
      s/^ADR;TYPE=HOME/ADR/gm;
      # only some parts of N are preserved
      s/^N((?:;[^;:]*)*)\:(.*)/@_ = split(\/(?<!\\);\/, $2); "N$1:$_[0];" . ($_[1] || "") . ";;" . ($_[3] || "")/gme;
      # breaks lines at semicolons, which adds white space
      while( s/^ADR:(.*); +/ADR:$1;/gm ) {}
      # no attributes stored for ATTENDEEs
      s/^ATTENDEE;.*?:/ATTENDEE:/msg;
    }

    if ($synthesis) {
      # VALARM not supported
      s/^BEGIN:VALARM.*?END:VALARM\r?\n?//msg;
    }

    if ($egroupware) {
      # CLASS:PUBLIC is added if none exists (as in our test cases),
      # several properties not preserved
      s/^(BDAY|CATEGORIES|FBURL|PHOTO|FN|X-[A-Z-]*|CALURI|CLASS|NICKNAME|UID|TRANSP|PRIORITY|SEQUENCE)(;[^:;\n]*)*:.*\r?\n?//gm;
      # org gets truncated
      s/^ORG:([^;:\n]*);.*/ORG:$1/gm;
    }

    if ($funambol) {
      # several properties are not preserved
      s/^(CALURI|FBURL|X-MOZILLA-HTML|X-EVOLUTION-FILE-AS|X-AIM|X-EVOLUTION-BLOG-URL|X-EVOLUTION-VIDEO-URL|X-GROUPWISE|X-ICQ|X-YAHOO|X-GADUGADU|X-JABBER|X-MSN|X-SIP|X-SKYPE|X-ASSISTANT)(;[^:;\n]*)*:.*\r?\n?//gm;

      # quoted-printable line breaks are =0D=0A, not just single =0A
      s/(?<!=0D)=0A/=0D=0A/g;
      # only three email addresses, fourth one from test case gets lost
      s/^EMAIL:john.doe\@yet.another.world\n\r?//mg;
      # this particular type is not preserved
      s/ADR;TYPE=PARCEL:Test Box #3/ADR;TYPE=HOME:Test Box #3/;
    }
    if ($funambol) {
      #several properties are not preserved by funambol server in icalendar2.0 format
      s/^(UID|SEQUENCE|TRANSP|LAST-MODIFIED|X-EVOLUTION-ALARM-UID)(;[^:;\n]*)*:.*\r?\n?//gm;
      if (/^BEGIN:VEVENT/m ) {
        #several properties are not preserved by funambol server in itodo2.0 format and
        s/^(RECURRENCE-ID|ATTENDEE)(;[^:;\n]*)*:.*\r?\n?//gm;
        #REPEAT:0 is added by funambol server so ignore it
        s/^(REPEAT:0).*\r?\n?//gm;
        #CN parameter is lost by funambol server
        s/^ORGANIZER([^:\n]*);CN=([^:\n]*)(;[^:\n])*:(.*\r?\n?)/ORGANIZER$1$3:$4/mg;
      }

      if (/^BEGIN:VTODO/m ) {
        #several properties are not preserved by funambol server in itodo2.0 format and
        s/^(STATUS|URL)(;[^:;\n]*)*:.*\r?\n?//gm;

        #some new properties are added by funambol server
        s/^(CLASS:PUBLIC|PERCENT-COMPLETE:0).*\r?\n?//gm;
      }
    }

    if($nokia_7210c) {
        if (/BEGIN:VCARD/m) {
            #ignore PREF, as it will added by default
            s/^TEL([^:\n]*);TYPE=PREF/TEL$1/mg;
            #remove non-digit prefix in TEL
            s/^TEL([^:\n]*):(\D*)/TEL$1:/mg;
            #properties N mismatch, sometimes lost part of components
            s/^(N|X-EVOLUTION-FILE-AS):.*\r?\n?/$1:[...]\n/gm;
            #strip spaces in 'NOTE'
            while (s/^(NOTE|DESCRIPTION):(\S+)[\t ]+(\S+)/$1:$2$3/mg) {}
            #preserve 80 chars in NOTE
            s/^NOTE:(.{70}).*\r?\n?/NOTE:$1\n/mg;
            #preserve one ADDR

            # ignore the PHOTO encoding data, sometimes it add a default photo
            s/^PHOTO(.*?): .*\n//mg; 
            #s/^(ADR)([^:;\n]*)(;TYPE=[^:\n]*)?:.*\r?\n?/$1:$4\n/mg;

            #lost properties
            s/^(NICKNAME|CATEGORIES|CALURI|FBURL|ROLE|X-AIM|X-ANNIVERSARY|X-ASSISTANT|X-EVOLUTION-BLOG-URL|X-EVOLUTION-VIDEO-URL|X-GROUPWISE|X-ICQ|X-MANAGER|X-SPOUSE|X-MOZILLA-HTML|X-YAHOO)(;[^:;\n]*)*:.*\r?\n?//gm;
        }

        if (/^BEGIN:VEVENT/m ) {
            #The properties phones add by default
            s/^(PRIORITY|CATEGORIES)(;[^:;\n]*)*:.*\r?\n?//gm;
            #strip spaces in 'DESCRIPTION'
            while (s/^DESCRIPTION:(\S+)[\t ]+(\S+)/DESCRIPTION:$1$2/mg) {}

        }

        if (/^BEGIN:VTODO/m) {
            #mismatch properties
            s/^(PRIORITY)(;[^:;\n]*)*:.*\r?\n?/$1:[...]\n/gm;
            #lost properties
            s/^(STATUS|DTSTART|CATEGORIES)(;[^:;\n]*)*:.*\r?\n?//gm;
        }

        #Testing with phones using vcalendar, do not support UID
        s/^(UID|CLASS|SEQUENCE|TRANSP)(;[^:;\n]*)*:.*\r?\n?//gm;
    }

    if ($ovi) {
        if (/^BEGIN:VCARD/m) {
            #lost properties
            s/^(X-AIM|CALURI|URL|FBURL|PHOTO|EMAIL)(;[^:;\n]*)*:.*\r?\n?//gm;
            #FN value mismatch (reordring and adding , by the server)
            s/^FN:.*\r?\n?/FN:[...]\n/gm;
            #X-EVOLUTION-FILE-AS adding '\' by the server
            while (s/^X-EVOLUTION-FILE-AS:(.*)\\(.*)/X-EVOLUTION-FILE-AS:$1$2/gm) {}

            # does not preserve X-EVOLUTION-UI-SLOT=
            s/^(\w+)([^:\n]*);X-EVOLUTION-UI-SLOT=\d+/$1$2/mg;

            # does not preserve third ADR
            s/^ADR:Test Box #3.*\n\r?//mg;
        }

        if (/^BEGIN:VEVENT/m) {
            #Testing with vcalendar, do not support UID
            s/^(UID|SEQUENCE|TRANSP)(;[^:;\n]*)*:.*\r?\n?//gm;
            #Add PRORITY by default
            s/^(PRIORITY)(;[^:;\n]*)*:.*\r?\n?//gm;
            # VALARM not supported
            s/^BEGIN:VALARM.*?END:VALARM\r?\n?//msg;
        }

        if (/^BEGIN:VTODO/m) {
            #Testing with vcalendar, do not support UID
            s/^(UID|SEQUENCE|PERCENT-COMPLETE)(;[^:;\n]*)*:.*\r?\n?//gm;
            #Mismatch DTSTART, COMPLETED
            s/^(DTSTART|COMPLETED)(;[^:;\n]*)*:.*\r?\n?/$1:[...]\n/gm;
        }
    }

    if ($funambol || $egroupware || $nokia_7210c) {
      # NOTE may be truncated due to length resistrictions
      s/^(NOTE(;[^:;\n]*)*:.{0,160}).*(\r?\n?)/$1$3/gm;
    }
    if ($memotoo) {
      if (/^BEGIN:VCARD/m ) {
        s/^(FN|FBURL|CALURI|ROLE|X-MOZILLA-HTML|X-EVOLUTION-BLOG-URL|X-EVOLUTION-VIDEO-URL|X-GADUGADU|X-JABBER|X-MSN|X-SIP|X-SKYPE|X-GROUPWISE)(;[^:;\n]*)*:.*\r?\n?//gm;
        # s/^(FN|FBURL|CALURI|CATEGORIES|ROLE|X-MOZILLA-HTML|X-EVOLUTION-FILE-AS|X-EVOLUTION-BLOG-URL|X-EVOLUTION-VIDEO-URL|X-GADUGADU|X-JABBER|X-MSN|X-SIP|X-SKYPE|X-GROUPWISE)(;[^:;\n]*)*:.*\r?\n?//gm;
        # strip 'TYPE=HOME' 
        s/^URL([^\n:]*);TYPE=HOME/URL$1/mg;
        s/^EMAIL([^\n:]*);TYPE=HOME/EMAIL$1/mg;
      }
      if (/^BEGIN:VEVENT/m ) {
        s/^(UID|SEQUENCE|TRANSP|RECURRENCE-ID|X-EVOLUTION-ALARM-UID|ORGANIZER)(;[^:;\n]*)*:.*\r?\n?//gm;
        # some parameters of 'ATTENDEE' will be lost by server
        s/^ATTENDEE([^\n:]*);CUTYPE=([^\n;:]*)/ATTENDEE$1/mg;
        s/^ATTENDEE([^\n:]*);LANGUAGE=([^\n;:]*)/ATTENDEE$1/mg;
        s/^ATTENDEE([^\n:]*);ROLE=([^\n;:]*)/ATTENDEE$1/mg;
        s/^ATTENDEE([^\n:]*);RSVP=([^\n;:]*)/ATTENDEE$1/mg;
        s/^ATTENDEE([^\n:]*);CN=([^\n;:]*)/ATTENDEE$1/mg;
        s/^ATTENDEE([^\n:]*);PARTSTAT=([^\n;:]*)/ATTENDEE$1/mg;
        if (/^BEGIN:VALARM/m ) {
            s/^(DESCRIPTION)(;[^:;\n]*)*:.*\r?\n?//mg;
        }
      }
      if (/^BEGIN:VTODO/m ) {
        s/^(UID|SEQUENCE|URL|CLASS|PRIORITY)(;[^:;\n]*)*:.*\r?\n?//gm;
        s/^PERCENT-COMPLETE:0\r?\n?//gm;
      }
    }
    if ($mobical) {
      s/^(CALURI|CATEGORIES|FBURL|NICKNAME|X-MOZILLA-HTML|X-EVOLUTION-FILE-AS|X-ANNIVERSARY|X-ASSISTANT|X-EVOLUTION-BLOG-URL|X-EVOLUTION-VIDEO-URL|X-GROUPWISE|X-ICQ|X-GADUGADU|X-JABBER|X-MSN|X-SIP|X-SKYPE|X-MANAGER|X-SPOUSE|X-YAHOO|X-AIM)(;[^:;\n]*)*:.*\r?\n?//gm;

      # some workrounds here for mobical's bug 
      s/^(FN|BDAY)(;[^:;\n]*)*:.*\r?\n?//gm;

      if (/^BEGIN:VEVENT/m ) {
        s/^(UID|SEQUENCE|CLASS|TRANSP|RECURRENCE-ID|ATTENDEE|ORGANIZER|AALARM|DALARM)(;[^:;\n]*)*:.*\r?\n?//gm;
      }

      if (/^BEGIN:VTODO/m ) {
        s/^(UID|SEQUENCE|DTSTART|URL|PERCENT-COMPLETE|CLASS)(;[^:;\n]*)*:.*\r?\n?//gm;
        s/^PRIORITY:0\r?\n?//gm;
      }
    }

    if ($zyb) {
        s/^(CALURI|CATEGORIES|FBURL|NICKNAME|X-MOZILLA-HTML|PHOTO|X-EVOLUTION-FILE-AS|X-ANNIVERSARY|X-ASSISTANT|X-EVOLUTION-BLOG-URL|X-EVOLUTION-VIDEO-URL|X-GROUPWISE|X-ICQ|X-MANAGER|X-SPOUSE|X-YAHOO|X-AIM)(;[^:;\n]*)*:.*\r?\n?//gm;
    }

    if ($exchange) {
        # unsupported properties
        s/^(SEQUENCE|X-EVOLUTION-ALARM-UID)(;[^:;\n]*)*:.*\r?\n?//gm;
        # added properties which can be ignored (?)
        s/^(X-MEEGO-ACTIVESYNCD-[a-zA-Z]*)(;[^:;\n]*)*:.*\r?\n?//gm;
        # ORGANIZER added - remove and thus ignore if we have no ATTENDEEs
        if (!/^ATTENDEE/m) {
            s/^(ORGANIZER)(;[^:;\n]*)*:.*\r?\n?//gm;
        }
        # ignore added VALARM DESCRIPTION
        s/^DESCRIPTION:Reminder\n//m;
    }

    if ($googleeas) {
        # unsupported properties
        s/^(FN)(;[^:;\n]*)*:.*\r?\n?//gm;
    }

    # treat X-MOZILLA-HTML=FALSE as if the property didn't exist
    s/^X-MOZILLA-HTML:FALSE\r?\n?//gm;

    my @formatted = ();

    # Modify lines to cover not more than
    # $width characters by folding lines (as done for the N or SUMMARY above),
    # but also indent each inner BEGIN/END block by 2 spaces
    # and finally sort the lines.
    # We need to keep a stack of open blocks in @formatted:
    # - BEGIN creates another open block
    # - END closes it, sorts it, and adds as single string to the parent block
    push @formatted, [];
    foreach $_ (split /\n/, $_) {
      if (/^BEGIN:/) {
        # start a new block
        push @formatted, [];
      }

      my $spaces = "  " x ($#formatted - 1);
      my $thiswidth = $width -1 - length($spaces);
      $thiswidth = 1 if $thiswidth <= 0;
      s/(.{$thiswidth})(?!$)/$1\n /g;
      s/^(.*)$/$spaces$1/mg;
      push @{$formatted[$#formatted]}, $_;

      if (/^\s*END:/) {
        my $block = pop @formatted;
        my $begin = shift @{$block};
        my $end = pop @{$block};

        # Keep begin/end as first/last line,
        # inbetween sort, but so that N or SUMMARY are
        # at the top. This ensures that the order of items
        # is the same, even if individual properties differ.
        # Also put indented blocks at the end, not the top.
        sub numspaces {
          my $str = shift;
          $str =~ /^(\s*)/;
          return length($1);
        }
        $_ = join("\n",
                  $begin,
                  sort( { $a =~ /^\s*(N|SUMMARY):/ ? -1 :
                          $b =~ /^\s*(N|SUMMARY):/ ? 1 :
                          ($a =~ /^\s/ && $b =~ /^\S/) ? 1 :
                          numspaces($a) == numspaces($b) ? $a cmp $b :
                          numspaces($a) - numspaces($b) }
                        @{$block} ),
                  $end);
        push @{$formatted[$#formatted]}, $_;
      }
    }

    return ${$formatted[0]}[0];
}

# parameters: text, width to use for reformatted lines
# returns list of lines without line breaks
sub Normalize {
    $_ = shift;
    my $width = shift;

    s/\r//g;

    my @items = ();

    # split into individual items
    foreach $_ ( split( /(?:(?<=\nEND:VCARD)|(?<=\nEND:VCALENDAR))\n*/ ) ) {
        if (/END:VEVENT\s+BEGIN:VEVENT/s) {
            # remove multiple events from calendar item
            s/(BEGIN:VEVENT.*END:VEVENT\n)//s;
            my $events = $1;
            my $calendar = $_;
            my $event;
            # inject every single one back into the calendar and process the result
            foreach $event ( split ( /(?:(?<=\nEND:VEVENT))\n*/, $events ) ) {
                $_ = $calendar;
                s/\nEND:VCALENDAR/\n$event\nEND:VCALENDAR/;
                push @items, NormalizeItem($width, $_);
            }
        } else {
            # already a single item
            push @items, NormalizeItem($width, $_);
        }
    }

    return split( /\n/, join( "\n\n", sort @items ));
}

# number of columns available for output:
# try tput without printing the shells error if not found,
# default to 80
my $columns = `which tput >/dev/null 2>/dev/null && tput 2>/dev/null && tput cols`;
if ($? || !$columns) {
  $columns = 80;
}

if($#ARGV > 1) {
  # error
  Usage();
  exit 1;
} elsif($#ARGV == 1) {
  # comparison

  my ($file1, $file2) = ($ARGV[0], $ARGV[1]);

  my $singlewidth = int(($columns - 3) / 2);
  $columns = $singlewidth * 2 + 3;
  my @normal1;
  my @normal2;

  if (-d $file1 && -d $file2) {
      # Both "files" are really directories of individual files.
      # Don't include files in the comparison which are known
      # to be identical because the refer to the same inode.
      # - build map from inode to filename(s) (each inode might be used more than once!)
      my %files1;
      my %files2;
      my @content1;
      my @content2;
      my $inode;
      my $fullname;
      my $entry;
      opendir(my $dh, $file1) || die "cannot read $file1: $!";
      foreach $entry (grep { -f "$file1/$_" } readdir($dh)) {
          $fullname = "$file1/$entry";
          $inode = (stat($fullname))[1];
          if (!$files1{$inode}) {
              $files1{$inode} = [];
          }
          push(@{$files1{$inode}}, $entry);
      }
      closedir($dh);
      # - remove common files, read others
      opendir(my $dh, $file2) || die "cannot read $file2: $!";
      foreach $entry (grep { -f "$file2/$_" } readdir($dh)) {
          $fullname = "$file2/$entry";
          $inode = (stat($fullname))[1];
          if (@{$files1{$inode}}) {
              # randomly match against the last file
              pop @{$files1{$inode}};
          } else {
              open(IN, "<:utf8", "$fullname") || die "$fullname: $!";
              push @content2, <IN>;
          }
      }
      # - read remaining entries from first dir
      foreach my $array (values %files1) {
          foreach $entry (@{$array}) {
              $fullname = "$file1/$entry";
              open(IN, "<:utf8", "$fullname") || die "$fullname: $!";
              push @content1, <IN>;
          }
      }
      my $content1 = join("", @content1);
      my $content2 = join("", @content2); 
      @normal1 = Normalize($content1, $singlewidth);
      @normal2 = Normalize($content2, $singlewidth);
  } else {
      if (-d $file1) {
          open(IN1, "-|:utf8", "find $file1 -type f -print0 | xargs -0 cat") || die "$file1: $!";
      } else {
          open(IN1, "<:utf8", $file1) || die "$file1: $!";
      }
      if (-d $file2) {
          open(IN2, "-|:utf8", "find $file2 -type f -print0 | xargs -0 cat") || die "$file2: $!";
      } else {
          open(IN2, "<:utf8", $file2) || die "$file2: $!";
      }
      my $buf1 = join("", <IN1>);
      my $buf2 = join("", <IN2>);
      @normal1 = Normalize($buf1, $singlewidth);
      @normal2 = Normalize($buf2, $singlewidth);
      close(IN1);
      close(IN2);
  }

  # Produce output where each line is marked as old (aka remove) with o,
  # as new (aka added) with n, and as unchanged with u at the beginning.
  # This allows simpler processing below.
  my $res = 0;
  if (0) {
    # $_ = `diff "--old-line-format=o %L" "--new-line-format=n %L" "--unchanged-line-format=u %L" "$normal1" "$normal2"`;
    # $res = $?;
  } else {
    # convert into same format as diff above - this allows reusing the
    # existing output formatting code
    my $diffs_ref = Algorithm::Diff::sdiff(\@normal1, \@normal2);
    @_ = ();
    my $hunk;
    foreach $hunk ( @{$diffs_ref} ) {
      my ($type, $left, $right) = @{$hunk};
      if ($type eq "-") {
        push @_, "o $left";
        $res = 1;
      } elsif ($type eq "+") {
        push @_, "n $right";
        $res = 1;
      } elsif ($type eq "c") {
        push @_, "o $left";
        push @_, "n $right";
        $res = 1;
      } else {
        push @_, "u $left";
      }
    }

    $_ = join("\n", @_);
  }

  if ($res) {
    print $ENV{CLIENT_TEST_HEADER};
    printf "%*s | %s\n", $singlewidth,
           ($ENV{CLIENT_TEST_LEFT_NAME} || "before sync"),
           ($ENV{CLIENT_TEST_RIGHT_NAME} || "after sync");
    printf "%*s <\n", $singlewidth,
           ($ENV{CLIENT_TEST_REMOVED} || "removed during sync");
    printf "%*s > %s\n", $singlewidth, "",
           ($ENV{CLIENT_TEST_ADDED} || "added during sync");
    print "-" x $columns, "\n";

    # fix confusing output like:
    # BEGIN:VCARD                             BEGIN:VCARD
    #                                      >  N:new;entry
    #                                      >  FN:new
    #                                      >  END:VCARD
    #                                      >
    #                                      >  BEGIN:VCARD
    # and replace it with:
    #                                      >  BEGIN:VCARD
    #                                      >  N:new;entry
    #                                      >  FN:new
    #                                      >  END:VCARD
    #
    # BEGIN:VCARD                             BEGIN:VCARD
    #
    # With the o/n/u markup this presents itself as:
    # u BEGIN:VCARD
    # n N:new;entry
    # n FN:new
    # n END:VCARD
    # n
    # n BEGIN:VCARD
    #
    # The alternative case is also possible:
    # o END:VCARD
    # o 
    # o BEGIN:VCARD
    # o N:old;entry
    # u END:VCARD

    # case one above
    while( s/^u BEGIN:(VCARD|VCALENDAR)\n((?:^n .*\n)+?)^n BEGIN:/n BEGIN:$1\n$2u BEGIN:/m) {}
    # same for the other direction
    while( s/^u BEGIN:(VCARD|VCALENDAR)\n((?:^o .*\n)+?)^o BEGIN:/o BEGIN:$1\n$2u BEGIN:/m) {}

    # case two
    while( s/^o END:(VCARD|VCALENDAR)\n((?:^o .*\n)+?)^u END:/u END:$1\n$2o END:/m) {}
    while( s/^n END:(VCARD|VCALENDAR)\n((?:^n .*\n)+?)^u END:/u END:$1\n$2n END:/m) {}

    # split at end of each record
    my $spaces = " " x $singlewidth;
    foreach $_ (split /(?:(?<=. END:VCARD\n)|(?<=. END:VCALENDAR\n))(?:^. \n)*/m, $_) {
      # ignore unchanged records
      if (!length($_) || /^((u [^\n]*\n)*(u [^\n]*?))$/s) {
        next;
      }

      # make all lines equally long in terms of printable characters
      s/^(.*)$/$1 . (" " x ($singlewidth + 2 - length($1)))/gme;

      # convert into side-by-side output
      my @buffer = ();
      foreach $_ (split /\n/, $_) {
        if (/^u (.*)/) {
          print join(" <\n", @buffer), " <\n" if $#buffer >= 0;
          @buffer = ();
          print $1, "   ", $1, "\n";
        } elsif (/^o (.*)/) {
          # preserve in buffer for potential merging with "n "
          push @buffer, $1;
        } else {
          /^n (.*)/;
          # have line to be merged with?
          if ($#buffer >= 0) {
            print shift @buffer, " | ", $1, "\n";
          } else {
            print join(" <\n", @buffer), " <\n" if $#buffer >= 0;
            print $spaces, " > ", $1, "\n";
          }
        }
      }
      print join(" <\n", @buffer), " <\n" if $#buffer >= 0;
      @buffer = ();

      print "-" x $columns, "\n";
    }
  }

  # unlink($normal1);
  # unlink($normal2);
  exit($res ? ((defined $ENV{CLIENT_TEST_COMPARISON_FAILED}) ? int($ENV{CLIENT_TEST_COMPARISON_FAILED}) : 1) : 0);
} else {
  # normalize
  my $in;
  if( $#ARGV >= 0 ) {
    my $file1 = $ARGV[0];
    if (-d $file1) {
        open(IN, "-|:utf8", "find $file1 -type f -print0 | xargs -0 cat") || die "$file1: $!";
    } else {
        open(IN, "<:utf8", $file1) || die "$file1: $!";
    }
    $in = *IN{IO};
  } else {
    $in = *STDIN{IO};
  }

  my $buf = join("", <$in>);
  print STDOUT join("\n", Normalize($buf, $columns)), "\n";
}
