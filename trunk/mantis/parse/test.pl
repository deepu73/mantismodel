#!/usr/bin/perl
#print "Hello World!\n";
 #   $x = 5;
  #  $y = 2;
   # $z = $x * $y;
#print "$z \n";


open(MYINPUTFILE, "<test-0-baseline.ac");
print "Time  ";
print "    Power(watts)\n";

while(<MYINPUTFILE>)
{
 # Good practice to store $_ value because
 # subsequent operations may change it.
    my($line) = $_;

 # Good practice to always strip the trailing
 # newline from the line.
    chomp($line);

 $line =~ s/]/,/g;
 $line =~ s/\[//g;
 print "$line\n";
}
