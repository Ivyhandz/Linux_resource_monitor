#include <cstdio>
#include "process.hpp"

int main() {
    ProcessSampler sampler;
    std::vector<ProcInfo> procs = sampler.sample(0, 0);
    std::printf("found %zu processes\n", procs.size());
    for (size_t i = 0; i < procs.size() && i < 5; ++i) {
        std::printf("pid %d\n", procs[i].pid);
    }
    return 0;
}
