#include "tf.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

namespace tf {

bool verbose = false;
void (*failContext)() = nullptr;

static int g_pipe = -1;
static std::string g_case;
static int c_pass = 0;
static int c_fail = 0;

static int t_pass = 0;
static int t_fail = 0;
static std::vector<std::string> t_failures;
static std::vector<std::string> t_observations;
static std::string g_suite;

std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list a;
    va_start(a, f);
    vsnprintf(buf, sizeof(buf), f, a);
    va_end(a);
    return buf;
}

static void sendLine(char kind, const std::string& msg) {
    if (g_pipe < 0) return;
    std::string line(1, kind);
    line += ':';
    for (char c : msg) line += (c == '\n' ? ' ' : c);
    line += '\n';
    ssize_t r = write(g_pipe, line.data(), line.size());
    (void)r;
}

void check(bool cond, const std::string& desc) {
    if (cond) {
        c_pass++;
        if (verbose) printf("    [PASS] %s\n", desc.c_str());
        sendLine('P', desc);
    } else {
        c_fail++;
        printf("    [FAIL] %s\n", desc.c_str());
        if (failContext) failContext();
        sendLine('F', g_case + " :: " + desc);
    }
}

void observe(const std::string& msg) {
    printf("    [OBS]  %s\n", msg.c_str());
    sendLine('O', g_case + " :: " + msg);
}

void info(const std::string& msg) {
    printf("    [INFO] %s\n", msg.c_str());
}

void suite(const std::string& name) {
    g_suite = name;
    printf("\n==================== %s ====================\n", name.c_str());
}

void run(const std::string& name, const std::function<void()>& fn) {
    fflush(stdout);
    int fds[2];
    if (pipe(fds) != 0) { perror("pipe"); return; }
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        g_pipe = fds[1];
        g_case = g_suite + " / " + name;
        printf("  - %s\n", name.c_str());
        fn();
        printf("    => %d pass, %d fail\n", c_pass, c_fail);
        fflush(stdout);
        close(g_pipe);
        _exit(0);
    }
    close(fds[1]);
    std::string data;
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) data.append(buf, (size_t)n);
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);

    size_t pos = 0;
    while (pos < data.size()) {
        size_t e = data.find('\n', pos);
        if (e == std::string::npos) break;
        std::string line = data.substr(pos, e - pos);
        pos = e + 1;
        if (line.size() < 2) continue;
        char k = line[0];
        std::string msg = line.substr(2);
        if (k == 'P') t_pass++;
        else if (k == 'F') { t_fail++; t_failures.push_back(msg); }
        else if (k == 'O') t_observations.push_back(msg);
    }
    if (WIFSIGNALED(status)) {
        t_fail++;
        t_failures.push_back(g_suite + " / " + name + " :: CRASH signal " + std::to_string(WTERMSIG(status)));
        printf("    [FAIL] CRASH signal %d\n", WTERMSIG(status));
    } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        t_fail++;
        t_failures.push_back(g_suite + " / " + name + " :: exit code " + std::to_string(WEXITSTATUS(status)));
    }
}

int summary() {
    printf("\n\n######################## SUMMARY ########################\n");
    printf("PASS: %d   FAIL: %d   OBSERVATIONS: %zu\n", t_pass, t_fail, t_observations.size());
    if (!t_failures.empty()) {
        printf("\n--- FAILURES ---\n");
        for (const auto& f : t_failures) printf("  x %s\n", f.c_str());
    }
    if (!t_observations.empty()) {
        printf("\n--- OBSERVATIONS ---\n");
        for (const auto& o : t_observations) printf("  * %s\n", o.c_str());
    }
    printf("#########################################################\n");
    return t_fail;
}

}
