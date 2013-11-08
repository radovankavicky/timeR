#!/usr/bin/env perl
#
# A script to generate timeR-timerlist.h from timeR.c

use warnings;
use strict;
use feature ':5.10';

if (scalar(@ARGV) != 2) {
    say STDERR "Usage: $0 timeR.c timeR-timerlist.h";
    exit 1;
}

my $input_file  = shift;
my $output_file = shift;

# search for start marker in timeR.c
open IN, "<", $input_file or die "Can't open $input_file: $!";
my $found = 0;
while (<IN>) {
    if (/MARKER:START/) {
        $found = 1;
        last;
    }
}

if (!$found) {
    say STDERR "ERROR: Did not find start marker in $input_file";
    exit 2;
}

# open output file
open OUT, ">", $output_file or die "Can't open $output_file: $!";

say OUT "// autogenerated file, do not modify!";
say OUT "// generated ",scalar(localtime);
print OUT <<EOF;
#ifndef TIMER_TIMERLIST
#define TIMER_TIMERLIST

typedef enum {
EOF

# read timer list from timeR.c
my @timers;

while (<IN>) {
    last if /MARKER:END/;

    chomp;
    if (/\"([^"]+)\"/) {
      # timer name
      say OUT "    TR_$1,";
      push @timers, $_;
    } else {
      # other string
      say OUT $_;
    }
}

print OUT <<EOF;

    /* must be the last entry, is assumed to be the first R_FunTab timer */
    TR_StaticBinCount
} tr_bin_id_t;
EOF

say OUT "\n#endif";

close OUT;
close IN;
