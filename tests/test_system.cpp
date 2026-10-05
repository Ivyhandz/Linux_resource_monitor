#include "system.hpp"

#include <iostream>
#include <string>

int main()
{
    // --------------------------------------------------
    // 1. Test real /proc/uptime reading
    // --------------------------------------------------

    double seconds = 0.0;

    if (!read_uptime(seconds))
    {
        std::cout << "FAIL: read_uptime()\n";
        return 1;
    }

    if (seconds < 0.0)
    {
        std::cout << "FAIL: uptime is negative\n";
        return 1;
    }

    std::cout << "PASS: read_uptime()\n";
    std::cout << "Uptime seconds: " << seconds << "\n";


    // --------------------------------------------------
    // 2. Test basic HH:MM:SS formatting
    // --------------------------------------------------

    if (format_uptime(0) != "00:00:00")
    {
        std::cout << "FAIL: zero uptime formatting\n";
        return 1;
    }

    if (format_uptime(3661) != "01:01:01")
    {
        std::cout << "FAIL: normal uptime formatting\n";
        return 1;
    }

    std::cout << "PASS: uptime formatting\n";


    // --------------------------------------------------
    // 3. Test hours greater than 24
    // --------------------------------------------------

    if (format_uptime(90061) != "25:01:01")
    {
        std::cout << "FAIL: uptime > 24 hours\n";
        return 1;
    }

    std::cout << "PASS: hours > 24\n";


    // --------------------------------------------------
    // 4. Test another formatting boundary
    // --------------------------------------------------

    if (format_uptime(59) != "00:00:59")
    {
        std::cout << "FAIL: seconds formatting\n";
        return 1;
    }

    if (format_uptime(60) != "00:01:00")
    {
        std::cout << "FAIL: minute formatting\n";
        return 1;
    }

    std::cout << "PASS: formatting boundaries\n";


    std::cout << "\nSYSTEM TESTS PASSED\n";

    return 0;
}