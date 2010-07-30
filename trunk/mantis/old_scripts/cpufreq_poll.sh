# Poll CPU frequency every second
# Usage: ./cpufreq_poll.sh <path/prefix to write to> <amount of time to poll

rm $1.freq0
rm $1.freq1

i=0

for i in `seq 1 $2`;
do
  echo "`cpufreq-info -c 0 -f`  `date`" >> $1.freq0
  echo "`cpufreq-info -c 1 -f`  `date`" >> $1.freq1
  sleep 1
done
