#pragma once


struct MemInfo {
    unsigned long long total_kb = 0;
    unsigned long long free_kb = 0;
    unsigned long long available_kb = 0;
    unsigned long long used_kb = 0;          // total - available
    unsigned long long swap_total_kb = 0;
    unsigned long long swap_used_kb = 0;     // SwapTotal - SwapFree
};