#include "memory.hpp"

#include <iostream>

bool check_close(double actual, double expected)
{
    return (actual >= expected - 0.01 &&
            actual <= expected + 0.01);
}

int main()
{
    // --------------------------------------------------
    // 1. Test real /proc/meminfo reading
    // --------------------------------------------------

    MemInfo mem;

    if (!read_meminfo(mem))
    {
        std::cout << "FAIL: read_meminfo()\n";
        return 1;
    }

    std::cout << "PASS: read_meminfo()\n";


    // --------------------------------------------------
    // 2. Test basic memory values
    // --------------------------------------------------

    if (mem.total_kb == 0)
    {
        std::cout << "FAIL: MemTotal is zero\n";
        return 1;
    }

    if (mem.available_kb > mem.total_kb)
    {
        std::cout << "FAIL: available memory > total memory\n";
        return 1;
    }

    if (mem.used_kb > mem.total_kb)
    {
        std::cout << "FAIL: used memory > total memory\n";
        return 1;
    }

    std::cout << "PASS: memory values\n";


    // --------------------------------------------------
    // 3. Test mem_used_pct()
    // --------------------------------------------------

    double mem_pct = mem_used_pct(mem);

    if (mem_pct < 0.0 || mem_pct > 100.0)
    {
        std::cout << "FAIL: mem_used_pct()\n";
        return 1;
    }

    std::cout << "PASS: mem_used_pct()\n";
    std::cout << "Memory usage: " << mem_pct << "%\n";


    // --------------------------------------------------
    // 4. Test swap_used_pct()
    // --------------------------------------------------

    double swap_pct = swap_used_pct(mem);

    if (swap_pct < 0.0 || swap_pct > 100.0)
    {
        std::cout << "FAIL: swap_used_pct()\n";
        return 1;
    }

    std::cout << "PASS: swap_used_pct()\n";
    std::cout << "Swap usage: " << swap_pct << "%\n";


    // --------------------------------------------------
    // 5. Test known percentage calculation
    // --------------------------------------------------

    MemInfo test{};

    test.total_kb = 1000;
    test.available_kb = 250;
    test.used_kb = 750;

    if (!check_close(mem_used_pct(test), 75.0))
    {
        std::cout << "FAIL: known memory percentage\n";
        return 1;
    }

    std::cout << "PASS: known memory percentage\n";


    // --------------------------------------------------
    // 6. Test known swap calculation
    // --------------------------------------------------

    test.swap_total_kb = 1000;
    test.swap_used_kb = 250;

    if (!check_close(swap_used_pct(test), 25.0))
    {
        std::cout << "FAIL: known swap percentage\n";
        return 1;
    }

    std::cout << "PASS: known swap percentage\n";


    // --------------------------------------------------
    // 7. Test zero totals
    // --------------------------------------------------

    MemInfo zero{};

    if (mem_used_pct(zero) != 0.0)
    {
        std::cout << "FAIL: zero memory total\n";
        return 1;
    }

    if (swap_used_pct(zero) != 0.0)
    {
        std::cout << "FAIL: zero swap total\n";
        return 1;
    }

    std::cout << "PASS: zero totals\n";


    std::cout << "\nMEMORY TESTS PASSED\n";

    return 0;
}