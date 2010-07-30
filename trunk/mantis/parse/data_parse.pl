#!/usr/bin/perl

=head1 NAME

data_parse: Driving script for parsing collected Mantis data and writing to a single .csv

=head1 USAGE

./data_parse.pl [path/trace-id]

Assumes that file path/trace-id.log is present, plus whichever files correspond to the system metrics taken.

Writes data to path/trace-id.csv.

=head1 ASSUMPTIONS

Assumes that

=over 4

=item * Logfile will be parsed before individual trace files

=item * FindBenchmarkTimestamps will be called for each trace file before writing data out to csv

=back 

=head1 MAINTENANCE 

System parameters - set these global variables in this file, which are used by all modules

=over 4

=item * $num_cpus - Set to number of cores in system

=item * $num_disks - Set to number of disks in system (although script can't handle > 1 yet)

=item * $sampling_int_secs - Set to the sampling interval in seconds.  Assumes all metrics are sampled at the same rate.

=back

To change log file format: Look at ParseLogFile and its subfunctions.

To change/rearrange CSV output format: Change WriteOutputHeaders and WriteOutputFile. Note that metrics MUST appear in the same order in both functions or the CSV columns will be mislabeled.  Also, both this file and the AC module assume that the timestamp will be printed by the AC module and will be the leftmost item in each row.

To change trace file format: Look at internal module, shouldn't make a difference to main script.

To remove a trace file: Comment out the "use xxxx" and all mentions of the module (which are fully qualified with &Module::function).

=cut

use strict;
$|++;
use FileHandle;

use Timestamp;  # Custom module for standardizing timestamps & math

# Edit these modules if trace formats change, or if different types of traces
# are swapped in and out
use Power;       # AC power
# use CPU_Freq;    # CPU frequencies
# use CPU_Ut;      # CPU % utilizations
# use Perfctr;     # CPU performance counters
# use Disk;        # Disk utilization metrics

# ----------------------------------------------------------------------
# --- Externally used functions ---
sub FindBenchmarkTimestamps;

# --- Internal functions ---
sub ParseInputFiles;
sub WriteOutputFile;
sub ParseLogFile;
sub FindLogBenchmarkStart;
sub FindLogBenchmarkDuration;
sub FindAllStartTimestamps;
sub WriteOutputHeaders;

# --- Global variables (all modules) ---
our($trace_prefix);       # Prefix of log and trace files
our($num_cpus)=4;
our($num_disks)=1;
our($sampling_int_sec)=1; # sampling interval in seconds for log data

# --- Global variables (main program only) ---
my($benchmark_start_timestamp)=0; # Start of actual benchmark (not just metric collection)
my($benchmark_duration);
my($daq_mut_diff);              # From .log file: (MUT sync time) - (DAQ sync time)
# ----------------------------------------------------------------------

# ----------------------------------------------------------------------
# --- Main program
# ----------------------------------------------------------------------
if ($#ARGV + 1 != 1) {
  die "Usage: data_parse.pl <path/logprefix>\n";
}
$trace_prefix = $ARGV[0];
&ParseInputFiles;
&WriteOutputFile;

# ----------------------------------------------------------------------
# --- ParseInputFiles
# Call internal parse routines for
#    * Log file (must be called before the others)
#    * AC power trace
#    * CPU frequency trace
#    * CPU utilization trace
#    * CPU performance counters
#    * Disk metrics
# ----------------------------------------------------------------------
sub ParseInputFiles {
  print "Parsing logfile...\n";       &ParseLogFile;
  print "Parsing AC trace ...\n";     &Power::ParseInputFiles($daq_mut_diff);
#  print "Parsing CPUfreq trace...\n"; &CPU_Freq::ParseInputFiles;
#  print "Parsing CPUutil trace...\n"; &CPU_Ut::ParseInputFiles;
#  print "Parsing perfctr trace...\n"; &Perfctr::ParseInputFiles;
#  print "Parsing disk trace...\n";    &Disk::ParseInputFiles;
  print "...done\n";
}

# ----------------------------------------------------------------------
# --- WriteOutputFile
# Write trace data to CSV.
# ----------------------------------------------------------------------
sub WriteOutputFile {

  # Find the timestamps correlating to the start of the benchmark
  &FindAllStartTimestamps;

  print "Writing stats to csv file...\n";
  open (OUTFILE, ">$trace_prefix.csv") or die "Couldn't open output file $trace_prefix.csv\n";

  &WriteOutputHeaders;

  # Iterate over the benchmark duration and print each metric
  for (my $i=0; $i < $benchmark_duration; $i++) {
    if ($i % 100 == 0) { print "."; }

    &Power::PrintDataPoint($i, \*OUTFILE);
#    &CPU_Freq::PrintDataPoint($i, \*OUTFILE);
    &Perfctr::PrintDataPoint($i, \*OUTFILE);
    &CPU_Ut::PrintDataPoint($i, \*OUTFILE);
    &Disk::PrintDataPoint($i, \*OUTFILE);

    print OUTFILE "\n";
  }
  close OUTFILE;
  
  print "...done\n";
}

# ----------------------------------------------------------------------
# --- ParseLogFile - parse session log
# Necessary to get synchronization information, benchmark start
#   and finish times, and date (for the traces that only print times)
# ----------------------------------------------------------------------
sub ParseLogFile {
  # Fields of log file line
  my %time_hash = (
    "day"   => 0,
    "month" => 1,
    "date"  => 2,
    "time"  => 3,
    "zone"  => 4,
    "year"  => 5,
  );

  open (LOGFILE, "<$trace_prefix.log") or die "Couldn't open session log $trace_prefix.log\n";
  my @log_lines = <LOGFILE>;
  close LOGFILE;

  # Iterate through lines of logfile
  for (my $line_num = 0; $line_num <= $#log_lines; $line_num++) {
    my $line = $log_lines[$line_num];
 
    # Get synchronization data 
    if ($line =~ m/^SYNC/) { 
       my $sync_mut_timestamp = &Timestamp::StandardizeTimestamp($log_lines[$line_num+1], \%time_hash);
       my $sync_daq_timestamp = &Timestamp::StandardizeTimestamp($log_lines[$line_num+2], \%time_hash);
       $daq_mut_diff = (&Timestamp::SubtractTimestamps($sync_mut_timestamp, $sync_daq_timestamp));
    }

    # Get start dates for traces that need it
    &Disk::FindLogStartTime  ($line, \%time_hash); 
    &CPU_Ut::FindLogStartTime($line, \%time_hash); 

    # Get benchmark start and end times (and calculate duration)
    &FindLogBenchmarkStart   ($line, \%time_hash);
    &FindLogBenchmarkDuration($line, \%time_hash);
  }

  # Convert benchmark start time from DAQ to MUT
  $benchmark_start_timestamp = &Timestamp::AddTime($benchmark_start_timestamp, $daq_mut_diff);
}
# --- end sub ParseLogFile ---
# ----------------------------------------------------------------------

# ----------------------------------------------------------------------
# --- FindLogBenchmarkStart(line, \%timehash)
# Set the global benchmark start timestamp if appropriate
# Inputs:
#   * A line from the logfile (not nec'y the relevant one)
#   * Hash of log timestamps
# ----------------------------------------------------------------------
sub FindLogBenchmarkStart { 
  if ($_[0] =~ m/Starting benchmark/) {
    $benchmark_start_timestamp = &Timestamp::StandardizeTimestamp($_[0], $_[1]);
  }
}

# ----------------------------------------------------------------------
# --- FindLogBenchmarkDuration(line, \%timehash)
# Recognize the logfile line containing benchmark end time,
#   and calculate benchmark duration
# Assumes benchmark start time is already set
# Inputs:
#   * A line from the logfile (not nec'y the relevant one)
#   * Hash of log timestamps
# ----------------------------------------------------------------------
sub FindLogBenchmarkDuration { 
  if ($_[0] =~ m/Ended benchmark/) {
    my $bench_end = &Timestamp::StandardizeTimestamp($_[0], $_[1]);
    $benchmark_duration = &Timestamp::SubtractTimestamps($bench_end, $benchmark_start_timestamp);
  }
}

# ----------------------------------------------------------------------
# --- FindAllStartTimestamps
# Iterate over all metrics and find the timestamps corresponding
#   to the benchmark start time. Will set internal vars in those modules.
# Assumes files have already been parsed.
# ----------------------------------------------------------------------
sub FindAllStartTimestamps {
  &Power::FindBenchmarkTimestamps;
#  &CPU_Freq::FindBenchmarkTimestamps;
  &Perfctr::FindBenchmarkTimestamps;
  &CPU_Ut::FindBenchmarkTimestamps;
  &Disk::FindBenchmarkTimestamps;
}

# ----------------------------------------------------------------------
# --- sub FindBenchmarkTimestamps 
# Input:  An array of timestamps, a string identifying the data (nec'y for debugging)
# Output: The index of the first array element matching the benchmark start time
# Error cases:
#    - Unknown behavior if globals $benchmark_start_timestamp and $benchmark_duration are not defined
#    - Dies if benchmark duration exceeds the number of samples in the array after (and including) the input timestamp 
# ----------------------------------------------------------------------
sub FindBenchmarkTimestamps {
  my @timestamps = @{$_[0]};
  my $data_src = $_[1];
  my $start_index = -1;

  # Get the index of the timestamp array corresponding to the benchmark start
  for( my $i=0; $i <= $#timestamps; $i++) {
    if ($timestamps[$i] eq $benchmark_start_timestamp) {
      $start_index = $i;
      last;
    }
  }
  my $num_timestamps = $#timestamps+1;

  print "$data_src stats begin: $timestamps[0]; Stats end: $timestamps[$#timestamps]; Num timestamps=$num_timestamps\n";
  die "$data_src stats do not cover benchmark duration" unless (($start_index != -1) and ($#timestamps - $start_index >= $benchmark_duration));

  return $start_index;
}

# ----------------------------------------------------------------------
# --- WriteOutputHeaders
# Call each module's routine for writing relevant CSV headers
# ----------------------------------------------------------------------
sub WriteOutputHeaders {
  &Power::PrintOutputHeaders(\*OUTFILE);
#  &CPU_Freq::PrintOutputHeaders(\*OUTFILE);
  &Perfctr::PrintOutputHeaders(\*OUTFILE);
  &CPU_Ut::PrintOutputHeaders(\*OUTFILE);
  &Disk::PrintOutputHeaders(\*OUTFILE);
  print OUTFILE "\n";
}

