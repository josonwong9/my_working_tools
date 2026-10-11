# blkio — Linux block-IO test scripts

Bash-only repo for benchmarking and tracing block devices on Linux test hosts. No build system, tests, lint, or CI; single `master` branch on an internal GitLab remote. The scripts need Linux and root (`/sys`, `/proc`, debugfs) and cannot run from a macOS checkout.

## Destructive defaults (fio)
- `script/fio/conf.sh` defaults to writing **directly to the raw device `/dev/nvme1n1`** (`direct=1`, size `1G`). Running any wrapper without `export test_file_name=...` first will clobber that disk.
- In `conf.sh`, every `if [ -z $var ]` block assigns twice and the second assignment wins (size `1G`, target `/dev/nvme1n1`, `test_time=60`). The first line is dead code, not the default.

## fio tests (`script/fio/`)
- Workload wrappers (they `cd` into `script/fio` themselves): `./rr.sh [jobs] [iodepth]` (randread 4K), `./rw.sh` (randwrite 4K), `./sr.sh` (read 256K), `./sw.sh` (write 256K). They source `conf.sh`; export any `test_*` var to override (`test_time`, `test_nr_jobs`, ...).
- Output goes to `log/<name>_<jobs>_<depth>.log` using `--minimal --terse-version=3`; `run_fio` immediately parses it to `<log>.parse`. Re-parse a file or whole dir with `./parse_fio_terse.sh <path>`.
- The parser is index-based against terse v3, 1-based: 7/48 bw, 8/49 iops, 40/81 mean latency, 30/71 p99. Full field map: `script/fio/fio_terse.log`.
- `./test.sh <sched> <test> <threads> <depth>`, e.g. `./test.sh bfq rr 8 32`, runs the same workload concurrently in cgroups `test1`/`test2` to compare scheduling: weights 400/100 for `bfq`/`iocost`, high/low for `wrr`; `test` is one of `sr|sw|rr|rw`. It rewrites `/sys/block/<disk>/queue/scheduler`, assumes cgroup v2 (`enable_cgroup_v2` is hardcoded in `main`; the v1 path via `diable_cgroup_v2` is never called), and `wrr` additionally calls `nvme set-feature` (NVMe only).
- `tcpu.sh` and `tloop.sh` are machine-specific sweeps with hardcoded `/dev/nvme0n1` and `-cpus_allowed=0-39`, and long sleeps (120 s / 60 s) between runs.
- `smix.sh`/`rmix.sh` are older standalone variants that bypass `run_fio`; they must be run from `script/fio` (they source `conf.sh` and call `./log/pmix.sh` relatively). `log/p.sh` and `log/pmix.sh` parse plain (non-terse) fio output.
- Runs leave untracked `log/*.log*` files and `result_20*` (from `tcpu.sh`); don't commit them.

## blktrace helpers (`script/blktrace/`)
- These do not use the `blktrace` tool; they drive the kernel ftrace `blk` tracer via `/sys/kernel/debug/tracing` (root, debugfs).
- `record.sh` resets/sets events, then logs `iostat` + trace to `/tmp/20*.log` (needs sysstat). `t.sh` records interactively until Enter into `./log`; `all.sh` records `time_s` (10) seconds into `./full.log`.
- `t.sh` and `all.sh` hardcode the device filter `dev == 8388628`; update it for the target disk before recording.
- `set_trace.sh`/`reset_trace.sh` toggle `block_bio_queue`; `get_blkq_setting.sh` dumps `/sys/block/*/queue`; `overlap.sh <dev>` polls `/proc/diskstats`.

## Other
- `rwc` is a prebuilt static x86-64 Linux ELF (read/write/compare tool). `rwc.c` is not in the repo and no script calls it; don't rebuild it or try to run it on macOS.
- `.gitignore` is the Linux-kernel file: `.*` ignores every dotfile, plus `*.patch`, `*.bin`, `*.gz`, `patches*`, `series`. Check it before assuming a new file will be tracked.
- Don't add `set -e` to these scripts: parser pipelines and unquoted `[ -z $var ]` checks rely on non-fatal failures. Keep the existing one-workload-per-file + shared `conf.sh` pattern. Commit subjects are short and scoped (`fio: ...`, `blktrace: ...`).
