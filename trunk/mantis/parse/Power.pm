#!/usr/bin/perl

=head1 NAME

Power - Utilities for parsing and outputting power measurement data

=head1 ASSUMPTIONS 

Assumes the following are defined in main:

=over 4

=item * num_cpus: Number of CPUs in the system

=item * trace_prefix: Prefix to all the logs/traces

=item * sampling_int_secs: Sampling interval of inputs, in seconds

=item * &FindBenchmarkTimestamps: Takes an array of timestamps and a string identifying the data as input, outputs the index of the array element matching the benchmark start time

=back

Assumes that the relevant lines of the trace have the following fields, in order:

=over 4

=item * 24-hour timestamp of form [hh:mm:ss]

=item * AC power measurement

=back

=head1 FUNCTIONS

=over 4

=item * ParseInputFiles(DAQ_MUT_diff) - Parse AC measurement traces and store in internal data structures. In order to convert the timestamps (which are relative to the DAQ machine) to the machine under test, pass in DAQ_MUT_diff = MUT sync time - DAQ sync time 

=item * FindBenchmarkTimestamps - Find the timestamp correlating to the start of the benchmark, and write to an internal data structure.

= item * PrintOutputHeaders(filehandle) - Print to filehandle the column headings (including timestamp) for CSV.

= item * PrintDataPoint(index, filehandle) - Print to filehandle the timestamp and AC power at time (benchmark_start + index). Assumes FindBenchmarkTimestamps has been called already.
 
=back 

=cut

package Power;
use Timestamp;
use strict;

# --- Externally visible functions ---
sub ParseInputFiles;
sub FindBenchmarkTimestamps;
sub PrintOutputHeaders;
sub PrintDataPoint;

# --- Internal helper functions ---
sub ParseACFile;
sub ValidateACInput;

# ----- Variables -------------------------------------------------------

my(@inpt_acpwr);
my(@inpt_timestamps_DAQ);  # Input timestamps are relative to DAQ
my(@timestamps_MUT);       # Convert to timestamps for machine under test
my($benchmk_start_timestamp);

# Fields of trace file
my %inpt_fields = (
#  "date"  => 0,
 # "month" => 1,
 # "year"  => 2,
  "time"  => 3,
#  "ampm"  => 4,
  "acpwr" => 5,
);
my $num_inpt_fields = 2;


# ----- Subroutines -------------------------------------------------------

# ----------------------------------------------------------------------
# --- sub ParseInputFiles(daq_mut_diff) ---
# Parse and validate AC power input data, given 
#   (MUT sync time) - (DAQ sync time)
# ----------------------------------------------------------------------
sub ParseInputFiles {
  &ParseACFile;
  &ValidateACInput($_[0]);
}

# ----------------------------------------------------------------------
# --- sub ParseACFile() ---
# Parse AC power input data
# ----------------------------------------------------------------------
sub ParseACFile {
  open (ACFILE, "<$main::trace_prefix.ac") or die "Couldn't open input file $main::trace_prefix.ac\n";

  # Input AC file line by line
  foreach my $line (<ACFILE>) {
    chomp($line);
    push (@inpt_timestamps_DAQ, &Timestamp::StandardizeTimestamp($line, \%inpt_fields));

    my @inp_array = split(/[\ \t\n]+/,$line);
    push (@inpt_acpwr, $inp_array[$inpt_fields{"acpwr"}]);    
  } 

  close ACFILE;
}


# ----------------------------------------------------------------------
# --- sub ValidateACInput(daq_mut_diff) ---
# Validate measurements and timestamps taken from trace
# Convert DAQ timestamps to MUT time using daq_mut_diff
# ----------------------------------------------------------------------
sub ValidateACInput {
  my $daq_mut_diff = $_[0];

  # Check for dropped samples 
  @inpt_timestamps_DAQ = &Timestamp::FixTimestamps(\@inpt_timestamps_DAQ,\@inpt_acpwr,$main::sampling_int_sec); 

  # Check for outliers in power measurements
  foreach my $i (0 .. $#inpt_acpwr) {
     if (($i == 0) or ($i == $#inpt_acpwr)) { next; }
     my $interpolated_pwr = ($inpt_acpwr[$i-1] + $inpt_acpwr[$i+1])/2;
     
     if (($inpt_acpwr[$i] < $interpolated_pwr/2) and ($inpt_acpwr[$i] < $interpolated_pwr - 5)) {
        print "Outlier in AC measurements at time $inpt_timestamps_DAQ[$i]\n";
     }
     
     if (($inpt_acpwr[$i] > $interpolated_pwr*2) and ($inpt_acpwr[$i] > $interpolated_pwr + 5)) {
        print "Outlier in AC measurements at time $inpt_timestamps_DAQ[$i]\n";
     }
  }

  # Convert timestamps to MUT
  foreach my $timestamp(@inpt_timestamps_DAQ) {
    push (@timestamps_MUT, &Timestamp::AddTime($timestamp, $daq_mut_diff));
  }

}

# ----------------------------------------------------------------------
# --- sub FindBenchmarkTimestamps() ---
# Set $benchmark_start_timestamp by calling main::FindBenchmarkTimestamps
# ----------------------------------------------------------------------
sub FindBenchmarkTimestamps {
  $benchmk_start_timestamp = &main::FindBenchmarkTimestamps(\@timestamps_MUT, "AC power");
}

# ----------------------------------------------------------------------
# --- sub PrintOutputHeaders(filehandle)
# Print column headings to CSV
# ----------------------------------------------------------------------
sub PrintOutputHeaders {
  my $fh = $_[0];

  print $fh "TIME, AC_PWR";
}

# ----------------------------------------------------------------------
# --- sub PrintDataPoint(index, filehandle) ---
# Print (index)th benchmark data point to filehandle
# ----------------------------------------------------------------------
sub PrintDataPoint {
  my $index = $_[0];
  my $fh    = $_[1];
  
  my $data_point = $inpt_acpwr[$benchmk_start_timestamp + $index];
  my $timestamp = $timestamps_MUT[$benchmk_start_timestamp + $index];

  print $fh "$timestamp, $data_point";
}


