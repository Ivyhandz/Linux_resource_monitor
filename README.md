# Linux System Resource & Process Monitor

A command-line tool that shows how much CPU, memory, swap and disk a Linux system is using, and which processes use the most. It reads its data directly from `/proc` and POSIX APIs. It never calls `top`, `ps`, `free`, `df`, `system()` or `popen()`.

It was built for the Scripting Languages course by three people, with one language per layer:

| Layer | Language | Role |
|---|---|---|
| Engine (`monitor`) | C++17 | Collects data, calculates percentages, shows it, logs it, sends signals |
| Control script (`scripts/monitor.sh`) | Bash | Starts, stops, snapshots, logs, reports and cleans |
| Report (`python/report.py`) | Python 3 | Reads the CSV logs and prints statistics |

## Quick start

```bash
make                    # build the monitor
./monitor --snapshot    # print one reading and exit
./monitor               # interactive view
```

Requirements: Linux, `g++` with C++17, GNU `make`, Bash 4 or newer, Python 3 (standard library only). It was developed and tested on WSL2 Ubuntu (see [Known issues](#known-issues)).

## Using the monitor

### Interactive mode: `./monitor`

Needs a real terminal. The screen refreshes every 2 seconds.

| Key | Action |
|---|---|
| `r` | Refresh now and clear any search |
| `s` | Search. Digits only means an exact PID, anything else is a case-insensitive part of the process name. A blank answer clears the search |
| `k` | Kill. Asks for a PID, then a signal (`t` = TERM, the default, or `k` = KILL), then a `y/N` confirmation |
| `q` | Quit. The terminal is restored on every way of leaving, including Ctrl+C |

Arrow keys and touchpad scrolling are ignored on purpose. The interface has no scrolling or selection. The process list shrinks to fit the terminal height, and long command lines are cut to the terminal width.

### Snapshot: `./monitor --snapshot`

Takes two readings 0.5 seconds apart (CPU needs a difference between two readings), prints the system header and the 10 busiest processes as plain text with no escape codes, and exits with `0`.

### Logging: `./monitor --log <dir> [--interval <sec>]`

Appends to `<dir>/system.csv` and `<dir>/processes.csv` until stopped.

- The default interval is 2 seconds. It must be a whole number from 1 to 3600.
- `<dir>` must already exist. `monitor.sh` creates `logs/` for you.
- On `SIGTERM` or `SIGINT` the program finishes the current sample, flushes its files and exits cleanly.

### Kill: `./monitor --kill <pid> <TERM|KILL>`

Sends the signal and prints the result to stderr.

### Help: `./monitor --help`

Prints the usage text.

### Exit codes

| Command | Code | Meaning |
|---|---|---|
| any mode | `0` | Success, or a clean stop |
| `--kill` | `1` | The signal failed or was refused |
| `--log` | `1` | The directory is missing or not writable |
| `--kill`, `--log`, unknown option | `2` | Bad usage (for example a PID that is not a number, an interval outside 1 to 3600, or `--bogus`) |

## Bash control script

```bash
./scripts/monitor.sh start       # run the logger in the background (PID saved in logs/monitor.pid)
./scripts/monitor.sh stop        # send SIGTERM to that PID
./scripts/monitor.sh snapshot    # same as ./monitor --snapshot
./scripts/monitor.sh log [sec]   # log in the foreground, default 2 s
./scripts/monitor.sh report      # run the Python report on logs/system.csv
./scripts/monitor.sh clean       # delete the CSVs and the PID file
./scripts/monitor.sh help
```

## Python report

```bash
python3 python/report.py logs/system.csv
python3 python/report.py logs/system.csv --procs logs/processes.csv --json report.json --csv report.csv
```

It prints the number of samples, the time span, the average and peak for CPU, memory, swap and load, the last disk reading, the processes with the highest CPU and memory, and the top 5 processes by average CPU. Malformed rows and a half-written last line are skipped with a warning.

## CSV log format

`system.csv` (one row per sample):

```text
timestamp,cpu_pct,mem_pct,swap_pct,disk_pct,load1
2026-10-05T10:00:01,42.7,54.1,4.0,72.0,1.3
```

`processes.csv` (the top 5 processes by CPU in each sample, all with the same timestamp):

```text
timestamp,pid,name,cpu_pct,mem_pct
2026-10-05T10:00:01,4211,firefox,32.4,8.2
```

Timestamps are local time. Numbers have one decimal place. A comma inside a process name is replaced with `_`. Process `cpu_pct` can exceed 100 (see below).

## How it works

### CPU (`/proc/stat`)

The file holds counters in jiffies since boot, so one reading says little. The monitor takes two readings and works with the difference:

```text
idle_all    = idle + iowait
delta_total = total(cur) - total(prev)
total %     = (delta_total - delta_idle_all) / delta_total * 100
user %      = (delta_user + delta_nice) / delta_total * 100
system %    = (delta_system + delta_irq + delta_softirq) / delta_total * 100
```

If a counter goes backwards, or `delta_total` is zero, every percentage is `0`.

### Memory and swap (`/proc/meminfo`)

`used = MemTotal - MemAvailable` and `swap used = SwapTotal - SwapFree`. If `MemAvailable` is missing, it falls back to `MemFree + Buffers + Cached`.

### Disk (`statvfs`)

`used = (f_blocks - f_bfree) * f_frsize`, and `used % = used / (used + available) * 100`, where available comes from `f_bavail`. This is how `df` computes its percentage. Counting `total - available` as used would also count the blocks ext4 reserves for root, which gave about 5.5% instead of about 0.4% on the test machine.

### Processes (`/proc/<pid>/`)

- The name comes from `stat`, between the first `(` and the last `)`, because names can contain brackets. The state and CPU jiffies (`utime + stime`) come from the same line.
- Resident memory (`VmRSS`) comes from `status`. Kernel threads have none, so theirs is `0`.
- The command line comes from `cmdline` with NUL bytes turned into spaces, or `[name]` if it is empty.
- Process CPU% follows the convention of `top`: `process_delta / delta_total * 100 * ncores`. The total adds up all cores, so multiplying by the core count makes 100% mean one fully used core. A multithreaded process can exceed 100%.
- The first reading has nothing to compare against, so every CPU% is `0`. A process seen for the first time also starts at `0`.
- Process MEM% is `rss_kb * 100 / MemTotal`.
- The list is sorted by CPU% (highest first), then by MEM%.

### Signals

Only `SIGTERM` and `SIGKILL` are accepted. PID 0, PID 1, negative PIDs and the monitor's own PID are always refused, because `kill(0, ...)` and `kill(-1, ...)` would signal whole groups of processes. The checks live inside `send_signal`, so every caller is covered.

### Clean shutdown

In logging mode, the signal handler only sets a flag. The main loop checks it every 100 ms, finishes the current sample, closes the files and exits with `0`.

## Project layout

```text
include/        public headers (the frozen interfaces)
src/            C++ sources: main, cpu, memory, disk, system, process, ui
tests/          C++ tests for the collectors and the process engine
scripts/        monitor.sh
python/         report.py and sample CSVs
logs/           CSV output (ignored by Git)
docs/           CONTRACTS.md, the agreed interfaces and rules
Makefile
```

## Testing

### Automated

```bash
make check
```

builds `monitor` and runs the CPU, memory, disk and system test programs. The process tests are not part of `make check` yet. Build and run them with:

```bash
g++ -std=c++17 -Wall -Wextra -O2 -Iinclude tests/test_process.cpp src/process.cpp -o tests/test_process && ./tests/test_process
```

They contain 22 checks: empty list, a real `/proc` scan, sorting, search (including that `100` does not match PID `1000`), refused and failing signals, and a real `SIGTERM` sent to a forked child. `ProcessSampler::sample()` reads the real `/proc`, so sorting is checked as a property of a real scan and exact CPU% values are not asserted.

### Comparison with system tools (WSL2 Ubuntu)

A `yes > /dev/null` process keeps one core busy. The monitor and `top` looked at the same PID:

| | Monitor | `top` |
|---|---|---|
| CPU% | 97.9 | 100.0 |
| RSS (KB) | 8644 | 8644 |
| MEM% | 0.1 | 0.1 |

Both show about one fully used core. The 2.1 point gap is probably sampling granularity: the snapshot measures over 0.5 s and `top` over 1 s. We did not test this explanation.

Taken at the same moment, memory used and total, swap total, uptime and the load averages matched `free -k`, `/proc/uptime` and `/proc/loadavg`. Disk used (3.7 GiB on `/`) matched `df`.

The machine has 18 cores (`nproc`), so one fully busy core is 100 / 18 = 5.6% of the whole machine. The Python report showed exactly that: a system CPU average of 5.6% while `yes` was at about 100%.

### Command-line behaviour checked by hand

`--help` exits `0`. An unknown option exits `2`. `--kill 1 TERM` is refused and exits `1`. `--kill abc TERM` exits `2`. A successful `--kill` of a `sleep` process exits `0`. `--log /no/such/dir` exits `1`. `--interval 0` exits `2`. In interactive mode, `s`, `k`, `r` and `q` were tried, including killing a `sleep` process by PID.

### Not tested

`monitor.sh snapshot`, `monitor.sh log` and `monitor.sh clean` used on their own in a longer run, `report.py --json` and `--csv`, `--interval 3601`, and any system other than WSL2 Ubuntu.

## Known issues

1. **Starting the logger twice.** A second `./scripts/monitor.sh start` overwrites `logs/monitor.pid`, so `stop` only reaches the second logger. The first keeps running, and both write to the same CSV files. (Confirmed by running it. A fix is to check the existing PID file with `kill -0` before starting.)
2. **Averages in the report.** `processes.csv` only keeps the top 5 processes of each sample, so the "Top 5 by Average CPU" averages a process over the samples where it appeared. The number of samples is printed next to each name.
3. **`make check` skips the process tests**, and `.gitignore` line 10 (`tests/test_*`) also matches test source files, so new test sources have to be added with `git add -f`.
4. **Platforms.** The contract targets WSL2 Ubuntu, VirtualBox Debian/Ubuntu and Arch Linux. Only WSL2 Ubuntu was tested.

## Team and contract

| Part | Owner (GitHub) |
|---|---|
| Process engine (`process.*`), `main.cpp`, `docs/CONTRACTS.md` | Ivyhandz |
| `scripts/monitor.sh`, `python/` | iamaarav10 |
| CPU, memory, disk and system collectors, `ui.cpp`, tests for those, Makefile | NaQeEb313 |

The three parts were built against `docs/CONTRACTS.md` (version 2), which freezes the function names, the command line, the CSV format and the formulas. Version 2 corrected one error in the first draft: the process CPU% formula was missing `* ncores`, which would have shown a fully busy core as 25% on a 4-core machine.
