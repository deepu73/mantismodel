#!/usr/bin/perl
# TO DO: Make CPU trace time estimate more automatic
#
#
# Program to generate tracefiles and instrumentation scripts
# Usage: ./trace_gen.pl -comp=<component> -cfg=<config_file>
#    Component = "cpu", "mem", "disk". May specify multiple components, e.g., -comp=cpu,mem,disk
#       Omitting this parameter generates calibration traces for all components 
#    Config_file = location of configuration file. Omitting this parameter means default of "config-calib" in current directory.
#
# Files generated:
#    calibrate.sh: Script to run calibration programs
#    Calibration input files (in trace directory specified in config file)
#    metrics.sh: Script to collect utilization data
#    daq_benchmark.sh: Script to copy to DAQ machine, which will poll AC measurements and initiate runs
#
use strict;
use Getopt::Long;
use Text::ParseWords;
use Sys::Hostname;

# --- Use test traces or real (longer) ones?
my($use_test) = 0;

# Globals: whether to generate traces for cpu, memory, and disk
my($gen_cpu) = 0;
my($gen_mem) = 0;
my($gen_disk) = 0;

# Need to test multiple CPU frequencies?
my($cpu_scale_avail) = 0;

# Length in seconds of CPU, memory, and disk traces
my($cpu_trace_sec) = 650; # SMR- Fix
my($mem_trace_sec);
my($disk_trace_sec);

# Command line variables
my(@components);
my($cfg_file) = "config-calib";

my(%config);

# --- Main program ---

# Parse command line
GetOptions('comp=s@' => \@components,
           'cfg=s'   => \$cfg_file);
&CheckOptions;

# Parse config file
&ParseCfgFile;

# Generate trace files
if ($gen_disk) { &GenDiskTrace; }
if ($gen_mem)  { &GenMemTrace; }
if ($gen_cpu)  { &GenCPUTrace; }

# Generate metrics and calibration scripts
&GenMetricScript;
&GenCalibrationScript;
&GenDAQScript;

close CFG_FILE;
# -- End main program ---

# --- sub CheckOptions ---
# Set which components to generate traces for; open configuration file
sub CheckOptions {
# Parse "comp" parameter
  if (@components) {
    my($component);
    foreach $component (@components) {
      if    (lc($component) eq "cpu")  { $gen_cpu  = 1; }
      elsif (lc($component) eq "mem")  { $gen_mem  = 1; }
      elsif (lc($component) eq "disk") { $gen_disk = 1; } 
      else  { print "Unknown component \"$component\".\n"; }
    }
  }
# If "component" flag is not specified, generate all traces
  else {
    $gen_cpu  = 1;
    $gen_mem  = 1;
    $gen_disk = 1;
  }
# If no valid components were specified (but component field was not empty), exit
  if ($gen_cpu + $gen_mem + $gen_disk == 0) {
    print ("No valid components specified; no traces generated.\n");
    exit (0);
  }
# Open configuration file or die trying
  die "Config file \"$cfg_file\" does not exist" unless ((-e $cfg_file) and (open CFG_FILE, "<$cfg_file"));
}
# --- end sub CheckOptions ---

