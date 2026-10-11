# AGENTS.md

eBPF sys%-time profiler for Linux x86-64 hosts (internal k8s nodes, CentOS/RHEL 8.x, kernel 4.18). Go loader (`main.go`) + BPF C program (`bpf/cpu_sys.c`), built with cilium/ebpf v0.6.2 `bpf2go`.

## Environment: cannot build in this workspace

- All code is Linux-only: `main.go` has `//go:build linux`, and the generated bpf2go files embed a linux-only build tag. The generated `cpusys_bpf*.go` / `*.o` are gitignored and not present in the repo, so nothing compiles until `./generate.sh` runs.
- This checkout commonly lives on macOS (no Go toolchain here). Build, generate, and run only on the Linux target host.

## Commands (run on the Linux host)

- `./generate.sh` (= `go generate -x`) — required before any build and after editing `bpf/cpu_sys.c`. It first regenerates `headers/vmlinux.h` from `/sys/kernel/btf/vmlinux` via `bpftool`, then runs bpf2go (clang) to emit `cpusys_bpfel.{go,o}`. Needs bpftool, clang, and kernel BTF. It overwrites the committed 2 MB `headers/vmlinux.h` with the running kernel's BTF — expect a huge diff and only regenerate against the target kernel.
- `./run.sh` (= `go run -exec sudo .`) — root is required to load BPF; the `-exec sudo` wrapper is intentional. `main.go` imports `"C"` (vestigial), so cgo/gcc must be enabled.
- `./unmount_bpffs.sh` — umounts `/sys/fs/bpf`. Use it to reset state when a run fails to load after map layout/size changes, because `stackmap` and `taskinfo` are pinned by name under `/sys/fs/bpf/debug` (`LIBBPF_PIN_BY_NAME` + PinPath from `mount_bpffs()`).

## How it works

- `main.go` mounts bpffs, loads the `CpuSys` objects, and attaches a single kprobe to the kernel-internal symbol `account_system_index_time`; alternate probes (`generic_exec_single`, tracepoints) are commented out. It then idles until SIGINT/SIGTERM.
- `bpf/cpu_sys.c` accumulates per-CPU cputime and, once per >1 s interval, computes sys% = `(curr_sys - last_sys) * 100 / interval`. When rate > `SYS_THRESHOLD` (50) and CPU != 0 (htop on CPU0 is deliberately filtered), it records a kernel stack id (`stackmap`) and `{comm, cpu, ts, sys, stackid}` keyed by pid (`taskinfo`).
- The loader prints nothing useful; results live in the pinned BPF maps, e.g. `bpftool map dump name stackmap` / `bpftool map dump name taskinfo`.
- Compilation hardcodes `-D__TARGET_ARCH_x86`: x86-64 targets only.

## Analyzing collected data

- Committed sample data: `mon_data/sample-node-01/` — `stack.log` (raw `bpftool map dump name stackmap` text), `taskinfo.log`, plus `kallsyms`, `uname.info`, `uptime.info`.
- Decode raw stack addresses to symbols with the standalone script. It reads hardcoded `./kallsyms` and `./stack.log` relative to the cwd, so run it from inside the data directory:
  `cd mon_data/<host-dir> && go run ../decode_stack.go`

## Conventions

- A rebuilt x86-64 Linux binary `cpu_sys_mon` is tracked at the repo root and updated in the same commit as source changes (it cannot be rebuilt on macOS).
- Dependencies are pinned (`go 1.15`, cilium/ebpf v0.6.2) and the generated API matches that version; do not bump casually.
