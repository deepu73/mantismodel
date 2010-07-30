#!/usr/bin/perl

use strict;
use Getopt::Long;
use Text::ParseWords;
use Sys::Hostname;
use ParseCfgFile;
use File::Basename;

# Command Line Variables
my($cfg_file) = "config.txt";
my(%config);
my $dir, my $filename;

# ---------------- Main Program ----------------------

# Parse Command Line
GetOptions('cfg=s'   => \$cfg_file);
%config = %{&ParseCfgFile::ParseCfgFile($cfg_file)};

# Run Installations

if ($config{"RUN_CALIB_CPU"} eq 'y')  { &CompileCPUBenchmark; }
if ($config{"RUN_CALIB_MEM"} eq 'y')  { &CompileMemBenchmark; }
if ($config{"RUN_CALIB_DISK"} eq 'y') { &CompileDiskBenchmark; }

# ---------------- End of Main Program ----------------------

# Subroutine to Compile CPU Benchmark
sub CompileCPUBenchmark {
	$filename = $config{"CALIB_CPU_INSTALL"};
	$dir = dirname($config{"CALIB_CPU_INSTALL"});
	system "cd $dir ; $filename"; 
}

# Subroutine to Compile Mem Benchmark
sub CompileMemBenchmark {
	$filename = $config{"CALIB_MEM_INSTALL"};
	$dir = dirname($config{"CALIB_MEM_INSTALL"});
	system "cd $dir ; $filename"; 
}

# Subroutine to Compile Disk Benchmark
sub CompileDiskBenchmark {
	$filename = $config{"CALIB_DISK_INSTALL"};
	$dir = dirname($config{"CALIB_DISK_INSTALL"});
	system "cd $dir ; $filename"; 
}