# --- sub ParseCfgFile ---
# Extract configuration parameters; check to make sure that the necessary params are defined
sub ParseCfgFile {
# Extract "name=value" pairs
  %config = map {
      s/#.*//; # Remove comments
      s/^\s+//; # Remove opening whitespace
      s/\s+$//;  # Remove closing whitespace
      m/(.*?)=(.*)/; 
   } <CFG_FILE>; 
#  Debug -- Print it out
#  use Data::Dumper;
#  print Dumper(\%config);

# Make sure general parameters are valid
  CfgFileDie("TRACEFILE_DIR") unless ( (exists($config{"TRACEFILE_DIR"})) and (-d $config{"TRACEFILE_DIR"}) ); 
  CfgFileDie("SCRIPT_DIR")    unless ( (exists($config{"SCRIPT_DIR"}))    and (-d $config{"SCRIPT_DIR"}) ); 
  CfgFileDie("DATA_DIR")      unless ( (exists($config{"DATA_DIR"}))      and (-d $config{"DATA_DIR"}) ); 
  CfgFileDie("TRACE_ID")      unless (exists($config{"TRACE_ID"}));

# Make sure CPU frequency scaling parameters are valid
  CfgFileDie("CPU_SCALING_AVAIL") unless (exists($config{"CPU_SCALING_AVAIL"}));
  if (lc($config{"CPU_SCALING_AVAIL"}) eq "y") {
    $cpu_scale_avail = 1;
    CfgFileDie("CPU_FREQS_AVAIL") unless (exists($config{"CPU_FREQS_AVAIL"}));
  }

# Make sure CPU calibration parameters are valid (if applicable)
  if ($gen_cpu) {
    CfgFileDie("CALIB_CPU_PROG") unless ((exists($config{"CALIB_CPU_PROG"})) and (-e $config{"CALIB_CPU_PROG"}) ); 
    CfgFileDie("CALIB_NUM_CPUS") unless ((exists($config{"CALIB_NUM_CPUS"})) and ($config{"CALIB_NUM_CPUS"} > 0) ); 
  }

# Make sure memory calibration parameters are valid (if applicable)
  if ($gen_mem) {
    CfgFileDie("CALIB_MEM_PROG")     unless ((exists($config{"CALIB_MEM_PROG"}))     and (-e $config{"CALIB_MEM_PROG"}) ); 
    CfgFileDie("CALIB_MEM_MAX_SIZE") unless ((exists($config{"CALIB_MEM_MAX_SIZE"})) and ($config{"CALIB_MEM_MAX_SIZE"} > 0) ); 
  }

# Make sure disk calibration parameters are valid (if applicable)
  if ($gen_disk) {
    CfgFileDie("CALIB_DISK_PROG") unless ((exists($config{"CALIB_DISK_PROG"})) and (-e $config{"CALIB_DISK_PROG"}) ); 
    CfgFileDie("CALIB_DISK_WORKFILES") unless (exists($config{"CALIB_DISK_WORKFILES"}));
  }

# Make sure metrics are specified
  CfgFileDie("METRICS_LABELS") unless (exists($config{"METRICS_LABELS"}));
  CfgFileDie("METRICS_CMDS") unless (exists($config{"METRICS_CMDS"}));

# Make sure DAQ parameters are specified
  CfgFileDie("DAQ_DATA_DIR") unless (exists($config{"DAQ_DATA_DIR"}));
  CfgFileDie("DAQ_PWR_PROG") unless (exists($config{"DAQ_PWR_PROG"}));
  CfgFileDie("DAQ_PWR_KILL") unless (exists($config{"DAQ_PWR_KILL"}));
}
# --- end sub ParseCfgFile ---

# --- sub CfgFileDie ---
# If configuration parameter is invalid, print message and die
sub CfgFileDie {
  die "Config file error: $_[0] unspecified or invalid.\n";
}
# --- end sub CfgFileDie ---

# --- sub GenDiskTrace ---
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

  if ($use_test == 0) {
    @blksizes=("1K","2K","4K","8K","16K","32K");
    @iorates=("1K","10K","100K","1M","10M","100M","1G");
    @iomixes=("0/1/0","0/1/1");
  }
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

   $disk_trace_sec=$cur_time + 20;
   close(TRACEFILE);
}
# --- end sub GenDiskTrace ---

# --- sub GenMemTrace ---
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

  $mem_trace_sec=$cur_time + 20;
  close(TRACEFILE);
}
# --- end sub GenMemTrace --- 

