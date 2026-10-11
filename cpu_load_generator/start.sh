#!/bin/bash
#
# 后台启动正弦波 CPU 负载（sine_wave.sh）。
#
# 目标容器列表在 sine_wave.sh 顶部配置，本脚本不接收参数;
# sine_wave.sh 的 stdout 被丢弃，需要看输出请在前台直接运行 ./sine_wave.sh。
#

cd "$(dirname "$0")" || exit 1
./sine_wave.sh > /dev/null &
echo "sine_wave.sh 已在后台启动 (pid $!)"
