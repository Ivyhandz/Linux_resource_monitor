#include "ui.hpp"
#include "system.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/ioctl.h>
#include <unistd.h>

static constexpr double KIB_PER_GIB = 1024.0 * 1024.0;
static constexpr double BYTES_PER_GIB =
    1024.0 * 1024.0 * 1024.0;

static double kib_to_gib(unsigned long long kib)
{
    return static_cast<double>(kib) / KIB_PER_GIB;
}

static double bytes_to_gib(unsigned long long bytes)
{
    return static_cast<double>(bytes) / BYTES_PER_GIB;
}

static double percent_used(unsigned long long used,
                           unsigned long long total)
{
    if (total == 0)
        return 0.0;

    return 100.0 * static_cast<double>(used) /
           static_cast<double>(total);
}

static int terminal_rows()
{
    struct winsize size{};

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1 ||
        size.ws_row == 0)
    {
        return 24;
    }

    return size.ws_row;
}

static int terminal_columns()
{
    struct winsize size{};

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1 ||
        size.ws_col == 0)
    {
        return 80;
    }

    return size.ws_col;
}

static std::string truncate_text(const std::string& text, int width)
{
    if (width <= 0)
        return "";

    const auto limit = static_cast<std::size_t>(width);

    if (text.size() <= limit)
        return text;

    if (width <= 3)
        return text.substr(0, limit);

    return text.substr(0, limit - 3) + "...";
}

static std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;

    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        lines.push_back(line);
    }

    // Preserve a final empty line if the input ends in '\n'.
    if (!text.empty() && text.back() == '\n')
        lines.emplace_back();

    return lines;
}

static std::string single_line(std::string text)
{
    for (char& ch : text)
    {
        if (ch == '\n' || ch == '\r' || ch == '\t')
            ch = ' ';
    }

    return text;
}

static void print_header(std::ostream& out, const SystemView& v)
{
    out << "Linux System Resource Monitor\n";
    out << "=============================\n";

    out << std::fixed << std::setprecision(1);

    out << "CPU Usage : " << v.cpu.total << "%\n";
    out << "CPU User  : " << v.cpu.user << "%\n";
    out << "CPU System: " << v.cpu.system << "%\n";
    out << "CPU Idle  : " << v.cpu.idle << "%\n";

    out << std::setprecision(2);
    out << "Load Avg  : "
        << v.load1 << "  "
        << v.load5 << "  "
        << v.load15 << "\n";

    out << std::setprecision(2);
    out << "Memory    : "
        << kib_to_gib(v.mem.used_kb) << " / "
        << kib_to_gib(v.mem.total_kb) << " GiB ("
        << percent_used(v.mem.used_kb, v.mem.total_kb) << "%)\n";

    out << "Swap      : "
        << kib_to_gib(v.mem.swap_used_kb) << " / "
        << kib_to_gib(v.mem.swap_total_kb) << " GiB ("
        << percent_used(v.mem.swap_used_kb, v.mem.swap_total_kb)
        << "%)\n";

    out << "Disk      : "
        << bytes_to_gib(v.disk.used_bytes) << " / "
        << bytes_to_gib(v.disk.total_bytes) << " GiB ("
        << percent_used(v.disk.used_bytes, v.disk.total_bytes)
        << "%)\n";

    out << "Uptime    : "
        << format_uptime(v.uptime_seconds) << "\n";
}

static void print_process_header(std::ostream& out)
{
    out << "\nProcesses:\n";
    out << std::left
        << std::setw(7) << "PID"
        << std::setw(20) << "NAME"
        << std::setw(7) << "STATE"
        << std::setw(9) << "CPU%"
        << std::setw(9) << "MEM%"
        << std::setw(12) << "RSS(KB)"
        << "COMMAND\n";
    out << "--------------------------------------------------------------------------\n";
}

static void print_process(std::ostream& out, const ProcInfo& p)
{
    out << std::left
        << std::setw(7) << p.pid
        << std::setw(20) << p.name
        << std::setw(7) << p.state
        << std::setw(9) << p.cpu_pct
        << std::setw(9) << p.mem_pct
        << std::setw(12) << p.rss_kb
        << p.cmdline << "\n";
}

static std::string format_interactive_process(
    const ProcInfo& p,
    int columns)
{
    std::ostringstream row;

    row << std::fixed << std::setprecision(1) << std::left
        << std::setw(7) << p.pid
        << std::setw(20) << truncate_text(p.name, 20)
        << std::setw(7) << p.state
        << std::setw(9) << p.cpu_pct
        << std::setw(9) << p.mem_pct
        << std::setw(12) << p.rss_kb;

    const std::string prefix = row.str();
    const int command_width =
        std::max(0, columns - static_cast<int>(prefix.size()));

    row << truncate_text(p.cmdline, command_width);
    return truncate_text(row.str(), columns);
}

void draw_screen(
    const SystemView& v,
    const std::vector<ProcInfo>& procs,
    const std::string& status_line)
{
    const int rows = terminal_rows();
    const int columns = terminal_columns();

    std::ostringstream header;
    print_header(header, v);
    std::vector<std::string> lines = split_lines(header.str());

    std::ostringstream process_heading;
    print_process_header(process_heading);
    std::vector<std::string> heading_lines =
        split_lines(process_heading.str());

    lines.insert(lines.end(), heading_lines.begin(), heading_lines.end());

    const std::string controls =
        "[q] Quit  [r] Refresh  [s] Search  [k] Kill process";
    const int status_lines = status_line.empty() ? 0 : 2;
    const int footer_lines = 1;

    const int process_limit = std::max(
        0, rows - static_cast<int>(lines.size())
             - status_lines - footer_lines);

    int count = 0;
    for (const ProcInfo& p : procs)
    {
        if (count >= process_limit)
            break;

        lines.push_back(format_interactive_process(p, columns));
        ++count;
    }

    if (!status_line.empty())
    {
        lines.emplace_back("");
        lines.push_back(truncate_text(single_line(status_line), columns));
    }

    lines.push_back(truncate_text(controls, columns));

    if (static_cast<int>(lines.size()) > rows)
        lines.resize(static_cast<std::size_t>(rows));

    for (std::string& line : lines)
        line = truncate_text(line, columns);

    // Clear each displayed row explicitly. This avoids remnants when a
    // refreshed line is shorter than the previous line.
    std::ostringstream frame;
    frame << "\033[H";

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        frame << "\033[2K" << lines[i];
        if (i + 1 < lines.size())
            frame << '\n';
    }

    frame << "\033[J";

    const std::string output = frame.str();
    std::cout.write(output.data(),
                    static_cast<std::streamsize>(output.size()));
    std::cout.flush();
}

void print_snapshot(
    const SystemView& v,
    const std::vector<ProcInfo>& procs)
{
    // Snapshot output stays plain text, with no ANSI screen controls.
    print_header(std::cout, v);
    print_process_header(std::cout);

    int count = 0;
    for (const ProcInfo& p : procs)
    {
        if (count >= 10)
            break;

        print_process(std::cout, p);
        ++count;
    }

    std::cout.flush();
}
