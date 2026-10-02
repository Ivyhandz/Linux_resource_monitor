#pragma once
#include <string>
#include <vector>
#include "cpu.hpp"
#include "memory.hpp"
#include "disk.hpp"
#include "process.hpp"

struct SystemView {
    CpuUsage cpu;
    MemInfo mem;
    DiskInfo disk;
    double load1 = 0, load5 = 0, load15 = 0;
    double uptime_seconds = 0;
};

// Redraws the whole screen (clears first). Shows as many processes as fit the terminal height.
void draw_screen(const SystemView& v, const std::vector<ProcInfo>& procs, const std::string& status_line);

// Plain text, no ANSI codes, no screen clearing. Shows the first 10 processes.
void print_snapshot(const SystemView& v, const std::vector<ProcInfo>& procs);
