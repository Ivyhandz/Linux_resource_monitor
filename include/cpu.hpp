#pragma once

struct CpuTimes {                       // cumulative jiffies since boot, from the first line of /proc/stat
    unsigned long long user = 0, nice = 0, system = 0, idle = 0,
                       iowait = 0, irq = 0, softirq = 0, steal = 0;
};

struct CpuUsage { double total = 0, user = 0, system = 0, idle = 0; };   // percentages over an interval

bool read_cpu_times(CpuTimes& out);                                // one snapshot; never sleeps
unsigned long long total_jiffies(const CpuTimes& t);               // sum of all eight fields
CpuUsage compute_usage(const CpuTimes& prev, const CpuTimes& cur); // delta-based percentages
bool read_loadavg(double& l1, double& l5, double& l15);
