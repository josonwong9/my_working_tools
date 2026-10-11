# cpu_load_generator

Socket-controlled CPU load generator: Python 2 + bash scripts that pin CPU utilization on remote Linux hosts using busy-loop processes plus `stress-ng`. Usage notes live in the Chinese `README.md`; no tests, CI, build system, or dependency manifest. Not its own git repo and untracked by the parent workspace repo (`?? cpu_load_generator/`) — no history, no remote.

Cannot run on this macOS checkout: no `python`/`python2` (only `python3`), no `stress-ng`, no `docker`. Verify edits by reading the code.

## Running

- Server (on the target host): `./cpu_load_generator_server.py`. Binds `socket.gethostname():31415`, so remote clients only reach it if that hostname resolves to a routable address. Port 31415 is hardcoded in all three Python files. SIGINT/SIGTERM kills all load.
- Interactive client: `./cpu_load_generator_client.py` — connects to the *local* hostname, so it only works on the server host itself. Uses `raw_input`: Python 2 only; don't port it to Python 3 blindly.
- Remote one-shot: `./oneshot.py <host> [port] util` — note the argument order (host, optional port, util). Fire-and-forget; prints nothing on success.
- `util` is absolute percent of the whole machine, 100 per core (250 on 4 CPUs ≈ 62.5%). `util <= 0` cleans all load; there is no decrement operation.

## Load model

- `util = whole*100 + part`: `whole` cores each get a `multiprocessing` `while True: pass` process; `part` runs one `stress-ng -l <part> -c 1` worker (server adds `-k -q`, `cpu.py` does not). Requires `stress-ng` on PATH.
- The server echoes the received integer back; invalid input returns an error string on the same connection instead of disconnecting.
- `cpu.py` is a standalone one-shot (`./cpu.py <util>`) with the same model. Its `10..nr_cpus*100` range check only prints a warning — it still starts the load regardless.

## Machine-specific wrappers (sanitized templates)

- Real container IDs/hostnames were deliberately replaced with `REPLACE_WITH_*` placeholders; the shell scripts abort with exit 1 while placeholders remain. Keep them as placeholders — never put real names/IDs back in.
- `sine_wave.sh` runs on the docker host: `docker inspect` resolves each `cid` entry to a hostname and quota (`quota/period`), then drives `oneshot.py` per container in an endless wave loop.
- `sine_wave.template.sh` is an unreferenced variant that lists container hostnames directly in two groups (`group_a`/`group_b`, group B gets double load); no `docker inspect`.
- `reset.sh [host]` sends 0 (parameterized; placeholder default aborts). `start.sh` backgrounds `sine_wave.sh`; its old hostname argument is gone.
- These still cannot run on macOS (no python2/stress-ng/docker).
