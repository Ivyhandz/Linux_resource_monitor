#include "cpu.hpp"

#include <cmath>
#include <iostream>

bool check_close(double actual, double expected)
{
    return std::fabs(actual - expected) < 0.01;
}

int main()
{
    // --------------------------------------------------
    // 1. Test real /proc/stat reading
    // --------------------------------------------------

    CpuTimes prev;
    CpuTimes cur;

    if (!read_cpu_times(prev))
    {
        std::cout << "FAIL: read_cpu_times(prev)\n";
        return 1;
    }

    if (!read_cpu_times(cur))
    {
        std::cout << "FAIL: read_cpu_times(cur)\n";
        return 1;
    }

    std::cout << "PASS: read_cpu_times()\n";


    // --------------------------------------------------
    // 2. Test total_jiffies()
    // --------------------------------------------------

    unsigned long long total = total_jiffies(cur);

    if (total == 0)
    {
        std::cout << "FAIL: total_jiffies()\n";
        return 1;
    }

    std::cout << "PASS: total_jiffies()\n";


    // --------------------------------------------------
    // 3. Test compute_usage() with real readings
    // --------------------------------------------------

    CpuUsage usage = compute_usage(prev, cur);

    if (usage.total < 0.0 || usage.total > 100.0)
    {
        std::cout << "FAIL: CPU total usage out of range\n";
        return 1;
    }

    std::cout << "PASS: compute_usage() real data\n";
    std::cout << "CPU usage: " << usage.total << "%\n";


    // --------------------------------------------------
    // 4. Test zero delta
    // --------------------------------------------------

    CpuTimes same = cur;

    CpuUsage zero_usage = compute_usage(same, same);

    if (zero_usage.total != 0.0 ||
        zero_usage.user != 0.0 ||
        zero_usage.system != 0.0 ||
        zero_usage.idle != 0.0)
    {
        std::cout << "FAIL: zero delta\n";
        return 1;
    }

    std::cout << "PASS: zero delta\n";


    // --------------------------------------------------
    // 5. Test counter rollback
    // --------------------------------------------------

    CpuTimes rollback_prev{};
    CpuTimes rollback_cur{};

    rollback_prev.user = 100;
    rollback_prev.nice = 100;
    rollback_prev.system = 100;
    rollback_prev.idle = 100;

    rollback_cur.user = 90;   // went backwards

    CpuUsage rollback_usage =
        compute_usage(rollback_prev, rollback_cur);

    if (rollback_usage.total != 0.0 ||
        rollback_usage.user != 0.0 ||
        rollback_usage.system != 0.0 ||
        rollback_usage.idle != 0.0)
    {
        std::cout << "FAIL: counter rollback\n";
        return 1;
    }

    std::cout << "PASS: counter rollback\n";


    // --------------------------------------------------
    // 6. Test known CPU calculation
    // --------------------------------------------------

    CpuTimes test_prev{};
    CpuTimes test_cur{};

    test_prev.user = 100;
    test_prev.nice = 0;
    test_prev.system = 0;
    test_prev.idle = 100;

    test_cur.user = 200;
    test_cur.nice = 0;
    test_cur.system = 0;
    test_cur.idle = 200;

    CpuUsage test_usage =
        compute_usage(test_prev, test_cur);

    // Total delta = 200
    // User delta  = 100
    // Idle delta  = 100
    //
    // Therefore:
    // user = 50%
    // idle = 50%
    // total busy = 50%

    if (!check_close(test_usage.total, 50.0) ||
        !check_close(test_usage.user, 50.0) ||
        !check_close(test_usage.idle, 50.0) ||
        !check_close(test_usage.system, 0.0))
    {
        std::cout << "FAIL: known CPU calculation\n";
        return 1;
    }

    std::cout << "PASS: known CPU calculation\n";


    // --------------------------------------------------
    // 7. Test load average
    // --------------------------------------------------

    double l1;
    double l5;
    double l15;

    if (!read_loadavg(l1, l5, l15))
    {
        std::cout << "FAIL: read_loadavg()\n";
        return 1;
    }

    std::cout << "PASS: read_loadavg()\n";
    std::cout << "Load average: "
              << l1 << " "
              << l5 << " "
              << l15 << "\n";


    // --------------------------------------------------
    // Everything passed
    // --------------------------------------------------

    std::cout << "\nCPU TESTS PASSED\n";

    return 0;
}