# --- sub GenCPUTrace --- 
sub GenCPUTrace {

  my($tracefile)     =$config{"TRACEFILE_DIR"} . "/" . $config{"TRACE_ID"} . "-cpu";

  my(@datatypes);
  my(@pctutils);
  my(@matdims);
  my(@iters);

  if ($use_test == 0) {
    @datatypes  =("int","fp");
    @pctutils   =(10,20,50,80,100);
    @matdims    =(50,500,1000);
    #@matdims   =(50,500,5000,50000);
    #@iters     =(20000,3000,14,3);
    @iters      =(10000,10,1);
  }
  else {
    @datatypes     =("int");
    @pctutils=(0,1);
    @matdims       =(50,500);
    @iters         =(20000);
  }

  die "Couldn't create tracefile" unless (open TRACEFILE, ">$tracefile");

# Iterate over:
# Datatypes
# Matrix sizes
# Utilizations
  my($datatype); my($matdim); my($pctutil);

  foreach $datatype(@datatypes) {
    for($matdim=0; $matdim<=$#matdims; $matdim++) {
        foreach $pctutil (@pctutils) {
              print TRACEFILE "$datatype $matdims[$matdim] $iters[$matdim] $pctutil\n";
        }
      }
  }
  close(TRACEFILE);
}
# --- end sub GenCPUTrace --- 

