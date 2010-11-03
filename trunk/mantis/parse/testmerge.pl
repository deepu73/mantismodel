#!/usr/bin/perl

package test;

open(DISK, "<disk-test-0-baseline.txt") || die("Could not open DISK file!");

foreach $line (<DISK>)
{
    ($time, $ampm, $cputype, $c, $d, $e, $f, $g, $h, $i, $util) = split(' ',$line);
    ($hr, $min, $sec) = split(/:/,$time);
    
    if ($cputype ne "DEV" && $line ne "\n" && $time ne "Average:" && $time ne "Linux")
    {
        if ($ampm eq "AM" && $hr eq "12")
	{
	    $hr = $hr - 12;
	}

	if ($ampm eq "PM" && $hr ne "12")
	{
	    $hr = $hr + 12;
	}
	print "PASS 1 ~ ";
	print $hr;
	print ":";
	print $min;
	print ":";
	print $sec;
	print "\n";

	open(CPU, "<cpu-.txt") || die("Could not open CPU file!");
	foreach $line2 (<CPU>)
	{
	    ($time2, $ampm2, $cputype2, $c, $d, $e, $f, $g, $usage) = split(' ',$line2);
	    ($hr2, $min2, $sec2) = split(/:/,$time2);

	     if ($cputype2 eq "0" || $cputype2 eq "1" || $cputype2 eq "2" || $cputype2 eq "3")
	     {
     		 if ($ampm2 eq "AM" && $hr2 eq "12")
		 {
		     $hr2 = $hr2 - 12;
		 }

		 if ($ampm2 eq "PM" && $hr2 ne "12")
		 {
		     $hr2 = $hr2 + 12;
		 }
		 
		 if ($hr eq $hr2 && $min eq $min2 && $sec eq $sec2)
		 {
		     open(AC, "<testac.txt") || die("Could not open AC file!");
		     foreach $line3 (<AC>)
		     {
			 $line3 =~ s/\[//g;
			 $line3 =~ s/]//g;

			 ($time3, $power) = split(' ',$line3);
			 ($hr3, $min3, $sec3) = split(/:/,$time3);
			 $sec3 =~ s/,//g;
		        			 
	      		 if ($hr2 eq $hr3 && $min2 eq $min3 && $sec2 eq $sec3)
			 {
			     print $hr;
			     print ":";
			     print $min;
			     print ":";
			     print $sec;
			     print ",";
			     print $power;
			     print ",";
			     print $cputype2;
			     print ",";
			     printf "%.2f", (100 - $usage);
			     print ",";
			     print $util;
			     print "\n";
			 }
		     }
		     close AC;
		 }
	     }
	}
	close CPU;
    }
}
