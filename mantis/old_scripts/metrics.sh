# Collect system metrics
# Usage: ./metrics.sh <prefix for output files> <secs to run>

DATA_DIR=/temp/diska/gamut/data

SCRIPT_DIR=/temp/diska/gamut/scripts3

TRACEFILE_DIR=/temp/diska/gamut/traces

echo "`date` [MUT] -- Starting software instrumentation"
echo "`date` [MUT] -- Starting CPU freq poll"
$SCRIPT_DIR/cpufreq_poll.sh $DATA_DIR/$1 $2 &
echo "`date` [MUT] -- Starting iostat"
iostat -xnt 1 $2 > $DATA_DIR/$1.iostat &
echo "`date` [MUT] -- Starting performance counters"
pfmon -k -u --events=rs_uops_dispatched,uops_retired:any,instructions_retired,unhalted_core_cycles --system-wide --print-interval=1000 -t$2 --outfile=$DATA_DIR/$1 --with-header &
echo "`date` [MUT] -- Starting CPU utilization poll"
sar -P ALL 1 $2 > $DATA_DIR/$1.cpu_ut
echo "`date` [MUT] -- Metrics started"
wait
echo "`date` [MUT] -- Metrics ended"
