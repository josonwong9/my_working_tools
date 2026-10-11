#!/bin/bash
#
# 清空目标机上所有 CPU 负载（向 server 发送 util=0）。
#
# 用法:
#   ./reset.sh <目标主机名或IP>   # 显式指定目标（推荐）
#   ./reset.sh                    # 使用下面的默认目标（占位符，需先替换）
#
# 前置条件: 目标机已运行 cpu_load_generator_server.py。
#

target="${1:-REPLACE_WITH_TARGET_HOST}"

if [[ "$target" == *REPLACE_WITH* ]]; then
	echo "ERROR: 请通过参数传入目标主机名，或替换脚本中的 REPLACE_WITH_TARGET_HOST（见文件头部说明）"
	exit 1
fi

./oneshot.py "$target" 0
