#include <cstdio>
#include <unistd.h>

#include "cpu.hpp"
#include "memory.hpp"
#include "process.hpp"

int main() {
    MemInfo mem;
    CpuTimes prev, cur;
    if (!read_meminfo(mem) || !read_cpu_times(prev)) {
        std::fprintf(stderr, "cannot read /proc\n");
        return 1;
    }

    ProcessSampler sampler;
    sampler.sample(0, mem.total_kb);              // first call: only records the baseline

    sleep(1);                                     // let some CPU time pass

    if (!read_cpu_times(cur)) {
        std::fprintf(stderr, "cannot read /proc/stat\n");
        return 1;
    }
    unsigned long long delta = total_jiffies(cur) - total_jiffies(prev);
    std::vector<ProcInfo> procs = sampler.sample(delta, mem.total_kb);

    std::printf("delta_total_jiffies=%llu  total_ram_kb=%llu  processes=%zu\n",
                delta, mem.total_kb, procs.size());
    std::printf("%6s %7s %6s %-2s %s\n", "PID", "CPU%", "MEM%", "S", "COMMAND");
    for (size_t i = 0; i < procs.size() && i < 8; ++i) {
        const ProcInfo& p = procs[i];
        std::printf("%6d %7.1f %6.1f %-2c %.60s\n", p.pid, p.cpu_pct, p.mem_pct, p.state, p.cmdline.c_str());
    }
    return 0;
}
