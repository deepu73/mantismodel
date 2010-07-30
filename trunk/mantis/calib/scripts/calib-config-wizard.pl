#!/usr/bin/perl 
use Net::Domain qw (hostname hostfqdn hostdomain);

# TODO: Read old config.txt and remind user of current values (like "make oldconfig")
# Be more robust about prompting for run_local and mut username
# Warn user to do ssh_keygen for account that they're using to shuttle between daq and mut
#
use constant CHECK_FOR_DIRECTORY => 1;
use constant CHECK_FOR_INSTALL_SCRIPT => 2;
use constant CHECK_FOR_PROGRAM => 3;

# Subroutine for grabbing basic input
sub GetInputParam {
	print "$_[0] ";	
	$output = <STDIN>;
	chop ($output);
	$output;}

# Subroutine to check that all of the calibration directories can be found
# Arguments: name of directory, a number
sub CheckDir {	
	$MANTIS_HOME = `echo \$MANTIS_HOME`;
	chop $MANTIS_HOME;
	if ($_[1] == CHECK_FOR_DIRECTORY) {$_ = "$MANTIS_HOME/calib/$_[0]";}
	if ($_[1] == CHECK_FOR_INSTALL_SCRIPT) {$_ = "$MANTIS_HOME/calib/benchmarks/$_[0]/install.sh";}
	if ($_[1] == CHECK_FOR_PROGRAM) {$_ = "$MANTIS_HOME/calib/benchmarks/$_[0]/calib-$_[0]";}
	if ($_[1] == CHECK_FOR_DIRECTORY && -d $_ == 0) {print "Error! Calibration benchmark for $_[0] was not in the correct directory. Exiting...\n"; exit;}
	if ($_[1] == CHECK_FOR_INSTALL_SCRIPT && -e $_ == 0) {print "Error! Could not find calibration file $_ for $_[0]. Exiting...\n"; exit;}
	$_;}

# Subroutine to Run CPU, Mem, and Disk Programs
sub RunProgs {
	print "Do you want to run $_[0] programs? (y/n) ";
	$run_prog = <STDIN>;
	chop ($run_prog);
	$run_prog; }

# ---------Beginning of Main Program---------

# Verification of Environment Variable
if ($ENV{'MANTIS_HOME'} eq "") { die "Environment variable MANTIS_HOME is not set.\n"; }
print "Your variable for \$MANTIS_HOME appears to be " . `echo \$MANTIS_HOME`;

# Directory Checks
$tracefile_dir = &CheckDir("inputs", CHECK_FOR_DIRECTORY);
$script_dir    = &CheckDir("scripts", CHECK_FOR_DIRECTORY);
$data_dir      = &CheckDir("data", CHECK_FOR_DIRECTORY);

# Fix me
$daq_data_dir = $data_dir;

# Trace ID
$trace_id = &GetInputParam("Please pick a unique ID for the input traces and output logs generated during this run:"); 

# Local vs. remote machine
print "Is this machine reading its own power measurements? (y/n)\n";
$run_local = <STDIN>;
chop($run_local);
if ($run_local eq 'n') {
	$daq_data_dir = &GetInputParam("Which directory on the DAQ machine will store the power measurement data?");
}


# CPU Calibration Phase
$run_calib_cpu = &RunProgs("cpu");
# If they want to run the CPU calibration...
if ($run_calib_cpu eq 'y') {
	# Make sure benchmarks are present
	$calib_cpu_prog    = &CheckDir("cpu", CHECK_FOR_PROGRAM);
	$calib_cpu_install = &CheckDir("cpu", CHECK_FOR_INSTALL_SCRIPT);

	# Decide how many CPUs to calibrate
	my $calib_num_cpus_find = `cat /proc/cpuinfo | grep processor | wc -l`;
	chop ($calib_num_cpus_find);
	print "You appear to have $calib_num_cpus_find CPU(s) available.\n"; 
	$calib_num_cpus_input = -1;
	until ($calib_num_cpus_input > 0) {
		$calib_num_cpus_input = &GetInputParam("How many CPUs do you want to exercise during the calibration phase?");
		if ($calib_num_cpus_input < 1) { print "Error! Number of CPUs must be greater than 1"; } }
	if ($calib_num_cpus_input > $calib_num_cpus_find) {
		print "Warning: it may not be possible to exercise this many CPUs.\n"; } }

