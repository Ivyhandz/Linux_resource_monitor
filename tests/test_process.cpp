#include "process.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <unistd.h>
#include <csignal>
#include <sys/wait.h>

static int failures = 0;

static void expect(bool ok, const char* name)
{
    if (ok)
    {
        std::cout << "PASS: " << name << "\n";
    }
    else
    {
        std::cout << "FAIL: " << name << "\n";
        failures++;
    }
}

int main()
{
    // --------------------------------------------------
    // 1. Empty process list
    // --------------------------------------------------

    std::vector<ProcInfo> none;

    expect(find_processes(none, "bash").empty(), "empty list: name search");
    expect(find_processes(none, "123").empty(), "empty list: pid search");

    // --------------------------------------------------
    // 2. First sample (no previous data)
    // --------------------------------------------------

    ProcessSampler sampler;
    std::vector<ProcInfo> first = sampler.sample(0, 1000000);

    expect(!first.empty(), "first sample: finds processes");

    bool all_zero = true;
    for (const ProcInfo& p : first)
    {
        if (p.cpu_pct != 0.0)
        {
            all_zero = false;
        }
    }
    expect(all_zero, "first sample: every cpu_pct is 0");

    const int self = static_cast<int>(getpid());
    std::vector<ProcInfo> me = find_processes(first, std::to_string(self));

    expect(me.size() == 1, "pid search finds own process");
    expect(me.size() == 1 && !me[0].name.empty() && me[0].rss_kb > 0,
           "own process has name and rss");

    // --------------------------------------------------
    // 3. Sorting (second sample, after burning CPU for 0.5 s)
    // --------------------------------------------------

    std::clock_t start = std::clock();
    while (std::clock() - start < CLOCKS_PER_SEC / 2)
    {
    }

    std::vector<ProcInfo> second = sampler.sample(50, 1000000);

    bool sorted = true;
    for (size_t i = 1; i < second.size(); ++i)
    {
        const ProcInfo& a = second[i - 1];
        const ProcInfo& b = second[i];

        if (a.cpu_pct < b.cpu_pct)
        {
            sorted = false;
        }
        if (a.cpu_pct == b.cpu_pct && a.mem_pct < b.mem_pct)
        {
            sorted = false;
        }
    }
    expect(sorted, "sorted by cpu desc, ties by mem desc");

    me = find_processes(second, std::to_string(self));
    expect(me.size() == 1 && me[0].cpu_pct > 0.0,
           "busy own process has cpu_pct > 0");

    // --------------------------------------------------
    // 4. Search (made-up processes)
    // --------------------------------------------------

    std::vector<ProcInfo> fake(4);
    fake[0].pid = 100;  fake[0].name = "Firefox";
    fake[1].pid = 200;  fake[1].name = "code";
    fake[2].pid = 300;  fake[2].name = "firefox-esr";
    fake[3].pid = 1000; fake[3].name = "systemd";

    expect(find_processes(fake, "fire").size() == 2, "search: substring");
    expect(find_processes(fake, "FIREFOX").size() == 2, "search: ignores case");
    expect(find_processes(fake, "zzz").empty(), "search: no match");
    expect(find_processes(fake, "").empty(), "search: empty query");

    std::vector<ProcInfo> exact = find_processes(fake, "100");
    expect(exact.size() == 1 && exact[0].pid == 100,
           "search: digits are an exact PID (100 is not 1000)");
    expect(find_processes(fake, "999").empty(), "search: unknown PID");

    // --------------------------------------------------
    // 5. Signals: refused and failing cases
    // --------------------------------------------------

    SignalResult r = send_signal(self, SIGHUP);
    expect(!r.ok && r.message == "unsupported signal", "signal: only TERM and KILL");

    r = send_signal(0, SIGTERM);
    expect(!r.ok, "signal: PID 0 refused");

    r = send_signal(1, SIGTERM);
    expect(!r.ok, "signal: PID 1 refused");

    r = send_signal(-1, SIGTERM);
    expect(!r.ok, "signal: negative PID refused");

    r = send_signal(self, SIGTERM);
    expect(!r.ok && r.message == "refusing to signal own process",
           "signal: own PID refused");

    r = send_signal(2000000000, SIGTERM);
    expect(!r.ok && r.message == "no such process", "signal: unknown PID");

    // --------------------------------------------------
    // 6. Signals: a real kill of a harmless child
    // --------------------------------------------------

    std::cout.flush();
    pid_t child = fork();
    if (child == 0)
    {
        pause();
        _exit(0);
    }

    r = send_signal(static_cast<int>(child), SIGTERM);
    expect(r.ok && r.message == "signal sent", "signal: TERM sent to child");

    int wait_status = 0;
    waitpid(child, &wait_status, 0);
    expect(WIFSIGNALED(wait_status) && WTERMSIG(wait_status) == SIGTERM,
           "signal: child died from SIGTERM");


    if (failures > 0)
    {
        std::cout << "\n" << failures << " PROCESS TEST(S) FAILED\n";
        return 1;
    }

    std::cout << "\nPROCESS TESTS PASSED\n";
    return 0;
}
