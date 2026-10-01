#pragma once

struct CpuTimes {
    unsigned long long user = 0;    // Time spent running user-space processes
    unsigned long long nice = 0;    // Time spent running low-priority (nice) processes
    unsigned long long system = 0;  // Time spent in kernel space
    unsigned long long idle = 0;    // Time the CPU spent doing nothing
    unsigned long long iowait = 0;  // Time waiting for I/O to complete
    unsigned long long irq = 0;     // Time spent servicing hardware interrupts
    unsigned long long softirq = 0; // Time spent servicing software interrupts
    unsigned long long steal = 0;   // Time "stolen" by a hypervisor (VMs)
};

bool Read_Cpu_Times(CpuTimes& cpu_times);
unsigned long long total_jiffies(const CpuTimes& t);
CpuTimes Compute_Usage(const CpuTimes& prev, const CpuTimes& curr);