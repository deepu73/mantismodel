#!/usr/bin/perl

package test;

#Attempt to open disk file, or spit out an error message to the shell.
open(FILE, "<disk-test-0-baseline.txt") || die("Could not open file!");

#Read lines from the file in one at a time.
foreach $line (<FILE>)
{
    #Splits the line of the file that is read into 11 different variables. 
    #time - The first item read in, the time
    #ampm - The AM or PM read in 2nd
    #cputype - The name/number of the CPU
    #c - i - Trash variables
    #util - The utilization of the CPU
    ($time, $ampm, $cputype, $c, $d, $e, $f, $g, $h, $i, $util) = split(' ',$line);
    
    #Splitting the $time variable into hours, $hr, minutes, $min, and seconds, $sec.
    ($hr, $min, $sec) = split(/:/,$time);
    
    #Checks the ampm variable to see if it is 12AM, and subtracts if so to show a 24hr time stamp.
    if ($a eq "AM" && $hr eq "12")
    {
        $hr = $hr - 12;
    }
    
    #Checks the ampm variable to see if it is after 12PM (But isn't in the 12PM hour) and adds 12 to reflect 24 hr time stamp.
    if ($ampm eq "PM" && $hr ne "12")
    {
	$hr = $hr + 12;
    }
    
    #Checks the line to be sure it is not a header, is not a blank line, and is not the average line. If it passes, then it will print the 24 hr time stamp, and utilization.
    if ($cputype ne "DEV" && $line ne "\n" && $time ne "Average:")
    {
        print $hr; 
	print ":";
	print $min;
	print ":";
	print $sec;
        print ",";
	print $util;
	print "\n";
    }
}
