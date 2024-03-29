#!/usr/bin/perl

=head1 NAME

Timestamp - Modules for manipulating timestamps generated by other programs

=head1 INTERFACE

=over

=item * B<StandardizeTimestamp>($string, \%field_map) - Takes a string containing time and date information and a hash mapping fields of the string to the information they provide (e.g. "date" => 1). Returns a timestamp in standard form (defined internally).  Hash must define keys "date", "time", "year", "month", and optionally "ampm".

=item * B<FixTimestamps>(\@timestamp_array, @values_array, sampling int) - Takes an array of timetamps, an array of values associated with each timestamp, and the sampling interval between timetamps in seconds.  If missing timestamps are detected, interpolates to get the missing values (modifying values_array in place) and returns a timestamp array with missing timestamps included (does not modify timestamp_array). Eliminates duplicate timestamps and their associated values (retains first sample only). Assumes timestamps are all valid and standardized.

=item * B<AddTime>($timestamp, $interval) - Takes a valid timestamp and an interval in seconds, and returns a timestamp = to $timestamp + $interval. 

=item * B<SubtractTime>($timestamp1, $timestamp2) - Returns ($timestamp1 - $timestamp2) in seconds. Assumes that both are valid and standardized. 

=item * B<InferDate>($timestamp, $timefield) - The idea is to generate a date field for data that is timestamped but with no date, using information from the logfile. Takes a valid timestamp and a string consisting of hh:mm:ss in 24-hour time.  Assumes that the timestamp contains the previously known valid date and that hh:mm:ss is later than $timestamp.  Therefore, if hh:mm:ss is earlier than the TIME FIELD of $timestamp, it must be the next day. Otherwise, it must be the same day.  Returns a timestamp consisting of hh:mm:ss and the inferred date.

=back

=cut

package Timestamp;
use strict;
use POSIX qw(ceil floor);

# --- Functions intended for external use ---
sub StandardizeTimestamp;  # Convert inpt string w/ timestamp to std format
sub FixTimestamps;         # Find missing timestamps and interpolate
sub AddTime;               # Add (timestamp + #secs = timestamp)
sub SubtractTimestamps;    # Subtract (timestamp - timestamp = #secs)
sub InferDate;             # From a last known date + new TS, infer date of new TS

# --- Internal helper functions ---
sub ConvertTo24Hr;
sub ExtendField;
sub SplitTimeFields;
sub JoinTimeFields;
sub SplitDateFields;
sub JoinDateFields;
sub JoinTimeDate;
sub GetTimeField;
sub GetDateField;
sub ValidateNonStdTimestamp;
sub ValidateTimestamp;
sub IsLeapYear;
sub IsLeapDay;
sub GetNumSecs;
sub AddTimeField;
sub IncrementDate;
sub DecrementDate;

# ----- Constants -------------------------------------------------------

# --- Specification for standard timestamp format
# Standard form is
# hh:mm:ss yyyy-Mon-dd [24 hr clock] 

# First-level fields: date and time
my($ts_time_f)=0;
my($ts_date_f)=1;

# Subfields of time field
my($ts_time_hr_f)=0;
my($ts_time_min_f)=1;
my($ts_time_sec_f)=2;

# Subfields of date field
my($ts_date_yr_f)=0;
my($ts_date_month_f)=1;
my($ts_date_date_f)=2;

# Separators for fields and subfields
my($ts_separator)=' ';
my($ts_time_separator)=':';
my($ts_date_separator)='-';

# --- Useful date-related constants
my %days_per_month = (
  "Jan" => 31, "Feb" => 28, "Mar" => 31,
  "Apr" => 30, "May" => 31, "Jun" => 30,
  "Jul" => 31, "Aug" => 31, "Sep" => 30,
  "Oct" => 31, "Nov" => 30, "Dec" => 31,
);

