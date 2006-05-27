#! /usr/bin/perl -w

use strict;

sub Usage {
  print "$0 <vcards.vcf\n";
  print "   normalizes one file (stdin or single argument), prints to stdout\n";
  print "$0 vcards1.vcf vcards2.vcf\n";
  print "   compares the two files\n";
  print "Also works for iCalendar files.\n";
}

sub Normalize {
  my $in = shift;
  my $out = shift;
  my $width = shift;

  $_ = join( "", <$in> );
  s/\r//g;

  my @items = ();

  foreach $_ ( split( /(?:(?<=\nEND:VCARD)|(?<=\nEND:VCALENDAR))\n*/ ) ) {
    # undo line continuation
    s/\n\s//gs;
    # ignore charset specifications, assume UTF-8
    s/;CHARSET="?UTF-8"?//g;

    # UID may differ
    s/^UID:.*\n//mg;
    # ignore extra email type
    s/^EMAIL(.*);TYPE=INTERNET/EMAIL$1/mg;
    s/^EMAIL(.*);TYPE=OTHER/EMAIL$1/mg;
    # ignore extra ADR type
    s/^ADR;TYPE=OTHER/ADR/mg;
    # ignore TYPE=PREF in address, does not matter in Evolution
    s/^((ADR|LABEL)[^:]*);TYPE=PREF/$1/mg;
    # ignore extra separators in multi-value fields
    s/^((ORG|N|(ADR[^:]*)):.*?);*$/$1/mg;
    # the type of certain fields is ignore by Evolution
    s/^X-(AIM|GROUPWISE|ICQ|YAHOO);TYPE=HOME/X-$1/gm;
    # TYPE=VOICE is the default in Evolution and may or may not appear in the vcard
    s/^TEL([^:]*);TYPE=VOICE([^:]*):/TEL$1$2:/mg;
    # don't care about the TYPE property of PHOTOs
    s/^PHOTO;(.*)TYPE=[A-Z]*/PHOTO;$1/mg;
    # encoding is not case sensitive, skip white space in the middle of binary data
    if (s/^PHOTO;.*?ENCODING=(b|B|BASE64).*?:\s*/PHOTO;ENCODING=B: /mgi) {
      while (s/^PHOTO(.*?): (\S+)[\t ]+(\S+)/PHOTO$1: $2$3/mg) {}
    }

    # remove fields which may differ
    s/^(PRODID|CREATED|LAST-MODIFIED):.*\r?\n?//gm;
    # remove optional fields
    s/^(METHOD):.*\r?\n?//gm;

    # replace parameters with a sorted parameter list
    s!^([^;:]*);(.*?):!$1 . ";" . join(';',sort(split(/;/, $2))) . ":"!meg;

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

      # there must be a better way to get a string of 2 * "level" spaces...
      my $spaces = "";
      my $i = 0;
      while ($i < $#formatted - 1) {
        $spaces .= "  ";
        $i++;
      }

      my $thiswidth = $width + 1 - length($spaces);
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

    push @items, ${$formatted[0]}[0];
  }

  print $out join( "\n\n", sort @items ), "\n";
}

# number of columns available for output:
# try tput without printing the shells error if not found,
# default to 80
my $columns = `which tput >/dev/null && tput cols`;
print $columns;
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
  my $normal1 = `mktemp`;
  my $normal2 = `mktemp`;
  chomp($normal1);
  chomp($normal2);

  open(IN1, "<$file1") || die "$file1: $!";
  open(IN2, "<$file2") || die "$file2: $!";
  open(OUT1, ">$normal1") || die "$normal1: $!";
  open(OUT2, ">$normal2") || die "$normal2: $!";
  my $singlewidth = int(($columns - 3) / 2);
  $columns = $singlewidth * 2 + 3;
  Normalize(*IN1{IO}, *OUT1{IO}, $singlewidth);
  Normalize(*IN2{IO}, *OUT2{IO}, $singlewidth);
  close(IN1);
  close(IN2);
  close(OUT1);
  close(OUT2);

  $_ = `diff --expand-tabs --side-by-side --width $columns "$normal1" "$normal2"`;
  my $res = $?;

  if ($res) {
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

    s/(BEGIN:(VCARD|VCALENDAR) +BEGIN:\2\n)((?: {$singlewidth} > .*\n)+)( {$singlewidth}) >\n {$singlewidth} > BEGIN:\2\n/$4 > BEGIN:$2\n$3\n$1/mg;

    # same for the other way around, note that we must insert variable padding
    s/(BEGIN:(VCARD|VCALENDAR) +BEGIN:\2)\n((?:.{$singlewidth} <\n)+)( {$singlewidth}) <\nBEGIN:\2 *<\n/"BEGIN:$2" . (" " x ($singlewidth - length("BEGIN:$2"))) . " <\n$3\n$1"/mge;

    # assume that blank lines separate chunks
    my @chunks = split /\n\n/, $_;

    # only print chunks which contain diffs
    print join( ( "-" x $columns ) . "\n", "",
                grep( /^.{$singlewidth} [<>|]/m && (s/\n*$/\n/s || 1), @chunks), "");
  }

  unlink($normal1);
  unlink($normal2);
  exit($res ? 1 : 0);
} else {
  # normalize
  my $in;
  if( $#ARGV >= 0 ) {
    open(IN, "<$ARGV[0]") || die "$ARGV[0]: $!";
    $in = *IN{IO};
  } else {
    $in = *STDIN{IO};
  }

  Normalize($in, *STDOUT{IO}, $columns);
}
