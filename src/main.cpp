#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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

// ---------- --log (skeleton: the loop and the clean shutdown; CSV comes next) ----------

// --log <dir> [--interval <sec>]
// Exit codes: 0 = clean stop, 1 = directory missing or not writable, 2 = bad usage.
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

    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || access(dir, W_OK) != 0) {
        std::fprintf(stderr, "monitor: cannot write to directory %s\n", dir);
        return 1;
    }

    install_signal_handlers();
    std::fprintf(stderr, "monitor: logging to %s every %d s\n", dir, interval);

    MemInfo mem;
    CpuTimes prev, cur;
    if (!read_meminfo(mem) || !read_cpu_times(prev)) {
        std::fprintf(stderr, "monitor: cannot read /proc\n");
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

        std::vector<ProcInfo> procs = sampler.sample(delta, mem.total_kb);
        prev = cur;
        ++samples;
        std::fprintf(stderr, "monitor: sample %llu (%zu processes)\n", samples, procs.size());
    }

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
