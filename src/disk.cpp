#include "disk.hpp"
#include <sys/statvfs.h>

bool read_disk(const char* path, DiskInfo& out)
{
    struct statvfs info;

    if (statvfs(path, &info) != 0)
    {
        return false;
    }

    out.total_bytes =
        (unsigned long long)info.f_blocks * info.f_frsize;

    out.available_bytes =
        (unsigned long long)info.f_bavail * info.f_frsize;

  out.used_bytes =
    (unsigned long long)(info.f_blocks - info.f_bfree) * info.f_frsize;

    return true;
}

double disk_used_pct(const DiskInfo& d)
{
    unsigned long long total =
        d.used_bytes + d.available_bytes;

    if (total == 0)
    {
        return 0.0;
    }

    return (double)d.used_bytes / total * 100.0;
}
