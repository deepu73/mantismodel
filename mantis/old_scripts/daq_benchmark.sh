#!/bin/sh

REMOTE=joulesort-1.stanford.edu
LOCAL=`hostname`
USER=root
REMOTE_DATA_DIR=/temp/diska/gamut/data
REMOTE_SCRIPT_DIR=/temp/diska/gamut/scripts3
LOCAL_DATA_DIR=/home/skrufi/zesti/data

# $1 = name of records, $2 = function to run on remote machine, $3 = instrumentation time

exec 1> $LOCAL_DATA_DIR/$1.log
exec 2> $LOCAL_DATA_DIR/$1.err

# Get synchronization data
echo "SYNC: Remote date first"
ssh $USER@$REMOTE date
date

# Start power measurements
echo "`date` [DAQ] -- Starting power measurements"
/home/skrufi/brandelectronics/brandelect >$LOCAL_DATA_DIR/$1.ac &
sleep 10s

# Run benchmark and metrics
temp=$2
echo "`date` [DAQ] -- Starting metrics"
ssh $USER@$REMOTE "$REMOTE_SCRIPT_DIR/metrics.sh $1 $3 &" &
sleep 10s
echo "`date` [DAQ] -- Starting benchmark"
ssh $USER@$REMOTE $2
echo "`date` [DAQ] -- Ended benchmark"
sleep 10s

echo "`date` [DAQ] -- End power measurements"
killall brandelect
wait

# Copy power measurements and log files to MUT
scp $LOCAL_DATA_DIR/$1.* $USER@$REMOTE:$REMOTE_DATA_DIR

# Pause between runs
echo "\n"
sleep 30s
