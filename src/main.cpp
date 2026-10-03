#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "cpu.hpp"
#include "memory.hpp"
#include "process.hpp"

namespace {

// ---------- usage and argument helpers ----------

void print_usage(std::FILE* out) {
    std::fprintf(out,
        "Usage:\n"
        "  monitor                                   interactive view\n"
        "  monitor --snapshot                        print one sample and exit\n"
        "  monitor --log <dir> [--interval <sec>]    write CSV logs until stopped\n"
        "  monitor --kill <pid> <TERM|KILL>          send a signal to a process\n"
        "  monitor --help                            show this text\n");
}

// A PID argument must be digits only and fit in an int.
bool parse_pid(const char* text, int& pid) {
    if (*text == '\0') return false;
    for (const char* c = text; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') return false;
    }
    long value = std::strtol(text, nullptr, 10);
    if (value > INT_MAX) return false;
    pid = static_cast<int>(value);
    return true;
}

// An interval must be a whole number from 1 to 3600 seconds.
bool parse_interval(const char* text, int& seconds) {
    if (*text == '\0') return false;
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (*end != '\0' || value < 1 || value > 3600) return false;
    seconds = static_cast<int>(value);
    return true;
}

// ---------- signal handling ----------

// Set by the signal handler, checked by the main loop. This is the ONLY thing the
// handler does, because almost nothing else is safe to do inside a signal handler.
volatile sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

void install_signal_handlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                       // no SA_RESTART: a signal interrupts sleeping
    sigaction(SIGINT, &sa, nullptr);       // Ctrl+C
    sigaction(SIGTERM, &sa, nullptr);      // what monitor.sh stop sends
}

// Waits up to `seconds`, in 100 ms slices, and returns early once a stop is requested.
void wait_interval(int seconds) {
    for (int i = 0; i < seconds * 10 && !g_stop; ++i) {
        struct timespec slice = {0, 100 * 1000 * 1000};
        nanosleep(&slice, nullptr);
    }
}

// ---------- CSV helpers ----------

// Local time as YYYY-MM-DDTHH:MM:SS.
std::string timestamp_now() {
    std::time_t now = std::time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_now);
    return buf;
}

// Opens a CSV file for appending. The header row is written only if the file is new or empty.
std::FILE* open_csv(const std::string& path, const char* header) {
    struct stat st;
    bool is_new = stat(path.c_str(), &st) != 0 || st.st_size == 0;
    std::FILE* file = std::fopen(path.c_str(), "a");
    if (file != nullptr && is_new) {
        std::fputs(header, file);
        std::fflush(file);
    }
    return file;
}

// A comma inside a process name would break the CSV columns, so it becomes '_'.
std::string csv_safe(std::string name) {
    for (char& c : name) {
        if (c == ',') c = '_';
    }
    return name;
}

// ---------- --kill ----------

// --kill <pid> <TERM|KILL>
// Exit codes: 0 = signal sent, 1 = signal failed, 2 = bad usage. Messages go to stderr.
int run_kill(int argc, char** argv) {
    int pid = 0;
    if (argc != 4 || !parse_pid(argv[2], pid)) {
        print_usage(stderr);
        return 2;
    }

    int sig = 0;
    if (std::strcmp(argv[3], "TERM") == 0) sig = SIGTERM;
    else if (std::strcmp(argv[3], "KILL") == 0) sig = SIGKILL;
    else {
        print_usage(stderr);
        return 2;
    }

    SignalResult result = send_signal(pid, sig);
    std::fprintf(stderr, "monitor: %s (PID %d)\n", result.message.c_str(), pid);
    return result.ok ? 0 : 1;
}

// ---------- --log ----------

