#include "ui.hpp"
#include "system.hpp"
#include <iomanip>
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>

static int terminal_rows()
{
    struct winsize size{};

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1)
    {
        return 24;
    }

    if (size.ws_row == 0)
    {
        return 24;
    }

    return size.ws_row;
}

static void print_header(const SystemView& v)
{
    std::cout << "Linux System Resource Monitor\n";
    std::cout << "=============================\n";

    std::cout << std::fixed << std::setprecision(1);

    std::cout << "CPU Usage : " << v.cpu.total << "%\n";
    std::cout << "CPU User  : " << v.cpu.user << "%\n";
    std::cout << "CPU System: " << v.cpu.system << "%\n";
    std::cout << "CPU Idle  : " << v.cpu.idle << "%\n";

    std::cout << "Load Avg  : "
              << v.load1 << " "
              << v.load5 << " "
              << v.load15 << "\n";

    std::cout << "Memory    : "
              << v.mem.used_kb << " / "
              << v.mem.total_kb << " kB\n";

    std::cout << "Swap      : "
              << v.mem.swap_used_kb << " / "
              << v.mem.swap_total_kb << " kB\n";

    std::cout << "Disk      : "
              << v.disk.used_bytes << " / "
              << v.disk.total_bytes << " bytes\n";

    std::cout << "Uptime    : "
              << format_uptime(v.uptime_seconds) << "\n";
}

static void print_process_header()
{
    std::cout << "\nProcesses:\n";

    std::cout << std::left
              << std::setw(7) << "PID"
              << std::setw(20) << "NAME"
              << std::setw(7) << "STATE"
              << std::setw(9) << "CPU%"
              << std::setw(9) << "MEM%"
              << std::setw(12) << "RSS(KB)"
              << "COMMAND\n";

    std::cout
        << "--------------------------------------------------------------------------\n";
}

static void print_process(const ProcInfo& p)
{
    std::cout << std::left
              << std::setw(7) << p.pid
              << std::setw(20) << p.name
              << std::setw(7) << p.state
              << std::setw(9) << p.cpu_pct
              << std::setw(9) << p.mem_pct
              << std::setw(12) << p.rss_kb
              << p.cmdline
              << "\n";
}

void draw_screen(
    const SystemView& v,
    const std::vector<ProcInfo>& procs,
    const std::string& status_line)
{
    // Clear the terminal and move the cursor to the top-left.
    std::cout << "\033[2J\033[H";

    print_header(v);
    print_process_header();

    /*
     * Header currently uses:
     *
     * 10 lines for system information
     * 3 lines for process section/header
     *
     * Reserve one line for status when it is present.
     */
    int rows = terminal_rows();

    int reserved_lines = 13;

    if (!status_line.empty())
    {
        reserved_lines++;
    }

    int process_limit = rows - reserved_lines;

    if (process_limit < 0)
    {
        process_limit = 0;
    }

    int count = 0;

    for (const ProcInfo& p : procs)
    {
        if (count >= process_limit)
        {
            break;
        }

        print_process(p);
        count++;
    }

    if (!status_line.empty())
    {
        std::cout << "\n" << status_line << "\n";
    }

    std::cout.flush();
}

void print_snapshot(
    const SystemView& v,
    const std::vector<ProcInfo>& procs)
{
    /*
     * Snapshot must be plain text.
     * No ANSI escape sequences and no screen clearing.
     */

    print_header(v);
    print_process_header();

    int count = 0;

    for (const ProcInfo& p : procs)
    {
        if (count >= 10)
        {
            break;
        }

        print_process(p);
        count++;
    }

    std::cout.flush();
}