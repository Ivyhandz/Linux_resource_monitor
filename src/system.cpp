#include "system.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>

bool read_uptime(double& seconds)
{
    std::ifstream file("/proc/uptime");

    if (!file.is_open())
    {
        return false;
    }

    if (!(file >> seconds))
    {
        return false;
    }

    return true;
}

std::string format_uptime(double seconds)
{
    unsigned long long total_seconds = (unsigned long long)seconds;

    unsigned long long hours = total_seconds / 3600;
    unsigned long long minutes = (total_seconds % 3600) / 60;
    unsigned long long secs = total_seconds % 60;

    std::ostringstream out;

    out << std::setfill('0')
        << std::setw(2) << hours
        << ":"
        << std::setw(2) << minutes
        << ":"
        << std::setw(2) << secs;

    return out.str();
}