#include "process.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <signal.h>
#include <sstream>
#include <unistd.h>
#include <utility>

namespace {

// True if the string is non-empty and every character is a digit.
bool all_digits(const char* s) {
    if (*s == '\0') return false;
    for (; *s != '\0'; ++s) {
        if (!std::isdigit(static_cast<unsigned char>(*s))) return false;
    }
    return true;
}

// Every numeric directory name under /proc is a PID.
std::vector<int> list_pids() {
    std::vector<int> pids;
    DIR* dir = opendir("/proc");
    if (dir == nullptr) return pids;               // cannot open /proc: report nothing

    while (dirent* entry = readdir(dir)) {
        if (all_digits(entry->d_name)) {
            pids.push_back(std::stoi(entry->d_name));
        }
    }
    closedir(dir);
    return pids;
}

// Number of online CPU cores (at least 1). Used to scale process CPU% the way top does.
long core_count() {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? n : 1;
}

// Lower-case copy of a string (used for case-insensitive search).
std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Reads /proc/<pid>/stat and fills name, state and cpu_jiffies (utime + stime).
// Returns false if the process vanished or the line is malformed.
bool read_stat(int pid, ProcInfo& out) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(file, line)) return false;      // also fails if the file could not be opened

    // The name sits in parentheses and may contain spaces or even ')'.
    // The first '(' and the LAST ')' are the only reliable boundaries.
    size_t open = line.find('(');
    size_t close = line.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) return false;
    out.name = line.substr(open + 1, close - open - 1);

    // After ") " the fields are whitespace separated: token 0 = state,
    // token 11 = utime, token 12 = stime (both in jiffies).
    std::istringstream rest(line.substr(close + 1));
    unsigned long long utime = 0, stime = 0;
    bool complete = false;
    int index = 0;
    for (std::string token; rest >> token; ++index) {
        if (index == 0) out.state = token[0];
        if (index == 11) utime = std::strtoull(token.c_str(), nullptr, 10);
        if (index == 12) {
            stime = std::strtoull(token.c_str(), nullptr, 10);
            complete = true;
            break;
        }
    }
    if (!complete) return false;

    out.cpu_jiffies = utime + stime;
    return true;
}

// Reads /proc/<pid>/status and fills rss_kb from the VmRSS line (the value is in kB).
// Kernel threads have no such line, so rss_kb stays 0.
void read_status(int pid, ProcInfo& out) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(file, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            out.rss_kb = std::strtoull(line.c_str() + 6, nullptr, 10);
            return;
        }
    }
}

// Reads /proc/<pid>/cmdline: arguments are separated by NUL bytes, so turn them into spaces.
// Kernel threads have an empty cmdline; show them as [name] instead.
void read_cmdline(int pid, ProcInfo& out) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    for (char& c : text) {
        if (c == '\0') c = ' ';
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    out.cmdline = text.empty() ? "[" + out.name + "]" : text;
}

}  // namespace

// Scans /proc and returns every process, sorted by CPU% (highest first).
// delta_total_jiffies: growth of total CPU time (all cores) since the previous call.
// The first call, and any process seen for the first time, get cpu_pct = 0.
std::vector<ProcInfo> ProcessSampler::sample(unsigned long long delta_total_jiffies,
                                             unsigned long long mem_total_kb) {
    const double ncores = static_cast<double>(core_count());
    std::vector<ProcInfo> result;
    std::unordered_map<int, unsigned long long> current;   // becomes prev_jiffies_ afterwards

    for (int pid : list_pids()) {
        ProcInfo p;
        p.pid = pid;
        if (!read_stat(pid, p)) continue;          // process vanished or unreadable: skip it
        read_status(pid, p);
        read_cmdline(pid, p);

        // CPU%: growth of this process's jiffies relative to growth of total jiffies.
        auto it = prev_jiffies_.find(pid);
        if (it != prev_jiffies_.end() && delta_total_jiffies > 0 && p.cpu_jiffies >= it->second) {
            double delta_proc = static_cast<double>(p.cpu_jiffies - it->second);
            p.cpu_pct = delta_proc / static_cast<double>(delta_total_jiffies) * 100.0 * ncores;
        }

        // MEM%: resident memory as a share of total RAM.
        if (mem_total_kb > 0) {
            p.mem_pct = static_cast<double>(p.rss_kb) * 100.0 / static_cast<double>(mem_total_kb);
        }

        current[pid] = p.cpu_jiffies;
        result.push_back(p);
    }

    prev_jiffies_ = std::move(current);            // forget processes that no longer exist

    std::sort(result.begin(), result.end(), [](const ProcInfo& a, const ProcInfo& b) {
        if (a.cpu_pct != b.cpu_pct) return a.cpu_pct > b.cpu_pct;
        return a.mem_pct > b.mem_pct;
    });
    return result;
}

// Sends SIGTERM or SIGKILL to a process. Never prints; the caller shows the message.
SignalResult send_signal(int pid, int sig) {
    if (sig != SIGTERM && sig != SIGKILL) return {false, "unsupported signal"};

    // kill(0, ...) signals a whole process group and kill(-1, ...) signals every process
    // we may signal, so PIDs <= 1 are always refused (PID 1 is init).
    if (pid <= 1) return {false, "refusing to signal PID <= 1"};
    if (pid == static_cast<int>(getpid())) return {false, "refusing to signal own process"};

    if (kill(pid, sig) == 0) return {true, "signal sent"};

    switch (errno) {
        case ESRCH: return {false, "no such process"};
        case EPERM: return {false, "permission denied"};
        default:    return {false, std::string("kill failed: ") + std::strerror(errno)};
    }
}

// A query of only digits is an exact PID match; anything else is a case-insensitive
// substring match on the process name. An empty query matches nothing.
std::vector<ProcInfo> find_processes(const std::vector<ProcInfo>& all, const std::string& query) {
    std::vector<ProcInfo> matches;
    if (query.empty()) return matches;

    if (all_digits(query.c_str())) {
        long wanted = std::strtol(query.c_str(), nullptr, 10);
        for (const ProcInfo& p : all) {
            if (static_cast<long>(p.pid) == wanted) matches.push_back(p);
        }
        return matches;
    }

    const std::string needle = to_lower(query);
    for (const ProcInfo& p : all) {
        if (to_lower(p.name).find(needle) != std::string::npos) matches.push_back(p);
    }
    return matches;
}
