# kernel_panic_monitor

内核 panic（crash）日志采集上报系统，自动收集机器上的内核 crash 日志并汇总到监控平台。

> **脱敏说明**：本仓库中的 Kafka 地址、SASL 凭证、接口域名、内部包名等均以 `YOUR_*` 占位符表示，部署时请替换为实际值；`unreport.md` 中的 IP、主机名、命名空间、机房、负责人姓名等同样已脱敏。

数据流向：

    /var/crash/<时间戳>/vmcore-dmesg.txt
        │  kernel_log_push（每台机器，cron 每 6 小时执行一次）
        ▼
    Kafka（topic: YOUR_KAFKA_TOPIC，key: crash_log）
        │  kernel_log_pull（常驻进程）
        ▼
    http://YOUR_API_HOST:8011/panic/crash/set

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `src/kernel_log_push/` | push 端（客户端采集器）源码，以及打包、cron、补报脚本 |
| `src/kernel_log_pull/` | pull 端（日志拉取上报）源码 |
| `src/vendor/` | 第三方依赖（GOPATH vendor 方式，含 sarama、docopt-go 等） |
| `unreport.md` | 产品线未安装监控机器的排查方法、机器列表等运维记录（已脱敏） |

## 核心文件

- `kernel_log_push.go`：客户端脚本，通过 crontab 定期执行，将系统发行版、内核版本、IP、crash 日志等信息 push 到 kafka 中。
- `kernel_log_pull.go`：日志拉取脚本，常驻运行，从 kafka 中拉取 crash 日志并提交至监控平台接口。

### kernel_log_push（客户端采集器）

运行位置：每台被监控机器，由 cron 每 6 小时以 root 执行一次。采集并上报的字段：

| 字段 | 说明 |
| --- | --- |
| crash_ip | 本机内网 IP（10./100. 网段） |
| crash_hostname | 主机名 |
| crash_os | /etc/redhat-release 内容 |
| crash_kernel | uname -r |
| crash_kpatch | /tmp/kpatch 下已加载的 kpatch 列表 |
| crash_kpatch_version | 补丁 rpm 版本（3.10 内核查 `YOUR_KPATCH_RPM`，2.6 内核查 `YOUR_HOTFIX_RPM`） |
| crash_time / crash_collect_time | crash 文件修改时间 / 采集时间 |
| crash_log | /var/crash/<时间戳>/vmcore-dmesg.txt 内容 |
| crash_version | 采集脚本版本（当前 1.2.4） |

行为说明：

- 每个 crash 目录生成一条 Kafka 消息；机器没有 crash 时也会推送一条只含系统基础信息的消息（可用于判断机器是否正常上报）。
- 首次运行（不存在 /tmp/kernel_log_push.flag）时，采集时间范围放宽到 2 年以补报历史 crash，并创建该 flag 文件；之后按 --duration 过滤。
- crash 日志超过约 5MB 时只保留末尾约 3MB（适配 Kafka 5MB 消息上限）。
- 启动时随机 sleep 0~600 秒，避免大量机器同时上报。

### kernel_log_pull（服务端拉取上报）

常驻进程，按 partition 消费 Kafka：

- 只处理 key 为 `crash_log` 的消息，将消息体原样 POST 到上报接口，并在日志中打印接口返回；
- offset 记录在 /tmp/kernel_log.offset（仅对匹配 key 的消息保存）。

## 编译

依赖已 vendor 到 `src/vendor`，仓库没有 go.mod，需要用 GOPATH 模式编译（要求已安装 Go 工具链）：

```bash
# 编译 push 端
cd src/kernel_log_push
GO111MODULE=off GOPATH=<仓库根目录> go build -o kernel_log_push kernel_log_push.go

# 编译 pull 端
cd src/kernel_log_pull
GO111MODULE=off GOPATH=<仓库根目录> go build -o kernel_log_pull kernel_log_pull.go
```

push 端也可以直接执行打包脚本，会在 `src/kernel_log_push/bin/` 下生成与目标机器路径一致的安装目录树：

```bash
cd src/kernel_log_push
./build.sh
```

生成结果：

    bin/usr/local/bin/kernel_log_push        # 采集程序
    bin/usr/local/bin/kernel_log_push.sh     # cron 入口脚本
    bin/etc/cron.d/cron_kernel_log_push      # crontab 配置
    bin/statistics.sh                        # 手动补报脚本

## 部署与使用

### push 端（被监控机器）

1. 将 `bin/` 下文件按路径拷贝到目标机器（二进制和 shell 脚本到 /usr/local/bin，cron 文件到 /etc/cron.d）。
2. cron 配置每 6 小时执行一次：`0 */6 * * * root /usr/local/bin/kernel_log_push.sh &>> /tmp/kernel_log_push.log`。
3. kernel_log_push.sh 执行采集程序后，还会上报三个 rpm 的安装状态（`YOUR_KERNEL_MONITOR_RPM`、`YOUR_HOTFIX_RPM`、`YOUR_KPATCH_MODULES_RPM`）。
4. 手动补报：执行 statistics.sh（以 nobody 用户运行 `kernel_log_push --sleep=1`，跳过随机等待）。

