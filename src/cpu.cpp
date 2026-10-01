#include "cpu.hpp"
#include <fstream>
#include <string>
bool Read_Cpu_Times(CpuTimes& cpu_times)
{
    // 1. Try to open the file
    std::ifstream file("/proc/stat");

    if (!file.is_open()) {
        return false; // Failed to open the file
    }

    std::string label;
    file >> label;  // gives the label first word of the file

    if(label!="cpu") {
        return false; // The first word is not "cpu"
    }

    // 5. Read the 8 numbers directly into the struct
    file >> cpu_times.user >> cpu_times.nice >> cpu_times.system >> cpu_times.idle 
         >> cpu_times.iowait >> cpu_times.irq >> cpu_times.softirq >> cpu_times.steal;

         return true; // Successfully read the CPU times
}


unsigned long long total_jiffies(const CpuTimes& t) {
    return t.user + t.nice + t.system + t.idle
         + t.iowait + t.irq + t.softirq + t.steal;
}

CpuTimes Compute_Usage(const CpuTimes& prev, const CpuTimes& curr)
{
    CpuTimes usage;

    usage.user = curr.user - prev.user;
    usage.nice = curr.nice - prev.nice;
    usage.system = curr.system - prev.system;
    usage.idle = curr.idle - prev.idle;
    usage.iowait = curr.iowait - prev.iowait;
    usage.irq = curr.irq - prev.irq;
    usage.softirq = curr.softirq - prev.softirq;
    usage.steal = curr.steal - prev.steal;
    
    return usage;
}