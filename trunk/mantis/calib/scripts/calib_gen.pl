#!/usr/bin/perl

# Program to generate calibration and metric collection scripts
# Usage: ./calib_gen.pl -cfg=<config_file>
#   Config_file = location of configuration file. Omitting this parameter means default of "config.txt" in current directory.
#
# Files generated:
#    calibrate.sh: Script to run calibration programs
#    metrics.sh: Script to collect utilization data
#    daq_benchmark.sh: Script which will poll AC measurements and initiate runs
#
use strict;
use Getopt::Long;
use Text::ParseWords;
use Sys::Hostname;
use ParseCfgFile;

# Length in seconds of CPU, memory, and disk traces
my($cpu_trace_sec)=-1;
my($mem_trace_sec)=-1;
my($disk_trace_sec)=-1;

# Command-line arguments
my($cfg_file) = "config.txt";
my(%config);

# ---------------- Main program ----------------------

# Parse command line
GetOptions('cfg=s'   => \$cfg_file);
%config = %{&ParseCfgFile::ParseCfgFile($cfg_file)};

# Read timing files for traces
if($config{"RUN_CALIB_CPU"} eq 'y') {
	$cpu_trace_sec = &ReadTimingFile("cpu");
}
if($config{"RUN_CALIB_MEM"} eq 'y') {
	$mem_trace_sec = &ReadTimingFile("mem");
}
if($config{"RUN_CALIB_DISK"} eq 'y') {
	$disk_trace_sec = &ReadTimingFile("disk");
}
# print "cpu: $cpu_trace_sec\nmem: $mem_trace_sec\ndisk: $disk_trace_sec\n";

# Generate metrics and calibration scripts
&GenMetricScript;
&GenCalibrationScript;
&GenDAQScript;

# ---------------- end main program ----------------------

# ---------------- sub ReadTimingFile ----------------------
# Grabs the timing info for a component from the tracefile directory
# Input: component name
# Output: number of seconds the input file should take to run
sub ReadTimingFile {
   my $numsecs;
   my $timefile = $config{"TRACEFILE_DIR"} . "/" . $config{"TRACE_ID"} . "-" . $_[0] . "-timing";
   die "Couldn't read timing file $timefile" unless (open TIMEFILE, "<$timefile");
   $numsecs = <TIMEFILE>;
   return $numsecs;
}
# ---------------- end sub ReadTimingFile ----------------------

