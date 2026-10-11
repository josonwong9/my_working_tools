#!/bin/bash
#
# 正弦波 CPU 负载 — 在 Docker 宿主机上运行
#
# 用途:
#   对一组容器周期性发送变化的 CPU 负载值，使其 CPU 使用率按"正弦波"
#   在 low~high 之间往复变化。
#   每个容器实际发送的负载 = 容器核数配额 × 当前百分比。
#
# 前置条件:
#   1. 在能执行 docker 命令的 Docker 宿主机上运行;
#   2. 每个目标容器内已启动 cpu_load_generator_server.py 并安装 stress-ng;
#   3. 本机能解析容器主机名，且能访问其 31415 端口。
#
# 使用前必须替换:
#   把下面 cid 列表换成你自己的容器 ID（`docker ps -q` 可列出全部 ID）。
#   脚本会通过 `docker inspect` 解析出各容器主机名，再逐个发送负载。
#
# 运行: ./sine_wave.sh
# 停止: ./reset.sh <容器主机名> 或 ./oneshot.py <容器主机名> 0
#

high=100    # 正弦波上限（百分比，相对容器配额）
low=10      # 正弦波下限（百分比）
step=5      # 每次变化的步长（百分比）
interval=30 # 每次变化的间隔（秒）

# 目标容器 ID 列表（占位符，必须替换；数量可增删）
cid="
REPLACE_WITH_CONTAINER_ID_1
REPLACE_WITH_CONTAINER_ID_2
REPLACE_WITH_CONTAINER_ID_3
"

if [[ "$cid" == *REPLACE_WITH* ]]; then
	echo "ERROR: 请先把 cid 中的 REPLACE_WITH_CONTAINER_ID_* 替换成你自己的容器 ID（见文件头部说明）"
	exit 1
fi

names=""
limits=""

for i in $cid
do
	name=`docker inspect -f {{.Config.Hostname}} $i`
	quota=`docker inspect -f {{.HostConfig.CpuQuota}} $i`
	period=`docker inspect -f {{.HostConfig.CpuPeriod}} $i`
	if [ -z "$name" ] || [ -z "$quota" ] || [ -z "$period" ]; then
		echo "ERROR: docker inspect 失败：请确认容器 ID 正确且本机 docker 可用: $i"
		exit 1
	fi
	names="${names} ${name}"
	limit=$((quota/period))
	limits="${limits} ${limit}"
done
read -r -a na <<< "$names"
read -r -a la <<< "$limits"

if [ ${#na[@]} -eq 0 ]; then
	echo "ERROR: 未解析到任何容器主机名，请检查 cid 列表"
	exit 1
fi

u=$high

while [[ 1 ]];
do
	for (( i=0;i<${#na[@]};i++ ))
	do
		cpu=${la[$i]}
		./oneshot.py ${na[$i]} $((cpu*u))
	done
	u=$((u-step))
	if [[ $u -le $low ]]; then
		step=$((-step))
	fi
	if [[ $u -ge $high ]]; then
		step=$((-step))
	fi
	sleep $interval
done
