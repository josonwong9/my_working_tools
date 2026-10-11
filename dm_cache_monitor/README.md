这个仓库是一个Linux dm-cache 缓存性能监控采集插件（Python 2 编写），用于把 device-mapper 缓存设备的运行指标输出为 JSON，供监控 Agent（输出格式类似 Open-Falcon 插件规范）采集上报。
具体来说，cache_info.py 每次运行做这几件事：
1. 发现缓存设备：通过 dmsetup status | grep writeback 找出所有处于 writeback 模式的 dm-cache 设备。
2. 解析设备名：用 dmsetup table 和 /dev、/dev/mapper 的符号链接，从缓存设备（cache_name）反查出对应的 dm 设备名和底层 HDD 名。
3. 采样两次：读取 /sys/block/<dm>/stat 的 I/O 累计计数，中间 sleep(tick=8) 秒，再采一次。
4. 计算指标（每块缓存设备 8 项）：
- r_iops / w_iops：读/写 IOPS（两次差值 ÷ 8 秒）
- read_hit_rate / write_hit_rate：读/写命中率（%）
- demote / promote：每 tick 的降级/提升速率
- used_rate：缓存使用率（%）
- dirty_rate：脏数据比例（%）
5. 输出 JSON：一组 {name, timestamp, value, tags:{host, dev}} 条目，timestamp 按 10 秒取整，tags 带主机名和设备名。
plugin.sh 是实际入口，负责切到脚本目录并用 python 执行。