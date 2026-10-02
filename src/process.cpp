#include "process.hpp"

#include <cctype>
#include <dirent.h>

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

}  // namespace

// For now this only fills in the PID. Later steps add name, state, CPU and memory.
std::vector<ProcInfo> ProcessSampler::sample(unsigned long long, unsigned long long) {
    std::vector<ProcInfo> result;
    for (int pid : list_pids()) {
        ProcInfo p;
        p.pid = pid;
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
