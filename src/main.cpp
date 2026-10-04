#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <string>
#include <sys/stat.h>
#include <termios.h>
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

// ---------- terminal control (interactive mode) ----------

// Puts the terminal into single-key mode and restores it on EVERY exit path, because the
// destructor runs however the function ends (return, break, early return).
class TerminalGuard {
public:
    TerminalGuard() {
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) return;
        struct termios raw = saved_;
        // No line buffering and no echo. ISIG stays on, so Ctrl+C still sends SIGINT.
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return;
        active_ = true;
        std::fputs("\033[?1049h\033[?25l", stdout);   // alternate screen, hide the cursor
        std::fflush(stdout);
    }

    ~TerminalGuard() {
        if (!active_) return;
        std::fputs("\033[?25h\033[?1049l", stdout);   // show the cursor, leave the alternate screen
        std::fflush(stdout);
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    }

    TerminalGuard(const TerminalGuard&) = delete;
    TerminalGuard& operator=(const TerminalGuard&) = delete;

    bool active() const { return active_; }

private:
    struct termios saved_ = {};
    bool active_ = false;
};

// Waits up to timeout_ms for one keypress. Returns 0 on timeout or when a signal interrupts
// the wait, and 'q' if the input is closed.
char read_key(int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ready = poll(&pfd, 1, timeout_ms);
    if (ready <= 0) return 0;
    char c = 0;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 0) return 'q';
    if (n < 0) return 0;
    return c;
}

// TEMPORARY: a plain frame so the terminal handling can be tested before ui.cpp exists.
// It is replaced by draw_screen() from ui.hpp once Teammate 3 has written it.
void draw_temp_frame(const std::vector<ProcInfo>& procs, double cpu, double mem,
                     const std::string& status) {
    std::printf("\033[H");                                   // cursor to the top left
    std::printf("Linux System Monitor (temporary view)\033[K\n");
    std::printf("CPU %.1f%%   Memory %.1f%%\033[K\n\033[K\n", cpu, mem);
    std::printf("%6s %7s %6s %-2s %s\033[K\n", "PID", "CPU%", "MEM%", "S", "COMMAND");
    for (size_t i = 0; i < procs.size() && i < 15; ++i) {
        const ProcInfo& p = procs[i];
        std::printf("%6d %7.1f %6.1f %-2c %.50s\033[K\n", p.pid, p.cpu_pct, p.mem_pct,
                    p.state, p.cmdline.c_str());
    }
    std::printf("\033[K\n[r] Refresh  [s] Search  [k] Kill  [q] Quit\033[K\n%s\033[K\n\033[J",
                status.c_str());
    std::fflush(stdout);
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

// ---------- interactive mode ----------

// Exit codes: 0 = normal quit, 1 = not a terminal or /proc could not be read.
int run_tui() {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::fprintf(stderr, "monitor: interactive mode needs a terminal\n");
        return 1;
    }
    install_signal_handlers();

    MemInfo mem;
    CpuTimes prev, cur;
    if (!read_meminfo(mem) || !read_cpu_times(prev)) {
        std::fprintf(stderr, "monitor: cannot read /proc\n");
        return 1;
    }
    ProcessSampler sampler;
    sampler.sample(0, mem.total_kb);       // baseline: CPU% needs two readings
    struct timespec first = {0, 500 * 1000 * 1000};
    nanosleep(&first, nullptr);

    TerminalGuard terminal;                // restores the terminal however this function ends
    if (!terminal.active()) {
        std::fprintf(stderr, "monitor: cannot set up the terminal\n");
        return 1;
    }

    std::string status = "ready";
    while (!g_stop) {
        if (read_meminfo(mem) && read_cpu_times(cur)) {
            unsigned long long total_now = total_jiffies(cur);
            unsigned long long total_before = total_jiffies(prev);
            unsigned long long delta = total_now >= total_before ? total_now - total_before : 0;
            CpuUsage usage = compute_usage(prev, cur);
            std::vector<ProcInfo> procs = sampler.sample(delta, mem.total_kb);
            prev = cur;
            draw_temp_frame(procs, usage.total, mem_used_pct(mem), status);
        }

        char key = read_key(2000);         // wait up to 2 s for a key, then refresh anyway
        if (key == 'q' || key == 'Q') break;
        if (key == 's' || key == 'k') status = "search and kill come in the next step";
        else if (key == 'r') status = "refreshed";
        else status = "";
    }
    return 0;
}

int not_implemented(const char* what) {
    std::fprintf(stderr, "monitor: %s is not implemented yet\n", what);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) return run_tui();

    const char* option = argv[1];
    if (std::strcmp(option, "--help") == 0) { print_usage(stdout); return 0; }
    if (std::strcmp(option, "--kill") == 0) return run_kill(argc, argv);
    if (std::strcmp(option, "--log") == 0) return run_log(argc, argv);
    if (std::strcmp(option, "--snapshot") == 0) return not_implemented("--snapshot");

    print_usage(stderr);
    return 2;
}
