# Copy this file to DAQ
# Run calibration suite and OS metrics
REMOTE=joulesort-1.stanford.edu
LOCAL=`hostname`
USER=root

REMOTE_DATA_DIR=/temp/diska/gamut/data
REMOTE_SCRIPT_DIR=/temp/diska/gamut/scripts3
REMOTE_TRACEFILE_DIR=/temp/diska/gamut/traces
TRACE_ID=calib-2008-03-03

ssh $USER@$REMOTE "cpufreq-set -c 0 -g userspace"
ssh $USER@$REMOTE "cpufreq-set -c 1 -g userspace"
for i in 2324000 1992000 1660000 1328000 996000
do
  ssh $USER@$REMOTE "cpufreq-set -f $i -c 0"
  ssh $USER@$REMOTE "cpufreq-set -f $i -c 1"

  echo "**** CPUFREQ = $i ****"

  echo "Calibration: Running baseline test"
  ./daq_benchmark.sh $TRACE_ID-$i-baseline "sleep 180" 240 
  echo "Calibration: Baseline test complete"

# Run CPU calibration
  
# 1 CPU(s)

  echo "Calibration: 1 CPU(s)"
  ./daq_benchmark.sh $TRACE_ID-$i-1cpu "/temp/diska/gamut/gamut-cpu-smr/run-matmult.sh 1 $REMOTE_TRACEFILE_DIR/$TRACE_ID-cpu" 650
  echo "Calibration: 1 CPU(s) test complete"
  
# 2 CPU(s)

  echo "Calibration: 2 CPU(s)"
  ./daq_benchmark.sh $TRACE_ID-$i-2cpu "/temp/diska/gamut/gamut-cpu-smr/run-matmult.sh 2 $REMOTE_TRACEFILE_DIR/$TRACE_ID-cpu" 1300
  echo "Calibration: 2 CPU(s) test complete"

  # Run memory calibration
  echo "Calibration: Running memory test"
  ./daq_benchmark.sh $TRACE_ID-$i-mem "/temp/diska/gamut/gamut-0.7.0/gamut -t $REMOTE_TRACEFILE_DIR/$TRACE_ID-mem" 2420
  echo "Calibration: Memory test complete"

  # Run disk calibration
  echo "Calibration: Running disk test"
  ./daq_benchmark.sh $TRACE_ID-$i-disk "/temp/diska/gamut/gamut-0.7.0/gamut -t $REMOTE_TRACEFILE_DIR/$TRACE_ID-disk" 5060
  echo "Calibration: Disk test complete"
done
