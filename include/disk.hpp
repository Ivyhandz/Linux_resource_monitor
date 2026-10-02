#pragma once

struct DiskInfo { unsigned long long total_bytes = 0, available_bytes = 0, used_bytes = 0; };

bool read_disk(const char* path, DiskInfo& out);   // uses statvfs()
double disk_used_pct(const DiskInfo& d);           // used / (used + available) * 100, same as df's Use%
