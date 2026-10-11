这个仓库是一个基于 eBPF 的 Linux 内核态 CPU 占用（sys%）采样分析工具。目标场景是排查线上节点（内部 k8s 节点，CentOS/RHEL 8.x、内核 4.18、x86-64）系统 CPU（kernel/sys 时间）异常升高时，到底是哪些内核函数在消耗 CPU。

# 工作方式
bpf/cpu_sys.c   →  eBPF 程序（内核态采样）
main.go         →  Go 加载器（挂载 BPF、加载并 attach 程序）
mon_data/       →  在目标机器采集的样本数据 + 栈解码脚本

内核态（bpf/cpu_sys.c）：kprobe 挂在内核函数 account_system_index_time 上（每次内核记账系统时间时触发）。程序按 CPU 累计 cputime，每隔约 1 秒计算一次系统时间占比：
sys% = (curr_sys - last_sys) * 100 / interval
当占比超过 SYS_THRESHOLD（50%）且不是 CPU 0（脚本作者发现 htop 常跑在 CPU 0 上，故意过滤）时，抓取当前内核调用栈（存入 stackmap），并把 {进程名, CPU, 时间戳, sys%, 栈 ID} 按 pid 存入 taskinfo map。两个 map 都 pin 在 /sys/fs/bpf/debug 下，可在程序运行时用 bpftool map dump 导出。

用户态（main.go）：挂载 bpffs、加载编译出的 CpuSys BPF 对象、把 kprobe 挂到目标函数上，然后一直运行到收到 SIGINT/SIGTERM。注意它本身不打印任何结果，数据都留在 BPF map 里。

# 脚本：
- generate.sh → go generate，用 bpftool 从内核 BTF 生成 vmlinux.h，再用 bpf2go/clang 编译 BPF 程序（先在 Linux 机器上跑，生成文件被 gitignore）
- run.sh → go run -exec sudo .，需要 root 才能加载 BPF
- unmount_bpffs.sh → 卸载 /sys/fs/bpf，用于清理 pin 住的旧 map（改了 map 结构导致加载失败时用）

# 数据与解码（mon_data/）
mon_data/sample-node-01/ 是一次真实采集：stack.log（bpftool map dump name stackmap 导出的原始栈地址）、taskinfo.log、以及目标机的 kallsyms、内核版本等信息。

mon_data/decode_stack.go 是独立的解码工具：把 stack.log 里的十六进制地址用 kallsyms 二分查找翻译成内核符号，输出调用栈（stack_decode.log 就是解码结果）。因为路径是写死的 ./kallsyms、./stack.log，必须 cd 进数据目录再运行：

cd mon_data/sample-node-01 && go run ../decode_stack.go
最终效果：从"某节点 sys% 飙到 50%+"定位到具体的内核调用栈（例如 account_system_index_time → update_process_times → tick_sched_timer ...），从而判断是定时器、页分配还是其他内核路径导致的高系统 CPU。

一个开发上的关键点：这个仓库本身在 macOS 上编译不了（代码全部带 //go:build linux，cpusys_bpf*.go 是 gitignore 的生成文件），需要在 Linux 目标机上 generate.sh + run.sh。仓库根目录还有个提交进 git 的 Linux 二进制 cpu_sys_mon，是历史构建产物。