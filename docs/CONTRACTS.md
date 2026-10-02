# PART 2: FROZEN CONTRACTS (version 2)

These are the agreed interfaces and required behaviours. Build to them exactly.

Version 2 is Teammate 3's revision, reviewed and corrected by Teammate 1. The changes are listed in section 2.13.

---

## 2.1 C++ headers

General rules:

- Memory values are in **kB**.
- Disk values are in **bytes**.
- Percentages are `double` values in the range **0 to 100**, unless explicitly stated otherwise (process CPU% can exceed 100, see the process rules).
- Functions that read system information return `bool` and return `false` on failure.
- Collector functions never print and never throw.
- Only `main.cpp` handles user-facing printing.
- C++ collectors read Linux system information directly from `/proc` or POSIX/Linux APIs.
- The C++ implementation must not call `top`, `ps`, `free`, `df`, `htop`, `system()`, or `popen()`.

### `include/cpu.hpp` (owner: Teammate 3)

```cpp
#pragma once

struct CpuTimes {
    // Cumulative CPU jiffies since boot, from the first line of /proc/stat.
    unsigned long long user = 0, nice = 0, system = 0, idle = 0,
                       iowait = 0, irq = 0, softirq = 0, steal = 0;
};

struct CpuUsage {
    // Percentages over a sampling interval.
    double total = 0, user = 0, system = 0, idle = 0;
};

bool read_cpu_times(CpuTimes& out);
// Reads one CPU snapshot from /proc/stat.
// Never sleeps.
// Returns false if /proc/stat cannot be opened or the first token is not "cpu".
// user, nice, system and idle are required.
// Older kernels may provide fewer trailing fields; missing trailing fields remain 0.
// guest and guest_nice fields are ignored.

unsigned long long total_jiffies(const CpuTimes& t);
// Returns the sum of all eight CpuTimes fields.

CpuUsage compute_usage(const CpuTimes& prev, const CpuTimes& cur);
// Calculates CPU percentages from the difference between two cumulative snapshots.
// If any counter goes backwards, or delta_total is zero, returns all-zero CpuUsage.

bool read_loadavg(double& l1, double& l5, double& l15);
// Reads the first three values from /proc/loadavg.
// l1 = 1-minute load average.
// l5 = 5-minute load average.
// l15 = 15-minute load average.
```

#### CPU percentage rules

Let:

```text
idle_all    = idle + iowait
delta_total = total_jiffies(cur) - total_jiffies(prev)
delta_idle  = idle_all(cur) - idle_all(prev)
```

Then:

```text
total %  = (delta_total - delta_idle) / delta_total * 100
user %   = (delta_user + delta_nice) / delta_total * 100
system % = (delta_system + delta_irq + delta_softirq) / delta_total * 100
idle %   = delta_idle / delta_total * 100
```

- If `delta_total == 0`, all returned percentages are `0`.
- If any individual CPU counter in `cur` is less than the corresponding counter in `prev`, all returned percentages are `0`.
- `user + nice` are grouped as user CPU time.
- `system + irq + softirq` are grouped as system CPU time.
- `idle + iowait` are grouped as idle CPU time.

### `include/memory.hpp` (owner: Teammate 3)

```cpp
#pragma once

struct MemInfo {
    unsigned long long total_kb = 0, free_kb = 0, available_kb = 0,
                       used_kb = 0,
                       swap_total_kb = 0, swap_used_kb = 0;
};

bool read_meminfo(MemInfo& out);
// Reads /proc/meminfo.
// Reads MemTotal, MemFree, MemAvailable, SwapTotal and SwapFree.
// If MemAvailable is unavailable, uses MemFree + Buffers + Cached as fallback.
// Returns false if /proc/meminfo cannot be opened.

double mem_used_pct(const MemInfo& m);
// used_kb / total_kb * 100.
// Returns 0 if total_kb is zero.

double swap_used_pct(const MemInfo& m);
// swap_used_kb / swap_total_kb * 100.
// Returns 0 if swap_total_kb is zero.
```

#### Memory calculation rules

```text
used_kb      = total_kb - available_kb
swap_used_kb = swap_total_kb - SwapFree
```

- If `MemAvailable` exists, use its value directly.
- If `MemAvailable` does not exist: `available_kb = MemFree + Buffers + Cached`.
- Only `MemAvailable` requires an availability/fallback check. Other parsed fields stay at zero if their line is missing.

### `include/disk.hpp` (owner: Teammate 3)

```cpp
#pragma once

struct DiskInfo {
    unsigned long long total_bytes = 0,
                       available_bytes = 0,
                       used_bytes = 0;
};

bool read_disk(const char* path, DiskInfo& out);
// Uses statvfs().
// Values are reported in bytes.

double disk_used_pct(const DiskInfo& d);
// used / (used + available) * 100.
// Returns 0 if used + available is zero.
```

