#include "disk.hpp"

#include <iostream>

bool check_close(double actual, double expected)
{
    return (actual >= expected - 0.01 &&
            actual <= expected + 0.01);
}

int main()
{
    // --------------------------------------------------
    // 1. Test valid filesystem path
    // --------------------------------------------------

    DiskInfo disk;

    if (!read_disk("/", disk))
    {
        std::cout << "FAIL: read_disk() valid path\n";
        return 1;
    }

    std::cout << "PASS: read_disk() valid path\n";


    // --------------------------------------------------
    // 2. Test disk values
    // --------------------------------------------------

    if (disk.total_bytes == 0)
    {
        std::cout << "FAIL: disk total is zero\n";
        return 1;
    }

    if (disk.available_bytes > disk.total_bytes)
    {
        std::cout << "FAIL: available > total\n";
        return 1;
    }

    if (disk.used_bytes > disk.total_bytes)
    {
        std::cout << "FAIL: used > total\n";
        return 1;
    }

    std::cout << "PASS: disk values\n";


    // --------------------------------------------------
    // 3. Test disk_used_pct() with real data
    // --------------------------------------------------

    double usage = disk_used_pct(disk);

    if (usage < 0.0 || usage > 100.0)
    {
        std::cout << "FAIL: disk_used_pct()\n";
        return 1;
    }

    std::cout << "PASS: disk_used_pct()\n";
    std::cout << "Disk usage: " << usage << "%\n";


    // --------------------------------------------------
    // 4. Test invalid filesystem path
    // --------------------------------------------------

    DiskInfo invalid;

    if (read_disk("/this/path/does/not/exist", invalid))
    {
        std::cout << "FAIL: invalid path should fail\n";
        return 1;
    }

    std::cout << "PASS: invalid path\n";


    // --------------------------------------------------
    // 5. Test known percentage calculation
    // --------------------------------------------------

    DiskInfo test{};

    test.used_bytes = 750;
    test.available_bytes = 250;

    if (!check_close(disk_used_pct(test), 75.0))
    {
        std::cout << "FAIL: known disk percentage\n";
        return 1;
    }

    std::cout << "PASS: known disk percentage\n";


    // --------------------------------------------------
    // 6. Test zero denominator
    // --------------------------------------------------

    DiskInfo zero{};

    if (disk_used_pct(zero) != 0.0)
    {
        std::cout << "FAIL: zero denominator\n";
        return 1;
    }

    std::cout << "PASS: zero denominator\n";


    std::cout << "\nDISK TESTS PASSED\n";

    return 0;
}