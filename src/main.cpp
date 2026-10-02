#include <cstdio>
#include "process.hpp"

int main() {
    ProcessSampler sampler;
    std::vector<ProcInfo> procs = sampler.sample(0, 0);
    std::printf("found %zu processes\n", procs.size());
    std::printf("%6s %-2s %10s  %s\n", "PID", "S", "JIFFIES", "NAME");
    for (size_t i = 0; i < procs.size() && i < 10; ++i) {
        const ProcInfo& p = procs[i];
        std::printf("%6d %-2c %10llu  %s\n", p.pid, p.state, p.cpu_jiffies, p.name.c_str());
    }
    return 0;
}
