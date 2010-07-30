# Install script for disk calibration
# Needs to compile gamut in ../mem directory (if not already done)
#  and symlink to relevant files
#!/bin/bash

# If memory benchmark has been installed, it will have created a .done
#   file for us; if not, we need to install it.
if [ ! -f .done ]

  # If not done, then install it.
then
 cd ../mem
 # Error check if ../mem doesn't exist
 if [ "$?" -ne 0 ] 
 then 
    echo "Installing gamut-disk failed; couldn't find directory mantis/calib/benchmarks/mem"
    exit 1
 fi

 ./install.sh
 # Error check if ./install.sh doesn't run
 if [ "$?" -ne 0 ] 
 then 
    echo "Installing gamut-mem failed (mantis/calib/benchmarks/mem/install.sh)"
    exit 1
 fi

 cd -
fi

# Symbolic link to relevant files from ../mem.
# First: check that files exist.
if [ ! -f ../mem/gamut ]
 then 
    echo "Installing gamut-disk failed; couldn't link to mantis/calib/benchmarks/mem/gamut."
    exit 1
 fi
if [ ! -f ../mem/benchmark_data.txt ]
 then 
    echo "Installing gamut-disk failed; couldn't link to mantis/calib/benchmarks/mem/benchmark_data.txt."
    exit 1
 fi


ln -s ../mem/gamut calib-disk
ln -s ../mem/benchmark_data.txt benchmark_data.txt
