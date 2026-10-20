// ProcessFocus: shared helper lifetime, idle shutdown, restart, failure paths.
// The helper is a shell one-liner standing in for as_alloc.
#include "process_focus.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace navicore_osmand;

namespace {

ProcessFocus::Options helper(const char* script, const char* marker, unsigned idle_ms = 200) {
    ProcessFocus::Options o;
    o.argv = {"/bin/sh", "-c", script};
    o.ready_marker = marker;
    o.ready_timeout_ms = 1500;
    o.idle_ms = idle_ms;
    o.settle_ms = 0;
    return o;
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void shared_until_idle_then_restart() {
    ProcessFocus f(helper("echo phase=allocate-sent status=0; exec sleep 30", "allocate-sent"));
    assert(!f.running());
    assert(f.request());
    assert(f.running() && f.starts() == 1);
    f.release();
    assert(f.request());               // within the idle window: same helper
    assert(f.starts() == 1);
    f.release();
    sleep_ms(700);                     // idle window (200 ms) elapsed
    assert(!f.running());
    assert(f.request());
    assert(f.starts() == 2);
    f.release();
}

void held_focus_is_not_stopped() {
    ProcessFocus f(helper("echo ready; exec sleep 30", "ready", 100));
    assert(f.request());
    sleep_ms(500);                     // longer than idle_ms, but still held
    assert(f.running());
    f.release();
}

void never_ready() {
    ProcessFocus::Options o = helper("exec sleep 30", "ready");
    o.ready_timeout_ms = 300;
    ProcessFocus f(o);
    assert(!f.request());
    assert(!f.running());
}

void exits_early() {
    ProcessFocus f(helper("echo boom; exit 3", "ready"));
    assert(!f.request());
    assert(!f.running());
}

void missing_executable() {
    ProcessFocus::Options o;
    o.argv = {"/nonexistent/helper"};
    o.ready_marker = "ready";
    o.ready_timeout_ms = 500;
    ProcessFocus f(o);
    assert(!f.request());
}

void destructor_stops_helper() {
    long pid_alive;
    {
        ProcessFocus f(helper("echo ready; exec sleep 30", "ready"));
        assert(f.request());
        assert(f.running());
        pid_alive = 1;
    }
    assert(pid_alive == 1);  // reaching here means the destructor joined and stopped the helper
}

} // namespace

int main() {
    shared_until_idle_then_restart();
    held_focus_is_not_stopped();
    never_ready();
    exits_early();
    missing_executable();
    destructor_stops_helper();
    std::puts("process_focus_test ok");
    return 0;
}