my %next_month = (
  "Jan" => "Feb", "Feb" => "Mar", "Mar" => "Apr",
  "Apr" => "May", "May" => "Jun", "Jun" => "Jul",
  "Jul" => "Aug", "Aug" => "Sep", "Sep" => "Oct",
  "Oct" => "Nov", "Nov" => "Dec", "Dec" => "Jan",
);

# Month index
my %prev_month = (
  "Jan" => "Dec", "Feb" => "Jan", "Mar" => "Feb",
  "Apr" => "Mar", "May" => "Apr", "Jun" => "May",
  "Jul" => "Jun", "Aug" => "Jul", "Sep" => "Aug",
  "Oct" => "Sep", "Nov" => "Oct", "Dec" => "Nov",
);

# ----- Subroutines -------------------------------------------------------

# ----------------------------------------------------------------------
# --- sub StandardizeTimestamp(string, field_map) ---
# Input:
#   * A string containing a timestamp (and possibly other things)
#   * Hash showing which fields of the string correspond to date/time info
# Returns:
#   * Timestamp of standard form (see Constants, above)
# ----------------------------------------------------------------------
sub StandardizeTimestamp {
  my %fields = %{$_[1]};
  my $timestamp;

  # Verify input
  &ValidateNonStdTimestamp($_[0], $_[1]);

  # Split original timestamp into fields
  my @inp_array = split(/[\ \t\n]+/, $_[0]);
 
  # Convert to 24-hour time if necessary
  $inp_array[$fields{"time"}] = &ConvertTo24Hr($_[0], \%fields);
  
  # Truncate month to 3 letters
  $inp_array[$fields{"month"}] = substr($inp_array[$fields{"month"}], 0, 3);

  # Extend time and date to 2 digits if necessary
  $inp_array[$fields{"date"}] = &ExtendField($inp_array[$fields{"date"}], 2);

  # Convert original format to standardized format
  my @date_array = ($inp_array[$fields{"year"}], $inp_array[$fields{"month"}], $inp_array[$fields{"date"}]);
  $timestamp = &JoinTimeDate($inp_array[$fields{"time"}], &JoinDateFields(\@date_array));

  # Make sure that resulting timestamp is valid
  &ValidateTimestamp($timestamp);
  return $timestamp;
}

# ----------------------------------------------------------------------
# --- sub FixTimestamps(\@timestamp_array, @values_array, sampling_int) 
# Input:
#   * Array of timestamps
#   * Array of values associated with each timestamp
#   * Sampling interval in seconds
# Output: 
#   * Returns array of timestamps
#   * Modifies values_array
#   If a sample is missing (as indicated by a missing timestamp),
#   adds the missing timestamp and interpolates to produce the missing value.
#   If duplicate samples are present, deletes all but the first duplicated TS
#   and associated value.
# Assumptions: Timestamps are in standardized format
# ----------------------------------------------------------------------
sub FixTimestamps {
  my @new_timestamps = @{$_[0]};
  my @values;
  my $interval   = $_[2];
  my $prev_timestamp = $new_timestamps[0];
  my $i;

  for ($i=1; $i <= $#new_timestamps; $i++) {
    my $expected_stamp = &AddTime($new_timestamps[$i-1], $interval);

    # If we have a duplicate timestamp
    if ($prev_timestamp eq $new_timestamps[$i]) {
      print "\tDuplicate timestamp at $prev_timestamp, deleting timestamp\n";

      # Delete it and its associated value
      splice(@new_timestamps,$i,1);
      splice(@{$_[1]},$i,1);
      $i--;
    }

    # If an expected data point is missing
    elsif ($new_timestamps[$i] ne $expected_stamp) {
      @values = @{$_[1]};

      # Interpolate to get the missing value
      my $est_val = ($values[$i-1] + $values[$i])/2;
      print "\tMissing sample between $new_timestamps[$i-1] and $new_timestamps[$i]; interpolating ($expected_stamp, $est_val)\n";
      
      # Insert the missing timestamp and value
      splice(@new_timestamps,$i,0,$expected_stamp);
      splice(@{$_[1]},$i,0,$est_val); 
    } 
    $prev_timestamp = $new_timestamps[$i];
  }
  return (@new_timestamps); 
}

