#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "process.hpp"

namespace {

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
    if (std::strcmp(option, "--snapshot") == 0) return not_implemented("--snapshot");
    if (std::strcmp(option, "--log") == 0) return not_implemented("--log");

    print_usage(stderr);
    return 2;
}
