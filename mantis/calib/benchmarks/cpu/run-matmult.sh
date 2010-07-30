# $1 = number of processors
# $2 = input file (parameters for matrix multiplications)
for i in $(seq $1)
do
  ./matmult2 -t $2 &
#   echo $i
done
