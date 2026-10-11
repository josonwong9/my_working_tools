Block IO scripts

这是一个用于 Linux 块设备 I/O 性能测试与内核块层追踪 的脚本仓库（纯 Bash，没有编译、测试、CI）。主要用途是对 NVMe/块设备做 fio 基准测试，并比较不同 I/O 调度策略（bfq / iocost / NVMe WRR）在 cgroup 权重下的表现。

目录结构与功能
blkio/
├── rwc                  # 预编译的 x86-64 Linux 静态 ELF（读写+比对小工具，源码不在仓库）
├── README.md            # 只有一行 "Block IO scripts"
└── script/
    ├── fio/             # fio 基准测试 + 结果解析
    ├── blktrace/        # 基于 ftrace blk tracer 的 I/O 追踪与设备状态采集
    └── trace/           # 参考资料：内核块层代码路径笔记、结果表格

script/fio/ — 性能测试主力
- conf.sh：公共配置 + run_fio() 函数。默认参数直接写裸设备 /dev/nvme1n1（direct=1、1G、60s、libaio）。所有 test_* 变量可被调用方 export 覆盖。
- 工作负载脚本：每个文件一个场景
- sr.sh / sw.sh：顺序读 / 写，256K
- rr.sh / rw.sh：随机读 / 写，4K
- 用法：./rr.sh [jobs] [iodepth]
- test.sh：调度器对比测试。用法 ./test.sh <sched> <test> <threads> <depth>（如 ./test.sh bfq rr 8 32），把同一负载在两个 cgroup（test1/test2，权重 400/100；wrr 用 high/low）中并发跑，对比调度效果。需要 root，会改写 /sys/block/<disk>/queue/scheduler；wrr 还会调用 nvme set-feature。
- parse_fio_terse.sh：解析 fio 的 terse v3 输出，打印带宽、IOPS、平均延迟、p99 延迟。字段索引对照表在 fio_terse.log（第 7/48 列带宽、8/49 列 IOPS、40/81 平均延迟、30/71 p99）。
- tcpu.sh / tloop.sh：批量扫描脚本（按 CPU 数或 iodepth 循环），硬编码了 /dev/nvme0n1、-cpus_allowed=0-39 和长 sleep，属于针对特定机器的测试。
- log/p.sh、log/pmix.sh：解析普通（非 terse）fio 输出的旧脚本。

script/blktrace/ — 内核侧追踪
注意：这里没有用 blktrace 工具，而是通过 /sys/kernel/debug/tracing 的 ftrace blk tracer 做记录（需 root、debugfs）：
- record.sh：自动 reset + 设置事件 + 采集 iostat 和 trace，输出到 /tmp/20*.log
- t.sh：交互式录制（按回车停止），输出 ./log
- all.sh：定时录制 10 秒，输出 ./full.log
- set_trace.sh / reset_trace.sh：开关 block_bio_queue 事件
- get_blkq_setting.sh：导出各设备 /sys/block/*/queue 参数
- overlap.sh <dev>：轮询 /proc/diskstats 估算设备繁忙（overlap）程度

典型使用流程
在 Linux 测试机上以 root 运行：
cd script/fio
export test_file_name=/dev/nvme0n1   # 必须覆盖，否则写 /dev/nvme1n1
./rr.sh 8 32                          # 单场景测试 → log/randread_8_32.log(.parse)
./test.sh bfq rr 8 32                 # 比较 bfq 调度下两个 cgroup 的表现
./parse_fio_terse.sh log/             # 汇总解析整个 log 目录

重要注意事项
- 破坏性默认值：不改 test_file_name 就直接跑，会清空 /dev/nvme1n1 上的数据。
- .gitignore 抄自 Linux 内核：.* 会忽略所有点文件，还包含 *.patch、*.bin、*.gz 等规则。
- 测试产生的 log/*.log*、result_20* 是临时产物；仓库里现有的 fio_terse.log、*.xlsm/xlsx、blk_stat.txt 是刻意保留的参考资料。


关于 rwc 这个程序：
rwc（未 strip，符号和字符串都在），它的完整行为可以还原出来。结论：用法只有一种：./rwc <大小(MB)>，没有其它参数或选项。
使用方法
# 在 Linux 测试机上（必须 x86-64，静态 ELF）
cd /你的可写目录        # abc.test 会建在当前目录
./rwc 1024             # 在当前目录创建/覆盖 1GB 的 abc.test，循环写→读→校验
- 唯一参数是文件大小，单位 MB（内部 atoi(argv[1]) << 20）。
- 无参数运行只会打印 please input file size in MB 然后退出（注意：它不会读 stdin，这句提示容易误解）。
- 程序本身无限循环，只能 Ctrl+C 或出错时退出。跑完文件不会删除，需自行 rm abc.test。

用途和定位
这是一个数据完整性校验 / 简单 IO 压测小工具（read-write-compare）：每轮把整个文件以 O_DIRECT 覆写、读回、逐字节比对，用来验证 SSD/设备是否发生静默数据损坏（返回数据和写入不一致）。它和 script/fio/ 里的 fio 压测是互补的，不被任何脚本调用，独立使用。
值得注意的坑
- 只能 Linux x86-64：仓库里这份是预编译静态 ELF，源码 rwc.c 不在仓库，无法重新编译；要改文件名/大小逻辑只能重写。
- O_DIRECT：文件必须放在支持 direct I/O 的本地文件系统（ext4/xfs 可以；tmpfs 不行），否则 open 失败。需要至少 size MB 的磁盘空间。
- 固定文件名 abc.test、固定位置（当前目录），不能指定路径或裸设备；同一目录并发跑两个实例会互相干扰。
- 出错时退出码仍是 0（返回的是 close() 的结果），判断成败要看输出，不要看 $?。
- 校验失败时 dump() 会从出错位置一直打印到缓冲区末尾（每行 4 字节 actual + "should be" + 4 字节 expected），size 很大时输出可能非常多。
- 原文里的两个小瑕疵：open 失败消息拼成了 erron:%d；读失败时也打印 write error, %d,%s 这条消息。
- 每轮之间有 1 秒 usleep，所以它测的是单次大块写/读 + 校验，不是持续吞吐压测；测吞吐请用 script/fio/。
示例：想验证某块盘的稳定性，把 rwc 拷到挂载该盘的目录，cd 过去执行 ./rwc 2048（2GB），观察是否出现 should be 的校验失败行。
