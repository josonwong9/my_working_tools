# AGENTS.md

Tooling for Linux kernel crash analysis: `sym_lookup.c` interactively maps a kernel text address (from panic/crash logs) to the nearest symbol in `/proc/kallsyms`; the shell scripts convert crash-log timestamps to wall-clock dates. No README, tests, or CI.

## Dev Mac vs. Linux target (read first)

Everything here is Linux-only, but this checkout lives on macOS:

- The tracked `sym_lookup` binary is a **Linux x86-64 ELF** (committed on purpose). Do **not** run `make` on macOS and commit the result — it silently overwrites the tracked ELF with a Mach-O build that is useless on target hosts.
- Running a local macOS build segfaults: `/proc/kallsyms` is missing, `load_kallsyms()` leaves `sym_cnt = 0`, `ksym_search()` returns `NULL`, and `main()` dereferences it.
- The scripts need GNU tools (`date -d`, `last reboot -F`, `/proc/uptime`); run them on the Linux target, not the Mac.

## Build / run (on a Linux host)

```sh
make          # gcc sym_lookup.c -o sym_lookup (plain gcc, no flags; warnings are expected)
make run      # ./sym_lookup, Ctrl+C to quit
make clean    # rm sym_lookup (no -f) — deletes the tracked ELF; restore with git checkout -- sym_lookup
```

- Run as root or with relaxed `kptr_restrict`, otherwise `/proc/kallsyms` shows zero addresses and `load_kallsyms()` skips them all.
- Input accepts hex with or without `0x`; byte-reversed little-endian strings from crash dumps are auto-detected (anything not starting with `ffffffff`) and swapped.

## Code notes before modifying `sym_lookup.c`

- `load_kallsyms()` has **no bounds check** against `MAX_SYMS` (300000) and stops at the first malformed line — a symbol-rich kernel can overflow `syms[]`.
- `ksym_search()` returns the symbol at or below the address, `syms[0]` (`_stext`) when out of range, and `NULL` when nothing was loaded; callers must check for `NULL`.
- `kallsyms_find()` is an unused streaming alternative to the cached table.

## Scripts

- `./timestamp_to_date.sh <seconds>` — seconds-since-boot (crash-log offset) to date, computed twice: via `last reboot -F` and via `/proc/uptime`.
- `./two_date_seconds.sh "<date1>" "<date2>"` — prints seconds between two date strings.
