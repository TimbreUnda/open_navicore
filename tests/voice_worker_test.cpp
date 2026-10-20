// Voice worker: ordering, supersession, preemption, failure accounting, shutdown.
// Engine and audio are fakes; gates make the interleavings deterministic.
#include "voice_worker.h"

#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

using namespace navicore_osmand;
using ui::loc::Language;

namespace {

// A one-shot gate: the worker thread announces it arrived, then waits for open().
struct Gate {
    std::mutex m;
    std::condition_variable cv;
    bool armed = false, arrived = false, opened = false;
    void arm() { std::lock_guard<std::mutex> l(m); armed = true; arrived = opened = false; }
    void pass() {
        std::unique_lock<std::mutex> l(m);
        if (!armed) return;
        arrived = true;
        cv.notify_all();
        cv.wait(l, [this] { return opened; });
        armed = false;
    }
    void wait_arrived() { std::unique_lock<std::mutex> l(m); cv.wait(l, [this] { return arrived; }); }
    void open() { std::lock_guard<std::mutex> l(m); opened = true; cv.notify_all(); }
};

struct FakeEngine : TtsEngine {
    Gate gate;
    std::mutex m;
    std::vector<std::string> texts;
    bool fail = false;
    bool synthesize(const std::string& text, Language, std::vector<short>& pcm,
                    unsigned& rate) override {
        gate.pass();
        { std::lock_guard<std::mutex> l(m); texts.push_back(text); }
        if (fail) return false;
        pcm.assign(VoiceWorker::kChunkSamples * 3, 100);
        rate = 24000;
        return true;
    }
};

struct FakeAudio : AudioOut {
    Gate gate;  // held on the first write
    std::mutex m;
    std::vector<std::string> log;
    bool refuse = false, fail_write = false;
    unsigned acquires = 0, releases = 0, writes = 0;
    bool acquire() override {
        std::lock_guard<std::mutex> l(m);
        if (refuse) return false;
        ++acquires;
        return true;
    }
    bool write(const short*, std::size_t, unsigned) override {
        gate.pass();
        std::lock_guard<std::mutex> l(m);
        ++writes;
        return !fail_write;
    }
    void release() override { std::lock_guard<std::mutex> l(m); ++releases; }
};

SpokenPrompt prompt(const char* text, PromptPriority priority = PromptPriority::Maneuver) {
    return {text, priority, text};
}

void basic() {
    FakeEngine e;
    FakeAudio a;
    VoiceWorker w(e, a);
    assert(w.start());
    assert(!w.start());
    w.enqueue(prompt("one"));
    w.wait_idle();
    VoiceStats s = w.stats();
    assert(s.spoken == 1 && s.dropped == 0);
    assert(a.acquires == 1 && a.releases == 1 && a.writes == 3);
    w.stop();
    w.enqueue(prompt("ignored"));
    w.stop();  // idempotent
}

void newer_maneuver_replaces_older() {
    FakeEngine e;
    FakeAudio a;
    VoiceWorker w(e, a);
    w.start();
    e.gate.arm();
    w.enqueue(prompt("m1"));
    e.gate.wait_arrived();       // m1 is being synthesized
    w.enqueue(prompt("m2"));     // supersedes m1 (cuts it) ...
    w.enqueue(prompt("m3"));     // ... and replaces m2 in the queue
    e.gate.open();
    w.wait_idle();
    VoiceStats s = w.stats();
    assert(s.spoken == 1);       // only m3
    assert(s.dropped == 2);      // m1 cut, m2 replaced
    assert(e.texts.size() == 2 && e.texts[0] == "m1" && e.texts[1] == "m3");
    w.stop();
}

void arrival_cuts_a_maneuver_prompt_and_goes_first() {
    FakeEngine e;
    FakeAudio a;
    VoiceWorker w(e, a);
    w.start();
    a.gate.arm();
    w.enqueue(prompt("turn"));
    a.gate.wait_arrived();       // "turn" is playing
    w.enqueue(prompt("arrived", PromptPriority::Arrival));
    a.gate.open();
    w.wait_idle();
    VoiceStats s = w.stats();
    assert(s.spoken == 1 && s.dropped == 1);
    assert(e.texts.back() == "arrived");
    assert(a.acquires == a.releases);  // focus always given back
    w.stop();
}

void reroute_is_not_replaced_by_maneuver() {
    FakeEngine e;
    FakeAudio a;
    VoiceWorker w(e, a);
    w.start();
    e.gate.arm();
    w.enqueue(prompt("first"));
    e.gate.wait_arrived();
    w.enqueue(prompt("reroute", PromptPriority::Reroute));
    w.enqueue(prompt("turn"));   // must not remove the reroute prompt
    e.gate.open();
    w.wait_idle();
    assert(e.texts.size() == 3);  // first (cut), reroute, turn
    assert(e.texts[1] == "reroute" && e.texts[2] == "turn");
    assert(w.stats().spoken == 2);
    w.stop();
}

void failures_are_counted() {
    FakeEngine e;
    FakeAudio a;
    VoiceWorker w(e, a);
    w.start();
    e.fail = true;
    w.enqueue(prompt("a"));
    w.wait_idle();
    e.fail = false;
    a.refuse = true;
    w.enqueue(prompt("b"));
    w.wait_idle();
    a.refuse = false;
    a.fail_write = true;
    w.enqueue(prompt("c"));
    w.wait_idle();
    VoiceStats s = w.stats();
    assert(s.synth_failed == 1 && s.acquire_failed == 1 && s.write_failed == 1 && s.spoken == 0);
    assert(a.acquires == a.releases);
    w.stop();
}

void cancel_all_cuts_current_and_clears() {
    FakeEngine e;
    FakeAudio a;
    VoiceWorker w(e, a);
    w.start();
    a.gate.arm();
    w.enqueue(prompt("x"));
    a.gate.wait_arrived();
    w.cancel_all();
    a.gate.open();
    w.wait_idle();
    assert(w.stats().spoken == 0 && w.stats().dropped == 1);
    w.stop();
}

void stop_while_speaking_joins() {
    FakeEngine e;
    FakeAudio a;
    VoiceWorker w(e, a);
    w.start();
    a.gate.arm();
    w.enqueue(prompt("long"));
    a.gate.wait_arrived();
    a.gate.open();               // let the write return, then stop
    w.stop();
    assert(a.acquires == a.releases);
}

} // namespace

int main() {
    basic();
    newer_maneuver_replaces_older();
    arrival_cuts_a_maneuver_prompt_and_goes_first();
    reroute_is_not_replaced_by_maneuver();
    failures_are_counted();
    cancel_all_cuts_current_and_clears();
    stop_while_speaking_joins();
    std::puts("voice_worker_test ok");
    return 0;
}
