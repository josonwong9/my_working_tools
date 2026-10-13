#!/usr/bin/env bash
#!/bin/sh

# 注意：以下包名与接口地址均为脱敏占位符，部署时替换为实际值
echo 'start check patch'

ip=`ip a | grep 'inet 10' | grep eth0 | awk -F'/' '{print $1}' | awk '{ print $2}'`
rpm -q YOUR_KERNEL_MONITOR_RPM >/dev/null || rpm -q YOUR_KERNEL_MONITOR_LEGACY_RPM >/dev/null && kernel_monitor=1 || kernel_monitor=0
rpm -q YOUR_HOTFIX_RPM >/dev/null && kernel_hotfix=1 || kernel_hotfix=0
rpm -q YOUR_KPATCH_MODULES_RPM >/dev/null && kernel_modules=1 || kernel_modules=0
curl --connect-timeout 10 -s "YOUR_API_HOST/panic/api/set_rpm_status/?ip=${ip}&kernel_monitor=${kernel_monitor}&kernel_hotfix=${kernel_hotfix}&kernel_modules=${kernel_modules}"

echo "    "
echo 'check over'

echo 'monitor push'
su -m nobody -c '/usr/local/bin/kernel_log_push --sleep=1 >>/dev/null 2>&1'
echo 'push over'
