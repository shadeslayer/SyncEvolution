#!/usr/bin/perl
#
# Turn reStructuredText README.rst into C file with
# static const char *synopsis, *options;

$_ = join("", <>);

# trailing :: is only needed for reST
s/ :://g;

# simplify colon
s/::/:/g;

# avoid special \-- which was necessary for some options
s/^\\--/--/gm;

# remove escape sequence
s/\\\*/*/g;

# extract parts
/SYNOPSIS\n===+\n\n(.*?)\nDESCRIPTION/s || die "no synopsis";
my $synopsis = $1;

/OPTIONS\n===+\n\n.*?(^--.*?)\nEXAMPLES/ms || die "no options";
my $options = $1;

# condense synopsis
$synopsis =~ s/\n\n/\n/g;

sub text2c {
    my $text = shift;
    foreach $_ (split ('\n', $text)) {
        s/\\/\\\\/g;
        s/"/\\"/g;
        print "    \"", $_, "\\n\"\n";
    }
}

# now print as C code
print "static const char synopsis[] =\n";
text2c($synopsis);
print ";\nstatic const char options[] =\n";
text2c($options);
print ";\n";

