trace_path=/sys/kernel/debug/tracing
#trace_event=(block_rq_insert block_rq_issue block_rq_complete)
#trace_event=(block_rq_insert block_rq_issue block_rq_complete block_getrq )
trace_event="block"

#set record duration in unit of second(s)
time_s=10
#if [ ! -d "trace_log" ]; then
#	mkdir trace_log
#fi

function set_trace_event {
	echo 1 > $trace_path/events/block/enable
	echo "dev == 8388628" > $trace_path/events/block/filter
}
function clr_trace_event {
	echo 0 > $trace_path/events/block/enable
	echo 0 > $trace_path/events/block/filter
}
function dump_time {
	i=0
	echo "recording..."
	while [ $i -lt $time_s ]
	do
		sleep 1
		let i=i+1
		echo -ne "\r$i s"
	done
	echo -e "\nrecord finsh"
}
#clear old tracer and record
echo 0 > $trace_path/tracing_on
echo "" > $trace_path/trace
echo nop > $trace_path/current_tracer
echo blk > $trace_path/current_tracer
clr_trace_event
echo 1 > $trace_path/tracing_on
set_trace_event

dump_time

echo 0 > $trace_path/tracing_on
clr_trace_event
#file=trace_$(date "+20%y.%m.%d.%H.%M.%S").log
file=full.log
cp $trace_path/trace $file
