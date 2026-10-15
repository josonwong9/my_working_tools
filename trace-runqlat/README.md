# Trace-runqlat

Trace_irqoff工具可以帮助我们排查由于中断被关闭导致的延迟抖动问题。但是这只是第一阶段可能遇到的问题。当我们将一个线程加入runqueue后，如果系统的负载很高，在runqueue中排在你前面的进程可能会由于执行很长时间（或许你会好奇怎么可能执行很长时间，难道不会抢占吗？还真可能出现这种情况。因为我们的kernel是不开抢占）导致最终runqueue latency很大。针对这种问题排查，我们开发一款新的排查工具**Run queue latency tracer。简称：trace-runqlat。**作用是记录被跟踪的进程在加入runqueue后，前面的每个线程执行的时间以及线程command等信息。

## 使用场景

- 某个延迟敏感线程（音视频、交易、游戏、实时计算等）偶发长时间卡顿，且已经排除了中断关闭（trace_irqoff）的原因，怀疑是 runqueue 排队导致。
- 系统 CPU 负载高（超卖、混部）时，线程长时间抢不到 CPU，需要回答"排在我前面的都有谁、各自跑了多久"。
- 内核不开抢占（如 CentOS 7 的 3.10 内核）时，长任务无法被及时抢占，导致后续线程 runqueue latency 抖动。
- 多租户/容器环境定位异常占用 CPU 的租户：结合记录中的 cgroup 字段和 sort.sh 按 cgroup 聚合运行时长，找出是谁占用了目标线程的等待时间。

## 工作原理

trace-runqlat 是一个内核模块（源码为单文件：trace_runqlat.c），通过挂接调度器 tracepoint 记录调度事件：

| tracepoint | 作用 |
| --- | --- |
| sched_wakeup / sched_wakeup_new | 被跟踪线程被唤醒加入 runqueue 时，记录入队时间与所在 CPU |
| sched_switch | 目标 CPU 上每次任务切换时，记录切出任务本次的运行时长；当被跟踪线程被调度上 CPU 时，计算 runqueue latency（本次等待时长），超过阈值则提交一条跟踪记录 |
| sched_migrate_task | 被跟踪线程在 CPU 间迁移时，收尾当前 CPU 上的记录，并切换到新 CPU 继续跟踪 |

> 内核未导出 `__tracepoint_sched_*` 符号，模块启动时会通过 `for_each_kernel_tracepoint()` 按名字查找这些 tracepoint，再注册回调。

- 只有 latency 超过阈值（threshold，默认 20ms）的等待事件才会被记录，避免高频写入带来额外开销；阈值设得过小会产生大量记录并增加模块开销。
- 每条记录包含：被跟踪线程的 latency、runqlen（记录到的前置任务数）、迁移次数，以及排在前面的每个任务的 COMM、PID、运行时长、cgroup、state 和各层级 cgroup 的 nr_running。
- 记录存放在环形缓冲区中，新记录会覆盖最旧记录：最多 max_trace_entries 条记录（默认 128，最大 256），每条最多保存 per_trace_tasks 个任务（默认 16，最大 32），全部占满时占用内存约 32MB。
- 每次等待事件同时计入延迟直方图（毫秒，2 的幂分桶），即 distribution 文件。
- 任务信息（含 cgroup 路径）在调度现场保存，因为打印时该任务可能已经退出，无法事后获取。
- `struct cfs_rq` 未导出，读取 nr_running 使用了固定偏移的 `struct my_cfs_rq`（目前仅验证过 3.10 与 4.18 的内核布局）。
- 通过 `LINUX_VERSION_CODE` 适配 3.10（CentOS 7）、4.18（CentOS 8）以及 5.6 以上内核（如 `proc_ops` 接口变更）。

## 如何安装

安装 trace_runqlat 工具很简单，git clone 代码后执行如下命令即可安装（需要安装对应内核版本的头文件/kernel-devel，以及 root 权限）。

```bash
make -j8
make install
```

卸载：

```bash
make remove
```

如需调整内存占用，可在加载时指定模块参数（模块参数只读，加载后不可修改）：

```bash
insmod trace_runqlat.ko max_trace_entries=256 per_trace_tasks=32
```

- `max_trace_entries`：环形缓冲区最多保存的跟踪记录条数，默认 128，最大 256。
- `per_trace_tasks`：每条记录最多保存的前置任务数，默认 16，最大 32。

## 如何使用

安装 trace-runqlat工具成功后。会创建如下 **/proc/trace_runqlat** 目录。

```bash
ls /proc/trace_runqlat
distribution  pid  runqlat  threshold
```

/proc/trace_runqlat 目录下存在 4 个文件，分别：distribution, pid, runqlat和threshold。工具安装后，默认是打开状态，但是并没有trace任何线程，我们需要手动设置需要trace的线程pid。

### 1. 跟踪特定pid线程

```
echo $pid > /proc/trace_runqlat/pid
```

同一时刻只能跟踪一个 PID；要换一个线程跟踪时，必须先关闭当前跟踪（见下节），否则写入会失败并返回 EPERM。写入新的 pid 会清空已有的记录。

### 2. 关闭跟踪

```
echo -1 > /proc/trace_runqlat/pid
```

### 3. 设置阈值

trace-runqlat只会针对延迟超过阈值的情况，记录runqueue前面的线程执行的时间情况。为了更高效的运作，我们有必要设定一个合理阈值。例如设置60ms的阈值：

```
echo 60000000 > /proc/trace_runqlat/threshold # 单位ns
```

如果需要查看当前设定的阈值，可执行如下命令：

