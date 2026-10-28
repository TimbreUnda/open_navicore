#pragma once

#include "voice_worker.h"

#include <map>
#include <string>
#include <vector>

namespace navicore_osmand {

// Offline sentence lookup and PCM assembly. A missing recording fails visibly;
// selecting RHVoice instead is an explicit daemon option, never a fallback.
class RecordedVoiceEngine : public TtsEngine {
public:
    RecordedVoiceEngine(std::string directory, std::string voice)
        : directory_(std::move(directory)), voice_(std::move(voice)) {}
    bool initialize();
    bool synthesize(const std::string& text, ui::loc::Language language,
                    std::vector<short>& pcm, unsigned& sample_rate_hz) override;

private:
    bool read_clip(const std::string& id, std::vector<short>& pcm);
    std::string directory_, voice_;
    std::map<std::string, std::vector<std::string>> recipes_;
    bool attempted_ = false, ready_ = false;
};

} // namespace navicore_osmand