# ----------------------------------------------------------------------
# --- sub AddTime (timestamp, interval) ---
# Input:
#   * Valid timestamp
#   * Number of seconds to add to it
# Output: 
#   * Returns new_timestamp = timestamp + interval
# ----------------------------------------------------------------------
sub AddTime {
  my $newstamp;          # Resulting complete timestamp
  my @new_time_fields, my @new_date_fields;
  my $inc;

  &ValidateTimestamp($_[0]);
  my @old_time_fields = &SplitTimeFields($_[0]); 
  my @old_date_fields = &SplitDateFields($_[0]); 

  # Add each field 
  ($new_time_fields[$ts_time_sec_f], $inc) = &AddTimeField($old_time_fields[$ts_time_sec_f], $_[1], 60);
  ($new_time_fields[$ts_time_min_f], $inc) = &AddTimeField($old_time_fields[$ts_time_min_f], $inc, 60);
  ($new_time_fields[$ts_time_hr_f], $inc)  = &AddTimeField($old_time_fields[$ts_time_hr_f], $inc, 24);

  # Increment or decrement date
  @new_date_fields = @old_date_fields;
  if ($inc > 0) { @new_date_fields = &IncrementDate(\@old_date_fields, $inc); }
  if ($inc < 0) { @new_date_fields = &DecrementDate(\@old_date_fields, $inc); }

  # Extend any necessary fields
  foreach my $field(@new_time_fields) { $field = &ExtendField($field, 2); }
  $new_date_fields[$ts_date_date_f] = &ExtendField($new_date_fields[$ts_date_date_f], 2);

  $newstamp = &JoinTimeDate(&JoinTimeFields(\@new_time_fields), &JoinDateFields(\@new_date_fields));

  return $newstamp;
}

# ----------------------------------------------------------------------
# --- sub SubtractTimestamps(timestamp1, timestamp2) ---
# Input:  2 valid timestamps TS1 and TS2
# Output: Returns the difference (TS1 - TS2) in seconds: 
#         negative if 1st is earlier than 2nd
# ----------------------------------------------------------------------
sub SubtractTimestamps { return (&GetNumSecs($_[0]) - &GetNumSecs($_[1])); }

# ----------------------------------------------------------------------
# --- sub InferDate(timestamp, timefield) ---
# Inputs:
#   * Most recent (valid) timestamp
#   * A time field (hh:mm:ss) on a 24-hour clock
# Output:
#   * Returns standard timestamp with the input time + a date inferred from the most recent TS
# ----------------------------------------------------------------------
sub InferDate {
  my $prev_timestamp = $_[0];
  my $new_time       = $_[1];
  my $new_timestamp;

  &ValidateTimestamp($prev_timestamp);

  # Start by assuming new_timestamp takes the same day as prev_timestamp
  $new_timestamp = &JoinTimeDate($new_time, &GetDateField($prev_timestamp));

  # If that assumption makes new_timestamp earlier than prev_timestamp,
  # new_timestamp must be a day later
  if (&SubtractTimestamps($new_timestamp, $prev_timestamp) < 0) {
     $new_timestamp = &AddTime($new_timestamp, 24*60*60);
  } 

  return $new_timestamp;
}

# ----------------------------------------------------------------------
# Internal helper functions

