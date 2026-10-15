# AGENTS.md

Out-of-tree Linux kernel module (single source file: `filecache.c`). Creates a
procfs entry `/proc/filecache` that lists page-cache pages per inode/superblock.
`README` documents the output format.

## Build & test (Linux only)

- This code only compiles and runs on Linux. The current checkout lives on
  macOS; do not try to build or "fix" the Makefile there.
- `make` builds `filecache.ko` against `/lib/modules/$(uname -r)/build`.
  Target another kernel with `make KERNELDIR=/path/to/kernel/build`
  (command-line override works despite the `:=` assignment).
- Kernel builds leave many intermediate files (`*.ko`, `*.o`, `.*.cmd`,
  `modules.order`, `.tmp_versions/`). They are untracked build junk; never
  commit them. `make clean` removes most but not all.
- Manual test cycle on a Linux host, as root: `insmod filecache.ko`,
  `cat /proc/filecache`, `rmmod filecache`. No test suite or CI exists.

## Constraints

- Version-gated for old vendor kernels: pre-4.0 uses `d_alias` and steals the
  global `inode_sb_list_lock` address via `kallsyms_lookup_name`; newer uses
  `d_u.d_alias` and `sb->s_inode_list_lock`. Keep both branches compiling —
  the legacy path is there for 3.10-based vendor kernels, not dead code.
- `iterate_supers` and `kallsyms_lookup_name` are resolved at module init via
  `kallsyms_lookup_name`. That symbol is no longer exported since Linux 5.7, so
  `insmod` fails there with an unknown-symbol error. Known limitation.
- `iterate_pagecache_sb()` runs while holding `sb->s_inode_list_lock` and
  `inode->i_lock`: no sleeping/blocking calls inside. The shared global
  `filename` buffer is only safe because of this locking — don't drop the
  locks around `dentry_path_raw`/`seq_printf`.
- Keep the README sample output anonymized (placeholder paths only). Never
  paste real production output — it would expose internal service, container,
  and host names.
