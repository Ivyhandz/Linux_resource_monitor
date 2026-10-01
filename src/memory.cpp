#include "memory.hpp"
#include <fstream>

bool Read_Memory_Info(MemInfo &out)
{
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) {
        return false; // Failed to open the file
    }

    std::string label;
    unsigned long long value;
    std::string unit;
    while (file >> label >> value >> unit) {
        if (label == "MemTotal:") {
            out.total_kb = value;
        } else if (label == "MemFree:") {
            out.free_kb = value;
        } else if (label == "MemAvailable:") {
            out.available_kb = value;
        } else if (label == "SwapTotal:") {
            out.swap_total_kb = value;
        } else if (label == "SwapFree:") {
            out.swap_used_kb = out.swap_total_kb - value; // Calculate swap used
        }
    }

    // Calculate used memory
    out.used_kb = out.total_kb - out.available_kb;

    return true; // Successfully read the memory info
}