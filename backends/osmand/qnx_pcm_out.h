#pragma once

// AudioOut for the radio: QNX io-audio playback, gated by audio focus.
// The PCM device is opened on the first write because the sample rate is only
// known after synthesis (eSpeak 22.05 kHz, RHVoice 24 kHz; io-audio resamples).
// Configuration follows the proven `uk-voice/tts/test/tts_play.c` (block mode,
// interleaved S16_LE, one voice).

#include "voice_worker.h"

#include <string>

namespace navicore_osmand {

// Who allows us to be heard. The stock route requests the NAVI source through
// SI_AudioHandling (slots 6/8, docs/design/voice-guidance-wiring.md); the bench
// route sends AllocateChannel (uk-voice/tts/test/as_alloc.cc).
class AudioFocus {
public:
    virtual ~AudioFocus() = default;
    virtual bool request() = 0;
    virtual bool valid() { return true; }
    virtual void release() = 0;
};

// Focus that is always granted (a source that is already allocated, bench use).
class AlwaysFocus : public AudioFocus {
public:
    bool request() override { return true; }
    void release() override {}
};

class QnxPcmOut : public AudioOut {
public:
    // `device` is a snd_pcm_open_name() name: "pcmC7D0p" (what nv_navigation opens).
    QnxPcmOut(std::string device, AudioFocus& focus) : device_(std::move(device)), focus_(focus) {}
    ~QnxPcmOut() override { release(); }

    bool acquire() override;
    bool write(const short* samples, std::size_t count, unsigned sample_rate_hz) override;
    void release() override;

private:
    bool open_device(unsigned sample_rate_hz);
    void close_device();

    std::string device_;
    AudioFocus& focus_;
    void* handle_ = nullptr;   // snd_pcm_t*, kept opaque so this header stays portable
    unsigned rate_ = 0;
    unsigned fragment_ = 4096;
    bool focused_ = false;
};

} // namespace navicore_osmand
