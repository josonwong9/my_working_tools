#!/bin/bash


if [ $# -lt 2 ]; then
	echo "Usage $0 \"Wed Aug 11 19:55:20 2021\" \"Thu Nov 25 20:28:29 2021\""
	echo "Two date string is needed, exit!"
	exit 1
fi

date1=$1
date2=$2

printf "\nseconds: %s\n" $(( $(date -d "$date2" "+%s") - $(date -d "$date1" "+%s") ))
