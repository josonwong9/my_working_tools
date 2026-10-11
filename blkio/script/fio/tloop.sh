#!/bin/bash
source conf.sh

max_test_io_depth=1

echo "test start 20$(date +%y-%m-%d.%H:%M:%S)" > t.log
apps=(sr.sh sw.sh rr.sh rw.sh)

for ((j=0;j<${#apps[*]};j=j+1))
do
	app=${apps[$j]}
	echo "$app test start 20$(date +%y-%m-%d.%H:%M:%S)" 2>&1|tee -a t.log
	for ((i=1; i<=$max_test_io_depth; i*=2))
	do
		./$app 1 $i 2>&1|tee -a t.log
		sleep 60
	done
done

