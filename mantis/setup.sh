# Run on machine under test

cd calib/scripts
perl calib-config-wizard.pl
echo
echo "**** GENERATING CALIBRATION INPUT FILES ****"
sleep 2
perl trace_gen.pl
echo
echo "**** INSTALLING CALIBRATION PROGRAMS ****"
sleep 2
perl calib-install.pl
echo
echo "**** GENERATING CALIBRATION SCRIPTS ****"
sleep 2
perl calib_gen.pl
echo
echo "Calibration files generated.";
echo "If this machine is measuring its own power, execute mantis/calib/scripts/calibrate.sh to run the calibration phase.";
echo "If another machine is taking power measurements for this machine, do the following:";
echo "   a) Use ssh-keygen to allow that machine to log on automatically to this machine as a superuser.";
echo "   b) Copy mantis/calib/scripts/calibrate.sh and mantis/calib/scripts/daq_benchmark.sh to that machine.";
echo "   c) Run calibrate.sh from that machine.";