# Memory Calibration Phase
$run_calib_mem = &RunProgs("memory");
# If they want to run the memory calibration...
if ($run_calib_mem eq 'y') {
	# Make sure benchmarks are present
	$calib_mem_prog = &CheckDir("mem", CHECK_FOR_PROGRAM);
	$calib_mem_install = &CheckDir("mem", CHECK_FOR_INSTALL_SCRIPT);

	# Decide how many CPUs to calibrate
	$calib_mem_max_size = int (`cat /proc/meminfo | grep MemTotal | awk '{print \$2}'` / 1024);
	print "You appear to have $calib_mem_max_size MB of memory available.\n"; 
	print "How much memory do you want to exercise during the calibration phase? ";
	$calib_mem_use_size = <STDIN>;
	chop ($calib_mem_use_size); 
	$value = int (($calib_mem_use_size / $calib_mem_max_size) * 100);
	print "This is $value" . "% of the available memory.\n";
	if ($calib_mem_use_size / $calib_mem_max_size > .9) {
		print "Warning: it may not be possible to allocate this amount of memory.\n"; } }

# Disk Calibration Phase
$run_calib_disk = &RunProgs("disk");
if ($run_calib_disk eq 'y') {
	$calib_disk_prog = &CheckDir("disk", CHECK_FOR_PROGRAM);
	$calib_disk_install = &CheckDir("disk", CHECK_FOR_INSTALL_SCRIPT);
	$calib_disk_workfiles = $tracefile_dir;
#	$calib_disk_workfiles = &GetInputParam("Location of working files for disk experiments (use full path)?"); 
#	while (-d $calib_disk_workfiles == 0) { $calib_disk_workfiles = &GetInputParam("Invalid location.  Please try again."); }
}

# CPU Scaling Phase
if (-e "/sys/devices/system/cpu/cpu0/cpufreq") {
	$cpu_scaling_avail = 'y'; }
	else { $cpu_scaling_avail = 'n'; }
if ($cpu_scaling_avail eq 'y') { 
	$cpu_freqs_avail = `cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies`; }

# Metrics Options
print "Metrics to collect:\n";
print 'You may use $1 for trace id, $2 for number of seconds, $SCRIPT_DIR for script directory and $DATA_DIR for data directory';
$metric_label_num = &GetInputParam("\nHow many metrics will you collect?");
@metric_label, @metric_cmds;
for ($metric_counter = 1; $metric_counter <= $metric_label_num; $metric_counter++) {
	$metric_label[$metric_counter] = &GetInputParam("What label will you give metric \#$metric_counter?");
	if ($metric_counter < $metric_label_num) { $metric_label[$metric_counter] .= "," };
	$metric_cmds[$metric_counter] = &GetInputParam("What is the command for label $metric_counter?");
	if ($metric_counter < $metric_label_num) { $metric_cmds[$metric_counter] .= "," };  }

# DAQ Info
$daq_pwr_prog = &GetInputParam("Command for measuring AC power?");
$daq_pwr_kill = &GetInputParam("Command for ending AC power measurements?");

# Print to config.txt Filehandle
open (OUT, ">config.txt");
print OUT "# Calibration phase configuration file\n\n# Paths\n";
print OUT "TRACEFILE_DIR=$tracefile_dir\nSCRIPT_DIR=$script_dir\nDATA_DIR=$data_dir\n";
print OUT "DAQ_DATA_DIR=$daq_data_dir\n\n";
print OUT "RUN_LOCAL=$run_local\n\n";
print OUT "MUT_USER=root\n";
print OUT "MUT_MACHINE=" . hostfqdn() . "\n\n";
print OUT "TRACE_ID=$trace_id\n\n";
print OUT "RUN_CALIB_CPU=$run_calib_cpu\nRUN_CALIB_MEM=$run_calib_mem\nRUN_CALIB_DISK=$run_calib_disk\n\n";
print OUT "# Location of calibration programs for each component\nCALIB_CPU_INSTALL=$calib_cpu_install\nCALIB_MEM_INSTALL=$calib_mem_install\nCALIB_DISK_INSTALL=$calib_disk_install\nCALIB_CPU_PROG=$calib_cpu_prog\nCALIB_MEM_PROG=$calib_mem_prog\nCALIB_DISK_PROG=$calib_disk_prog\n\n";
print OUT "#CPU frequency scaling information\nCPU_SCALING_AVAIL=$cpu_scaling_avail\n";
if ($cpu_scaling_avail eq 'y') { print OUT "CPU_FREQS_AVAIL=$cpu_freqs_avail\n"; }
print OUT "# Tracefile options\n#   Maximum amount of memory to beat up on (in MB)\nCALIB_MEM_MAX_SIZE=$calib_mem_use_size\n#   Working file for disk experiments\nCALIB_DISK_WORKFILES=$calib_disk_workfiles\n#   Number of CPUs to exercise\nCALIB_NUM_CPUS=$calib_num_cpus_input\n\n";
print OUT "# Metrics options\nMETRICS_LABELS=@metric_label\nMETRICS_CMDS=@metric_cmds\n";
print OUT "DAQ_PWR_PROG=$daq_pwr_prog\nDAQ_PWR_KILL=$daq_pwr_kill";
close (OUT);

# ---------End of Main Program---------

