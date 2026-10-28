#include "recorded_voice_engine.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace navicore_osmand;
using ui::loc::Language;

namespace {
void u16(std::ofstream& f, unsigned n) { f.put(static_cast<char>(n)); f.put(static_cast<char>(n >> 8)); }
void u32(std::ofstream& f, unsigned n) { u16(f, n); u16(f, n >> 16); }
void wav(const std::string& path, unsigned rate, short sample) {
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4); u32(f, 40); f.write("WAVEfmt ", 8); u32(f, 16);
    u16(f, 1); u16(f, 1); u32(f, rate); u32(f, rate * 2); u16(f, 2); u16(f, 16);
    f.write("data", 4); u32(f, 4); u16(f, static_cast<unsigned short>(sample)); u16(f, 123);
}
void manifest(const std::string& root, const std::string& rows) {
    std::ofstream(root + "/recipes.tsv") << "NAVI-RECORDED-1\t24000\t480\n" << rows;
}
void fixture() {
    char directory[] = "/tmp/onc-recorded-XXXXXX";
    const char* made = mkdtemp(directory); assert(made);
    std::string root(made);
    for (const auto& voice : {"male", "female"}) {
        assert(mkdir((root + "/" + voice).c_str(), 0700) == 0);
        assert(mkdir((root + "/" + voice + "/clips").c_str(), 0700) == 0);
    }
    wav(root + "/male/clips/distance.wav", 24000, -1000);
    wav(root + "/male/clips/action.wav", 24000, 2000);
    wav(root + "/female/clips/distance.wav", 24000, -3000);
    wav(root + "/female/clips/action.wav", 24000, 4000);
    manifest(root, "Через 200 метрів поверніть праворуч\tdistance,action\nmissing\tdistance,missing\n");
    std::vector<short> pcm; unsigned rate = 0;
    RecordedVoiceEngine male(root, "male"), female(root, "female");
    assert(male.initialize() && female.initialize());
    assert(male.synthesize("Через 200 метрів поверніть праворуч.", Language::Ukrainian, pcm, rate));
    assert(rate == 24000 && pcm.size() == 484 && pcm[0] == -1000 && pcm[482] == 2000);
    for (std::size_t i = 2; i < 482; ++i) assert(pcm[i] == 0);
    assert(female.synthesize("Через 200 метрів поверніть праворуч", Language::Ukrainian, pcm, rate));
    assert(pcm[0] == -3000 && pcm[482] == 4000);
    assert(!male.synthesize("unknown", Language::Ukrainian, pcm, rate) && pcm.empty() && rate == 0);
    assert(!male.synthesize("missing", Language::Ukrainian, pcm, rate) && pcm.empty() && rate == 0);
    assert(!male.synthesize("Через 200 метрів поверніть праворуч", Language::English, pcm, rate));
    assert(pcm.empty() && rate == 0);
    wav(root + "/male/clips/action.wav", 22050, 2000);
    assert(!male.synthesize("Через 200 метрів поверніть праворуч", Language::Ukrainian, pcm, rate));
    assert(pcm.empty() && rate == 0);
    assert(!RecordedVoiceEngine(root, "../male").initialize());
    // A malformed final line without a newline must not expose a partial pack.
    manifest(root, "valid\tdistance\ninvalid\t../escape");
    assert(!RecordedVoiceEngine(root, "male").initialize());
    manifest(root, "duplicate\tdistance\nduplicate\taction\n");
    assert(!RecordedVoiceEngine(root, "male").initialize());
    for (const auto& voice : {"male", "female"}) {
        for (const auto& id : {"distance", "action"})
            std::remove((root + "/" + voice + "/clips/" + id + ".wav").c_str());
        rmdir((root + "/" + voice + "/clips").c_str()); rmdir((root + "/" + voice).c_str());
    }
    std::remove((root + "/recipes.tsv").c_str()); rmdir(root.c_str());
}

// Optional real-pack integration: every planner action, distance, follow-up and
// status must resolve and read actual PCM for both voices. Not a synthetic map.
void actual_pack(const char* root) {
    const ui::ManeuverKind kinds[] = {ui::ManeuverKind::Left, ui::ManeuverKind::SlightLeft,
        ui::ManeuverKind::SharpLeft, ui::ManeuverKind::Right, ui::ManeuverKind::SlightRight,
        ui::ManeuverKind::SharpRight, ui::ManeuverKind::KeepLeft, ui::ManeuverKind::KeepRight,
        ui::ManeuverKind::UTurn, ui::ManeuverKind::Arrive, ui::ManeuverKind::Roundabout};
    std::vector<ui::Maneuver> actions;
    for (auto kind : kinds) {
        ui::Maneuver m; m.kind = kind;
        if (kind == ui::ManeuverKind::Roundabout) {
            for (int exit = 0; exit <= 8; ++exit) { m.roundabout_exit = exit; actions.push_back(m); }
        } else actions.push_back(m);
    }
    for (const auto& voice : {"male", "female"}) {
        RecordedVoiceEngine engine(root, voice); assert(engine.initialize());
        unsigned count = 0;
        auto check = [&](const std::string& text) {
            std::vector<short> pcm; unsigned rate = 0;
            if (!engine.synthesize(text, Language::Ukrainian, pcm, rate)) {
                std::fprintf(stderr, "Missing real pack prompt: %s\n", text.c_str()); std::abort();
            }
            assert(rate == 24000 && !pcm.empty()); ++count;
        };
        for (const auto& action : actions) {
            ui::GuidanceState s; s.maneuver = action;
            for (int distance = 50; distance <= 1000; distance += 50) {
                s.distance_to_maneuver_metres = distance == 950 ? 949 : distance;
                check(PromptPlanner::maneuver_sentence(s, 0, Language::Ukrainian));
            }
            check(PromptPlanner::maneuver_sentence(s, 3, Language::Ukrainian));
            s.has_following_maneuver = true;
            for (const auto& following : actions) {
                s.following_maneuver = following;
                for (int distance = 50; distance <= 200; distance += 50) {
                    s.distance_to_maneuver_metres = distance;
                    check(PromptPlanner::maneuver_sentence(s, 2, Language::Ukrainian));
                }
                check(PromptPlanner::maneuver_sentence(s, 3, Language::Ukrainian));
            }
        }
        check("Прокладаю новий маршрут"); check("Ви прибули до місця призначення");
        std::printf("real recorded pack voice=%s planner cases=%u OK\n", voice, count);
    }
}
} // namespace

int main(int argc, char** argv) {
    fixture();
    if (argc == 2) actual_pack(argv[1]);
    std::puts("recorded voice tests passed");
}
