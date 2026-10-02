#include "process.hpp"

#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>

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

}  // namespace

// Fills in PID, name, state and cumulative CPU jiffies. CPU% and memory come in later steps.
std::vector<ProcInfo> ProcessSampler::sample(unsigned long long, unsigned long long) {
    std::vector<ProcInfo> result;
    for (int pid : list_pids()) {
        ProcInfo p;
        p.pid = pid;
        if (!read_stat(pid, p)) continue;          // process vanished or unreadable: skip it
        result.push_back(p);
    }
    return result;
}

SignalResult send_signal(int, int) {
    return {false, "not implemented"};
}

std::vector<ProcInfo> find_processes(const std::vector<ProcInfo>&, const std::string&) {
    return {};
}
