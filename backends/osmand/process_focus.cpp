#include "process_focus.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace navicore_osmand {

namespace {

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool alive(long pid) {
    if (pid <= 0) return false;
    int status = 0;
    pid_t r = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
    return r == 0;  // 0: still running; the pid or a reaped child is "not alive"
}

} // namespace

ProcessFocus::ProcessFocus(Options options) : options_(std::move(options)) {
    idle_thread_ = std::thread([this] { idle_loop(); });
}

ProcessFocus::~ProcessFocus() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutting_down_ = true;
    }
    wake_.notify_all();
    if (idle_thread_.joinable()) idle_thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    stop_locked();
}

bool ProcessFocus::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return alive(pid_);
}

unsigned ProcessFocus::starts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return starts_;
}

bool ProcessFocus::request() {
    std::lock_guard<std::mutex> lock(mutex_);
    held_ = true;
    idle_deadline_ms_ = 0;
    if (pid_ > 0 && alive(pid_)) return true;
    pid_ = 0;
    if (start_locked()) return true;
    held_ = false;
    return false;
}

void ProcessFocus::release() {
    std::lock_guard<std::mutex> lock(mutex_);
    held_ = false;
    if (pid_ > 0) idle_deadline_ms_ = now_ms() + static_cast<long long>(options_.idle_ms);
    wake_.notify_all();
}

bool ProcessFocus::start_locked() {
    if (options_.argv.empty()) return false;
    int fds[2];
    if (pipe(fds) != 0) return false;

    pid_t child = fork();
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (child == 0) {
        // The helper may print after we stopped reading; do not die of SIGPIPE.
        signal(SIGPIPE, SIG_IGN);
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        if (fds[1] > STDERR_FILENO) close(fds[1]);
        for (const std::string& entry : options_.env) {
            std::size_t eq = entry.find('=');
            if (eq != std::string::npos)
                setenv(entry.substr(0, eq).c_str(), entry.c_str() + eq + 1, 1);
        }
        std::vector<char*> args;
        for (std::string& a : options_.argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execv(args[0], args.data());
        _exit(127);
    }
    close(fds[1]);
    ++starts_;

    // Read the helper's output until the ready marker, the helper dies, or time runs out.
    std::string seen;
    const long long deadline = now_ms() + static_cast<long long>(options_.ready_timeout_ms);
    bool ready = options_.ready_marker.empty();
    while (!ready) {
        long long left = deadline - now_ms();
        if (left <= 0) break;
        struct pollfd p = {fds[0], POLLIN, 0};
        int rc = poll(&p, 1, static_cast<int>(left < 100 ? left : 100));
        if (rc > 0 && (p.revents & (POLLIN | POLLHUP))) {
            char buffer[256];
            ssize_t n = read(fds[0], buffer, sizeof buffer);
            if (n > 0) {
                seen.append(buffer, static_cast<std::size_t>(n));
                if (seen.find(options_.ready_marker) != std::string::npos) ready = true;
            } else if (n == 0) {
                break;  // closed: the helper exited
            }
        }
        int status = 0;
        if (!ready && waitpid(child, &status, WNOHANG) == child) {
            close(fds[0]);
            std::fprintf(stderr, "voice: focus helper exited early: %s\n", seen.c_str());
            return false;
        }
    }
    close(fds[0]);
    if (!ready) {
        std::fprintf(stderr, "voice: focus helper not ready in %u ms: %s\n",
                     options_.ready_timeout_ms, seen.c_str());
        pid_ = child;
        stop_locked();
        return false;
    }
    pid_ = child;
    if (options_.settle_ms) usleep(options_.settle_ms * 1000u);
    return true;
}

void ProcessFocus::stop_locked() {
    if (pid_ <= 0) return;
    pid_t child = static_cast<pid_t>(pid_);
    kill(child, SIGTERM);
    int status = 0;
    bool reaped = false;
    for (int i = 0; i < 40 && !reaped; ++i) {  // up to 2 s for a clean exit
        reaped = waitpid(child, &status, WNOHANG) == child;
        if (!reaped) usleep(50 * 1000);
    }
    if (!reaped) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
    pid_ = 0;
}

void ProcessFocus::idle_loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!shutting_down_) {
        if (idle_deadline_ms_ == 0 || held_) {
            wake_.wait(lock);
            continue;
        }
        long long left = idle_deadline_ms_ - now_ms();
        if (left > 0) {
            wake_.wait_for(lock, std::chrono::milliseconds(left));
            continue;
        }
        stop_locked();
        idle_deadline_ms_ = 0;
    }
}

} // namespace navicore_osmand
