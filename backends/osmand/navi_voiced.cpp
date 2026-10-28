// navi_voiced: speaks navigation prompts sent by the OsmAnd backend.
//
//   navi_voiced [--queue NAME] [--device PCM] [--rhvoice-data DIR] [--voice NAME]
//               [--rate PERCENT] [--focus stock|none|alloc] [--alloc-cmd PATH]
//               [--alloc-route CHANNEL SOURCE ZONE] [--selftest TEXT]
//               [--engine recorded|rhvoice] [--recorded-data DIR]
//
// Receives messages (voice_link.h), assembles recorded clips or synthesizes with
// explicitly selected RHVoice, plays on io-audio
// and waits for stock navigation allocation and preparation before playing.
// --focus alloc is retained only for explicit diagnostics. --selftest speaks one
// sentence through the same stack and exits; it is the first thing to run on the radio.
#include "process_focus.h"
#include "stock_focus.h"
#include "qnx_pcm_out.h"
#include "rhvoice_engine.h"
#include "recorded_voice_engine.h"
#include "voice_link.h"
#include "voice_worker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <signal.h>

using namespace navicore_osmand;

namespace {

VoiceServer* g_server = nullptr;

void on_signal(int) {
    if (g_server) g_server->stop();  // stores to a lock-free atomic flag
}

struct Options {
    std::string queue = kDefaultQueue;  // /dev/mq/navicore-voice
    std::string device = "pcmC7D0p";
    std::string rhvoice_data = "/tmp/tts/rhvoice";
    std::string voice = "Natalia";
    std::string engine = "rhvoice";
    std::string recorded_data;
    int rate_percent = 100;
    std::string focus = "stock";
    std::string alloc_cmd = "/tmp/tts/as_alloc";
    std::string channel = "2", source = "8", zone = "1";  // NAVIGATION / NAVI / DRIVER
    std::string selftest;
};

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        if (a == "--queue") { if (!value(o.queue)) return false; }
        else if (a == "--device") { if (!value(o.device)) return false; }
        else if (a == "--rhvoice-data") { if (!value(o.rhvoice_data)) return false; }
        else if (a == "--voice") { if (!value(o.voice)) return false; }
        else if (a == "--engine") {
            if (!value(o.engine) || (o.engine != "recorded" && o.engine != "rhvoice")) return false;
        }
        else if (a == "--recorded-data") { if (!value(o.recorded_data)) return false; }
        else if (a == "--rate") { std::string v; if (!value(v)) return false; o.rate_percent = std::atoi(v.c_str()); }
        else if (a == "--focus") {
            std::string v;
            if (!value(v) || (v != "stock" && v != "none" && v != "alloc")) return false;
            o.focus = v;
        }
        else if (a == "--alloc-cmd") { if (!value(o.alloc_cmd)) return false; }
        else if (a == "--alloc-route") {
            if (i + 3 >= argc) return false;
            o.channel = argv[++i]; o.source = argv[++i]; o.zone = argv[++i];
        }
        else if (a == "--selftest") { if (!value(o.selftest)) return false; }
        else return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) {
        std::fprintf(stderr,
            "usage: %s [--queue NAME] [--device PCM] [--rhvoice-data DIR] [--voice NAME]\n"
            "          [--rate PERCENT] [--focus stock|none|alloc] [--alloc-cmd PATH]\n"
            "          [--alloc-route CHANNEL SOURCE ZONE] [--selftest TEXT]\n", argv[0]);
        std::fprintf(stderr, "          [--engine recorded|rhvoice] [--recorded-data DIR] (recorded voices: male|female)\n");
        return 64;
    }

    std::unique_ptr<TtsEngine> engine;
    if (opt.engine == "recorded") {
        if (opt.rate_percent != 100) {
            std::fprintf(stderr, "voice: --rate is unavailable for recorded clips\n");
            return 64;
        }
        auto recorded = std::make_unique<RecordedVoiceEngine>(opt.recorded_data, opt.voice);
        if (opt.recorded_data.empty() || !recorded->initialize()) return 78;
        engine = std::move(recorded);
    } else {
#ifdef ONC_WITH_RHVOICE
        auto rhvoice = std::make_unique<RhVoiceEngine>(opt.rhvoice_data, opt.voice);
        rhvoice->set_rate_percent(opt.rate_percent);
        engine = std::move(rhvoice);
#else
        std::fprintf(stderr, "voice: RHVoice support was not built; select --engine recorded\n");
        return 78;
#endif
    }

    std::unique_ptr<AudioFocus> focus;
    if (opt.focus == "stock") {
        focus.reset(new StockFocus());
    } else if (opt.focus == "alloc") {
        std::fprintf(stderr, "voice: --focus alloc is an unverified diagnostic path; use stock for guidance\n");
        ProcessFocus::Options f;
        f.argv = {opt.alloc_cmd, opt.channel, opt.source, opt.zone, "86400"};
        f.env = {"LD_LIBRARY_PATH=/pas/lib/dll:/pas/lib:/lib:/usr/lib:/proc/boot"};
        f.ready_marker = "allocate-sent status=0";
        focus.reset(new ProcessFocus(f));
    } else {
        focus.reset(new AlwaysFocus());
    }
    QnxPcmOut out(opt.device, *focus);
    VoiceWorker worker(*engine, out);
    worker.start();

    if (!opt.selftest.empty()) {
        worker.set_language(ui::loc::Language::Ukrainian);
        worker.enqueue({opt.selftest, PromptPriority::Arrival, "selftest"});
        worker.wait_idle();
        VoiceStats s = worker.stats();
        std::printf("selftest spoken=%u dropped=%u synth_failed=%u acquire_failed=%u write_failed=%u\n",
                    s.spoken, s.dropped, s.synth_failed, s.acquire_failed, s.write_failed);
        worker.stop();
        return s.spoken == 1 ? 0 : 1;
    }

    VoiceServer server(opt.queue, worker);
    if (!server.bind()) return 69;
    g_server = &server;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;  // sets an atomic flag; run() notices within 200 ms
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    std::printf("navi_voiced ready queue=%s device=%s focus=%s engine=%s voice=%s\n", opt.queue.c_str(),
                opt.device.c_str(), opt.focus.c_str(), opt.engine.c_str(), opt.voice.c_str());
    std::fflush(stdout);
    server.run();
    g_server = nullptr;
    worker.stop();
    VoiceStats s = worker.stats();
    std::printf("navi_voiced exit received=%u spoken=%u dropped=%u synth_failed=%u acquire_failed=%u\n",
                server.received(), s.spoken, s.dropped, s.synth_failed, s.acquire_failed);
    return 0;
}