### `include/system.hpp` (owner: Teammate 3)

```cpp
#pragma once
#include <string>

bool read_uptime(double& seconds);
// Reads the first number from /proc/uptime.

std::string format_uptime(double seconds);
// Formats uptime as HH:MM:SS.
// Hours may exceed 24.
```

### `include/process.hpp` (owner: Teammate 1)

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct ProcInfo {
    int pid = 0;
    std::string name;
    std::string cmdline;
    char state = '?';
    double cpu_pct = 0.0;
    double mem_pct = 0.0;
    unsigned long long rss_kb = 0;
    unsigned long long cpu_jiffies = 0;
};

class ProcessSampler {
public:
    std::vector<ProcInfo> sample(
        unsigned long long delta_total_jiffies,
        unsigned long long mem_total_kb
    );

private:
    std::unordered_map<int, unsigned long long> prev_jiffies_;
};

struct SignalResult {
    bool ok;
    std::string message;
};

SignalResult send_signal(int pid, int sig);

std::vector<ProcInfo> find_processes(
    const std::vector<ProcInfo>& all,
    const std::string& query
);
```

#### Process rules

`ProcessSampler` scans `/proc`.

`delta_total_jiffies` is:

```text
total_jiffies(cur) - total_jiffies(prev)
```

calculated by the CPU module (the sum over **all** CPU cores).

**Process CPU percentage** uses the **top-style convention**:

```text
ncores                = sysconf(_SC_NPROCESSORS_ONLN)     (read once)
process_delta_jiffies = cpu_jiffies(now) - prev_jiffies_[pid]
cpu_pct               = process_delta_jiffies / delta_total_jiffies * 100 * ncores
```

Because `delta_total_jiffies` adds up all cores, multiplying by `ncores` makes **100% mean one fully used core**. A multithreaded process can exceed 100%. This matches `top`, so the values can be verified against it.

`cpu_pct` is `0` when any of these is true:

- This is the first call (there is no previous data).
- The PID has no entry in `prev_jiffies_` (a new process).
- `delta_total_jiffies` is `0`.
- The process's jiffies went backwards.

After every scan, `prev_jiffies_` is rebuilt with only the PIDs seen in that scan.

**Process memory percentage:**

```text
mem_pct = rss_kb * 100 / mem_total_kb        (0 if mem_total_kb is 0)
```

`rss_kb` comes from `VmRSS` in `/proc/<pid>/status` (kB). If the line is missing (kernel threads), `rss_kb` is `0`.

**Sorting:**

1. `cpu_pct` descending
2. `mem_pct` descending for ties

**Fields:**

- `name` comes from the `comm` field in `/proc/<pid>/stat`, found between the first `(` and the **last** `)`.
- `cmdline` comes from `/proc/<pid>/cmdline`, with NUL characters replaced by spaces. If it is empty, use `[name]`.
- `state` comes from `/proc/<pid>/stat` (`R S D Z T ...`).
- A process that disappears while it is being scanned is skipped.

**`find_processes(all, query)`:**

- If `query` consists only of digits, return the process whose PID equals it.
- Otherwise, return the processes whose `name` contains `query`, ignoring upper and lower case.
- An empty query returns an empty vector.

### `include/ui.hpp` (owner: Teammate 3)

```cpp
#pragma once

#include <string>
#include <vector>

#include "cpu.hpp"
#include "memory.hpp"
#include "disk.hpp"
#include "process.hpp"

struct SystemView {
    CpuUsage cpu;
    MemInfo mem;
    DiskInfo disk;

    double load1 = 0;
    double load5 = 0;
    double load15 = 0;

    double uptime_seconds = 0;
};

void draw_screen(
    const SystemView& v,
    const std::vector<ProcInfo>& procs,
    const std::string& status_line
);
// Redraws the whole terminal screen.
// Clears the screen first.
// Shows as many processes as fit the terminal height.
// Builds the complete frame before writing it.
// UI does not own input or signal handling.