# --- sub GenMetricScript --- 
# Create the script to gather system metrics
sub GenMetricScript{
  my($scriptfile) = $config{"SCRIPT_DIR"} . "/metrics.sh"; 
  die "Couldn't create scripts" unless (open SCRIPTFILE, ">$scriptfile");

  my(@labels)=quotewords(',' , 0, $config{"METRICS_LABELS"});
  my(@cmds)  =quotewords(',' , 0, $config{"METRICS_CMDS"});
  die "Config file error: Metric labels and commands do not match\n" unless ($#labels == $#cmds);

  print SCRIPTFILE "# Collect system metrics\n";
  print SCRIPTFILE "# Usage: ./metrics.sh <prefix for output files> <secs to run>\n\n";
  print SCRIPTFILE "DATA_DIR=" . $config{"DATA_DIR"} . "\n\n";
  print SCRIPTFILE "SCRIPT_DIR=" . $config{"SCRIPT_DIR"} . "\n\n";
  print SCRIPTFILE "TRACEFILE_DIR=" . $config{"TRACEFILE_DIR"} . "\n\n";

  print SCRIPTFILE "echo \"\`date\` [MUT] -- Starting software instrumentation\"\n";

  my($i);
  for ($i=0; $i <= $#labels; $i++) {
    print SCRIPTFILE "echo \"\`date\` [MUT] -- " . $labels[$i] . "\"\n";
    print SCRIPTFILE $cmds[$i] . "&\n";
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

  print SCRIPTFILE "# Run calibration suite and OS metrics\n";
  print SCRIPTFILE "\nDATA_DIR=" . $config{"DATA_DIR"} . "\n";
  print SCRIPTFILE "SCRIPT_DIR=" . $config{"SCRIPT_DIR"} . "\n";
  print SCRIPTFILE "TRACEFILE_DIR=" . $config{"TRACEFILE_DIR"} . "\n";
  print SCRIPTFILE "TRACE_ID=" . $config{"TRACE_ID"} . "\n";
  print SCRIPTFILE "SSH_LINE=" . $config{"MUT_USERNAME"} . "@" . $config{"MUT_MACHINE"} . "\n\n";

# Run CPU calibration -- 1...N CPUs (currently only 2??)
  my $i; 
  my $cpu_scale_avail = ($config{"CPU_SCALING_AVAIL"} eq 'y') ;
  if ($cpu_scale_avail) {
    print SCRIPTFILE "echo \"Setting CPU frequency on machine under test\"\n";

    for ($i=0; $i < $config{"CALIB_NUM_CPUS"}; $i++) {
       print SCRIPTFILE "ssh \$SSH_LINE \"cpufreq-selector -c $i -g userspace\"\n";
    }
    print SCRIPTFILE "for i in " . $config{"CPU_FREQS_AVAIL"} . "\ndo\n";
    for ($i=0; $i < $config{"CALIB_NUM_CPUS"}; $i++) {
      print SCRIPTFILE "  ssh \$SSH_LINE \"cpufreq-selector -c $i -f \$i\"\n";
    }
  }
  else {
     print SCRIPTFILE "i=0\n\n";
  }

  if ($cpu_scale_avail) {
	print SCRIPTFILE "\n  echo \"**** CPUFREQ = \$i ****\"\n";
  }
  print SCRIPTFILE "\n# Run baseline test (240 sec)\n";
  print SCRIPTFILE "\n  echo \"Calibration: Running baseline test  (240 sec)\"\n";
  print SCRIPTFILE "  ./daq_benchmark.sh \$TRACE_ID-\$i-baseline \"sleep 180\" 240 \n";
  print SCRIPTFILE "  echo \"Calibration: Baseline test complete\"\n";

  if ($config{"RUN_CALIB_CPU"} eq 'y') {
	  my($j); my($k);
  	print SCRIPTFILE "\n# Run CPU calibration\n";
  		for ($j=0; $j<$config{"CALIB_NUM_CPUS"}; $j++) {
    			print SCRIPTFILE "  \n# " . ($j+1) . " CPU(s)\n";
    			print SCRIPTFILE "\n  echo \"Calibration: " . ($j+1) . " CPU(s) (" . $cpu_trace_sec*($j+1) . " sec)\"\n";
    			print SCRIPTFILE "  ./daq_benchmark.sh \$TRACE_ID-\$i-" . ($j+1) . 
					 "cpu \"" . $config{"CALIB_CPU_PROG"} . " " . ($j+1) . 
					' $TRACEFILE_DIR/$TRACE_ID-cpu ' . 
					 "\&>" .  ' $DATA_DIR/$TRACE_ID' .
					 "-\$i-" .
					 ($j+1) . ".log\" " . $cpu_trace_sec*($j+1) . 
					"\n";
    
    			print SCRIPTFILE "  echo \"Calibration: " . ($j+1) . " CPU(s) test complete\"\n";
  		}
  }

 #Run memory calibration
  if ($config{"RUN_CALIB_MEM"} eq 'y') {
  print SCRIPTFILE "\n  # Run memory calibration ($mem_trace_sec secs)\n";
  print SCRIPTFILE "  echo \"Calibration: Running memory test ($mem_trace_sec sec)\"\n";
  print SCRIPTFILE "  ./daq_benchmark.sh \$TRACE_ID-\$i-mem \"" . $config{"CALIB_MEM_PROG"} . ' -t $TRACEFILE_DIR/$TRACE_ID-mem' . "\" $mem_trace_sec\n";
  print SCRIPTFILE "  echo \"Calibration: Memory test complete\"\n";
}

# Run disk calibration
  if ($config{"RUN_CALIB_DISK"} eq 'y') {

  print SCRIPTFILE "\n  # Run disk calibration ($disk_trace_sec secs)\n";
  print SCRIPTFILE "  echo \"Calibration: Running disk test ($disk_trace_sec sec)\"\n";
  print SCRIPTFILE "  ./daq_benchmark.sh \$TRACE_ID-\$i-disk \"" . $config{"CALIB_DISK_PROG"} . ' -t $TRACEFILE_DIR/$TRACE_ID-disk' . "\" $disk_trace_sec\n";
  print SCRIPTFILE "  echo \"Calibration: Disk test complete\"\n";
}

# Close cpufreq loop
  if ($cpu_scale_avail) {
    print SCRIPTFILE "done\n";
  }
#return freq governor from userspace to ondemand
  my $i; 
  for ($i=0; $i < $config{"CALIB_NUM_CPUS"}; $i++) {
      print SCRIPTFILE "ssh \$SSH_LINE \"cpufreq-selector -c $i -g ondemand\"\n";
  }
  print SCRIPTFILE "# Frequency governor set back to ondemand.";

  close(SCRIPTFILE);
  my($mode) = 0754; chmod $mode, $scriptfile;
}
# --- end sub GenCalibrationScript ---

# --- sub GenDAQScript ---
sub GenDAQScript {
  my($scriptfile) = $config{"SCRIPT_DIR"} . "/daq_benchmark.sh"; 
  die "Couldn't create scripts" unless (open SCRIPTFILE, ">$scriptfile");

  print SCRIPTFILE "#!/bin/sh\n\n";
  print SCRIPTFILE "DAQ_DATA_DIR=" . $config{"DAQ_DATA_DIR"} . "\n";
  if ($config{"RUN_LOCAL"} eq 'n') {
	  print SCRIPTFILE "SSH_LINE=" . $config{"MUT_USERNAME"} . "@" . $config{"MUT_MACHINE"} . "\n";
  }	
	
  print SCRIPTFILE "\n# \$1 = name of records, \$2 = function, \$3 = instrumentation time\n";

  print SCRIPTFILE "\nexec 1\> \$DAQ_DATA_DIR/\$1.log\n";
  print SCRIPTFILE "exec 2\> \$DAQ_DATA_DIR/\$1.err\n";
	
  print SCRIPTFILE "\n# Get synchronization data\n";
	if ($config{"RUN_LOCAL"} eq 'n') {
			print SCRIPTFILE "echo \"[MUT]\"\n";
			print SCRIPTFILE "ssh \$SSH_LINE date\n";
			print SCRIPTFILE "echo \"[DAQ]\"\n";
	}
  print SCRIPTFILE "date\n";

  print SCRIPTFILE "\n# Start power measurements\n";
  print SCRIPTFILE "echo \"\`date\` [DAQ] -- Starting power measurements\"\n";
  print SCRIPTFILE $config{"DAQ_PWR_PROG"} . " >\$DAQ_DATA_DIR/\$1.ac &\n";
  print SCRIPTFILE "sleep 10s\n";

  print SCRIPTFILE "\n# Run benchmark and metrics\n";
#  print SCRIPTFILE "temp=\$2\n";
  print SCRIPTFILE "echo \"\`date\` [DAQ] -- Starting metrics\"\n";
	if ($config{"RUN_LOCAL"} eq 'y') {
		  print SCRIPTFILE $config{"SCRIPT_DIR"} . "/metrics.sh \$1 \$3 &\n";
	}
	else {
		print SCRIPTFILE "ssh \$SSH_LINE \"" . $config{"SCRIPT_DIR"} . "/metrics.sh \$1 \$3 &\" &\n";
	}
  print SCRIPTFILE "sleep 10s\n";
  print SCRIPTFILE "echo \"\`date\` [DAQ] -- Starting benchmark\"\n";
	if ($config{"RUN_LOCAL"} eq 'y') {
		print SCRIPTFILE "\$2\n";
	}
	else {
		print SCRIPTFILE "ssh \$SSH_LINE \$2\n";
	}
  print SCRIPTFILE "echo \"\`date\` [DAQ] -- Ended benchmark\"\n";
  print SCRIPTFILE "sleep 10s\n";

  print SCRIPTFILE "\necho \"\`date\` [DAQ] -- End power measurements\"\n";
  print SCRIPTFILE $config{"DAQ_PWR_KILL"} . "\n";
  #print SCRIPTFILE (shellwords($config{"DAQ_PWR_KILL"}))[0] . "\n";
  print SCRIPTFILE "wait\n";
	
	if ($config{"RUN_LOCAL"} eq 'n') {
		print SCRIPTFILE "scp \$DAQ_DATA_DIR/\$1.* \$SSH_LINE:" . $config{"DATA_DIR"} . "/\n";
  		print SCRIPTFILE "\necho \"Copied files from DAQ to MUT\"\n";
	}
  print SCRIPTFILE "\n# Pause between runs\n";
  print SCRIPTFILE 'echo "\n"' . "\n";
  print SCRIPTFILE "sleep 30s\n";



  close(SCRIPTFILE);
  my($mode) = 0754; chmod $mode, $scriptfile;
}
# --- end sub GenDAQScript

