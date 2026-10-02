#pragma once
#include <string>

bool read_uptime(double& seconds);                  // first number of /proc/uptime
std::string format_uptime(double seconds);          // "HH:MM:SS" (hours may exceed 24)
