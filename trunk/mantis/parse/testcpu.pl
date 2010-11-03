#!/usr/bin/perl

package test;

#Opens cpu data profile, spits out an error if it does not open.
open(FILE, "<cpu-.txt") || die("Could not open file!"); 

#Reads in each line of the file one at a time.
foreach $line (<FILE>) 
{
    #Splits the line into variables that we can use or trash.
    ($time, $ampm, $cputype, $c, $d, $e, $f, $g, $usage) = split(' ',$line);
    ($hr, $min, $sec) = split (":", $time);

    #Checks to see if it is 12 in the morning, subtracts 12 if so (To reflect 24 hr time stamp)
    if ($ampm eq "AM" && $hr eq "12")
    {
	$hr = $hr - 12;
    }
    
    #Checks to see if it is after 12PM, adds 12 if so (To reflect 24 hr time stamp)
    if ($ampm eq "PM" && $hr ne "12")
    {
	$hr = $hr + 12;
    }
    
    #Checks to see if it is the first CPU being read, in order to print out 1 time stamp, rather than multiple of the same time stamp.
    if ($cputype eq "0")
    {
	print $hr;
	print ":";
	print $min;
	print ":";
	print $sec;
	print ",";
	#Usage is the 100% minus the idle.
	printf "%.2f", (100 - $usage);
    }
    
    elsif ($cputype eq "1")
    {
	print ",";
	printf "%.2f", (100 - $usage);
    }
	
    elsif ($cputype eq "2")
    {
	print ",";
	printf "%.2f", (100 - $usage);
    }
    elsif ($cputype eq "3")
    {
	print ",";
	printf "%.2f", (100 - $usage);
	print "\n";
    }
    else
    {
    }
}
