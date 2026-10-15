# AGENTS.md

Out-of-tree Linux kernel module that traces the runqueue latency of one PID. `trace_runqlat.c` is the entire module, `sort.sh` post-processes its output, and `README.md` (Chinese) is the user-facing doc.

## Build, load, verify

- Build needs matching kernel headers (`KERNELDIR=/lib/modules/$(uname -r)/build` in the `Makefile`): `make -j8`, then `make install` (insmod, needs root); `make remove` (rmmod), `make clean`.
- This checkout is on macOS, where `make` cannot succeed. Build/verify on a Linux host; there is no CI and no test suite, so verification means insmod plus manual exercises of `/proc/trace_runqlat/*`.
- Module params are load-time only (0444): defaults are 128 trace entries / 16 tasks per entry, max 256 / 32. `make install` runs plain `insmod`, so pass params manually: `insmod trace_runqlat.ko max_trace_entries=256 per_trace_tasks=32`.

## /proc/trace_runqlat interface (runtime behavior to know)

- `pid`: write a PID to trace it; write `-1` to stop. You cannot switch PIDs while one is active — such writes return `-EPERM`. Setting a PID resets all recorded data.
- `threshold`: ns, default 20 ms; only latencies at or above it are recorded.
- `runqlat`: recorded traces. README says tracing must be stopped before reading, but that check is `#if 0`-disabled in `runqlat_show()`. Writing `0` clears the buffers.
- `distribution`: read-only latency histogram (msecs).

## Editing quirks

- Kernel compatibility is deliberate: the code targets 3.10/4.18 (CentOS 7/8) through 5.6+. Keep the `LINUX_VERSION_CODE` guards and the `proc_ops` vs `file_operations` split in `DEFINE_PROC_ATTRIBUTE`.
- `struct my_cfs_rq` mirrors `struct cfs_rq` with hardcoded padding to read `nr_running`; the offsets were only verified on 3.10 and 4.18. Re-check the target kernel layout before touching it.
- Tracepoints (`sched_wakeup`, `sched_wakeup_new`, `sched_switch`, `sched_migrate_task`) are located via `for_each_kernel_tracepoint` because the `__tracepoint_*` symbols are not exported; there is a separate pre-4.10 `tracepoint_iter` path.
- `sort.sh` parses `runqlat` output by field names (`COMM:`, `PID:`, `RUNTIME(us):`, `CGROUP:`), e.g. `./sort.sh -f FILE -s cgroup|pid`. Changing `seq_printf` formats in `trace_runqlat.c` breaks it — update both.

## Review workflow

- Gerrit-style review: before use, replace the placeholder URL in `alias` with your own server. It installs the `git master` push alias (`refs/for/master`) and a commit-msg hook; commits need `Signed-off-by` and `Change-Id`. `elevate/cr.yml` holds placeholder reviewer/owner names to replace.
- `np.sh <n> <branch>` re-pushes the last n commits as separate review changes.
