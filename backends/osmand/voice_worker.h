#pragma once

// Speaks SpokenPrompts on a worker thread so synthesis and PCM writes never run on
// the render thread. The speech engine and the audio output are interfaces: the
// QNX build binds them to the TTS library and to audio focus + io-audio
// (docs/design/voice-guidance-wiring.md), the tests bind fakes.
//
// Queue rules: bounded; a newer Maneuver prompt replaces queued older Maneuver
// prompts (stale directions are worse than none); Reroute and Arrival outrank
// Maneuver and are never dropped for it; when full the oldest lowest-priority
// entry goes. A prompt that outranks the one being spoken cuts it short.

#include "voice_prompts.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace navicore_osmand {

// PCM samples are plain `short` (QNX's int16_t carries an attribute that the
// project's -Werror rejects inside std::vector).
static_assert(sizeof(short) == 2, "PCM sample must be 16 bits");

class TtsEngine {
public:
    virtual ~TtsEngine() = default;
    // Mono signed 16-bit PCM for `text`; false when the language has no voice.
    virtual bool synthesize(const std::string& text, ui::loc::Language language,
                            std::vector<short>& pcm, unsigned& sample_rate_hz) = 0;
};

class AudioOut {
public:
    virtual ~AudioOut() = default;
    // Obtain audio focus for the navigation source and open the PCM device.
    virtual bool acquire() = 0;
    // Write samples; returns false on a device error. Called in short chunks so
    // the worker can stop between them.
    virtual bool write(const short* samples, std::size_t count, unsigned sample_rate_hz) = 0;
    // Drain, close the device, give focus back.
    virtual void release() = 0;
};

struct VoiceStats {
    unsigned spoken = 0;
    unsigned dropped = 0;       // replaced, evicted or cut short
    unsigned synth_failed = 0;
    unsigned acquire_failed = 0;
    unsigned write_failed = 0;
};

class VoiceWorker {
public:
    static constexpr std::size_t kCapacity = 4;
    static constexpr std::size_t kChunkSamples = 2400;  // 100 ms at 24 kHz

    VoiceWorker(TtsEngine& engine, AudioOut& audio) : engine_(engine), audio_(audio) {}
    ~VoiceWorker() { stop(); }
    VoiceWorker(const VoiceWorker&) = delete;
    VoiceWorker& operator=(const VoiceWorker&) = delete;

    bool start();   // false if already running
    void stop();    // finishes nothing new, joins the thread; idempotent

    void set_language(ui::loc::Language language);
    void enqueue(SpokenPrompt prompt);
    // Drop everything queued and cut the current prompt (route cancelled, mute).
    void cancel_all();
    // Block until the queue is empty and nothing is speaking (tests, shutdown).
    void wait_idle();

    VoiceStats stats() const;

private:
    void run();
    bool should_cut(PromptPriority speaking) const;  // lock held

    TtsEngine& engine_;
    AudioOut& audio_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<SpokenPrompt> queue_;
    std::thread thread_;
    bool running_ = false;
    bool stopping_ = false;
    bool speaking_ = false;
    bool cut_ = false;
    PromptPriority speaking_priority_ = PromptPriority::Maneuver;
    ui::loc::Language language_ = ui::loc::Language::Ukrainian;
    VoiceStats stats_;
};

} // namespace navicore_osmand