void print_snapshot(
    const SystemView& v,
    const std::vector<ProcInfo>& procs
);
// Plain text only.
// No ANSI escape sequences.
// No screen clearing.
// Shows the first 10 processes.
```

The UI must not directly handle keyboard input or signals.

`main.cpp` owns input, refresh timing, search handling, kill handling, signal handling, and command-line processing.

---

## 2.2 Signals

`send_signal(pid, sig)` is declared in `process.hpp` and implemented in `process.cpp`.

- It accepts only `SIGTERM` and `SIGKILL`. Any other signal returns `ok = false` with the message `unsupported signal`.
- It refuses to signal PID `0`, PID `1`, any negative PID, or the monitor's own PID (`getpid()`). It returns `ok = false` with a message such as `refusing to signal PID <= 1` or `refusing to signal own process`.

  This protection lives inside `send_signal`, so every caller is covered. `kill(0, sig)` signals the whole process group and `kill(-1, sig)` signals every process the user may signal.
- Otherwise it calls `kill()` and returns:

  | Result | `ok` | `message` |
  |---|---|---|
  | Success | `true` | `signal sent` |
  | `ESRCH` | `false` | `no such process` |
  | `EPERM` | `false` | `permission denied` |
  | Any other error | `false` | `kill failed: <strerror text>` |

- It does not print. `main.cpp` is responsible for displaying the result.

---

## 2.3 Command line of the C++ binary

Binary:

```text
./monitor
```

### Interactive mode

```text
./monitor
```

Keys:

```text
r = refresh
s = search
k = kill
q = quit
```

Exit code: `0`.

### Snapshot

```text
./monitor --snapshot
```

Takes one sample, prints plain text to stdout, and exits with `0`.

### Headless logging

```text
./monitor --log <dir> [--interval <sec>]
```

Behaviour:

- Default interval: `2` seconds.
- Interval must be an integer from `1` to `3600`.
- Appends to `<dir>/system.csv` and `<dir>/processes.csv`.
- `<dir>` must already exist.
- The C++ engine does not create the directory; `monitor.sh` creates required directories.
- On `SIGTERM` or `SIGINT`, the program finishes the current sample, flushes output, and exits cleanly.

Exit codes:

```text
0 = clean stop
1 = directory missing or not writable
```

### Kill

```text
./monitor --kill <pid> <TERM|KILL>
```

Exit codes:

```text
0 = signal sent
1 = signal failed
2 = bad usage
```

Messages go to stderr.

### Help

```text
./monitor --help
```

Prints usage information and exits `0`.

Unknown options print usage text to stderr and exit with `2`.

---

## 2.4 CSV log format

CSV files are plain text.

Rules:

- One header row.
- Comma separated.
- No quotes.
- Timestamps use local time.
- Timestamp format: `YYYY-MM-DDTHH:MM:SS`.
- Numeric values use one decimal place.
- Header is written only when the file is new or empty.
- Existing files are appended to.
- Every row is flushed immediately.
- Python readers must tolerate blank lines and a half-written final line by skipping it with a warning.

### `system.csv`

```text
timestamp,cpu_pct,mem_pct,swap_pct,disk_pct,load1
2026-10-05T10:00:01,42.7,54.1,4.0,72.0,1.3
2026-10-05T10:00:03,38.2,54.3,4.0,72.0,1.3
```

`cpu_pct` is whole-machine CPU usage and is `0` to `100`.

### `processes.csv`

Top 5 processes by CPU at each sample:

```text
timestamp,pid,name,cpu_pct,mem_pct
2026-10-05T10:00:01,4211,firefox,32.4,8.2
2026-10-05T10:00:01,3821,code,18.7,4.1
```

- All process rows belonging to one sample use the same timestamp.
- Process `cpu_pct` uses the top-style convention and can exceed `100`.
- Before writing, any comma in `name` is replaced with `_`.

---

## 2.5 Portability rules

The project targets:

1. WSL2 Ubuntu
2. VirtualBox Debian/Ubuntu
3. Arch Linux

### C++

- C++17.
- Standard C++17 library.
- POSIX/Linux headers only.
- No hard-coded user paths.
- Must build cleanly with `-Wall -Wextra`.

### Bash

Every project Bash script must begin with:

```bash
#!/usr/bin/env bash
```

Requirements:

- Bash 4 or newer.
- Quote every variable expansion.
- Project scripts must run under Bash even if the developer's interactive shell is Fish or Zsh.

### Python

Target Python 3.8. Standard library only.

Do not use:

```text
match
X | Y type unions
list[str]
str.removeprefix
```

### Line endings

The repository forces LF line endings through `.gitattributes`.

If a script reports:

```text
bad interpreter: /bin/bash^M
```

convert it with:

```bash
dos2unix <file>
```

### Paths

Paths must be relative to the project root or supplied as command-line arguments. Never hard-code a developer-specific path.

---

## 2.6 Ownership

### Teammate 1

```text
include/process.hpp
src/process.cpp
src/main.cpp
docs/CONTRACTS.md      (maintainer; changes need team agreement, see 2.12)
```

### Teammate 2

```text
python/report.py
python/sample/
scripts/monitor.sh
```

### Teammate 3

```text
include/cpu.hpp        src/cpu.cpp
include/memory.hpp     src/memory.cpp
include/disk.hpp       src/disk.cpp
include/system.hpp     src/system.cpp
include/ui.hpp         src/ui.cpp
Makefile
.gitattributes
tests/
```

Only the owner edits a file. If you need a change in someone else's file, ask the owner.

---

## 2.7 Module boundaries

Collectors are responsible only for collecting and calculating their own data.

They must not:

- print output
- handle keyboard input
- handle signals
- invoke external monitoring programs
- depend on another teammate's implementation unnecessarily

`main.cpp` coordinates the modules.

The UI receives already-collected data through `SystemView` and process vectors.

The Python layer reads generated CSV data and performs reporting and analysis.

The Bash layer handles project automation such as starting, stopping, snapshotting, logging, reporting, and cleaning.

---

## 2.8 Required C++ restrictions

The C++ monitor must collect information directly from Linux interfaces.

Do not implement collectors by invoking:

```text
top
ps
free
df
htop
system()
popen()
```

Examples:

```text
CPU       -> /proc/stat
Load      -> /proc/loadavg
Memory    -> /proc/meminfo
Uptime    -> /proc/uptime
Processes -> /proc/<pid>/*
Disk      -> statvfs()
Signals   -> POSIX kill()
```

---

## 2.9 Error-handling rules

- System-reading functions return `false` on failure.
- They do not throw exceptions, print errors, or terminate the program.
- The caller decides how to handle failures.
- Output and user-facing error messages belong to `main.cpp`.
- Numeric percentage functions return `0` when their denominator is zero.

---

## 2.10 Testing requirements

Each owner tests their own module. At minimum, the tests should cover:

### CPU

- Valid `/proc/stat` style input.
- Incorrect first token.
- Missing optional trailing fields.
- Counter rollback.
- Zero delta.
- Load-average parsing.

### Memory

- Normal `/proc/meminfo` input.
- `MemAvailable` present.
- `MemAvailable` missing.
- Swap present.
- No swap.
- Zero totals.

### Disk

- Valid filesystem path.
- Invalid path.
- Zero denominator handling.

### System

- Valid uptime.
- Invalid input.
- Uptime formatting.
- Hours greater than 24.

### Process

- Empty process list.
- CPU sorting.
- Memory sorting tie-break.
- Search.
- Signal failure handling (including the refused PIDs in section 2.2).

Parser and helper functions may offer an `std::istream&` variant as an **additive** testing API, provided the frozen public interfaces above remain unchanged.

Process tests may also be done by comparing the results with `top` and by running `sleep` test processes, and the results written down in the README.

---

## 2.11 Build requirements

The project uses:

```text
C++17
-Wall
-Wextra
-O2
-Iinclude
-MMD
-MP
```

The final Makefile must:

- build C++ object files
- generate dependency files
- link `monitor`
- provide a `check` target that builds and runs the collector tests
- provide a `clean` target

No external libraries are required. No `ncurses` dependency is required.

---

## 2.12 Frozen-interface rule

The following are considered frozen contracts:

- function names
- function parameters
- return types
- public structures
- required fields
- ownership boundaries
- command-line interface
- CSV format
- the behaviour rules written in this document (percentage formulas, signal rules, exit codes)

Implementations may use private and local variables, helper functions, parsing logic, flags, and internal calculations as needed.

Do not change the public interfaces merely to simplify an implementation.

Any change to a frozen interface must be agreed upon by all three teammates **before** implementation, and `docs/CONTRACTS.md` must be updated in the same commit.

Additive changes (a new function in your own header) are allowed. Announce them in the group chat.

---

## 2.13 Change log

**Version 2 (reviewed by Teammate 1)**

Changes made to Teammate 3's revision:

1. **Process CPU% formula corrected.** The text said `process_delta_jiffies / delta_total_jiffies * 100` and also "one fully used core is approximately 100%". Both cannot be true, because `delta_total_jiffies` covers all cores (on 4 cores a fully used core would show 25%). The formula now includes `* ncores`, which gives the top-style convention and matches `top`.
2. **Process rules completed:** edge cases that give `cpu_pct = 0`, rebuilding `prev_jiffies_` after each scan, `VmRSS` missing gives `0`, skipping vanished processes, and the matching rule for `find_processes` (which was missing).
3. **Signals (2.2):** only `SIGTERM` and `SIGKILL` are supported, other signals are refused; PIDs `<= 1` and the monitor's own PID are refused inside `send_signal`; error messages for each case.
4. **Structure:** every header is now listed under 2.1 with the same heading level (`ui.hpp` previously appeared after the signals section), and the signals section is 2.2.
5. **Ownership (2.6):** added `src/main.cpp` and `docs/CONTRACTS.md` (Teammate 1), `python/sample/` (Teammate 2), and `.gitattributes` (Teammate 3).
6. **Testing (2.10):** each owner tests their own module, and process tests may be done against `top` and `sleep` processes.
7. **Build (2.11):** the test target is named `check`.

Unchanged from Teammate 3's revision: all header declarations, the CPU, memory, disk and system rules, the command line, the CSV format, portability, error handling, and the C++ restrictions.

**Version 1:** the original contract committed on Day 1 (`92f4d0c`).
