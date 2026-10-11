#!/bin/bash
#
# 正弦波 CPU 负载（模板版）— 直接维护容器主机名，不依赖 docker inspect
#
# 用途:
#   与 sine_wave.sh 相同：周期性发送变化的 CPU 负载，让一组容器的 CPU
#   使用率按正弦波在 low~high（归一化百分比）之间变化。
#   区别：本模板不执行 docker inspect，容器主机名直接写在下面的两个组里，
#   适合主机名固定、或容器环境不方便执行 docker 命令的场景。
#   本文件不被其他脚本引用。
#
# 前置条件:
#   1. 每个目标容器内已启动 cpu_load_generator_server.py 并安装 stress-ng;
#   2. 本机能解析下列主机名，且能访问其 31415 端口。
#
# 使用前必须替换:
#   group_a / group_b 中的 REPLACE_WITH_HOST_* 占位符，以及 nr_cpus 和 B 组倍数。
#
# 运行: ./sine_wave.template.sh
# 停止: ./reset.sh <容器主机名> 或 ./oneshot.py <容器主机名> 0
#

normalized_high=100 # 归一化上限（百分比）
normalized_low=10   # 归一化下限（百分比）
normalized_step=5   # 每次变化的步长（百分比）
nr_cpus=4           # A 组每个容器的核数配额，按实际修改
interval=60         # 每次变化的间隔（秒）

high=$((normalized_high * nr_cpus))
low=$((normalized_low * nr_cpus))
step=$((normalized_step * nr_cpus))
echo "High CPU Util: " $high
echo "Low CPU Util: " $low

# A 组容器主机名（占位符，必须替换；可增删行）
group_a=(
"REPLACE_WITH_HOST_A_1"
"REPLACE_WITH_HOST_A_2"
"REPLACE_WITH_HOST_A_3"
)

# B 组容器主机名；假定该组核数为 A 组的 2 倍，故发送 2 倍负载（按实际修改）
group_b=(
"REPLACE_WITH_HOST_B_1"
"REPLACE_WITH_HOST_B_2"
)

if [[ "${group_a[*]}" == *REPLACE_WITH* || "${group_b[*]}" == *REPLACE_WITH* ]]; then
	echo "ERROR: 请先把 group_a / group_b 中的 REPLACE_WITH_HOST_* 替换成你自己的容器主机名（见文件头部说明）"
	exit 1
fi

if [ ${#group_a[@]} -eq 0 ] || [ ${#group_b[@]} -eq 0 ]; then
	echo "ERROR: group_a / group_b 不能为空"
	exit 1
fi

u=$high

while [[ 1 ]];
do
	echo "$u / $((nr_cpus*100)) = $((u/nr_cpus))%"
	for c in ${group_a[@]}
	do
		./oneshot.py $c $u
	done
	for d in ${group_b[@]}
	do
		./oneshot.py $d $((u*2))
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
