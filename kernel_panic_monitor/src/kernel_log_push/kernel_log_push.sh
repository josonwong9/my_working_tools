#!/bin/bash
# 注意：以下包名与接口地址均为脱敏占位符，部署时替换为实际值
set -x
/usr/local/bin/kernel_log_push

ip=`/sbin/ip a | /bin/grep 'inet 10' | /bin/grep eth0 | /bin/awk -F'/' '{print $1}' | /bin/awk '{ print $2}'`
/bin/rpm -q YOUR_KERNEL_MONITOR_RPM >/dev/null || /bin/rpm -q YOUR_KERNEL_MONITOR_LEGACY_RPM >/dev/null && kernel_monitor=1 || kernel_monitor=0
/bin/rpm -q YOUR_HOTFIX_RPM >/dev/null && kernel_hotfix=1 || kernel_hotfix=0
/bin/rpm -q YOUR_KPATCH_MODULES_RPM >/dev/null && kernel_modules=1 || kernel_modules=0
/usr/bin/curl --connect-timeout 10 -s "YOUR_API_HOST/panic/api/set_rpm_status/?ip=${ip}&kernel_monitor=${kernel_monitor}&kernel_hotfix=${kernel_hotfix}&kernel_modules=${kernel_modules}"