# --- end sub GenMetricScript --- 
# Create the script to gather system metrics
sub GenMetricScript{
  my($scriptfile) = $config{"SCRIPT_DIR"} . "/metrics.sh"; 
  die "Couldn't create scripts" unless (open SCRIPTFILE, ">$scriptfile");

  my(@labels)=quotewords(',' , 0, $config{"METRICS_LABELS"});
  my(@cmds)  =quotewords(',' , 0, $config{"METRICS_CMDS"});
  die "Metric labels and commands do not match\n" unless ($#labels == $#cmds);

  print SCRIPTFILE "# Collect system metrics\n";
  print SCRIPTFILE "# Usage: ./metrics.sh <prefix for output files> <secs to run>\n\n";
  print SCRIPTFILE "DATA_DIR=" . $config{"DATA_DIR"} . "\n\n";
  print SCRIPTFILE "SCRIPT_DIR=" . $config{"SCRIPT_DIR"} . "\n\n";
  print SCRIPTFILE "TRACEFILE_DIR=" . $config{"TRACEFILE_DIR"} . "\n\n";

  print SCRIPTFILE "echo \"\`date\` [MUT] -- Starting software instrumentation\"\n";

  my($i);
  for ($i=0; $i <= $#labels; $i++) {
    print SCRIPTFILE "echo \"\`date\` [MUT] -- " . $labels[$i] . "\"\n";
    print SCRIPTFILE $cmds[$i] . "\n";
  }

  print SCRIPTFILE "echo \"\`date\` [MUT] -- Metrics started\"\n";
  print SCRIPTFILE "wait\n";
  print SCRIPTFILE "echo \"\`date\` [MUT] -- Metrics ended\"\n";
  close(SCRIPTFILE);
  my($mode) = 0754; chmod $mode, $scriptfile;
}
# --- end sub GenMetricScript --- 

# --- sub GenCalibrationScript ---
sub GenCalibrationScript {
  my($scriptfile) = $config{"SCRIPT_DIR"} . "/calibrate.sh"; 
  die "Couldn't create scripts" unless (open SCRIPTFILE, ">$scriptfile");

  print SCRIPTFILE "# Copy this file to DAQ\n";
  print SCRIPTFILE "# Run calibration suite and OS metrics\n";
  print SCRIPTFILE "REMOTE=" . hostname . "\n";
  print SCRIPTFILE "LOCAL=\`hostname\`\n";
  print SCRIPTFILE "USER=root\n";
  print SCRIPTFILE "\nREMOTE_DATA_DIR=" . $config{"DATA_DIR"} . "\n";
  print SCRIPTFILE "REMOTE_SCRIPT_DIR=" . $config{"SCRIPT_DIR"} . "\n";
  print SCRIPTFILE "REMOTE_TRACEFILE_DIR=" . $config{"TRACEFILE_DIR"} . "\n";
  print SCRIPTFILE "TRACE_ID=" . $config{"TRACE_ID"} . "\n\n";

# Run CPU calibration -- 1...N CPUs

  if ($cpu_scale_avail) {
    print SCRIPTFILE "ssh \$USER\@\$REMOTE \"cpufreq-set -c 0 -g userspace\"\n";
    print SCRIPTFILE "ssh \$USER\@\$REMOTE \"cpufreq-set -c 1 -g userspace\"\n";

    print SCRIPTFILE "for i in " . $config{"CPU_FREQS_AVAIL"} . "\ndo\n";
    print SCRIPTFILE "  ssh \$USER\@\$REMOTE \"cpufreq-set -f \$i -c 0\"\n";
    print SCRIPTFILE "  ssh \$USER\@\$REMOTE \"cpufreq-set -f \$i -c 1\"\n";
  }

  print SCRIPTFILE "\n  echo \"**** CPUFREQ = \$i ****\"\n";
  print SCRIPTFILE "\n  echo \"Calibration: Running baseline test\"\n";
  print SCRIPTFILE "  ./daq_benchmark.sh \$TRACE_ID-\$i-baseline \"sleep 180\" 240 \n";
  print SCRIPTFILE "  echo \"Calibration: Baseline test complete\"\n";

  my($j); my($k);
  print SCRIPTFILE "\n# Run CPU calibration\n";
  for ($j=0; $j<$config{"CALIB_NUM_CPUS"}; $j++) {
    print SCRIPTFILE "  \n# " . ($j+1) . " CPU(s)\n";
    print SCRIPTFILE "\n  echo \"Calibration: " . ($j+1) . " CPU(s)\"\n";
    print SCRIPTFILE "  ./daq_benchmark.sh \$TRACE_ID-\$i-" . ($j+1) . "cpu \"" . $config{"CALIB_CPU_PROG"} . " " . ($j+1) . ' $REMOTE_TRACEFILE_DIR/$TRACE_ID-cpu" '. $cpu_trace_sec*($j+1) . "\n";
    
#    for ($k=0; $k<=$j; $k++) { 
#      print SCRIPTFILE "  " . $config{"CALIB_CPU_PROG"} . ' -t $TRACEFILE_DIR/$TRACE_ID-cpu' . " &\n";
#    }
#    print SCRIPTFILE "  wait\n";
    print SCRIPTFILE "  echo \"Calibration: " . ($j+1) . " CPU(s) test complete\"\n";
#    print SCRIPTFILE "  sleep 10s\n";
  }

# Run memory calibration
  print SCRIPTFILE "\n  # Run memory calibration\n";
  print SCRIPTFILE "  echo \"Calibration: Running memory test\"\n";
#  print SCRIPTFILE '  $SCRIPT_DIR/metrics.sh $TRACE_ID ' . $mem_trace_sec . " &\n";
  print SCRIPTFILE "  ./daq_benchmark.sh \$TRACE_ID-\$i-mem \"" . $config{"CALIB_MEM_PROG"} . ' -t $REMOTE_TRACEFILE_DIR/$TRACE_ID-mem' . "\" $mem_trace_sec\n";
  print SCRIPTFILE "  echo \"Calibration: Memory test complete\"\n";

# Run disk calibration
  print SCRIPTFILE "\n  # Run disk calibration\n";
  print SCRIPTFILE "  echo \"Calibration: Running disk test\"\n";
#  print SCRIPTFILE '  $SCRIPT_DIR/metrics.sh $TRACE_ID ' . $disk_trace_sec. " &\n";
  print SCRIPTFILE "  ./daq_benchmark.sh \$TRACE_ID-\$i-disk \"" . $config{"CALIB_DISK_PROG"} . ' -t $REMOTE_TRACEFILE_DIR/$TRACE_ID-disk' . "\" $disk_trace_sec\n";
#  print SCRIPTFILE "  " . $config{"CALIB_DISK_PROG"} . ' -t $TRACEFILE_DIR/$TRACE_ID-disk' . "\n";
#  print SCRIPTFILE "  wait\n";
  print SCRIPTFILE "  echo \"Calibration: Disk test complete\"\n";
#  print SCRIPTFILE "  sleep 10s\n";

# Close cpufreq loop
  if ($cpu_scale_avail) {
    print SCRIPTFILE "done\n";
  }

  close(SCRIPTFILE);
  my($mode) = 0754; chmod $mode, $scriptfile;
}
# --- end sub GenCalibrationScript ---

# --- sub GenDAQScript ---
sub GenDAQScript {
  my($scriptfile) = $config{"SCRIPT_DIR"} . "/daq_benchmark.sh"; 
  die "Couldn't create scripts" unless (open SCRIPTFILE, ">$scriptfile");

  print SCRIPTFILE "#!/bin/sh\n\n";
  print SCRIPTFILE "REMOTE=" . hostname . "\n";
  print SCRIPTFILE "LOCAL=\`hostname\`\n";
  print SCRIPTFILE "USER=root\n";
  print SCRIPTFILE "REMOTE_DATA_DIR=" . $config{"DATA_DIR"} . "\n";
  print SCRIPTFILE "REMOTE_SCRIPT_DIR=" . $config{"SCRIPT_DIR"} . "\n";
  print SCRIPTFILE "LOCAL_DATA_DIR=" . $config{"DAQ_DATA_DIR"} . "\n";

  print SCRIPTFILE "\n# \$1 = name of records, \$2 = function to run on remote machine, \$3 = instrumentation time\n";

  print SCRIPTFILE "\nexec 1\> \$LOCAL_DATA_DIR/\$1.log\n";
  print SCRIPTFILE "exec 2\> \$LOCAL_DATA_DIR/\$1.err\n";
  print SCRIPTFILE "\n# Get synchronization data\n";
  print SCRIPTFILE "echo \"SYNC: Remote date first\"\n";
  print SCRIPTFILE "ssh \$USER\@\$REMOTE date\n";
  print SCRIPTFILE "date\n";

  print SCRIPTFILE "\n# Start power measurements\n";
  print SCRIPTFILE "echo \"\`date\` [DAQ] -- Starting power measurements\"\n";
  print SCRIPTFILE $config{"DAQ_PWR_PROG"} . " >\$LOCAL_DATA_DIR/\$1.ac &\n";
  print SCRIPTFILE "sleep 10s\n";

  print SCRIPTFILE "\n# Run benchmark and metrics\n";
  print SCRIPTFILE "temp=\$2\n";
#  print SCRIPTFILE '  if echo "$temp" | egrep "*/calibrate.sh" > /dev/null;' . "\n";
  print SCRIPTFILE "echo \"\`date\` [DAQ] -- Starting metrics\"\n";
  print SCRIPTFILE "ssh \$USER\@\$REMOTE \"\$REMOTE_SCRIPT_DIR/metrics.sh \$1 \$3 &\" &\n";
  print SCRIPTFILE "sleep 10s\n";
  print SCRIPTFILE "echo \"\`date\` [DAQ] -- Starting benchmark\"\n";
  print SCRIPTFILE "ssh \$USER\@\$REMOTE \$2\n";
  print SCRIPTFILE "echo \"\`date\` [DAQ] -- Ended benchmark\"\n";
  print SCRIPTFILE "sleep 10s\n";

  print SCRIPTFILE "\necho \"\`date\` [DAQ] -- End power measurements\"\n";
  print SCRIPTFILE (shellwords($config{"DAQ_PWR_KILL"}))[0] . "\n";
  print SCRIPTFILE "wait\n";
  print SCRIPTFILE "\n# Copy power measurements and log files to MUT\n";
  print SCRIPTFILE "scp \$LOCAL_DATA_DIR/\$1.* \$USER\@\$REMOTE:\$REMOTE_DATA_DIR\n";
#  print SCRIPTFILE "  scp \$LOCAL_DATA_DIR/\$1.ac \$USER\@\$REMOTE:\$REMOTE_DATA_DIR\n";
#  print SCRIPTFILE "  scp \$LOCAL_DATA_DIR/\$1.log \$USER\@\$REMOTE:\$REMOTE_DATA_DIR\n";

  print SCRIPTFILE "\n# Pause between runs\n";
  print SCRIPTFILE 'echo "\n"' . "\n";
  print SCRIPTFILE "sleep 30s\n";

#  print SCRIPTFILE "benchmark " . $config{"TRACE_ID"} . " " . $config{"SCRIPT_DIR"} . "/calibrate.sh\n";
#  print SCRIPTFILE "# benchmark sleep20 \"sleep 20\" 50\n"; 

  close(SCRIPTFILE);
  my($mode) = 0754; chmod $mode, $scriptfile;
}
# --- end sub GenDAQScript
