#pragma once

// TtsEngine backed by RHVoice (Ukrainian voices Anatol, Volodymyr, Natalia,
// Marianna; data under `data_path`, see uk-voice/tts/). The engine is created on
// the first synthesis, on the worker thread, and kept: loading costs about 1.8 s
// on the radio, so doing it per prompt would delay every announcement.
//
// Only built where RHVoice.h is available (ONC_WITH_RHVOICE); the radio data set
// holds Ukrainian only, so other languages return false and stay silent.

#include "voice_worker.h"

#include <string>

namespace navicore_osmand {

class RhVoiceEngine : public TtsEngine {
public:
    RhVoiceEngine(std::string data_path, std::string voice_profile)
        : data_path_(std::move(data_path)), voice_(std::move(voice_profile)) {}
    ~RhVoiceEngine() override;

    bool synthesize(const std::string& text, ui::loc::Language language,
                    std::vector<short>& pcm, unsigned& sample_rate_hz) override;

    // Speaking speed, percent of the voice's default (100 = normal). Guidance is
    // short and time-critical, so a slightly faster setting is reasonable.
    void set_rate_percent(int percent) { rate_percent_ = percent < 50 ? 50 : (percent > 200 ? 200 : percent); }

private:
    bool ensure_engine();

    std::string data_path_;
    std::string voice_;
    void* engine_ = nullptr;   // RHVoice_tts_engine, opaque to keep this header light
    bool failed_ = false;      // do not retry a failed load for every prompt
    int rate_percent_ = 100;
};

} // namespace navicore_osmand
