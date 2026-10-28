#include "recorded_voice_engine.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

namespace navicore_osmand {
namespace {
constexpr unsigned kSampleRate = 24000;
constexpr std::size_t kGapSamples = 480;
constexpr std::size_t kMaxClipBytes = 480000; // ten seconds, mono PCM16

std::string key(std::string text) {
    while (!text.empty() && (text.back() == '.' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

bool clip_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

unsigned le16(const unsigned char* p) { return p[0] | (unsigned(p[1]) << 8); }
unsigned le32(const unsigned char* p) {
    return le16(p) | (le16(p + 2) << 16);
}
bool tag(const unsigned char* p, const char* s) {
    return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3];
}
} // namespace

bool RecordedVoiceEngine::initialize() {
    if (attempted_) return ready_;
    attempted_ = true;
    if (voice_ != "male" && voice_ != "female") {
        std::fprintf(stderr, "voice: recorded voice must be male or female, got %s\n", voice_.c_str());
        return false;
    }
    std::ifstream file(directory_ + "/recipes.tsv");
    std::string line;
    if (!std::getline(file, line) || line != "NAVI-RECORDED-1\t24000\t480") {
        std::fprintf(stderr, "voice: missing/invalid recorded manifest %s/recipes.tsv\n", directory_.c_str());
        return false;
    }
    std::map<std::string, std::vector<std::string>> parsed;
    bool malformed = false;
    while (std::getline(file, line)) {
        const auto tab = line.find('\t');
        if (line.size() > 1024 || tab == std::string::npos || tab == 0 || parsed.size() >= 8192) {
            malformed = true; break;
        }
        std::string text = key(line.substr(0, tab));
        std::istringstream fields(line.substr(tab + 1));
        std::vector<std::string> ids;
        std::string id;
        bool valid = true;
        while (std::getline(fields, id, ',')) {
            if (!clip_id(id) || ids.size() >= 3) { valid = false; break; }
            ids.push_back(id);
        }
        if (!valid || ids.empty() || line.back() == ',' || !parsed.emplace(text, ids).second) {
            malformed = true; break;
        }
    }
    if (malformed || !file.eof() || parsed.empty()) {
        std::fprintf(stderr, "voice: malformed recorded manifest %s/recipes.tsv\n", directory_.c_str());
        return false;
    }
    recipes_ = std::move(parsed);
    ready_ = true;
    std::fprintf(stderr, "voice: recorded pack ready voice=%s recipes=%zu directory=%s\n",
                 voice_.c_str(), recipes_.size(), directory_.c_str());
    return true;
}

bool RecordedVoiceEngine::read_clip(const std::string& id, std::vector<short>& pcm) {
    const std::string path = directory_ + "/" + voice_ + "/clips/" + id + ".wav";
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const std::streamoff length = file ? static_cast<std::streamoff>(file.tellg()) : 0;
    if (length < 44 || length > static_cast<std::streamoff>(kMaxClipBytes + 4096)) {
        std::fprintf(stderr, "voice: missing/invalid recorded clip %s\n", path.c_str());
        return false;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) return false;
    bool format = false;
    const unsigned char* data = nullptr;
    std::size_t count = 0;
    bool valid = tag(bytes.data(), "RIFF") && tag(bytes.data() + 8, "WAVE") &&
                 le32(bytes.data() + 4) == bytes.size() - 8;
    std::size_t offset = 12;
    while (valid && offset + 8 <= bytes.size()) {
        const auto* chunk = bytes.data() + offset;
        const std::size_t size = le32(chunk + 4);
        offset += 8;
        if (size > bytes.size() - offset) { valid = false; break; }
        if (tag(chunk, "fmt ")) {
            format = size >= 16 && le16(bytes.data() + offset) == 1 &&
                le16(bytes.data() + offset + 2) == 1 && le32(bytes.data() + offset + 4) == kSampleRate &&
                le32(bytes.data() + offset + 8) == kSampleRate * 2 &&
                le16(bytes.data() + offset + 12) == 2 && le16(bytes.data() + offset + 14) == 16;
            if (!format) valid = false;
        } else if (tag(chunk, "data")) {
            if (data || size == 0 || size % 2 || size > kMaxClipBytes) { valid = false; break; }
            data = bytes.data() + offset;
            count = size / 2;
        }
        offset += size + (size & 1);
    }
    if (!valid || !format || !data || offset != bytes.size()) {
        std::fprintf(stderr, "voice: recorded clip must be 24000 Hz mono PCM16 WAV: %s\n", path.c_str());
        return false;
    }
    pcm.reserve(pcm.size() + count);
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned value = le16(data + 2 * i);
        pcm.push_back(static_cast<short>(value < 32768 ? static_cast<int>(value) : static_cast<int>(value) - 65536));
    }
    return true;
}

bool RecordedVoiceEngine::synthesize(const std::string& text, ui::loc::Language language,
                                     std::vector<short>& pcm, unsigned& sample_rate_hz) {
    pcm.clear();
    sample_rate_hz = 0;
    if (language != ui::loc::Language::Ukrainian || !initialize()) return false;
    auto recipe = recipes_.find(key(text));
    if (recipe == recipes_.end()) {
        std::fprintf(stderr, "voice: recorded prompt unavailable: %s\n", text.c_str());
        return false;
    }
    for (const auto& id : recipe->second) {
        if (!pcm.empty()) pcm.insert(pcm.end(), kGapSamples, 0);
        if (!read_clip(id, pcm)) { pcm.clear(); return false; }
    }
    sample_rate_hz = kSampleRate;
    return true;
}

} // namespace navicore_osmand
