# Log Power - retrieve power data from a file

# We have our WattsUp power meters set up on the network with a PHP script so
# we can read it remotely. This is a simple script to retrieve this information. 
# It reads the line from the a directory (the adress of a file on a Stanford computer)
# and then echo's it (we might add parsing the string later)

POWER_FILE=$1 #power file passed as an argument
# Loop to read and output data
while [ 1 ] 
do
	# Read data from server
	POWER_INFO=`wget -qO - $POWER_FILE` # Write data
	echo $POWER_INFO
	sleep 1;
done