# ----------------------------------------------------------------------
# --- sub ConvertTo24Hr(timestamp, \%field_map) ---
# Inputs: A timestamp in any format and a field map
# Output: Returns a string containing the time in format hh:mm:ss
# Assumptions: Timestamp contains AT LEAST a time of form hh:mm:ss
# ----------------------------------------------------------------------
sub ConvertTo24Hr {
  my %fields = %{$_[1]};
  my @time;
  my @inp_array = split(/[\ \t\n]+/, $_[0]);

  @time = split($ts_time_separator, $inp_array[$fields{"time"}]);
  if (exists($fields{"ampm"})) {
     # If time is PM, need to add 12 hours (except for 12:00 PM, which should not become 24:00)
     if ($inp_array[$fields{"ampm"}] eq "PM") {
       if ($time[$ts_time_hr_f] < 12) { $time[$ts_time_hr_f] += 12; }
     }

     # AM times are OK, except for 12:00 AM, which becomes 0:00
    else {
      if ($time[$ts_time_hr_f] == 12) { $time[$ts_time_hr_f] = "00"; }
    }
  }

  return (join($ts_time_separator, @time));
}

# ----------------------------------------------------------------------
# --- sub ExtendField ---
# Input:
#   * A numeric string
#   * The desired length of the string
# Output:
#   * Zero-extended string of desired length
# Assumptions:
#   * string is numeric (doesn't check)
#   * string is <= desired length (if not, dies)
# ----------------------------------------------------------------------
sub ExtendField {
  my $field       = $_[0];
  my $desired_len = $_[1];

  (length($field) <= $desired_len) or die "Field $field has is longer than desired length $desired_len\n";

  while (length($field) < $desired_len) {
    $field = '0' . $field; 
  }

  return $field;
}

# ----------------------------------------------------------------------
# --- sub SplitTimeFields ---
# Input:  Valid timestamp
# Output: Array with fields for hour, minute, and second
# ----------------------------------------------------------------------
sub SplitTimeFields { return (split($ts_time_separator, &GetTimeField($_[0]))); }

# ----------------------------------------------------------------------
# --- sub JoinTimeFields ---
# Input: Array with fields for hour, minute, and second
#        (as specified in standard)
# Output: Time as string
# ----------------------------------------------------------------------
sub JoinTimeFields { return (join($ts_time_separator, @{$_[0]})); }

# ----------------------------------------------------------------------
# --- sub SplitDateFields ---
# Input:  Valid timestamp
# Output: Array with fields for year, month, day
# ----------------------------------------------------------------------
sub SplitDateFields { return (split($ts_date_separator, &GetDateField($_[0]))); }

# ----------------------------------------------------------------------
# --- sub JoinDateFields ---
# Input: Array with fields for year, month, day
#        (as specified in standard)
# Output: Date as string
# ----------------------------------------------------------------------
sub JoinDateFields { return (join($ts_date_separator, @{$_[0]})); }

# ----------------------------------------------------------------------
# --- sub JoinTimeDate(time, date) ---
# Input: 
#   * A valid time string
#   * A valid date string 
# Output: A timestamp in standard format
# ----------------------------------------------------------------------
sub JoinTimeDate { return ($_[0] . $ts_separator . $_[1]); }

# ----------------------------------------------------------------------
# --- sub GetTimeField(timestamp) ---
# Input:  Valid timestamp
# Output: Input's time field as string
# ----------------------------------------------------------------------
sub GetTimeField { return (&GetField($_[0], $ts_time_f)); }

# ----------------------------------------------------------------------
# --- sub GetTimeField(timestamp) ---
# Input:  Valid timestamp
# Output: Input's date field as string
# ----------------------------------------------------------------------
sub GetDateField { return (&GetField($_[0], $ts_date_f)); }

# ----------------------------------------------------------------------
# --- sub GetField(timestamp, field_num) ---
# Input:
#   * Valid timestamp
#   * Field number
# Output: Requested field of timestamp
# ----------------------------------------------------------------------
sub GetField { return ((split($ts_separator, $_[0]))[$_[1]]); }

