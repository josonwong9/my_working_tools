#!/bin/bash

#cur_dir=$(dirname $0)
f=$1

#rw: read/write/randread/randwrite
#head -1 $f | cut -d ":" -f 1

#jobs: thread/process number
grep Starting $f | cut -d " " -f 2

#io_depth
head -1 $f | cut -d "=" -f 6

#IOPS
iops=`grep IOPS $f | cut -d "," -f 1 | cut -d "=" -f 2 | cut -d "k" -f 1`
grep IOPS $f | cut -d "," -f 1 | cut -d "=" -f 2 | grep k > /dev/null 2>&1
if [ $? -eq 0 ]; then
	iops=`echo "scale=0; $iops * 1000" | bc | cut -d "." -f 1`
fi
echo $iops

#bandwidth (MiB/s), convert to MiB/s if show KiB/s
grep BW $f | awk '{print $3}' | cut -d "=" -f 2 | grep K > /dev/null 2>&1
if [ $? -eq 0 ]; then
bw=`grep BW $f | awk '{print $3}' | cut -d "=" -f 2 | cut -d "K" -f 1`
bw=`echo "scale=2; $bw / 1024" | bc`
else
bw=`grep BW $f | awk '{print $3}' | cut -d "=" -f 2 | cut -d "M" -f 1`
fi
echo $bw

#latency unit
unit=`grep -w lat $f | grep min | awk '{print $2}' | cut -d ":" -f 1`
if [ $unit = "(msec)" ]; then
base=1000
else
base=1
fi
#latency min
l=`grep -w lat $f | grep min | awk '{print $3}' | cut -d "=" -f 2 | cut -d "," -f 1`
l=`echo "scale=2; $l*$base" | bc`
echo $l

#latency max
l=`grep -w lat $f | grep min | awk '{print $4}' | cut -d "=" -f 2 | cut -d "," -f 1`
echo $l

#latency avg
l=`grep -w lat $f | grep min | awk '{print $5}' | cut -d "=" -f 2 | cut -d "," -f 1`
l=`echo "scale=2; $l*$base" | bc`
echo $l

#latency stdev
l=`grep -w lat $f | grep min | awk '{print $6}' | cut -d "=" -f 2 | cut -d "," -f 1`
if [ -z $l ]; then
l=`grep -w lat $f | grep min | awk -F , '{print $4}' | cut -d " " -f 3`
fi
l=`echo "scale=2; $l*$base" | bc`
echo $l

#latency unit
unit=`grep -w "clat percentiles" $f | awk '{print $3}'`
if [ $unit = "(msec):" ]; then
base=1000
else
base=1
fi
#latency 99%
l=`grep "99.00th" $f | awk -F "[" '{print $2}'| awk -F "]" '{print $1}'| awk '{print $1}'`
l=`echo "scale=2; $l*$base" | bc`
echo $l

#io.util
grep -w util $f | awk -F = '{print $6}' | cut -d % -f 1

