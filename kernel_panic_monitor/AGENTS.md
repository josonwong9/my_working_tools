# AGENTS.md

Internal kernel-panic collection tooling (Chinese-language project). Two independent Go programs share one Kafka topic: a per-host pusher and a long-running puller. `README.md` is the release changelog; `unreport.md` is an ops runbook (host inventories plus batch install commands), not documentation.

> All Kafka brokers, SASL credentials, API hosts, and internal package names are redacted as `YOUR_*` placeholders; replace them with real values at deploy time.

## Layout

- `src/kernel_log_push/kernel_log_push.go` — deployed to every monitored host, run by cron every 6h (`cron_kernel_log_push` -> `/usr/local/bin/kernel_log_push.sh`). Scans `/var/crash/<dir>/vmcore-dmesg.txt`, gathers IP/hostname/OS/kernel/kpatch list (from `/tmp/kpatch`), and publishes JSON to Kafka.
- `src/kernel_log_pull/kernel_log_pull.go` — long-running consumer; POSTs matching Kafka messages to `KERNEL_SUBMIT_API` and writes progress to `/tmp/kernel_log.offset`. No build script exists for it; build it the same way as the pusher.
- `src/kernel_log_push/build.sh` — builds the push binary and stages an install tree under `bin/` (`bin/usr/local/bin`, `bin/etc/cron.d`, `bin/statistics.sh`). Assumes cwd = `src/kernel_log_push` and `go` on PATH.
- `src/vendor/` — all Go deps vendored GOPATH-style (sarama, docopt-go). Never edit vendor; there is no `go.mod`.

## Build / test

- Legacy GOPATH layout, no go.mod / Makefile / CI / first-party tests. Modules mode fails; build with vendor resolution:
  `cd src/kernel_log_push && GO111MODULE=off GOPATH=<repo-root> go build -o <output> kernel_log_push.go` (same for `kernel_log_pull.go`).
- `go` is NOT installed in this environment; install it before building. Target hosts are RPM-based (CentOS/RHEL) per the shell scripts.
- Code is intentionally old-style (`ioutil`, old sarama API). Keep edits compatible; do not modernize tooling silently.

## Configuration gotchas

- All runtime config is hardcoded `YOUR_*` placeholder constants in each `*.go` file: Kafka cluster/topic/key, SASL user/password, submit API, crash paths, version. There are no config files; fill in real values before deploying.
- Push publishes with Kafka key `crash_log`; pull submits only messages with that exact key. Change both in sync.
- On first push run (no `/tmp/kernel_log_push.flag`) the crash-age window is widened to 2 years to backfill history, overriding `--duration`; the flag is then created.
- Push sleeps a random 0–600s by default (cron collision avoidance); `statistics.sh` passes `--sleep=1` to skip it.
- Crash logs larger than ~5MB are truncated to the last ~3MB to fit sarama's 5MB max message.
- `--crash_path` is parsed but ignored; the constant `CRASH_PATH` (`/var/crash`) is always used.
- `kernel_log_pull` offset handling is fragile: a missing `/tmp/kernel_log.offset` panics in `check(err)`, and when the file exists `getPreOffset` returns `OffsetNewest`, so the saved offset is never actually resumed.
- `kernel_log_push.sh` / `statistics.sh` also report RPM install status (`YOUR_KERNEL_MONITOR_RPM`, `YOUR_HOTFIX_RPM`, `YOUR_KPATCH_MODULES_RPM`) to `YOUR_API_HOST/panic/api/set_rpm_status/`.
- All endpoints are intranet-only; the `YOUR_*` placeholders must be replaced with real intranet addresses, which are unreachable from outside the corporate network (runtime verification here is impossible).

## Versioning

- `KERNEL_VERSION` in `kernel_log_push.go` ("1.2.4") is manually kept in sync with the changelog at the top of `README.md`. Bump both when changing push behavior.

## Git

- This directory has no standalone Git metadata (`.git` removed). The internal hosting location is redacted as `<internal-gitlab>/kernel/kernel_panic_monitor.git`.
- Do not push this directory (or any pre-redaction copy) to external git hosting; check for leftover real endpoints/credentials before committing.
