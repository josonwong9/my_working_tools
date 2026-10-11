#!/bin/bash
trace_path=/sys/kernel/debug/tracing

echo 0 > $trace_path/tracing_on
echo "" > $trace_path/trace
echo nop > $trace_path/current_tracer
echo 0 > $trace_path/events/block/enable
echo 0 > $trace_path/events/block/filter
echo "trace reset finish"
