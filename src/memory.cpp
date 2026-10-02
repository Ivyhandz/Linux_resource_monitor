#include "memory.hpp"
#include <fstream>

bool read_meminfo(MemInfo &out)
{
    out = MemInfo{};
    bool found_available = false;

    unsigned long long buffers_kb = 0;
    unsigned long long cached_kb = 0;

    std::ifstream file("/proc/meminfo");

    if (!file.is_open())
    {
        return false;
    }

    std::string label;
    unsigned long long value;
    std::string unit;

    while (file >> label >> value >> unit)
    {
        if (label == "MemTotal:")
        {
            out.total_kb = value;
        }
        else if (label == "MemFree:")
        {
            out.free_kb = value;
        }
        else if (label == "MemAvailable:")
        {
            out.available_kb = value;
            found_available = true;
        }
        else if (label == "Buffers:")
        {
            buffers_kb = value;
        }
        else if (label == "Cached:")
        {
            cached_kb = value;
        }
        else if (label == "SwapTotal:")
        {
            out.swap_total_kb = value;
        }
        else if (label == "SwapFree:")
        {
            out.swap_used_kb = out.swap_total_kb - value;
        }
    }

    if (!found_available)
    {
        out.available_kb = out.free_kb + buffers_kb + cached_kb;
    }

    out.used_kb = out.total_kb - out.available_kb;

    return true;
}

double mem_used_pct(const MemInfo& m)
{
    if (m.total_kb == 0)
        return 0.0;

    return (double)m.used_kb / m.total_kb * 100.0;
}

double swap_used_pct(const MemInfo& m)
{
    if (m.swap_total_kb == 0)
        return 0.0;

    return (double)m.swap_used_kb / m.swap_total_kb * 100.0;
}