```
cat /proc/trace_runqlat/threshold
```

### 4. 查看线程runqueue latency分布

我们以直方图的形式展示被跟踪进程runqueue latency的分布情况。

```
cat /proc/trace_runqlat/distribution
```

你看到的信息展示如同下面这样：

```
     msecs      : count  distribution
     1 -> 1     : 0     |                                        |
     2 -> 3     : 0     |                                        |
     4 -> 7     : 0     |                                        |
     8 -> 15    : 0     |                                        |
     16 -> 31   : 0     |                                        |
     32 -> 63   : 68    |****************************************|
```

我们可以看到latency集中在[32, 63]毫秒，次数68次。

### 5. 是谁在runqueue的前面

针对latency超过阈值的情况，我们会记录runqueue前面的线程执行情况。

Note: 建议先关闭跟踪（echo -1）再查看该文件，以保证输出的一致性。

```
cat /proc/trace_runqlat/runqlat
[27994217.785957] [028] PID: 3789446    0   latency(us): 35999 runtime(us): 0 runqlen: 10 migrations: 0, cgroup: /
[27994217.785958] [028]       COMM: loop7            PID: 3789453  RUNTIME(us):   4000 CGROUP: / state: R nr_running(at sched_switch):2/2/3
[27994217.789958] [028]       COMM: loop2            PID: 3789448  RUNTIME(us):   4001 CGROUP: / state: R nr_running(at sched_switch):2/2/3
[27994217.793959] [028]       COMM: loop4            PID: 3789450  RUNTIME(us):   3998 CGROUP: / state: R nr_running(at sched_switch):2/2/3
[27994217.797957] [028]       COMM: loop1            PID: 3789447  RUNTIME(us):   3999 CGROUP: / state: R nr_running(at sched_switch):2/2/3
[27994217.801956] [028]       COMM: loop3            PID: 3789449  RUNTIME(us):   4000 CGROUP: / state: R nr_running(at sched_switch):2/2/3
[27994217.805956] [028]       COMM: loop8            PID: 3789454  RUNTIME(us):   3999 CGROUP: / state: R nr_running(at sched_switch):2/2/3
[27994217.809958] [028]       COMM: loop9            PID: 3789455  RUNTIME(us):   4001 CGROUP: / state: R nr_running(at sched_switch):2/2/3
[27994217.809961] [028]       COMM: kworker/0:2     PID: 3621088  RUNTIME(us):      3 CGROUP: / state: R nr_running(at migrate_task):2/2/3
[27994217.813956] [028]       COMM: loop5            PID: 3789451  RUNTIME(us):   3994 CGROUP: / state: R nr_running(at sched_switch):2/2/3
[27994217.817955] [028]       COMM: loop6            PID: 3789452  RUNTIME(us):   3999 CGROUP: / state: R nr_running(at sched_switch):2/2/3
```

我们可以看到runqueue latency是35999us。runqueue前面有10个进程。每个进程的执行时间加在一起差不多就是总的latency。

输出字段说明：

| 字段 | 说明 |
| --- | --- |
| `latency(us)` | 被跟踪线程本次在 runqueue 中的等待时长 |
| `runtime(us)`（trace 行）| 被跟踪线程上一次运行的时长 |
| `runqlen` | 本次等待记录到的前置任务数（受 per_trace_tasks 限制，实际任务更多时可能被覆盖而偏小）|
| `migrations` | 本次等待期间被跟踪线程迁移 CPU 的次数 |
| `cgroup`（trace 行）| 被跟踪线程所在的 cpu cgroup |
| `COMM` / `PID` | 前置任务的进程名 / PID |
| `RUNTIME(us)` | 该前置任务本次占用 CPU 的时长 |
| `CGROUP` | 该前置任务所属 cgroup |
| `state` | 记录时该任务的状态：R(可运行)、S(可中断睡眠)、D(不可中断睡眠)、O(其他) |
| `nr_running(at sched_switch/migrate_task)` | 记录时机（任务正常切出还是因迁移记录），以及该任务所在各层级 cgroup 的 nr_running（从叶到根，以 / 分隔）|

### 6. 清空记录

```
echo 0 > /proc/trace_runqlat/runqlat
```

### 7. 按cgroup/pid聚合统计（sort.sh）

仓库中的 sort.sh 可以解析 runqlat 的输出，按 cgroup 或 pid 汇总各任务占用的运行时长，便于快速找出消耗目标线程等待时间的"大户"。

```
./sort.sh [-f FILE] [-s sort]
    f: 要解析的文件，默认为 /proc/trace_runqlat/runqlat
    s: 聚合方式，cgroup（默认）或 pid
```

按 cgroup 聚合的输出示例（pod UID、容器 ID、服务名均为占位符）：

```
runtime      cgroup
4854         /
5238         /kubepods/burstable/pod<UID>/<CONTAINER_ID>/example-service
48956        /example-service
```

按 pid 聚合的输出示例：

```
runtime      pid          comm             cgroup
5346         558589       java             /example-service
4864         1826390      example-agent    /kubepods/burstable/pod<UID>/<CONTAINER_ID>
```

注意：
- 以上示例中的 pod UID、容器 ID、服务名均为占位符，仅用于说明输出格式，脚本无需修改；
- sort.sh 按字段名（COMM、PID、RUNTIME(us)、CGROUP）解析 runqlat 的输出，修改输出格式时需要同步更新脚本；
- 脚本默认读取 /proc/trace_runqlat/runqlat，如果解析的是已保存的输出文件，用 -f 指定文件路径。
