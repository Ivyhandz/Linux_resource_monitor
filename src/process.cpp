#include "process.hpp"

std::vector<ProcInfo> ProcessSampler::sample(unsigned long long, unsigned long long) {
    return {};
}

SignalResult send_signal(int, int) {
    return {false, "not implemented"};
}

std::vector<ProcInfo> find_processes(const std::vector<ProcInfo>&, const std::string&) {
    return {};
}
