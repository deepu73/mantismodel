#!/usr/bin/perl
#
# Program to concatenate CSV files
# Usage: ./csv_join.pl [name of output file] [list of input files]
#   (include .csv suffix)

use strict;
use FileHandle;

my $outfilename = shift;

if (-e $outfilename) { die "$outfilename exists, and I'm not willing to stomp on it\n"; }
open (OUTFILE, ">$outfilename") or die "Couldn't open output file $outfilename for writing\n";

my $first_header;
foreach my $argnum (0 .. $#ARGV) {
   my $infilename = $ARGV[$argnum];
   open (INFILE, "<$infilename") or die "Couldn't open input file $infilename for reading\n";

#   print "$ARGV[$argnum]\n";

   # Omit header row for all but first file
   my $header;
   if ($argnum == 0) {
     $first_header = <INFILE>;
     print OUTFILE $first_header;
   }
   else { 
     $header = <INFILE>;
     if ($header ne $first_header) {
       print "Warning: CSV header for $infilename doesn't match $ARGV[0]\n";
     } 
   }

   # Write out each file's contents
   foreach my $line (<INFILE>) {
     print OUTFILE $line;
   }
   close INFILE;
}

close OUTFILE;
