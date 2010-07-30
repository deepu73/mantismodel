#!/usr/bin/perl

# Program to generate tracefiles (input files for calibration benchmarks)
# Usage: ./trace_gen.pl -cfg=<config_file>
#    Config_file = location of configuration file. Omitting this parameter means default of "config.txt" in current directory.
#
# Files generated:
#    Calibration input files (in trace directory specified in config file)
#    Calibration timing files (same directory)
#
use strict;
use Getopt::Long;
use Text::ParseWords;
use ParseCfgFile;

# Length in seconds of CPU, memory, and disk traces
my($cpu_trace_sec);
my($mem_trace_sec);
my($disk_trace_sec);

# Amount of extra time to allow for calibration programs beyond the amount specified in the input file
my($trace_padding_sec) = 30; 

# Command-line arguments
my($cfg_file) = "config.txt";
my(%config);

# Use test traces? Set to 1 to produce shorter traces for debugging.
my $use_test = 0;


# ---------------- Main program ----------------------

# Parse command line
GetOptions('cfg=s'   => \$cfg_file);
%config = %{&ParseCfgFile::ParseCfgFile($cfg_file)};

# Generate trace files
if ($config{"RUN_CALIB_CPU"} eq 'y')  { &GenCPUTrace; &WriteTimingFile("cpu", $cpu_trace_sec + $trace_padding_sec);}
if ($config{"RUN_CALIB_MEM"} eq 'y')  { &GenMemTrace; &WriteTimingFile("mem", $mem_trace_sec + $trace_padding_sec);}
if ($config{"RUN_CALIB_DISK"} eq 'y') { &GenDiskTrace; &WriteTimingFile("disk", $disk_trace_sec + $trace_padding_sec);}

# ---------------- end main program ----------------------


# ---------------- sub WriteTimingFile ----------------------
# Writes component timing file
# Parameters: The component and the number of seconds to write
sub WriteTimingFile {
  my($timefile)=$config{"TRACEFILE_DIR"} . "/" . $config{"TRACE_ID"} . "-" . $_[0] . "-timing";
  die "Couldn't create timing file $timefile" unless (open TIMEFILE, ">$timefile");
  print TIMEFILE $_[1];
  close TIMEFILE;
}
# ---------------- end sub WriteTimingFile ----------------------

# ---------------- sub GenDiskTrace ----------------------
# Generate input file for disk calibration program
sub GenDiskTrace {

  my($tracefile)=$config{"TRACEFILE_DIR"} . "/" . $config{"TRACE_ID"} . "-disk";
  my($cur_time)=0;

# Options for gamut worker threads
  my(@workfiles)=split (',' , $config{"CALIB_DISK_WORKFILES"});
  my($threadtime)=55;
  my($buffertime)=5;
  my($nblks)="50M";
  my(@blksizes);
  my(@iorates);
  my(@iomixes);

  # Real traces
  if ($use_test == 0) {
    @blksizes=("1K","2K","4K","8K","16K","32K");
    @iorates=("1K","10K","100K","1M","10M","100M","1G");
    @iomixes=("0/1/0","0/1/1");
  }
  # Test/debug traces
  else {
    @blksizes=("1K");
    @iorates=("1K");
    @iomixes=("0/1/0");
  }

  die "Couldn't create tracefile" unless (open TRACEFILE, ">$tracefile");

# Iterate over:
# sequential vs. random
# blocksize
# iorate
  my($iomix); my($blksize); my($iorate); my($workfile);
  
  foreach $workfile(@workfiles) {
    foreach $iomix(@iomixes) {
      foreach $blksize(@blksizes) {
        foreach $iorate(@iorates) {
           print TRACEFILE "$cur_time.0 wctl add disk file=$workfile,blksize=$blksize,nblks=$nblks,mode=2,iorate=$iorate,iomix=$iomix,etime=$threadtime\n";
         $cur_time += $threadtime + $buffertime;
       }
      }
    }
   }
   print TRACEFILE "$cur_time quit\n";

   $disk_trace_sec=$cur_time;
   close(TRACEFILE);
}
# ---------------- end sub GenDiskTrace ----------------------

# ---------------- sub GenMemTrace ----------------------
# Generate input file for memory calibration program
sub GenMemTrace {

  my($tracefile)=$config{"TRACEFILE_DIR"} . "/" . $config{"TRACE_ID"} . "-mem";
  my($cur_time)=0;

# Options for gamut worker threads
  my(@wssizes);
  my($threadtime)=55;
  my($buffertime)=5;
  my(@iorates);
  my(@strides);
  
  if ($use_test == 0) {
    @iorates=("1024M","2048M","4096M","10240M","20480M","40960M","81920M", "102400M", "204800M", "409600M");
    @strides=(1,16);
  }
  else {
    @iorates=("1024M");
    @strides=("1");
  }

  die "Couldn't create tracefile" unless (open TRACEFILE, ">$tracefile");

# Iterate over:
#  working set sizes
#  sequential vs. random
#  iorate
  my($stride); my($iorate); my($wssize);

  push(@wssizes, $config{"CALIB_MEM_MAX_SIZE"} . "M", ($config{"CALIB_MEM_MAX_SIZE"}/2) . "M");

  foreach $wssize(@wssizes) {
    foreach $stride(@strides) {
       foreach $iorate(@iorates) {
             print TRACEFILE "$cur_time.0 wctl add mem total=" . $config{"CALIB_MEM_MAX_SIZE"} . "M,wset=$wssize,iorate=$iorate,stride=$stride,etime=$threadtime\n";
             $cur_time += $threadtime+$buffertime;
       }
    }
  }
  print TRACEFILE "$cur_time quit\n";

  $mem_trace_sec=$cur_time;
  close(TRACEFILE);
}
# ---------------- end sub GenMemTrace ----------------------

# ---------------- sub GenCPUTrace ----------------------
# Generate input file for CPU calibration program
sub GenCPUTrace {
  my($tracefile)     =$config{"TRACEFILE_DIR"} . "/" . $config{"TRACE_ID"} . "-cpu";

  my(@datatypes);
  my(@pctutils);
  my(@matdims);
  my(@secs);
  $cpu_trace_sec = 0;

  if ($use_test == 0) {
    @datatypes  =("int","fp");
    @pctutils   =(10,20,50,80,100);
    @matdims    =(100,500,1000);
    #@matdims   =(50,500,5000,50000);
    #@iters     =(20000,3000,14,3);
    @secs =(20,20,20);
  }
  else {
    @datatypes     =("int");
    @pctutils=(0,1);
    @matdims       =(50,500);
    @secs          =(120);
  }

  die "Couldn't create tracefile \"$tracefile\"" unless (open TRACEFILE, ">$tracefile");

# Iterate over:
# Datatypes
# Matrix sizes
# Utilizations
  my($datatype); my($matdim); my($pctutil);

  foreach $datatype(@datatypes) {
    for($matdim=0; $matdim<=$#matdims; $matdim++) {
        foreach $pctutil (@pctutils) {
              print TRACEFILE "$datatype $matdims[$matdim] $secs[$matdim] $pctutil\n";
	      $cpu_trace_sec += $secs[$matdim];
        }
      }
  }
  close(TRACEFILE);
}
# --- end sub GenCPUTrace --- 

