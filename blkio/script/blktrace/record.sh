#!/bin/bash
log="/tmp/20$(date +%y.%m.%d_%H.%M.%S).log"
trace_path=/sys/kernel/debug/tracing

#reset trace log and trace events
./reset_trace.sh 2>&1 |tee -a $log
#set trace events
./set_trace.sh 2>&1 |tee -a $log
echo "" > $log
#record UTC
dmesg | grep UTC 2>&1|tee -a $log

#record start time
echo "test start" 2>&1|tee -a $log
echo "20$(date +%y.%m.%d_%H.%M.%S)" 2>&1|tee -a $log

#record queue setting
./get_blkq_setting.sh 2>&1 |tee -a $log

#start record trace
echo 1 > $trace_path/tracing_on

#record latency ($1: time slice) and ($2:count)
test_time=10 #Unit: s
start_time=`date +%s`
stop_time=`expr $start_time + $test_time`
while true
do
	cur_time=`date +%s`
	if [ $cur_time -gt $stop_time ]; then
		break
	fi
	echo "20$(date +%y.%m.%d_%H.%M.%S)" 2>&1|tee -a $log
	iostat -ydx 1 1 | awk 'NR>2 {print $0}' 2>&1|tee -a $log
done

#========================================
#stop record trace
echo 0 > $trace_path/tracing_on

#copy trace log and append it to $log
cat $trace_path/trace 2>&1|tee -a $log

#record stop time
echo "test stop" 2>&1|tee -a $log
echo "20$(date +%y.%m.%d_%H.%M.%S)" 2>&1|tee -a $log

#cleanup: reset trace
./reset_trace.sh
