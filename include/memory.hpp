#pragma once

struct MemInfo {
    unsigned long long total_kb = 0, free_kb = 0, available_kb = 0,
                       used_kb = 0,                           // total - available
                       swap_total_kb = 0, swap_used_kb = 0;   // swap_used = SwapTotal - SwapFree
};

bool read_meminfo(MemInfo& out);
double mem_used_pct(const MemInfo& m);     // used_kb / total_kb * 100  (0 if total is 0)
double swap_used_pct(const MemInfo& m);    // 0 if there is no swap
