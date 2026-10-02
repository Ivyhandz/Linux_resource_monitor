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