命令行参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| --kafka | YOUR_KAFKA_BROKER1:9092,YOUR_KAFKA_BROKER2:9092 | Kafka 集群地址 |
| --kafka_topic | YOUR_KAFKA_TOPIC | Kafka topic |
| --kafka_key | crash_log | Kafka 消息 key |
| --duration | 87000.00 | 只采集该时长（秒）内产生的 crash |
| --sleep | 600 | 启动时随机 sleep 0~600 秒 |
| --crash_path | /var/crash | crash 目录（当前实现固定使用该常量，传参不生效） |
| --crash_name | vmcore-dmesg.txt | crash 文件名 |
| --version | 1.2.4 | 上报的采集脚本版本 |

### pull 端（中心机器）

以常驻方式运行（systemd / nohup 等自行选择）：

```bash
./kernel_log_pull \
  --kafka=YOUR_KAFKA_BROKER1:9092,YOUR_KAFKA_BROKER2:9092 \
  --kafka_topic=YOUR_KAFKA_TOPIC \
  --kafka_key=crash_log \
  --submit_api=http://YOUR_API_HOST:8011/panic/crash/set \
  --offset_file=/tmp/kernel_log.offset
```

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| --kafka | YOUR_KAFKA_BROKER1:9092,YOUR_KAFKA_BROKER2:9092 | Kafka 集群地址 |
| --kafka_topic | YOUR_KAFKA_TOPIC | Kafka topic |
| --kafka_key | crash_log | 只上报该 key 的消息 |
| --submit_api | http://YOUR_API_HOST:8011/panic/crash/set | 上报接口 |
| --offset_file | /tmp/kernel_log.offset | offset 记录文件 |

## 占位符说明（脱敏）

| 占位符 | 出现位置 | 说明 |
| --- | --- | --- |
| `YOUR_KAFKA_BROKER1:9092` / `YOUR_KAFKA_BROKER2:9092` | 代码 / 文档 | Kafka broker 地址 |
| `YOUR_KAFKA_USER` / `YOUR_KAFKA_PASSWORD` | 代码 | Kafka SASL 账号与密码 |
| `YOUR_KAFKA_TOPIC` | 代码 / 文档 | Kafka topic 名称 |
| `YOUR_ODIN_HOST` / `YOUR_KAFKA_NAMESPACE` | 代码 | 机器列表查询服务地址与命名空间 |
| `YOUR_API_HOST` | 代码 / 脚本 / 文档 | 上报接口服务地址（端口 8011） |
| `YOUR_KPATCH_RPM` / `YOUR_HOTFIX_RPM` | 代码 | 补丁 RPM 包名前缀 |
| `YOUR_KERNEL_MONITOR_RPM` / `YOUR_KERNEL_MONITOR_LEGACY_RPM` / `YOUR_KPATCH_MODULES_RPM` | 脚本 | rpm 状态上报涉及的包名 |
| `<ip>` / `<hostname>` / `<region>` / `<namespace>` / `<owner>` | unreport.md | 机器 IP、主机名、机房、产品线、负责人 |
| `<internal-yum-repo>` / `BATCH_HOST_TOOL` / `BATCH_SSH_TOOL` | unreport.md | 内部 yum 源与批量运维工具 |
| `<internal-gitlab>` | AGENTS.md | 内部代码托管地址 |

## 注意事项

- 接口地址与 Kafka 均为内网地址，外网无法访问。
- Kafka 集群、SASL 账号密码、接口地址等为代码常量占位符，部署时替换为实际值；不要把真实凭证提交到代码仓库。
- `--crash_path` 参数不生效，程序固定使用常量 /var/crash。
- pull 端 offset 处理待完善：offset 文件不存在时会触发 panic；且当前实现总是从 Kafka 最新位置开始消费，保存的 offset 未真正生效，改动该逻辑时需注意。
- push/pull 两端通过 Kafka key `crash_log` 对接，修改 key 时两端需要同步。

## 版本记录

- v1.0
    1. 初始化版本
- v1.1
    - Feature：
        1. 增加收集已加载 kpatch list
        2. 记录 rpm 版本号
        3. 支持内网机器日志收集
    - Fix：
        1. 修复 pull offset 文件不存在的问题
- v1.2
    1. 修复 panic 文件过大问题
    2. 上报增加补丁版本
- v1.2.4
    1. 修改 crontab 任务为每 6 小时执行一次
    2. 修复某些废弃 kafka 不通的问题，并且针对当前所有 kafka 全部失败的情形下去内部服务获取新的 kafka 来进行重试兜底
    3. 修复 --duration 参数无效问题
