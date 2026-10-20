#include "rhvoice_engine.h"

#include <cstdio>
#include <cstring>

#include "RHVoice.h"

namespace navicore_osmand {

namespace {

struct Sink {
    std::vector<short>* pcm = nullptr;
    unsigned rate = 0;
};

int on_rate(int rate, void* user) {
    static_cast<Sink*>(user)->rate = static_cast<unsigned>(rate);
    return 1;
}

int on_speech(const short* samples, unsigned int count, void* user) {
    Sink* sink = static_cast<Sink*>(user);
    sink->pcm->insert(sink->pcm->end(), samples, samples + count);
    return 1;
}

// Engine-wide callbacks receive the per-message user data given to
// RHVoice_new_message, so one engine can serve any sink.
} // namespace

RhVoiceEngine::~RhVoiceEngine() {
    if (engine_) RHVoice_delete_tts_engine(static_cast<RHVoice_tts_engine>(engine_));
}

bool RhVoiceEngine::ensure_engine() {
    if (engine_) return true;
    if (failed_) return false;
    RHVoice_init_params params;
    std::memset(&params, 0, sizeof params);
    params.data_path = data_path_.c_str();
    params.config_path = data_path_.c_str();
    params.callbacks.set_sample_rate = on_rate;
    params.callbacks.play_speech = on_speech;
    RHVoice_tts_engine engine = RHVoice_new_tts_engine(&params);
    if (!engine) {
        std::fprintf(stderr, "voice: RHVoice engine failed to load (data=%s)\n", data_path_.c_str());
        failed_ = true;
        return false;
    }
    engine_ = engine;
    return true;
}

bool RhVoiceEngine::synthesize(const std::string& text, ui::loc::Language language,
                               std::vector<short>& pcm, unsigned& sample_rate_hz) {
    if (language != ui::loc::Language::Ukrainian || text.empty()) return false;
    if (!ensure_engine()) return false;
    RHVoice_synth_params synth;
    std::memset(&synth, 0, sizeof synth);
    synth.voice_profile = voice_.c_str();
    synth.relative_rate = rate_percent_ / 100.0;
    synth.relative_pitch = 1.0;
    synth.relative_volume = 1.0;
    synth.punctuation_mode = RHVoice_punctuation_default;
    synth.capitals_mode = RHVoice_capitals_default;

    pcm.clear();
    Sink sink;
    sink.pcm = &pcm;
    RHVoice_message message =
        RHVoice_new_message(static_cast<RHVoice_tts_engine>(engine_), text.c_str(), text.size(),
                            RHVoice_message_text, &synth, &sink);
    if (!message) return false;
    int ok = RHVoice_speak(message);
    RHVoice_delete_message(message);
    sample_rate_hz = sink.rate;
    return ok != 0 && !pcm.empty() && sink.rate != 0;
}

} // namespace navicore_osmand
