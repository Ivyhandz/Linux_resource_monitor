#include <cstdio>
#include "process.hpp"

int main() {
    ProcessSampler sampler;
    std::vector<ProcInfo> procs = sampler.sample(0, 0);
    std::printf("found %zu processes\n", procs.size());
    std::printf("%6s %-2s %9s  %s\n", "PID", "S", "RSS_KB", "CMDLINE");
    for (size_t i = 0; i < procs.size() && i < 10; ++i) {
        const ProcInfo& p = procs[i];
        std::printf("%6d %-2c %9llu  %s\n", p.pid, p.state, p.rss_kb, p.cmdline.c_str());
    }
    return 0;
}
