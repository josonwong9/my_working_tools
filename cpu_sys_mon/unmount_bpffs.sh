#!/bin/bash
m="$(mount -t bpf)"
if [[ -n $m ]]; then
	umount /sys/fs/bpf
	echo "umount /sys/fs/bpf"
else
	echo "bpffs not mounted"
fi
