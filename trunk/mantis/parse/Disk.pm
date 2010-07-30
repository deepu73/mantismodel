#!/usr/bin/perl

# ----- POD ------------------------------------------------------------

=head1 NAME

Disk - Utilities for parsing and outputting iostat/disk data

=head1 ASSUMPTIONS 

Assumes the following are defined in main:

=over 4

=item * num_cpus: Number of CPUs in the system

=item * trace_prefix: Prefix to all the logs/traces

=item * sampling_int_secs: Sampling interval of inputs, in seconds

=item * &FindBenchmarkTimestamps: Takes an array of timestamps and a string identifying the data as input, outputs the index of the array element matching the benchmark start time

=back

Expected trace format: Relevant data is in (2+D)-line blocks, where D is the number of disks (only handling one right now)

=over 4

=item * Line 1: Timestamp of the form "Time: hh:mm:ss [AM/PM]"

=item * Line 2: Column headings (ignore)

=item * Lines 3-(D-1): Data of the form "[disk_id] [rrqm/s] [wrqm/s] [r/s] [w/s] [rsec/s] [wsec/s] [avgrq-sz] [avgqu-sz] [await] [svctm] [%util]"

=back

=head1 FUNCTIONS

=over 4

=item * FindLogStartTime(logfile line, logfile field hash) - Parse lines of logfile to see if they contain the start time for trace, and set internal variable if so.

=item * ParseInputFiles - Parse traces and store in internal data structures. Assumes that FindLogStartTime has been called.

=item * FindBenchmarkTimestamps - For each iostat trace, find the timestamps correlating to the start of the benchmark, and write to an internal data structure.

= item * PrintOutputHeaders(filehandle) - Print to filehandle the column headings for CSV.

= item * PrintDataPoint(index, filehandle) - Print to filehandle each metric's value at time (benchmark_start + index). Assumes FindBenchmarkTimestamps has been called already.
 
=back 

=cut

package Disk;
use Timestamp;
use strict;

# --- Externally visible functions ---
sub FindLogStartTime;
sub ParseInputFiles;
sub FindBenchmarkTimestamps;
sub PrintOutputHeaders;
sub PrintDataPoint;

# --- Internal helper functions ---
sub ProcessInputTimestamp;
sub ProcessInputData;
sub AdjustTimestamps;
sub PrintOutputHeader;
sub PrintDPMetric;

# ----- Variables -------------------------------------------------------

# Metrics - one array for each
my(@reads);
my(@writes);
my(@rsecs);
my(@wsecs);
my(@accesses);
my(@sectors);
my(@utils);

my(@inpt_timestamps);
my($log_start_timestamp) = 0;
my($benchmk_start_timestamp);

# The fields of each sample
# Necessary to generate timestamp and parse sample
# Note that several metrics can map to the same field # because sample spans
#   multiple lines
my %inpt_fields = (
  # Time line (field 0="Time:")
  "time" => 1,
  "ampm" => 2,

  # Data line
  "device_id"  => 0,
  "rrqm0s_f"   => 1,
  "wrqm0s_f"   => 2,
  "r0s_f"      => 3,
  "w0s_f"      => 4,
  "rsec0s_f"   => 5,
  "wsec0s_f"   => 6,
  "avgrq_sz_f" => 7,
  "avgqu_sz_f" => 8,
  "await_f"    => 9,
  "svctm_f"    => 10,
  "util"       => 11,
);

# ----- Subroutines -------------------------------------------------------
# ----------------------------------------------------------------------
# --- sub FindLogStartTime(logfile line, logfile fields) ---
# Parse lines of logfile to see if they contain the start time for
#   trace, and set internal variable if so.
# ----------------------------------------------------------------------
sub FindLogStartTime {
  my $line = $_[0];
  my %log_fields = %{$_[1]};

  if ($line =~ m/Starting iostat/) {
    $log_start_timestamp = &Timestamp::StandardizeTimestamp($line, \%log_fields);
  }
}

# ----------------------------------------------------------------------
# --- sub ParseInputFiles() ---
# Parse disk input data
# ----------------------------------------------------------------------
sub ParseInputFiles {
  open (IOSTATFILE, "<$main::trace_prefix.iostat") or die "Couldn't open input file $main::trace_prefix.iostat\n";

  my $cur_timestamp = $log_start_timestamp;
  my $expecting_disk = -1;

  # Input file line by line
  foreach my $line (<IOSTATFILE>) {
    chomp($line);

    if ($line =~ /^Time/) { 
      $cur_timestamp = &ProcessInputTimestamp($line, $cur_timestamp); 
      $expecting_disk = 0;
    }

    if ($line =~ /^sd*/) {
      &ProcessInputData($line, $expecting_disk);
      $expecting_disk++;
    }
  }
  close IOSTATFILE;

  &AdjustTimestamps;
}

# ----------------------------------------------------------------------
# --- sub ProcessInputTimestamp(line, cur_timestamp) ---
# Process the "Time:" line of the trace and infer date
# Return an updated cur_timestamp
# ----------------------------------------------------------------------
sub ProcessInputTimestamp {
  my $line = $_[0];
  my $cur_timestamp = $_[1];

  my $time_f     = &Timestamp::ConvertTo24Hr($line, \%inpt_fields);
  $cur_timestamp = &Timestamp::InferDate($cur_timestamp, $time_f);
  push(@inpt_timestamps, $cur_timestamp); 

  return $cur_timestamp;
}