# ----------------------------------------------------------------------
# --- sub ValidateNonStdTimestamp(timestamp, \%field_map) ---
# Input:  
#  * A (non-standardized) timestamp string
#  * A map showing which fields of the string contain which data
# Output: Dies if necessary fields don't exist
# ----------------------------------------------------------------------
sub ValidateNonStdTimestamp {
  my $string = $_[0];
  my %fields = %{$_[1]};
  my @inp_array = split(/[\ \t\n]+/, $string);

  # Check that field map contains necessary information
  die "Bad timestamp: $string\n" unless (
      (exists $fields{"date"}) and 
      (exists $fields{"month"}) and 
      (exists $fields{"year"}) and
      (exists $fields{"time"}));

  # Check that those fields exist
  die "Bad timestamp: $string\n" unless (
      (exists $inp_array[$fields{"date"}]) and 
      (exists $inp_array[$fields{"month"}]) and 
      (exists $inp_array[$fields{"year"}]) and
      (exists $inp_array[$fields{"time"}]));
}

# ----------------------------------------------------------------------
# --- sub ValidateTimestamp(timestamp) ---
# Input:  A timestamp string
# Output: Dies if timestamp is invalid or not standardized
# ----------------------------------------------------------------------
sub ValidateTimestamp {
  my @time_fields = &SplitTimeFields($_[0]);
  my @date_fields = &SplitDateFields($_[0]); 
  my $die_msg = "Timestamp $_[0] is invalid\n";

  # Validate hour
  my ($hour) = $time_fields[$ts_time_hr_f];
  die ($die_msg) unless (($hour <= 23) and ($hour >= 0) and (length($hour) == 2));

  # Validate minute 
  my ($min) = $time_fields[$ts_time_min_f];
  die ($die_msg) unless (($min <= 59) and ($min >= 0) and (length($min) == 2));

  # Validate second 
  my ($sec) = $time_fields[$ts_time_sec_f];
  die ($die_msg) unless (($sec <= 59) and ($sec >= 0) and (length($sec) == 2));

  # Validate year
  my ($year) = $date_fields[$ts_date_yr_f];
  die ($die_msg) unless (($year > 0) and (length($year) == 4));

  # Validate month
  my ($month) = $date_fields[$ts_date_month_f];
  die ($die_msg) unless (exists($days_per_month{$month}));

  # Validate date
  my ($date) = $date_fields[$ts_date_date_f];
  die ($die_msg) unless ((length($date) == 2) and $date >= 0); 

  # If date exceeds the number of days per month, it is either Leap Day or invalid
  if ($date > $days_per_month{$month}) {
    die ($die_msg) unless (&IsLeapDay(\@date_fields));
  }  
}

# ----------------------------------------------------------------------
# --- sub IsLeapYear(year) --- 
# Input:  A number (year)
# Output: 1 if is leap year, 0 else
# ----------------------------------------------------------------------
sub IsLeapYear {
  if ($_[0] % 400 == 0) { return 1; }
  if ($_[0] % 100 == 0) { return 0; } 
  if ($_[0] % 4   == 0) { return 1; }
  return 0;
}

# ----------------------------------------------------------------------
# --- sub IsLeapDay(date) --- 
# Input:  The date fields of a timestamp (as an array)
# Output: 1 if it's Feb 29 of a leap year, 0 else
# ----------------------------------------------------------------------
sub IsLeapDay {
  my @date_fields = @{$_[0]};
  return ((&IsLeapYear($date_fields[$ts_date_yr_f])) 
     and ($date_fields[$ts_date_month_f] eq "Feb") 
     and ($date_fields[$ts_date_date_f] == 29));
}

