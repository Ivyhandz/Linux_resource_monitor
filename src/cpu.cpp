#include "cpu.hpp"
#include <fstream>
#include <string>
bool read_cpu_times(CpuTimes& out)
{
    out = CpuTimes{}; // initialize all counters to 0

    std::ifstream file("/proc/stat");

    if (!file.is_open()) {
        return false;
    }

    std::string label;

    if (!(file >> label)) {
        return false;
    }

    if (label != "cpu") {
        return false;
    }

    // These four are required
    if (!(file >> out.user
              >> out.nice
              >> out.system
              >> out.idle))
    {
        return false;
    }

    // These are optional trailing fields
    file >> out.iowait;
    file >> out.irq;
    file >> out.softirq;
    file >> out.steal;

    return true;
}


unsigned long long total_jiffies(const CpuTimes& t) {
    return t.user + t.nice + t.system + t.idle
         + t.iowait + t.irq + t.softirq + t.steal;
}

CpuUsage compute_usage(const CpuTimes& prev, const CpuTimes& cur)
{
    CpuUsage usage;

    if (cur.user < prev.user ||
        cur.nice < prev.nice ||
        cur.system < prev.system ||
        cur.idle < prev.idle ||
        cur.iowait < prev.iowait ||
        cur.irq < prev.irq ||
        cur.softirq < prev.softirq ||
        cur.steal < prev.steal)
    {
        return usage;
    }

    unsigned long long delta_total =
        total_jiffies(cur) - total_jiffies(prev);

    if (delta_total == 0)
        return usage;

    unsigned long long delta_user =
        (cur.user - prev.user) +
        (cur.nice - prev.nice);

    unsigned long long delta_system =
        (cur.system - prev.system) +
        (cur.irq - prev.irq) +
        (cur.softirq - prev.softirq);

    unsigned long long delta_idle =
        (cur.idle - prev.idle) +
        (cur.iowait - prev.iowait);

    usage.total =
        (double)(delta_total - delta_idle) /
        delta_total * 100.0;

    usage.user =
        (double)delta_user /
        delta_total * 100.0;

    usage.system =
        (double)delta_system /
        delta_total * 100.0;

    usage.idle =
        (double)delta_idle /
        delta_total * 100.0;

    return usage;
}

bool read_loadavg(double& l1, double& l5, double& l15)
{
    std::ifstream file("/proc/loadavg");

    if (!file.is_open())
        return false;

    if (!(file >> l1 >> l5 >> l15))
        return false;

    return true;
}