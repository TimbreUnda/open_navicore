#pragma once

// AudioFocus held by a helper process. The bench route is `as_alloc`
// (uk-voice/tts/test/as_alloc.cc): it opens a PAS session to as_audio_service,
// sends AllocateChannel and keeps the allocation while it lives. This class
// starts such a command on request(), waits for a ready line on its stdout,
// and stops it again after the focus has been idle for `idle_ms`, so consecutive
// prompts share one allocation instead of paying the session setup each time.
//
//   argv: {"/tmp/tts/as_alloc", "2", "8", "1", "3600"}   ready: "allocate-sent status=0"
//
// The command must keep running until it is killed; SIGTERM is sent on idle
// expiry or destruction (SIGKILL after a grace period).

#include "qnx_pcm_out.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace navicore_osmand {

class ProcessFocus : public AudioFocus {
public:
    struct Options {
        std::vector<std::string> argv;       // argv[0] is the executable path
        std::vector<std::string> env;        // extra "NAME=value" entries
        std::string ready_marker;            // substring of a stdout line
        unsigned ready_timeout_ms = 4000;
        unsigned idle_ms = 8000;
        unsigned settle_ms = 300;            // pause after the marker: DSP route in place
    };

    explicit ProcessFocus(Options options);
    ~ProcessFocus() override;

    bool request() override;   // true once the helper reported ready
    void release() override;   // starts the idle countdown; does not stop it yet

    bool running() const;      // helper process alive (tests, diagnostics)
    unsigned starts() const;   // how many times the helper was launched

private:
    bool start_locked();
    void stop_locked();
    void idle_loop();

    Options options_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread idle_thread_;
    long pid_ = 0;          // pid_t, kept as long so this header needs no <sys/types.h>
    bool held_ = false;     // request() granted and not yet released
    bool shutting_down_ = false;
    long long idle_deadline_ms_ = 0;  // 0: no countdown
    unsigned starts_ = 0;
};

} // namespace navicore_osmand
