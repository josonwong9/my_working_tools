#!/bin/bash
#
# sort.sh - 解析 /proc/trace_runqlat/runqlat 的输出，按 cgroup 或 pid 聚合各任务的运行时长
#
# 用法:
#   ./sort.sh [-f FILE] [-s cgroup|pid]
#     -f FILE        要解析的文件，默认为 /proc/trace_runqlat/runqlat
#     -s cgroup|pid  聚合方式，默认 cgroup
#
# 说明:
#   - 需在加载了 trace_runqlat 内核模块的 Linux 机器上运行；
#   - 解析的如果是已保存的输出文件，用 -f 指定文件路径即可；
#   - 以下示例中的 pod UID、容器 ID、服务名等均为脱敏占位符，仅用于说明输出格式，
#     实际输出以你的环境为准，脚本本身不需要修改。

g_file=/proc/trace_runqlat/runqlat
g_sort="cgroup" # cgroup, pid

# 输入示例（runqlat 的输出格式）:
# [27994217.785957] [028]       COMM: PM2 v2.7.2: God  PID: 31109    RUNTIME(us):    215 CGROUP: /kubepods/burstable/pod<UID>/<CONTAINER_ID> state: S nr_running(at sched_switch):0/0/1/1/1

# sort_by_cgroup - sort runtime by cgroup
#
# 输出示例:
#runtime      cgroup
#4854         /
#5238         /kubepods/burstable/pod<UID>/<CONTAINER_ID>/example-service
#5533         /kubepods/burstable/pod<UID>/<CONTAINER_ID>
#5730         /kubepods/burstable/pod<UID>/<CONTAINER_ID>
#5931         /kubepods/burstable/pod<UID>/<CONTAINER_ID>
#9526         /kubepods/burstable/pod<UID>/<CONTAINER_ID>/example-service
#48956        /example-service

function sort_by_cgroup()
{
	local g

	printf "\33[47;30m%-12s %s\33[0m\n" "runtime" "cgroup"

	for g in `grep RUNTIME $g_file | awk '{for (i=1;i<=NF;i++) {if ($i == "CGROUP:") print $(i+1)}}' | sort | uniq`
	do
		awk -v vg=$g '
			BEGIN {s=0}
			{
				for (i=1;i<=NF;i++) {
					if ($i == "CGROUP:") {
						if ($(i+1) == vg)
							is_match=1
						else
							is_match=0
						break;
					}
				}
				if (is_match == 1) {
					for (i=1;i<=NF;i++) {
						if ($i == "RUNTIME(us):")
							s += $(i+1)
					}
				}
			}
			END {printf("%-12s %s\n", s, vg)}' $g_file
	done | sort -rnk 1
}

# 输出示例:
#runtime      pid          comm             cgroup
#5346         558589       java             /example-service
#4864         1826390      example-agent    /kubepods/burstable/pod<UID>/<CONTAINER_ID>
#4834         552367       java             /example-service
#4759         2028695      docker-containe  /system.slice/docker.service
#4107         2037610      client           /

function sort_by_pid()
{
	local p

	# header
	printf "\33[47;30m%-12s %-12s %-16s %s\33[0m\n" "runtime" "pid" "comm" "cgroup"

	for p in `grep RUNTIME $g_file | awk '{for (i=1;i<=NF;i++) {if ($i == "PID:") print $(i+1)}}' | sort -n | uniq`
	do
		awk -v vp=$p '
			BEGIN {s=0}
			{
				for (i=1;i<=NF;i++) {
					if ($i == "PID:") {
						if ($(i+1) == vp)
							is_match=1
						else
							is_match=0
						break;
					}
				}
				if (is_match == 1) {
					for (i=1;i<=NF;i++) {
						if ($i == "COMM:")
							comm=$(i+1)
						else if ($i == "RUNTIME(us):")
							s += $(i+1)
						else if ($i == "CGROUP:")
							cgroup = $(i+1)
					}
				}
			}
			END {printf("%-12s %-12s %-16s %s\n", s, vp, comm, cgroup)}' $g_file
	done | sort -rnk 1

}

function usage()
{
	echo "sort.sh [-f FILE] [-s sort] [-h}"
	echo "    f: target file to parse, /proc/trace_runqlat/runqlat by default"
	echo "    s: sort the output, sort output by cgroup by default"
	echo "       The available sort keys: cgroup, pid"
	echo "    h: show this help"
}

function main()
{
	local k

	while getopts "f:s:h" k
	do
		case $k in
			f) g_file=$OPTARG;;
			s) g_sort=$OPTARG;;
			h) usage; exit;;
			*) usage; exit;;
		esac
	done

	if [[ ! -e $g_file ]]; then
		echo "$g_file is not exit, plese check!!!"
		usage
		exit
	fi


	case $g_sort in
		"cgroup") # sort by cgroup
			sort_by_cgroup;;
		"pid") # sort by pid
			sort_by_pid;;
		*) usage; exit;;
	esac
}

main $@
