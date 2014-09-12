#! /bin/bash
if [ "$#" -lt 4 ] 
then
    echo "Illegal number of parameters"
    echo "Format : sh get_vals.sh <num_procs> <max_msg_size> <iterations> <rma_type> <directory_path(optional)>"
else
msg_size=8
num_procs="$1"
max_msg_size="$2"
iter="$3"
rma_type="$4"
if [ -n "$5" ]
then
	dir_path="$5"
else
	dir_path="/home/prabhashankar/work/"
fi
rm -f "$dir_path"/vals_raw_"$num_procs"_"$rma_type"
while [ "$msg_size" -le "$max_msg_size" ]
do
	echo "***********************************************************************" >> "$dir_path"/vals_raw_"$num_procs"_"$rma_type"
	echo "Size : $msg_size Bytes" >> "$dir_path"/vals_raw_"$num_procs"_"$rma_type"
	echo "***********************************************************************" >> "$dir_path"/vals_raw_"$num_procs"_"$rma_type"
	/home/prabhashankar/work/launch_benchmark.out "$num_procs" "$msg_size" "$iter" "$rma_type" >> "$dir_path"/vals_raw_"$num_procs"_"$rma_type"
	msg_size=$((msg_size*2))
done
fi
