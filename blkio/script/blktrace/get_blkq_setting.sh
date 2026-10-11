#!/bin/bash
dev=`iostat -d | awk 'NR>3 {print $1}'`
for d in $dev
do
	echo ""
	echo "========================================"
	echo "Device:$d"
	set=`ls /sys/block/$d/queue`
	for st in $set
	do
		if [ -d /sys/block/$d/queue/$st ];then
			continue
		fi
		v=`cat /sys/block/$d/queue/$st`
		echo "$st:$v"
	done
done
