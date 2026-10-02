# PART 2: FROZEN CONTRACTS

These are the agreed interfaces. Build to them exactly.

## 2.1 C++ headers

Units: memory in **kB**, disk in **bytes**, percentages are `double` in **0 to 100**. Functions that read the system return `bool` (`false` on failure). They never throw and never print. Only `main.cpp` prints.

**`include/cpu.hpp`** (owner: Teammate 3)

```cpp
#pragma once

struct CpuTimes {                       // cumulative jiffies since boot, from the first line of /proc/stat
    unsigned long long user = 0, nice = 0, system = 0, idle = 0,
                       iowait = 0, irq = 0, softirq = 0, steal = 0;
};

struct CpuUsage { double total = 0, user = 0, system = 0, idle = 0; };   // percentages over an interval

bool read_cpu_times(CpuTimes& out);                                // one snapshot; never sleeps
unsigned long long total_jiffies(const CpuTimes& t);               // sum of all eight fields
CpuUsage compute_usage(const CpuTimes& prev, const CpuTimes& cur); // delta-based percentages
bool read_loadavg(double& l1, double& l5, double& l15);
```

**`include/memory.hpp`** (owner: Teammate 3)

```cpp
#pragma once

struct MemInfo {
    unsigned long long total_kb = 0, free_kb = 0, available_kb = 0,
                       used_kb = 0,                      // total - available
                       swap_total_kb = 0, swap_used_kb = 0;   // swap_used = SwapTotal - SwapFree
};

bool read_meminfo(MemInfo& out);
double mem_used_pct(const MemInfo& m);     // used_kb / total_kb * 100  (0 if total is 0)
double swap_used_pct(const MemInfo& m);    // 0 if there is no swap
```

**`include/disk.hpp`** (owner: Teammate 3)

```cpp
#pragma once

struct DiskInfo { unsigned long long total_bytes = 0, available_bytes = 0, used_bytes = 0; };

bool read_disk(const char* path, DiskInfo& out);   // uses statvfs()
double disk_used_pct(const DiskInfo& d);           // used / (used + available) * 100, same as df's Use%
```

**`include/system.hpp`** (owner: Teammate 3)

```cpp
#pragma once
#include <string>

bool read_uptime(double& seconds);                  // first number of /proc/uptime
std::string format_uptime(double seconds);          // "HH:MM:SS" (hours may exceed 24)
```

**`include/process.hpp`** (owner: Teammate 1)

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct ProcInfo {
    int pid = 0;
    std::string name;               // "comm" from /proc/<pid>/stat
    std::string cmdline;            // /proc/<pid>/cmdline with NULs replaced by spaces; "[name]" if empty
    char state = '?';               // R S D Z T ...
    double cpu_pct = 0.0;           // top-style: 100.0 = one full core (can exceed 100 for multithreaded)
    double mem_pct = 0.0;           // VmRSS / MemTotal * 100
    unsigned long long rss_kb = 0;
    unsigned long long cpu_jiffies = 0;   // utime + stime, cumulative
};

class ProcessSampler {
public:
    // Scans /proc. delta_total_jiffies = total_jiffies(cur) - total_jiffies(prev) from the CPU module.
    // The first call has no previous data, so every cpu_pct is 0.
    // Result is sorted by cpu_pct descending (ties: mem_pct descending).
    std::vector<ProcInfo> sample(unsigned long long delta_total_jiffies,
                                 unsigned long long mem_total_kb);
private:
    std::unordered_map<int, unsigned long long> prev_jiffies_;
};

struct SignalResult { bool ok; std::string message; };
SignalResult send_signal(int pid, int sig);          // sig is SIGTERM or SIGKILL
std::vector<ProcInfo> find_processes(const std::vector<ProcInfo>& all, const std::string& query);
// numeric query = exact PID match; otherwise case-insensitive substring of name
```

**`include/ui.hpp`** (owner: Teammate 3)

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
    double load1 = 0, load5 = 0, load15 = 0;
    double uptime_seconds = 0;
};

// Redraws the whole screen (clears first). Shows as many processes as fit the terminal height.
void draw_screen(const SystemView& v, const std::vector<ProcInfo>& procs, const std::string& status_line);

// Plain text, no ANSI codes, no screen clearing. Shows the first 10 processes.
void print_snapshot(const SystemView& v, const std::vector<ProcInfo>& procs);
```

## 2.2 Command line of the C++ binary `./monitor`

| Command | Behaviour | Exit code |
|---|---|---|
| `./monitor` | Interactive terminal UI. Keys: `r` refresh, `s` search, `k` kill, `q` quit | 0 |
| `./monitor --snapshot` | One sample as plain text on stdout, then exit | 0 |
| `./monitor --log <dir> [--interval <sec>]` | Headless loop. Default interval 2 s (integer, 1 to 3600). Appends to `<dir>/system.csv` and `<dir>/processes.csv`. On SIGTERM or SIGINT it finishes the current sample, flushes, and exits | 0 on clean stop, 1 if `<dir>` is missing or not writable |
| `./monitor --kill <pid> <TERM\|KILL>` | Sends the signal. Messages go to stderr | 0 sent, 1 failed (no such process, permission denied, refused), 2 bad usage |
| `./monitor --help` | Usage text | 0 |

Any unknown option prints the usage text to stderr and exits with 2. The `<dir>` for `--log` must already exist (`monitor.sh` creates it).

## 2.3 CSV log format

Plain text, one header row, comma separated, no quotes. Timestamps are **local time**, ISO 8601, `YYYY-MM-DDTHH:MM:SS`. Numbers have one decimal place.

**`system.csv`** (one row per sample)

```
timestamp,cpu_pct,mem_pct,swap_pct,disk_pct,load1
2026-10-05T10:00:01,42.7,54.1,4.0,72.0,1.3
2026-10-05T10:00:03,38.2,54.3,4.0,72.0,1.3
```

**`processes.csv`** (the **top 5 processes by CPU** at each sample; all rows of one sample share the same timestamp)

```
timestamp,pid,name,cpu_pct,mem_pct
2026-10-05T10:00:01,4211,firefox,32.4,8.2
2026-10-05T10:00:01,3821,code,18.7,4.1
```

Rules:

- The header is written only if the file is new or empty. The engine appends to existing files.
- `name` has any comma replaced by `_` before writing.
- Every row is flushed immediately. A reader (Python) must tolerate a blank line or a half-written last line by skipping it with a warning.
- `cpu_pct` in `processes.csv` uses the top-style convention (can exceed 100). `cpu_pct` in `system.csv` is the whole machine (0 to 100).

## 2.4 Portability rules (three different systems: WSL2 Ubuntu, VirtualBox Debian/Ubuntu, Arch)

1. **C++:** standard C++17 and POSIX/Linux headers only. The build must be clean under `-Wall -Wextra` on all three compilers. No hard-coded user paths.
2. **Bash:** first line `#!/usr/bin/env bash`; assume Bash 4 or newer only; quote every variable expansion.
3. **Python:** target **3.8**. Standard library only. Do not use `match`, `X | Y` type unions, `list[str]` annotations, or `str.removeprefix` (all need 3.9 or 3.10).
4. **Line endings:** the repo forces LF (`.gitattributes`). If a script fails with `bad interpreter: /bin/bash^M`, convert it with `dos2unix`.
5. **Paths:** relative to the project root, or passed as arguments.

---

