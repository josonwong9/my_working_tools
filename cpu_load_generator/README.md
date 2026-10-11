这个目录是一个远程可控制的 CPU 负载生成器，用来在 Linux 主机上按指定百分比"压"出对应的 CPU 使用率，主要面向 Docker 测试环境（比如验证调度、资源隔离、超卖检测等场景）。

# 核心工作方式
- cpu_load_generator_server.py — 服务端，跑在目标主机上，监听 31415 端口。
- 收到一个整数 util（整机绝对百分比，每核 100，例如 4 核机器发 250 ≈ 62.5%）后，拆成两部分加载：
- whole = util // 100：启动 N 个死循环进程（while True: pass），每个吃满一个核；
- part = util % 100：用 stress-ng -l <part> -c 1 压出剩余的小数部分。
- 收到 util <= 0 就清理所有负载；收到非法输入回一条错误信息。
- cpu_load_generator_client.py — 交互式客户端（Python 2，用 raw_input），只能连本机 hostname，所以要在服务端主机上运行；输入数字改变负载，输 q 退出。
- oneshot.py — 非交互的远程客户端：./oneshot.py <host> [port] util，发一次就断开，供脚本自动化调用。
- cpu.py — 不联网的本地单机版，直接用同样的模型加载本机 CPU，Ctrl-C 停止。

# 自动化脚本（基于 docker 环境）
- sine_wave.sh — 在 Docker 宿主机上运行：用 docker inspect 读取一批容器的 CPU quota/period（即各自分到几个核），然后循环调用 oneshot.py 向每个容器按 配额 × 百分比 发送负载，并让百分比在 10~100 之间往复变化——在一组容器上模拟正弦波式的 CPU 利用率曲线，每 30 秒一变。
- sine_wave.template.sh — 另一个版本（模板）：不依赖 docker inspect，直接维护两组容器主机名列表（group_a/group_b，按核数比例换算负载），没有被任何脚本引用。
- start.sh / reset.sh — 操作入口：一个后台启动正弦波脚本，一个向指定主机发送 0 清空负载。目标主机改为参数传入或占位符，使用前需替换。

# 典型用途
从脚本逻辑看，它是在内部测试集群上人为制造可控的 CPU 压力：给容器/主机叠加已知的负载波形，然后观察或验证监控、调度、隔离类工具（比如这个工作区里的 cpu_sys_mon、overselling_detection 等）的表现。
需要提醒的是：这些代码是 Python 2 写法的 Linux 工具，当前 macOS 机器上没有 python2/stress-ng/docker，不能直接运行，只能阅读代码验证。


下面是完整用法。

# 前置条件
- 服务端（跑在目标机/容器里）：Linux + Python 2（/usr/bin/python）+ 已安装 stress-ng，31415 端口可被控制机访问。
- 控制端：oneshot.py 刚才实测在 Python 3 下也能跑，所以这台 Mac 就能用来远程控制；交互式客户端（raw_input）必须 Python 2 且在服务端本机跑。

场景 1：远程施加/调整 CPU 负载（最常用）
在目标机启动服务端（前台会打印接收日志，Ctrl-C 会清理负载退出；长期用建议套 nohup/tmux）：
./cpu_load_generator_server.py
在控制机上发送负载值（util 是整机绝对百分比，每核 100）：

4 核机器为例
python3 oneshot.py <目标机IP或主机名> 100    # 1 个核满载，整机 25%
python3 oneshot.py <目标机IP或主机名> 250    # 2.5 个核，整机 62.5%
python3 oneshot.py <目标机IP或主机名> 0      # 立即清空所有负载

需要指定端口时（默认 31415）：
python3 oneshot.py <host> 31415 250
参数顺序是 <host> [port] util，端口省略时第二个参数就是 util。

场景 2：在目标机本机交互式调节
只能在服务端所在机器上运行（客户端连的是本机 hostname）：
./cpu_load_generator_client.py
-> 250        # 回车后服务端返回回显，可反复输入
-> 0          # 清空负载
-> q          # 断开退出（注意：不会清负载）

场景 3：不需要网络的单机版
./cpu.py 250    # 直接在本机加载，Ctrl-C 停止
注意：cpu.py 对越界值（<10 或 >核数×100）只打印警告，仍会照常加载，别依赖它做校验。

场景 4：Docker 宿主机上的正弦波压测
start.sh / sine_wave.sh / reset.sh 是一套自动化：在 Docker 宿主机上，按容器配额推算每个容器该发多少，让一组容器的 CPU 使用率呈正弦波变化（10%~100%，每 30 秒一步）。

# 编辑 sine_wave.sh 顶部的 cid 列表（替换 REPLACE_WITH_CONTAINER_ID_* 占位符）
./sine_wave.sh          # 前台无限循环
./start.sh              # 后台启动（目标列表在 sine_wave.sh 中配置）
./reset.sh <主机名>     # 给指定主机发 0 停止加载（不带参数则用脚本内默认值）
工作原理：脚本对每个容器 docker inspect 出 CpuQuota/CpuPeriod（即分到几个核），然后发 核数 × 当前百分比——所以负载正好按容器配额比例缩放。这些容器 ID/主机名都是特定测试环境的，换环境必须替换占位符。sine_wave.template.sh 是另一个版本模板（按核数归一化、主机列表分 group_a/group_b 两组），没有任何脚本引用它。

# 注意事项
- 无认证：任何能访问 31415 端口的人都能把目标机 CPU 拉满，不要暴露到不可信网络。
- 服务端一次只处理一个连接（listen(1)），且每次新数值都会先清掉旧负载再起新负载（reset），不是叠加。
- 只有 part 部分依赖 stress-ng；即使 stress-ng 没装，whole 部分的死循环仍会跑起来（负载会偏小）。
- 服务端绑定的是 socket.gethostname()，如果目标机 hostname 只解析到 127.0.0.1，远程就连不上。