# ----------------------------------------------------------------------
# --- sub ProcessInputData(line, cur_disk) ---
# Process the data line(s) of the trace
# ----------------------------------------------------------------------
sub ProcessInputData {
  my $line = $_[0];
  my $cur_disk= $_[1];
  my @inp_array = split(/[\ \t\n]+/,$line);

  if ($cur_disk >= $main::num_disks) {
    die "Disk parse error: expecting nonexistent Disk $cur_disk\n";
  }

  my $read = $inp_array[$inpt_fields{"r0s_f"}];
  my $write = $inp_array[$inpt_fields{"w0s_f"}];
  my $readsec = $inp_array[$inpt_fields{"rsec0s_f"}];
  my $writesec = $inp_array[$inpt_fields{"wsec0s_f"}];
  my $util = $inp_array[$inpt_fields{"util"}];
  
  push(@{$reads[$cur_disk]}, $read); 
  push(@{$writes[$cur_disk]}, $write);
  push(@{$rsecs[$cur_disk]},  $readsec);
  push(@{$wsecs[$cur_disk]},  $writesec);
  push(@{$accesses[$cur_disk]}, ($read + $write));
  push(@{$sectors[$cur_disk]}, ($readsec + $writesec));
  push(@{$utils[$cur_disk]}, $util);
}

# ----------------------------------------------------------------------
# --- sub AdjustTimestamps() ---
# Fix missing timestamps by interpolating
# ----------------------------------------------------------------------
sub AdjustTimestamps {
  my @temp_timestamps;

  for my $disk (0 .. $main::num_disks-1) {
    @temp_timestamps = &Timestamp::FixTimestamps(\@inpt_timestamps, \@{$reads[$disk]}, $main::sampling_int_sec);
    @temp_timestamps = &Timestamp::FixTimestamps(\@inpt_timestamps, \@{$writes[$disk]}, $main::sampling_int_sec);
    @temp_timestamps = &Timestamp::FixTimestamps(\@inpt_timestamps, \@{$rsecs[$disk]}, $main::sampling_int_sec);
    @temp_timestamps = &Timestamp::FixTimestamps(\@inpt_timestamps, \@{$wsecs[$disk]}, $main::sampling_int_sec);
    @temp_timestamps = &Timestamp::FixTimestamps(\@inpt_timestamps, \@{$accesses[$disk]}, $main::sampling_int_sec);
    @temp_timestamps = &Timestamp::FixTimestamps(\@inpt_timestamps, \@{$utils[$disk]}, $main::sampling_int_sec);
    @temp_timestamps = &Timestamp::FixTimestamps(\@inpt_timestamps, \@{$sectors[$disk]}, $main::sampling_int_sec);
  }
  @inpt_timestamps = @temp_timestamps;
}


# ----------------------------------------------------------------------
# --- sub FindBenchmarkTimestamps() ---
# Set @benchmark_start_timestamps by calling main::FindBenchmarkTimestamps
# ----------------------------------------------------------------------
sub FindBenchmarkTimestamps {
  $benchmk_start_timestamp = &main::FindBenchmarkTimestamps(\@inpt_timestamps, "Disk");
}

# ----------------------------------------------------------------------
# --- sub PrintOutputHeaders(filehandle)
# Print column headings to CSV
# ----------------------------------------------------------------------
sub PrintOutputHeaders {
  my $fh = $_[0];

  &PrintOutputHeader($fh, "R");  
  &PrintOutputHeader($fh, "W");  
  &PrintOutputHeader($fh, "RBLK");  
  &PrintOutputHeader($fh, "WBLK");  
  &PrintOutputHeader($fh, "ACC");  
  &PrintOutputHeader($fh, "BLKS");  
  &PrintOutputHeader($fh, "UTIL");  

}

# ----------------------------------------------------------------------
# --- sub PrintOutputHeader(filehandle, header)
# Print column headings to CSV
# ----------------------------------------------------------------------
sub PrintOutputHeader {
  my $fh = $_[0];

  foreach my $disk (0 .. $main::num_disks-1) {
    print $fh ", DISK[" . $disk . "]_" . $_[1];
  }
  if ($main::num_disks > 1) {
    print $fh ", TOTAL-DISK-$_[1]"; 
  }
}

# ----------------------------------------------------------------------
# --- sub PrintDataPoint(index, filehandle) ---
# Print (index)th benchmark data point to filehandle
#    for each metric
# ----------------------------------------------------------------------
sub PrintDataPoint {
  &PrintDPMetric($_[0], \@reads, $_[1]);
  &PrintDPMetric($_[0], \@writes, $_[1]);
  &PrintDPMetric($_[0], \@rsecs, $_[1]);
  &PrintDPMetric($_[0], \@wsecs, $_[1]);
  &PrintDPMetric($_[0], \@accesses, $_[1]);
  &PrintDPMetric($_[0], \@sectors, $_[1]);
  &PrintDPMetric($_[0], \@utils, $_[1]);
}

# ----------------------------------------------------------------------
# --- sub PrintDPMetric(index, array, filehandle) ---
# Print the (index)th element for a particular metric
# ----------------------------------------------------------------------
sub PrintDPMetric {
   my $index = $_[0];
   my @metric = @{$_[1]};
   my $fh = $_[2];

   my $cur_total = 0;

   foreach my $disk (0 .. $main::num_disks-1) { 
     my @disk_metric = @{$metric[$disk]};
     my $data_point  = $disk_metric[$benchmk_start_timestamp + $index];
     print $fh ", $data_point";
     $cur_total += $data_point;  
   }

   if ($main::num_disks > 1) {
     print $fh ", $cur_total";
   }
}