// --log <dir> [--interval <sec>]
// Exit codes: 0 = clean stop, 1 = directory or files not usable, 2 = bad usage.
int run_log(int argc, char** argv) {
    if (argc != 3 && argc != 5) {
        print_usage(stderr);
        return 2;
    }
    const char* dir = argv[2];
    int interval = 2;
    if (argc == 5) {
        if (std::strcmp(argv[3], "--interval") != 0 || !parse_interval(argv[4], interval)) {
            print_usage(stderr);
            return 2;
        }
    }

    // The directory must already exist; monitor.sh creates it, not this program.
    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || access(dir, W_OK) != 0) {
        std::fprintf(stderr, "monitor: cannot write to directory %s\n", dir);
        return 1;
    }

    const std::string base = dir;
    std::FILE* sys_csv = open_csv(base + "/system.csv",
                                  "timestamp,cpu_pct,mem_pct,swap_pct,disk_pct,load1\n");
    std::FILE* proc_csv = open_csv(base + "/processes.csv",
                                   "timestamp,pid,name,cpu_pct,mem_pct\n");
    if (sys_csv == nullptr || proc_csv == nullptr) {
        std::fprintf(stderr, "monitor: cannot open the CSV files in %s\n", dir);
        if (sys_csv != nullptr) std::fclose(sys_csv);
        if (proc_csv != nullptr) std::fclose(proc_csv);
        return 1;
    }

    install_signal_handlers();
    std::fprintf(stderr, "monitor: logging to %s every %d s\n", dir, interval);

    MemInfo mem;
    CpuTimes prev, cur;
    if (!read_meminfo(mem) || !read_cpu_times(prev)) {
        std::fprintf(stderr, "monitor: cannot read /proc\n");
        std::fclose(sys_csv);
        std::fclose(proc_csv);
        return 1;
    }
    ProcessSampler sampler;
    sampler.sample(0, mem.total_kb);       // baseline: CPU% needs two readings

    unsigned long long samples = 0;
    while (!g_stop) {
        wait_interval(interval);
        if (g_stop) break;

        if (!read_meminfo(mem) || !read_cpu_times(cur)) continue;   // skip a failed reading
        unsigned long long total_now = total_jiffies(cur);
        unsigned long long total_before = total_jiffies(prev);
        unsigned long long delta = total_now >= total_before ? total_now - total_before : 0;

        CpuUsage usage = compute_usage(prev, cur);
        double load1 = 0, load5 = 0, load15 = 0;
        read_loadavg(load1, load5, load15);
        std::vector<ProcInfo> procs = sampler.sample(delta, mem.total_kb);
        prev = cur;

        const std::string ts = timestamp_now();

        // TODO: replace this placeholder with disk_used_pct(...) once disk.cpp is written.
        const double disk_pct = 0.0;

        std::fprintf(sys_csv, "%s,%.1f,%.1f,%.1f,%.1f,%.1f\n", ts.c_str(), usage.total,
                     mem_used_pct(mem), swap_used_pct(mem), disk_pct, load1);
        std::fflush(sys_csv);

        // procs is already sorted by CPU%, so the first five are the top five.
        for (size_t i = 0; i < procs.size() && i < 5; ++i) {
            const ProcInfo& p = procs[i];
            std::fprintf(proc_csv, "%s,%d,%s,%.1f,%.1f\n", ts.c_str(), p.pid,
                         csv_safe(p.name).c_str(), p.cpu_pct, p.mem_pct);
        }
        std::fflush(proc_csv);

        ++samples;
    }

    std::fclose(sys_csv);
    std::fclose(proc_csv);
    std::fprintf(stderr, "monitor: stopped after %llu samples\n", samples);
    return 0;
}

int not_implemented(const char* what) {
    std::fprintf(stderr, "monitor: %s is not implemented yet\n", what);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) return not_implemented("interactive mode");

    const char* option = argv[1];
    if (std::strcmp(option, "--help") == 0) { print_usage(stdout); return 0; }
    if (std::strcmp(option, "--kill") == 0) return run_kill(argc, argv);
    if (std::strcmp(option, "--log") == 0) return run_log(argc, argv);
    if (std::strcmp(option, "--snapshot") == 0) return not_implemented("--snapshot");

    print_usage(stderr);
    return 2;
}
