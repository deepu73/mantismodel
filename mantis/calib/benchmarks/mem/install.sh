echo "**** COMPILING GAMUT BENCHMARK PROGRAM ****"
make
if [ "$?" -ne 0 ];  then echo "Failed to compile gamut"; exit 1; fi 
ln -s gamut calib-mem
echo
echo
echo "**** CALIBRATING GAMUT BENCHMARK PROGRAM (this should take less than a minute) ****"
sleep 2
if [ ! -f benchmark_data.txt ]; then ./gamut -b -q -s benchmark_data.txt; fi
if [ "$?" -ne 0 ];  then echo "Failed to initialize gamut"; exit 1; fi 
touch ../disk/.done
