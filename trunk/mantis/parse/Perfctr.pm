#!/usr/bin/perl

# ----- POD ------------------------------------------------------------

=head1 NAME

Perfctr - Utilities for parsing and outputting perfctr data

=head1 ASSUMPTIONS 

Assumes the following are defined in main:

=over 4

=item * num_cpus: Number of CPUs in the system

=item * trace_prefix: Prefix to all the logs/traces

=item * sampling_int_secs: Sampling interval of inputs, in seconds

=item * &FindBenchmarkTimestamps: Takes an array of timestamps and a string identifying the data as input, outputs the index of the array element matching the benchmark start time

=back

Expected trace format:

=over 4

=item * Multiple opening comments prefixed with "#", including one line per perfctr of format "# PMD[x] [PERFCTRNAME]..."

=item * Multiple samples of the form "[cpu_id] [timestamp] [perfctr1] [perfctr2]..." These samples may be split across multiple lines.

=item * A final data point, formatted like the others, that prints aggregate counts rather than per-interval samples. Should be disregarded.

=back

=head1 FUNCTIONS

=over 4

=item * ParseInputFiles - Parse perfmon traces and store in internal data structures.

=item * FindBenchmarkTimestamps - For each CPU's perfmon trace, find the timestamps correlating to the start of the benchmark, and write to an internal data structure.

= item * PrintOutputHeaders(filehandle) - Print to filehandle the column headings for CSV.

= item * PrintDataPoint(index, filehandle) - Print to filehandle each metric's value at time (benchmark_start + index). Assumes FindBenchmarkTimestamps has been called already.
 
=back 

=cut

package Perfctr;
use Timestamp;
use strict;

# --- Externally visible functions ---
sub ParseInputFiles;
sub FindBenchmarkTimestamps;
sub PrintOutputHeaders;
sub PrintDataPoint;

# --- Internal helper functions ---
sub ParseSingleInputFile;
sub ParsePMD;
sub ParseCPUID;
sub ParseTimestamp;
sub ParsePerfctr;
sub FinishParsingCore;
sub ReportParseError;
sub PrintHeader;
sub PrintDPMetric; 

# ----- Variables -------------------------------------------------------

# Performance counters - one array for each
my(@perfctr1);
my(@perfctr2);
my(@perfctr3);
my(@perfctr4);

my(@inpt_timestamps);
my(@benchmk_start_timestamps);

# Fields of PMD declaration
my %pmd_fields = (
  "pmd_num" => 1,
  "pmd_name" => 2,
);

# The fields of each sample
# Necessary to generate timestamp and parse sample
my %sample_fields= (
  "cpu_id"  => 0,
  "day"     => 1,
  "month"   => 2,
  "date"    => 3,
  "time"    => 4,
  "year"    => 5,
  "perfctr1" => 6,
  "perfctr2" => 7,
  "perfctr3" => 8,
  "perfctr4" => 9,
);
my $num_time_fields = 5;

my $last_field = 5;
my $num_perfctrs = 0;
my %perfctr_names;

# ----- Subroutines -------------------------------------------------------

# ----------------------------------------------------------------------
# --- sub ParseInputFiles() ---
# Parse performance counter input data
# ----------------------------------------------------------------------
sub ParseInputFiles {
  foreach my $cpu (0 .. $main::num_cpus-1) {

# debug
    print "Parsing perfctrs for CPU $cpu\n";

    $last_field -= $num_perfctrs;
    %perfctr_names=();
    $num_perfctrs = 0;
    &ParseSingleInputFile($cpu, $main::trace_prefix);
  }
}

