#!/bin/bash

go build -o ./bin/usr/local/bin/kernel_log_push kernel_log_push.go
cp kernel_log_push.sh ./bin/usr/local/bin
mkdir -p ./bin/etc/cron.d
cp cron_kernel_log_push ./bin/etc/cron.d
cp statistics.sh ./bin