# ----------------------------------------------------------------------
# --- sub GetNumSecs(timestamp) ---
# Input:  A valid timestamp
# Output: Number of seconds between 00:00:00 0000-Jan-01 and timestamp
# ----------------------------------------------------------------------
sub GetNumSecs {
  &ValidateTimestamp($_[0]);
  my @timefields = &SplitTimeFields($_[0]);
  my @datefields = &SplitDateFields($_[0]);
  
  # Add number of seconds in the years prior to the current year
  my $cur_yr = $datefields[$ts_date_yr_f];

  # Assumes 1st year is yr 1
  my $num_leap_days = ($cur_yr / 4) - ($cur_yr / 100) + ($cur_yr / 400);
  my $secs = ($cur_yr * 365 + $num_leap_days) * 24 * 3600;

  # Add number of seconds in prior months this year
  my $month     = "Jan";
  while ($month ne $datefields[$ts_date_month_f]) {
     $secs += $days_per_month{$month} * 24 * 3600;
     $month = $next_month{$month};
  }

  # Add number of seconds in the days of this month prior to the current day
  my $prev_day = $datefields[$ts_date_date_f];
  $secs += ($datefields[$ts_date_date_f]-1) * 24 * 3600;

  # Add number of seconds in the current day
  $secs += $timefields[$ts_time_hr_f] * 3600 + $timefields[$ts_time_min_f] * 60 + $timefields[$ts_time_sec_f];

  return $secs;
}

# ----------------------------------------------------------------------
# --- sub AddTimeField($field, $increment, $field_max) ---
# Add an increment to a single timestamp field, and return the new
# value + the increment to the next field.
# Input:
#   * Numerical value of the original field
#   * Increment to add to it
#   * The numerical limit for that field (60 for a minute field, e.g.)
# Output: 
#   * Numerical value of new field
#   * Increment to add to the next field
# ----------------------------------------------------------------------
sub AddTimeField {
  my $sum = $_[0] + $_[1];
  return ($sum % $_[2], floor($sum / $_[2]));
}

# ----------------------------------------------------------------------
# --- sub IncrementDate(\@date_array, increment) ---
# Move (date) forward by (increment) days
# Input:
#   * Array containing standard date fields
#   * Number of days to move forward
# Output:
#   * New date = (old date + increment) 
# ----------------------------------------------------------------------
sub IncrementDate {
  my @date_fields = @{$_[0]};
  my $inc         = $_[1];

  while ($inc > 0) {
    $date_fields[$ts_date_date_f]++;
   
    # If we're past the end of the month, increment month and set date to the 1st
    if (($date_fields[$ts_date_date_f] > $days_per_month{$date_fields[$ts_date_month_f]}) and 
        (&IsLeapDay(\@date_fields)) == 0) {
      $date_fields[$ts_date_date_f] = 1; 
      $date_fields[$ts_date_month_f] = $next_month{$ts_date_month_f};

      # If it's now January, increment year
      if ($date_fields[$ts_date_month_f] eq "Jan") {
        $date_fields[$ts_date_yr_f]++;
      }
    }
    $inc--;
  }
  return @date_fields; 
}

# ----------------------------------------------------------------------
# --- sub DecrementDate(\@date_array, decrement) ---
# Move (date) backward by (decrement) days
# Input:
#   * Array containing standard date fields
#   * Number of days to move backward 
# Output:
#   * New date = (old date + decrement) 
# ----------------------------------------------------------------------
sub DecrementDate {
  my @date_fields = @{$_[0]};
  my $dec = $_[1];

  while ($dec < 0) {
    $date_fields[$ts_date_date_f]--;
   
    # If it was the first of the month, need to go back to previous month 
    if ($date_fields[$ts_date_date_f] <= 0) { 
      $date_fields[$ts_date_month_f] = $prev_month{$ts_date_month_f};
      $date_fields[$ts_date_date_f] = $days_per_month{$date_fields[$ts_date_month_f]};

      # If it's Feb of a leap year, there's an extra day on the end
      if ((&IsLeapYear($date_fields[$ts_date_yr_f])) and ($date_fields[$ts_date_month_f] eq "Feb")) {
	$date_fields[$ts_date_date_f]++;
      }
      # If it's now Dec. 31, decrement the year
      if ($date_fields[$ts_date_month_f] eq "Dec") {
        $date_fields[$ts_date_yr_f]--;
      }
    }
    $dec++;
  }
  return @date_fields;
}
