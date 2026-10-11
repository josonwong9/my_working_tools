# AGENTS.md

Tiny Linux dm-cache monitoring plugin: one Python 2 script plus a shell wrapper. No git repo, tests, CI, or dependency manifest — don't hunt for them.

## Layout

- `cache_info.py` — collects dm-cache stats by shelling out to `dmsetup` / `ls /dev`, reads `/sys/block/*/stat`, prints a JSON array of `{name, timestamp, value, tags:{host,dev}}` metric entries to stdout.
- `plugin.sh` — the usual entrypoint (`cd $(dirname $0) && python cache_info.py`); `cache_info.py` can also be run directly (`./cache_info.py`). Both require Python 2 on PATH.
- `README.md` is empty; this file is the documentation.

## Running

- Linux-only and Python 2-only (`long()`, `print` statement, integer `/`). Requires `dmsetup`, `/dev/mapper`, `/sys/block`, and at least one cache device in `writeback` mode, so it cannot run on this macOS machine — verify edits by reading the code.
- `python`/`python2` are not installed here (only `python3`); a Python 3 run fails with `SyntaxError` on `print`.
- Each run samples `dmsetup status` twice with `time.sleep(tick)` (`tick = 8`) in between; rates (`r_iops`, `w_iops`, `demote`, `promote`) are per `tick`.

## Non-obvious internals

- Flow: `get_record()` (two samples) → `make_request()` (fills `cache_request`) → `print_info()` (JSON out).
- `make_request()` skips a device for the current run on inconsistent samples (name change, counter rollback, short status/stat lines); records are only appended once all 11 fields are computed, and `print_info()` emits `len(cache_request)/11` devices, so output can have fewer entries than active cache devices.
- Per device, `cache_request` holds 11 fields but only 8 are emitted; `print_info` maps them with `format[i%8+3]` and `cache_request[i/8*11+3+i%8]`. Keep these offsets in sync when changing `format`.
- The first 3 entries of `format` (cache_name, dm_name, hdd_name) are labels, not metrics.
