#!/bin/bash

if [ $# -lt 1 ]; then
	echo "Usage: $0 <seconds>"
	exit 1
fi

echo "Boot date: " `last reboot -F | grep "still running" | head -1 | awk '{print $5,$6,$7,$8,$9}'` "+ $1 seconds ="

echo "=================================================="

echo "According to last reboot comand output"
date -d "`last reboot -F | grep "still running" | head -1 | awk '{print $5,$6,$7,$8,$9}'` + $1 seconds"

echo "=================================================="

echo "According to /proc/uptime"
boot_day=$(date -d "$(awk -F. '{print $1}' /proc/uptime) second ago")
date -d "${boot_day} + $1 seconds"

echo "=================================================="
