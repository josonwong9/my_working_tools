#!/bin/bash

trace_path=/sys/kernel/debug/tracing
trace_event=(block_bio_queue)

function set_trace_event {
	for ev in ${trace_event[@]}
	do
		echo 1 > $trace_path/events/block/$ev/enable
	done
}

echo "" > $trace_path/trace
echo blk > $trace_path/current_tracer
set_trace_event
echo "set trace finish"


