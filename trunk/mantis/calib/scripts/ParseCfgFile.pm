package ParseCfgFile;

use strict;

=head1 NAME

ParseCfgFile - Perl module for parsing the calibration config.txt file

=head1 SYNOPSIS

=head1 DESCRIPTION

=head1 METHODS

ParseCfgFile -- pass it the name of the configuration file, and it will return a hash populated with CONFIGFILEENTRY => value entries.  It will also do error-checking (to some extent overlapping wtih what the configuration wizard does).

=head1 AUTHOR

Suzanne Rivoire (suzanne.rivoire@sonoma.edu)

=head1 COPYRIGHT

GPL.

=cut

# --- sub ParseCfgFile ---
# Extract configuration parameters; check to make sure that the necessary params are defined
# Input: name of config file
# Returns: hash of configuration parameters
sub ParseCfgFile {

  # Open config file or die trying
  my $cfg_file = $_[0];
  die "Config file \"$cfg_file\" does not exist" unless ((-e $cfg_file) and (open CFG_FILE, "<$cfg_file"));

  # Extract "name=value" pairs
  my %config = map {
      s/#.*//; # Remove comments
      s/^\s+//; # Remove opening whitespace
      s/\s+$//;  # Remove closing whitespace
      m/(.*?)=(.*)/; 
   } <CFG_FILE>; 

# Make sure general parameters are valid
  CfgFileDie("TRACEFILE_DIR") unless ( (exists($config{"TRACEFILE_DIR"})) and (-d $config{"TRACEFILE_DIR"}) ); 
  CfgFileDie("SCRIPT_DIR")    unless ( (exists($config{"SCRIPT_DIR"}))    and (-d $config{"SCRIPT_DIR"}) ); 
  CfgFileDie("DATA_DIR")      unless ( (exists($config{"DATA_DIR"}))      and (-d $config{"DATA_DIR"}) ); 
  CfgFileDie("TRACE_ID")      unless (exists($config{"TRACE_ID"}));

# Make sure CPU frequency scaling parameters are valid
  CfgFileDie("CPU_SCALING_AVAIL") unless (exists($config{"CPU_SCALING_AVAIL"}));
  if (lc($config{"CPU_SCALING_AVAIL"}) eq "y") {
    CfgFileDie("CPU_FREQS_AVAIL") unless (exists($config{"CPU_FREQS_AVAIL"}));
  }

# Make sure CPU calibration parameters are valid (if applicable)
  CfgFileDie("RUN_CALIB_CPU") unless (exists $config{"RUN_CALIB_CPU"});
  if ($config{"RUN_CALIB_CPU"} eq 'y') {
    CfgFileDie("CALIB_CPU_INSTALL") unless ((exists($config{"CALIB_CPU_INSTALL"})) and (-e $config{"CALIB_CPU_INSTALL"}) ); 
    CfgFileDie("CALIB_CPU_PROG") unless (exists($config{"CALIB_CPU_PROG"}));
    CfgFileDie("CALIB_NUM_CPUS") unless ((exists($config{"CALIB_NUM_CPUS"})) and ($config{"CALIB_NUM_CPUS"} > 0) ); 
  }

# Make sure memory calibration parameters are valid (if applicable)
  CfgFileDie("RUN_CALIB_MEM") unless (exists $config{"RUN_CALIB_MEM"});
  if ($config{"RUN_CALIB_MEM"} eq 'y') {
    CfgFileDie("CALIB_MEM_INSTALL")     unless ((exists($config{"CALIB_MEM_INSTALL"}))     and (-e $config{"CALIB_MEM_INSTALL"}) ); 
    CfgFileDie("CALIB_MEM_PROG")     unless (exists($config{"CALIB_MEM_PROG"})); 
    CfgFileDie("CALIB_MEM_MAX_SIZE") unless ((exists($config{"CALIB_MEM_MAX_SIZE"})) and ($config{"CALIB_MEM_MAX_SIZE"} > 0) ); 
  }

# Make sure disk calibration parameters are valid (if applicable)
  CfgFileDie("RUN_CALIB_DISK") unless (exists $config{"RUN_CALIB_DISK"});
  if ($config{"RUN_CALIB_DISK"} eq 'y') {
    CfgFileDie("CALIB_DISK_INSTALL") unless ((exists($config{"CALIB_DISK_INSTALL"})) and (-e $config{"CALIB_DISK_INSTALL"}) ); 
    CfgFileDie("CALIB_DISK_PROG") unless (exists($config{"CALIB_DISK_PROG"})) ;

    # Make sure all workfiles exist
    CfgFileDie("CALIB_DISK_WORKFILES") unless (exists($config{"CALIB_DISK_WORKFILES"}));
    my @workfiles = split(',' , $config{"CALIB_DISK_WORKFILES"});
    CfgFileDie("CALIB_DISK_WORKFILES") unless (@workfiles > 0);
    foreach my $workfile (@workfiles) {
	`touch $workfile`;
        CfgFileDie("Disk workfile $workfile") unless (-e $workfile);
    }
  }

# Make sure metrics are specified
#  CfgFileDie("METRICS_LABELS") unless (exists($config{"METRICS_LABELS"}));
#  CfgFileDie("METRICS_CMDS") unless (exists($config{"METRICS_CMDS"}));

# Make sure DAQ parameters are specified
#  CfgFileDie("DAQ_DATA_DIR") unless (exists($config{"DAQ_DATA_DIR"}));
#  CfgFileDie("DAQ_PWR_PROG") unless (exists($config{"DAQ_PWR_PROG"}));
#  CfgFileDie("DAQ_PWR_KILL") unless (exists($config{"DAQ_PWR_KILL"}));
  close CFG_FILE;
  return \%config;
}
# --- end sub ParseCfgFile ---

# --- sub CfgFileDie ---
# If configuration parameter is invalid, print message and die
sub CfgFileDie {
  die "Config file error: $_[0] unspecified or invalid.\n";
}
# --- end sub CfgFileDie ---


1;
__END__
