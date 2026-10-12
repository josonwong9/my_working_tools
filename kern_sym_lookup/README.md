这是一个 Linux 内核崩溃/panic 日志分析工具集。典型场景：内核 panic 日志里只有裸地址（如 RIP: 0xffffffff81001234）和"开机后 N 秒"这类时间戳，需要人工转换成符号名和真实日期。仓库里 4 个文件分别解决这个问题：

# 三个工具
1. sym_lookup.c — 内核地址 → 符号名（核心工具）
- 启动时把 /proc/kallsyms 里的全部内核符号读入内存，按地址排序；
- 然后进入交互循环：输入一个十六进制内核地址，二分查找并输出包含该地址的最近符号（即该地址落在哪个函数里）；
- 能自动识别小端字节序的地址——崩溃 dump 里地址常以反转形式出现（如 00000081ffffffff 实为 ffffffff81000000），只要字符串不以 ffffffff 开头就会自动按字节对调；
- 输入可带或不带 0x，Ctrl+C 退出。
2. timestamp_to_date.sh <秒数> — 开机后的秒数 → 日历时间
- 崩溃日志常写"启动后 12345 秒发生 panic"，此脚本用两种方式推算真实时间：last reboot -F 查到的开机时刻，以及 /proc/uptime 反推的开机时刻，分别加秒数输出，互为校验。
3. two_date_seconds.sh "<日期1>" "<日期2>" — 两个日期相差多少秒
- 输出 date2 - date1 的秒数，用于时间轴换算（比如日志时间与开机时间的间隔）。

# 怎么用
在 Linux 目标机上（需 root，否则 kptr_restrict 会让 /proc/kallsyms 全部显示为 0）：

make                # gcc 编译出 sym_lookup

sudo ./sym_lookup   # 或 sudo make run

提示 "input kernel symbol address (hex):" 后输入地址，例如：

ffffffff81001234  或  0xffffffff81001234  或  34120081ffffffff

输出 [[ 函数名 ]] 0x函数起始地址

脚本直接在 Linux 上用：

./timestamp_to_date.sh 12345

./two_date_seconds.sh "Wed Aug 11 19:55:20 2021" "Thu Nov 25 20:28:29 2021"

# 重要限制
- 只能在 Linux 上运行；
- 仓库里已提交的 sym_lookup 是 Linux x86-64 ELF，是有意保留的可直接拷到目标机运行的产物；
- 修改代码前注意：load_kallsyms() 对数组上限 MAX_SYMS（30 万）没有越界检查，符号多的大内核可能溢出；ksym_search() 无符号可查时返回 NULL，调用方必须判空。
