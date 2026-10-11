trace_path=/sys/kernel/debug/tracing
#trace_event=(block_rq_insert block_rq_issue block_rq_complete)
trace_event=( \
block_bio_backmerge   block_bio_queue     block_plug         block_rq_issue    block_split \
block_bio_bounce      block_bio_remap     block_rq_abort     block_rq_remap    block_touch_buffer \
block_bio_complete    block_dirty_buffer  block_rq_complete  block_rq_requeue  block_unplug \
block_bio_frontmerge  block_getrq         block_rq_insert    block_sleeprq)

trace_event=( \
block_bio_backmerge   block_bio_queue     block_rq_issue    block_split \
block_bio_bounce      block_bio_remap     block_rq_abort     block_rq_remap    \
block_bio_complete    block_rq_complete  block_rq_requeue  \
block_bio_frontmerge  block_getrq         block_rq_insert    block_sleeprq)
trace_event=(block_rq_remap block_rq_insert block_bio_backmerge block_bio_frontmerge block_rq_issue block_getrq)
#set record duration in unit of second(s)
time_s=10

function set_trace_event {
	for ev in ${trace_event[@]}
	do
		echo 1 > $trace_path/events/block/$ev/enable
		#echo "dev == 8388624" > $trace_path/events/block/$ev/filter
		echo "dev == 8388628" > $trace_path/events/block/$ev/filter
	done
}
function clr_trace_event {
	echo 0 > $trace_path/events/block/enable
	for ev in ${trace_event[@]}
	do
		echo 0 > $trace_path/events/block/$ev/enable
		echo 0 > $trace_path/events/block/$ev/filter
	done
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
clr_trace_event

echo blk > $trace_path/current_tracer
set_trace_event
echo 1 > $trace_path/tracing_on

#dump_time
read -p "press any key to stop record"

echo 0 > $trace_path/tracing_on
clr_trace_event
#file=trace_$(date "+20%y.%m.%d.%H.%M.%S").log
file=log
\cp -f $trace_path/trace $file
echo "log file:$file"
