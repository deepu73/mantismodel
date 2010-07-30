#!/usr/bin/perl

=head1 NAME

data_postprocess.pl: Take a csv output by data_parse, and create a new csv that aggregates some of the columns

=head1 USAGE

./data_postprocess.pl -p=[param-file] -t=[CSV trace file]

=head1 ASSUMPTIONS

Parameter file has 0 or more lines in the following format:

[NEW_HEADER_NAME] = [OLD_HEADER_NAME1] [OLD_HEADER_NAME2]...

Lines beginning with "#" are ignored.

=cut

use strict;
$|++;
use FileHandle;
use Getopt::Long;

# --- Subroutines ------------------------------------------------------
sub ReadParamFile;      # Read param file into 2D array

sub SplitCSVLine;       # Split line of CSV file on commas and whitespace
sub ParseCSVHeader;     # Get headings from first line of CSV file

sub GetOutfileName;     # Derive output file name from input file name
sub GetNewHeaders;      # Get names of new headers from param file
sub ComputeNewMetrics;  # Compute the new aggregate metrics for given line

# --- Global vars ------------------------------------------------------
my $csv_file=0;
my %g_csv_headers;   # CSV header names mapped to field indices

# ----------------------------------------------------------------------
# --- Main program
# ----------------------------------------------------------------------
{
my $param_file=0;
my @params;

&GetOptions("p=s" => \$param_file, "t=s" => \$csv_file);
die "Usage: data_postprocess.pl -p=[param file] -t=[CSV trace file]\n" 
   unless ($param_file and $csv_file);

my $out_file = &GetOutfileName($csv_file);
open (CSVFILE, "<$csv_file") or die "Couldn't open input file $csv_file\n";
open (OUTFILE, ">$out_file") or die "Couldn't open output file $out_file\n";

@params = &ReadParamFile($param_file);

# Handle header line of input and output files
my $line = <CSVFILE>;
chomp $line;
&ParseCSVHeader($line);

if (@params) {
  $line .= (", " . &GetNewHeaders(\@params));
}
print OUTFILE "$line\n";

foreach $line (<CSVFILE>) {
  chomp $line;
  
  if (@params) { 
    $line .= (", " . &ComputeNewMetrics($line, \@params));
  }

  print OUTFILE "$line\n";

}

close CSVFILE;
close OUTFILE;

}

# ----------------------------------------------------------------------
# --- ReadParamFile(filename)
# Read filename into an array; return array.
# Die if filename can't be opened.
# ----------------------------------------------------------------------
sub ReadParamFile {
  open (PARAMFILE, "<$_[0]") or die "Couldn't open parameter file $_[0]\n";

  my @params;
  foreach my $line (<PARAMFILE>) {
    chomp ($line);
    if ((length($line) == 0) or ($line =~ /^#/)) { next; }

    my @this_line = split(/\s+/, $line);

    if ($this_line[1] ne "=") {
       print "Improperly specified parameter: $line\n";
       next;
    }
    splice(@this_line, 1, 1);

    push(@params, \@this_line);
  }

  close PARAMFILE;
  return @params;
}

# ----------------------------------------------------------------------
# --- SplitCSVLine($line) 
# Split line of CSV file into fields based on commas and whitespace
# ----------------------------------------------------------------------
sub SplitCSVLine {
 my $line = $_[0];

 # Remove whitespace
 $line =~ s/\s+//g;

 return (split(',',$line));
}


# ----------------------------------------------------------------------
# --- ParseCSVHeader
# Get headings from first line of CSV file
# ----------------------------------------------------------------------
sub ParseCSVHeader {
 my @fields = &SplitCSVLine($_[0]);

 for (my $i=0; $i <= $#fields; $i++) {
   $g_csv_headers{$fields[$i]} = $i;
 } 

}

# -------------------------------------------------------------------------
# --- GetOutfileName($infile_name)
# Get output file name: if input filename is xx.yy, output file is xx-pp.yy
# -------------------------------------------------------------------------
sub GetOutfileName {
  my $infile_name = $_[0];

  # Get position of file extension (the rightmost '.' in the filename)
  my $file_ext_index = rindex($infile_name, '.');

  # If no extension
  if ($file_ext_index == -1) {
    return ($infile_name . "-pp");
  }
 
  return (substr($infile_name, 0, $file_ext_index) . "-pp" . substr($infile_name, $file_ext_index)); 
}

# -------------------------------------------------------------------------
# --- GetNewHeaders(@params)
# Get header names from parameter file lines
# -------------------------------------------------------------------------
sub GetNewHeaders {
  my @params = @{$_[0]};
  my @new_hdrs;

  foreach my $ref (@params) {
     my @param = @{$ref};
     push(@new_hdrs, $param[0]);
  }

  return (join(", ", @new_hdrs));
}

# -------------------------------------------------------------------------
# --- ComputeNewMetrics($line, @params)
# Compute values of new aggregate metrics for given line
# -------------------------------------------------------------------------
sub ComputeNewMetrics {
  my $line = $_[0];
  my @params = @{$_[1]};
  my @new_values;
  my @fields = &SplitCSVLine($line);

  foreach my $ref (@params) {
    my @hdr_array = @{$ref};
    my $new_value = 0;

    for my $i (1 .. $#hdr_array) {
      my $field_name = $hdr_array[$i];

      if (exists ($g_csv_headers{$field_name})) {
        my $field_num = $g_csv_headers{$field_name};
        $new_value += $fields[$field_num];
      }
      else { die "Field $field_name does not exist\n"; }
    }
    push(@new_values, $new_value);
  }
  
  return (join(", ", @new_values));

}
