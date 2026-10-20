// Plugin-to-daemon link: datagram format, truncation, malformed input, and a
// full client -> server -> worker run over a real POSIX message queue with fakes.
#include "voice_link.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

using namespace navicore_osmand;
using ui::loc::Language;

namespace {

struct Engine : TtsEngine {
    std::mutex m;
    std::vector<std::pair<std::string, Language>> heard;
    bool synthesize(const std::string& text, Language language, std::vector<short>& pcm,
                    unsigned& rate) override {
        { std::lock_guard<std::mutex> l(m); heard.emplace_back(text, language); }
        pcm.assign(2400, 1);
        rate = 24000;
        return true;
    }
};
struct Audio : AudioOut {
    bool acquire() override { return true; }
    bool write(const short*, std::size_t, unsigned) override { return true; }
    void release() override {}
};

void roundtrip_and_sanitising() {
    SpokenPrompt p{"Через 200 метрів\tповерніть\nправоруч", PromptPriority::Reroute, "k"};
    std::string wire = encode_prompt(p, Language::Ukrainian);
    SpokenPrompt out;
    Language lang = Language::English;
    assert(decode_message(wire.data(), wire.size(), out, lang) == Message::Prompt);
    assert(lang == Language::Ukrainian);
    assert(out.priority == PromptPriority::Reroute);
    assert(out.text == "Через 200 метрів поверніть праворуч");
    std::string cancel = encode_cancel();
    assert(decode_message(cancel.data(), cancel.size(), out, lang) == Message::Cancel);
}

void long_text_is_cut_on_a_character_boundary() {
    SpokenPrompt p;
    p.text.clear();
    for (int i = 0; i < 1000; ++i) p.text += "ї";  // 2 bytes each
    std::string wire = encode_prompt(p, Language::Ukrainian);
    assert(wire.size() <= kMaxDatagram);
    SpokenPrompt out;
    Language lang;
    assert(decode_message(wire.data(), wire.size(), out, lang) == Message::Prompt);
    assert(out.text.size() % 2 == 0);  // no half character
    for (std::size_t i = 0; i < out.text.size(); i += 2)
        assert(out.text.compare(i, 2, "ї") == 0);
}

void malformed_input_is_rejected() {
    SpokenPrompt out;
    Language lang;
    const char* bad[] = {"", "garbage", "NVP2\tcancel", "NVP1", "NVP1\tprompt",
                         "NVP1\tprompt\t9\tuk\ttext", "NVP1\tprompt\t1\txx\ttext",
                         "NVP1\tprompt\t1\tuk\t", "NVP1\tcancel\textra", "NVP1\tother"};
    for (const char* b : bad) assert(decode_message(b, std::char_traits<char>::length(b), out, lang) == Message::Invalid);
}

void no_daemon_never_blocks() {
    VoiceClient client("/navicore-voice-test-absent");
    SpokenPrompt p{"x", PromptPriority::Maneuver, ""};
    for (int i = 0; i < 100; ++i) assert(!client.send(p, Language::Ukrainian));
    assert(client.sent() == 0 && client.dropped() == 100);
}

void end_to_end() {
    std::string path = "/navicore-voice-test-" + std::to_string(getpid());
    Engine engine;
    Audio audio;
    VoiceWorker worker(engine, audio);
    worker.start();
    VoiceServer server(path, worker);
    assert(server.bind());
    std::thread loop([&] { server.run(); });

    VoiceClient client(path);
    assert(client.send({"Привіт", PromptPriority::Maneuver, ""}, Language::Ukrainian));
    for (int i = 0; i < 200 && server.received() < 1; ++i) usleep(10000);
    assert(server.received() == 1);
    worker.wait_idle();
    assert(worker.stats().spoken == 1);
    assert(engine.heard.size() == 1 && engine.heard[0].first == "Привіт");
    assert(engine.heard[0].second == Language::Ukrainian);

    assert(client.send({"Hello", PromptPriority::Arrival, ""}, Language::English));
    for (int i = 0; i < 200 && server.received() < 2; ++i) usleep(10000);
    worker.wait_idle();
    assert(engine.heard.size() == 2 && engine.heard[1].second == Language::English);

    server.stop();
    loop.join();
    worker.stop();
}

} // namespace

int main() {
    roundtrip_and_sanitising();
    long_text_is_cut_on_a_character_boundary();
    malformed_input_is_rejected();
    no_daemon_never_blocks();
    end_to_end();
    std::puts("voice_link_test ok");
    return 0;
}
