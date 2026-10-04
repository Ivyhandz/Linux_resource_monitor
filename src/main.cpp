#include <cctype>
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

// Removes spaces and tabs from both ends of a string.
std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
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
        raw_ = saved_;
        // No line buffering and no echo. ISIG stays on, so Ctrl+C still sends SIGINT.
        raw_.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
        raw_.c_cc[VMIN] = 1;
        raw_.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_) != 0) return;
        active_ = true;
        std::fputs("\033[?1049h\033[?25l", stdout);   // alternate screen, hide the cursor
        std::fflush(stdout);
    }

    ~TerminalGuard() {
        if (!active_) return;
        std::fputs("\033[?25h\033[?1049l", stdout);   // show the cursor, leave the alternate screen
        std::fflush(stdout);
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_);    // always back to the ORIGINAL settings
    }

    TerminalGuard(const TerminalGuard&) = delete;
    TerminalGuard& operator=(const TerminalGuard&) = delete;

    bool active() const { return active_; }

    // Normal line mode with echo and a visible cursor, for typing the answer to a prompt.
    void to_normal_mode() {
        if (!active_) return;
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        std::fputs("\033[?25h", stdout);
        std::fflush(stdout);
    }

    // Back to single-key mode. TCSAFLUSH discards any keys typed in the meantime.
    void to_raw_mode() {
        if (!active_) return;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_);
        std::fputs("\033[?25l", stdout);
        std::fflush(stdout);
    }

private:
    struct termios saved_ = {};
    struct termios raw_ = {};
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

// Reads one line in normal mode (the terminal itself handles Backspace). A signal such as
// Ctrl+C interrupts the read, and the main loop then stops because g_stop is set.
std::string read_line() {
    std::string line;
    char c = 0;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n') line.push_back(c);
    return line;
}

// Shows a prompt on the bottom row, reads the answer in normal mode, and returns to
// single-key mode. The answer comes back with surrounding spaces removed.
std::string ask(TerminalGuard& terminal, const char* prompt) {
    terminal.to_normal_mode();
    std::printf("\033[999;1H\033[K%s", prompt);
    std::fflush(stdout);
    std::string answer = read_line();
    terminal.to_raw_mode();
    return trim(answer);
}

// TEMPORARY: a plain frame so the interactive mode can be tested before ui.cpp exists.
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

// The k key: asks for a PID, a signal and a confirmation, then calls send_signal().
// The result goes into `status`, which the next screen refresh shows.
void handle_kill(TerminalGuard& terminal, const std::vector<ProcInfo>& procs,
                 std::string& status) {
    std::string pid_text = ask(terminal, "Kill which PID? (blank to cancel): ");
    if (pid_text.empty()) { status = "kill cancelled"; return; }

    int pid = 0;
    if (!parse_pid(pid_text.c_str(), pid)) { status = "not a valid PID"; return; }

    std::vector<ProcInfo> found = find_processes(procs, std::to_string(pid));
    if (found.empty()) { status = "PID " + std::to_string(pid) + " is not in the process list"; return; }

    std::string choice = ask(terminal, "Signal: t = TERM (default), k = KILL: ");
    int sig = SIGTERM;
    const char* sig_name = "TERM";
    if (choice == "k" || choice == "K") {
        sig = SIGKILL;
        sig_name = "KILL";
    } else if (!choice.empty() && choice != "t" && choice != "T") {
        status = "unknown signal choice, kill cancelled";
        return;
    }

    std::string question = std::string("Send ") + sig_name + " to " + std::to_string(pid) +
                           " (" + found[0].name + ")? [y/N]: ";
    std::string answer = ask(terminal, question.c_str());
    if (answer != "y" && answer != "Y") { status = "kill cancelled"; return; }

    SignalResult result = send_signal(pid, sig);
    status = result.message + " (PID " + std::to_string(pid) + ")";
}

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

    std::vector<ProcInfo> procs;           // the latest sample
    std::string filter;                    // the active search; empty means show everything
    std::string status = "ready";
    double cpu_total = 0.0, mem_pct = 0.0;

    while (!g_stop) {
        if (read_meminfo(mem) && read_cpu_times(cur)) {
            unsigned long long total_now = total_jiffies(cur);
            unsigned long long total_before = total_jiffies(prev);
            unsigned long long delta = total_now >= total_before ? total_now - total_before : 0;
            cpu_total = compute_usage(prev, cur).total;
            mem_pct = mem_used_pct(mem);
            procs = sampler.sample(delta, mem.total_kb);
            prev = cur;
        }

        std::vector<ProcInfo> view = filter.empty() ? procs : find_processes(procs, filter);
        std::string line = status;
        if (!filter.empty()) {
            line = "search '" + filter + "': " + std::to_string(view.size()) +
                   " match(es), press r to clear";
            if (!status.empty()) line += "  |  " + status;
        }
        draw_temp_frame(view, cpu_total, mem_pct, line);

        char key = read_key(2000);         // wait up to 2 s for a key, then refresh anyway
        if (key == 'q' || key == 'Q') break;
        if (key == 'r' || key == 'R') {
            filter.clear();
            status = "refreshed";
        } else if (key == 's' || key == 'S') {
            filter = ask(terminal, "Search PID or name (blank clears): ");
            status = filter.empty() ? "search cleared" : "";
        } else if (key == 'k' || key == 'K') {
            handle_kill(terminal, procs, status);
        } else if (key != 0) {
            status = "";                   // a timeout keeps the message; any other key clears it
        }
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
