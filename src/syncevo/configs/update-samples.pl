#! /usr/bin/env perl

sub basename {
    $_ = shift;
    s;.*/;;;
    return $_;
}

# Concatenate all files ending in .xml in the given directory
# plus those in a specific subdirectory for client or server.
# Order lexicographic ascending of the base filename.

sub readfragments {
    my $dir = shift;
    my $subdir = shift;
    my @res = ();
    
    my @files = ();
    if (opendir(my $dh, $dir)) {
        foreach (grep (/.*\.xml$/, readdir($dh))) {
            push @files, "$dir/$_";
        }
        closedir($dh);

        if (opendir(my $dh, "$dir/$subdir")) {
            foreach (grep (/.*\.xml$/, readdir($dh))) {
                push @files, "$dir/$subdir/$_";
            }
            closedir($dh);
        }
    }

    @files = sort { basename($a) <=> basename($b) } @files;
    foreach (@files) {
        open(IN, "<$_") || die "cannot read $_: $!";
        push @res, <IN>;
        close(IN);
    }

    return join("", @res);
}

# replace content of <debug>, <scripting>, <datatypes> and all <remoterule>s
# with the corresponding shared and/or client/server .xml fragments
sub update {
    my $file = shift;
    my $subdir = shift;
    my $write = shift;

    open(IN, "<$file") || die "cannot read $file: $!";
    $_ = join("", <IN>);
    close(IN) || die "closing $file: $!";

    s;(<debug>\n).*(\n *</debug>);$1 . readfragments("debug", $subdir) . $2;se;
    s;(<scripting>\n).*(\n *</scripting>);$1 . readfragments("scripting", $subdir) . $2;se ||
        s;(\n *)<scripting/>;$1 . "<scripting>\n" . readfragments("scripting", $subdir) . $1 . "</scripting>";se;
    s;(<datatypes>\n).*(\n *</datatypes>);$1 . readfragments("datatypes", $subdir) . $2;se ||
        s;(\n *)<datatypes/>;$1 . "<datatypes>\n" . readfragments("datatypes", $subdir) . $1 . "</datatypes>";se;
    s;(\n *)<remoterule>.*</remoterule>;$1 . readfragments("remoterules", $subdir);se ||
        s;(\n *)<remoterules/>;readfragments("remoterules", $subdir);se;

    if ($write) {
        open(OUT, ">$file") || die "cannot write $file: $!";
        print OUT;
        close(OUT) || die "closing $file: $!";
    } else {
        print;
    }
}

if ($#ARGV == -1) {
    update("syncclient_sample_config.xml", "client", 1);
    update("syncserv_sample_config.xml", "server", 1);
} else {
    update($ARGV[0], $ARGV[1], 0);
}