# ----------------------------------------------------------------------
# --- sub ParseSingleInputFile(cpu_num, trace_prefix) ---
# Parse 1 CPU's performance counter trace
# ----------------------------------------------------------------------
sub ParseSingleInputFile {
  my $cpu_num = $_[0];
  my $trace_prefix = $_[1];
  my @perfctr1_core, my @perfctr2_core, my @perfctr3_core, my @perfctr4_core;
  my @timestamps_core;
  my $cur_state = 0;
  my $next_state = 0;
  my $line_ct=0;

  open (PERFCTFILE, "<$trace_prefix.cpu$cpu_num") or die "Couldn't open input file $trace_prefix.cpu$cpu_num\n";

  foreach my $line (<PERFCTFILE>) {
    chomp($line);

    # Infer performance counter names
    if ($line =~ /^#/) { &ParsePMD($line); next; }

    die "No performance counter names found\n" unless ($num_perfctrs >= 1);

    $line_ct++;
    if ($line_ct % 100 == 0) { print "."; }
    my @inp_array = split(' ', $line);
    my $i;
    for ($i=0; $i<=$#inp_array; $i++) {
      $cur_state = $next_state;

      $next_state = &ParseCPUID($cur_state, $next_state, $inp_array[$i]);
      ($next_state, $i) = &ParseTimestamp($cur_state, $next_state, $i, $inp_array[$i], $line, \@timestamps_core);
      $next_state = &ParsePerfctr($cur_state, $next_state, "perfctr1", $inp_array[$i], \@perfctr1_core); 
      $next_state = &ParsePerfctr($cur_state, $next_state, "perfctr2", $inp_array[$i], \@perfctr2_core);
      $next_state = &ParsePerfctr($cur_state, $next_state, "perfctr3", $inp_array[$i], \@perfctr3_core);
      $next_state = &ParsePerfctr($cur_state, $next_state, "perfctr4", $inp_array[$i], \@perfctr4_core);

      if ($next_state > $last_field) { $next_state = 0; }
    }
  }

  &FinishParsingCore(\@timestamps_core, \@perfctr1_core, \@perfctr2_core, \@perfctr3_core, \@perfctr4_core); 

  close PERFCTFILE;
}

# ----------------------------------------------------------------------
# --- sub ParsePMD (line) ---
# If $line contains a perfctr name, add that name to the global vars
# ----------------------------------------------------------------------
sub ParsePMD {
  my @line_fields = split(' ', $_[0]);
 
  if (($line_fields[0] eq "#") && ($line_fields[$pmd_fields{"pmd_num"}] =~ /PMD[0-9]+/)) {
   
    my @subfields = split(':', $line_fields[$pmd_fields{"pmd_name"}]);
 
    $num_perfctrs++;
    $last_field++;
    $perfctr_names{"perfctr$num_perfctrs"} = $subfields[0];
  }
}

# ----------------------------------------------------------------------
# --- sub ParseCPUID(cur_state, next_state, token) ---
# Returns next_state, which = next_state if cur_state doesn't match
#    the CPU_ID state
# Else updates next state 
# ----------------------------------------------------------------------
sub ParseCPUID {
  my $cur_state = $_[0];
  my $next_state = $_[1];
  my $token = $_[2];
  my $timestamp = $_[3];

  if ($cur_state == $sample_fields{"cpu_id"}) {

    # If we're expecting CPU_ID, make sure we got it
    if ($token !~ /^CPU\d/) {
      &ReportParseError("CPU ID");
    }
    $next_state++;
  }
  return $next_state;
}


# ----------------------------------------------------------------------
# --- sub ParseTimestamp(cur_state, next_state, index, token, line, timestamp array) ---
# Returns (next_state, index) and updates @timestamps
# 
# Cases:
#   1. Cur_state is not expecting timestamp: leave $next_state and $i unchanged
#   2. Cur_state expects timestamp
#      A. Token is a timestamp: append to @timestamps, increment next state
#         and $i by the number of fields in a timestamp
#      B. Token is not a timestamp (it got dropped)
#         i. We can infer it from a previous timestamp: infer it, increment 
#            next state, and decrement $i to process this field again
#         ii. There are no previous timestamps: drop the entire point by
#             resetting $cur_state to cpu_id
# ----------------------------------------------------------------------
sub ParseTimestamp {
   my $cur_state = $_[0];
   my $next_state = $_[1];
   my $index = $_[2];
   my $token = $_[3];
   my $line  = $_[4];
   my @timestamps = @{$_[5]};

   # Case 1 leaves $next_state and $index unchanged so don't worry about it

   # Case 2: Expecting timestamp
   if ($cur_state == $sample_fields{"day"}) {

     # Case 2A - Token is a timestamp
     if ($token !~ /^\d/) {
       push(@{$_[5]}, &Timestamp::StandardizeTimestamp($line, \%sample_fields));
       $next_state += $num_time_fields;
       $index += $num_time_fields - 1;
     }

     # Case 2B - Token is not a timestamp
     else {

        # Case 2Bii - No previous timestamps
        if ($#timestamps < 0) {
           print "Throwing out initial perfctr data point: no timestamp\n";
           $next_state = $sample_fields{"cpu_id"};
           $index = $last_field;
        }

        # Case 2Bi - Infer previous timestamp
        else { 
          push(@{$_[5]}, &Timestamp::AddTime($timestamps[$#timestamps], $main::sampling_int_sec));
          $next_state+=$num_time_fields;
          $index--;
        }
     }
   }
  
   return ($next_state, $index); 
}

# ----------------------------------------------------------------------
# --- sub ParsePerfctr(cur_state, next_state, expected_state(string),
#        token, perfctr_array)
# Returns next_state, which = next_state if cur_state doesn't match
#    the expected state
# Else updates next state and appends token to perfctr array
# ----------------------------------------------------------------------
sub ParsePerfctr {
  my $cur_state = $_[0];
  my $next_state = $_[1];
  my $expected_state = $_[2];
  my $token = $_[3];

  if ($cur_state == $sample_fields{$expected_state}) {
    if ($token !~ /^\d/) {
      &ReportParseError($perfctr_names{$expected_state});
    }
    else {
      push(@{$_[4]}, $token);
    }
    $next_state++;
  }
  return $next_state; 
}

# ----------------------------------------------------------------------
# --- sub FinishParsingCore(@timestamps, @perfctr1...4) ---
# Adjust parsed data by removing last data point and fixing timestamps
# Then add it to global data structs
# ----------------------------------------------------------------------
sub FinishParsingCore {

  my @timestamps = @{$_[0]};
  my @perfctr1_t   = @{$_[1]};
  my @perfctr2_t   = @{$_[2]};
  my @perfctr3_t   = @{$_[3]};
  my @perfctr4_t   = @{$_[4]};

  # Remove the last data point, which represents aggregate totals
  # and not a single sample
  pop(@timestamps);
  pop(@perfctr1_t);
  pop(@perfctr2_t);
  pop(@perfctr3_t);
  pop(@perfctr4_t);

  # Interpolate missing timestamps
  my @temp_timestamps;

  if ($num_perfctrs >= 4) {
    @temp_timestamps = &Timestamp::FixTimestamps(\@timestamps,\@perfctr4_t,$main::sampling_int_sec); 
    push(@perfctr4, \@perfctr4_t);
  }
  if ($num_perfctrs >= 3) {
    @temp_timestamps = &Timestamp::FixTimestamps(\@timestamps,\@perfctr3_t,$main::sampling_int_sec); 
    push(@perfctr3, \@perfctr3_t);
  }
  if ($num_perfctrs >= 2) {
    @temp_timestamps = &Timestamp::FixTimestamps(\@timestamps,\@perfctr2_t,$main::sampling_int_sec); 
    push(@perfctr2, \@perfctr2_t);
  }
  @timestamps = &Timestamp::FixTimestamps(\@timestamps,\@perfctr1_t,$main::sampling_int_sec);
  push(@perfctr1, \@perfctr1_t);
  push(@inpt_timestamps, \@timestamps);
}


# ----------------------------------------------------------------------
# --- sub ReportParseError(expected_string) ---
# Report timestamped error in parsing input data
# ----------------------------------------------------------------------
sub ReportParseError {
  die "Perfctr log parse error: $_[0] expected\n";
}

# ----------------------------------------------------------------------
# --- sub FindBenchmarkTimestamps() ---
# Set @benchmark_start_timestamps by calling main::FindBenchmarkTimestamps
# once per CPU
# ----------------------------------------------------------------------
sub FindBenchmarkTimestamps {
  foreach my $cpu (0 .. $main::num_cpus-1) {
     my @timestamps_this_cpu = @{$inpt_timestamps[$cpu]};
     push(@benchmk_start_timestamps, &main::FindBenchmarkTimestamps(\@timestamps_this_cpu, "CPU[$cpu] performance counters"));
  }
}

# ----------------------------------------------------------------------
# --- sub PrintOutputHeaders(filehandle)
# Print column headings to CSV
# ----------------------------------------------------------------------
sub PrintOutputHeaders {
  my $fh = $_[0];

  if ($num_perfctrs < 1) { return; }
  &PrintHeader($perfctr_names{"perfctr1"}, $fh); 

  if ($num_perfctrs < 2) { return; }
  &PrintHeader($perfctr_names{"perfctr2"}, $fh); 

  if ($num_perfctrs < 3) { return; }
  &PrintHeader($perfctr_names{"perfctr3"}, $fh); 

  if ($num_perfctrs < 4) { return; }
  &PrintHeader($perfctr_names{"perfctr4"}, $fh); 
}

# ----------------------------------------------------------------------
# --- sub PrintHeader(string, filehandle) ---
# ----------------------------------------------------------------------
sub PrintHeader {
  my $string = $_[0];
  my $fh    = $_[1];
  
  foreach my $cpu (0 .. $main::num_cpus-1) {
    print $fh ", CPU[" . $cpu . "]-$string";
  }
  if ($main::num_cpus > 1) {
    print $fh ", TOTAL-$string";
  }
}

# ----------------------------------------------------------------------
# --- sub PrintDataPoint(index, filehandle) ---
# Print (index)th benchmark data point to filehandle
#    for each metric and each CPU
# ----------------------------------------------------------------------
sub PrintDataPoint {
  my $index = $_[0];
  my $fh    = $_[1];
  
  if ($num_perfctrs < 1) { return; }
  &PrintDPMetric($index, \@perfctr1, $fh); 

  if ($num_perfctrs < 2) { return; }
  &PrintDPMetric($index, \@perfctr2, $fh); 

  if ($num_perfctrs < 3) { return; }
  &PrintDPMetric($index, \@perfctr3, $fh); 

  if ($num_perfctrs < 4) { return; }
  &PrintDPMetric($index, \@perfctr4, $fh); 
}

# ----------------------------------------------------------------------
# --- sub PrintDPMetric(index, perfctr array, filehandle) ---
# Print the (index)th element for a particular metric
# ----------------------------------------------------------------------
sub PrintDPMetric {
   my $index = $_[0];
   my @perfctrs = @{$_[1]};
   my $fh = $_[2];

   my $cur_total = 0;

   foreach my $cpu (0 .. $main::num_cpus-1) {
     my @perfctr_core = @{$perfctrs[$cpu]};
     my $data_point = $perfctr_core[$benchmk_start_timestamps[$cpu] + $index];
     print $fh ", $data_point";
     $cur_total += $data_point;  
   }
   if ($main::num_cpus > 1) {
     print $fh ", $cur_total";
   }
}

return 1;
