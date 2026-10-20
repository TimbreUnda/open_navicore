// Spoken guidance planner: distance wording and plurals, one announcement per
// stage, joining a route late, reroute and arrival, unsupported languages.
#include "voice_prompts.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace navicore_osmand;
using ui::ManeuverKind;
using ui::loc::Language;

namespace {

ui::GuidanceState state_at(double distance, ManeuverKind kind = ManeuverKind::Right,
                           std::size_t index = 7) {
    ui::GuidanceState s;
    s.position_available = true;
    s.distance_to_maneuver_metres = distance;
    s.maneuver.kind = kind;
    s.maneuver.geometry_index = index;
    s.maneuver.distance_from_start_metres = 5000.0;
    return s;
}

std::string only(const std::vector<SpokenPrompt>& prompts) {
    assert(prompts.size() == 1);
    return prompts[0].text;
}

void check(const std::string& got, const char* want, int line) {
    if (got != want) {
        std::fprintf(stderr, "line %d: got '%s' want '%s'\n", line, got.c_str(), want);
        std::abort();
    }
}
#define CHECK_TEXT(got, want) check((got), (want), __LINE__)

void distances() {
    CHECK_TEXT(PromptPlanner::spoken_distance(200, Language::Ukrainian), "200 метрів");
    CHECK_TEXT(PromptPlanner::spoken_distance(487, Language::Ukrainian), "500 метрів");
    CHECK_TEXT(PromptPlanner::spoken_distance(12, Language::Ukrainian), "50 метрів");
    CHECK_TEXT(PromptPlanner::spoken_distance(1000, Language::Ukrainian), "1 кілометр");
    CHECK_TEXT(PromptPlanner::spoken_distance(2000, Language::Ukrainian), "2 кілометри");
    CHECK_TEXT(PromptPlanner::spoken_distance(5000, Language::Ukrainian), "5 кілометрів");
    CHECK_TEXT(PromptPlanner::spoken_distance(21000, Language::Ukrainian), "21 кілометр");
    CHECK_TEXT(PromptPlanner::spoken_distance(1500, Language::Ukrainian), "1,5 кілометра");
    CHECK_TEXT(PromptPlanner::spoken_distance(200, Language::English), "200 metres");
    CHECK_TEXT(PromptPlanner::spoken_distance(1000, Language::English), "1 kilometre");
    CHECK_TEXT(PromptPlanner::spoken_distance(2500, Language::English), "2.5 kilometres");
}

void stages_once() {
    PromptPlanner p;
    assert(p.update(state_at(1500), Language::Ukrainian).empty());
    CHECK_TEXT(only(p.update(state_at(990), Language::Ukrainian)),
               "Через 1 кілометр поверніть праворуч");
    assert(p.update(state_at(900), Language::Ukrainian).empty());  // no repeat
    CHECK_TEXT(only(p.update(state_at(500), Language::Ukrainian)),
               "Через 500 метрів поверніть праворуч");
    CHECK_TEXT(only(p.update(state_at(200), Language::Ukrainian)),
               "Через 200 метрів поверніть праворуч");
    CHECK_TEXT(only(p.update(state_at(40), Language::Ukrainian)), "Поверніть праворуч");
    assert(p.update(state_at(10), Language::Ukrainian).empty());
}

void joining_late_speaks_only_nearest() {
    PromptPlanner p;
    CHECK_TEXT(only(p.update(state_at(320), Language::Ukrainian)),
               "Через 300 метрів поверніть праворуч");
    assert(p.update(state_at(310), Language::Ukrainian).empty());
    // A different manoeuvre starts over.
    assert(p.update(state_at(1500, ManeuverKind::Left, 12), Language::Ukrainian).empty());
    CHECK_TEXT(only(p.update(state_at(480, ManeuverKind::Left, 12), Language::Ukrainian)),
               "Через 500 метрів поверніть ліворуч");
}

void english_and_unsupported() {
    PromptPlanner p;
    CHECK_TEXT(only(p.update(state_at(190), Language::English)), "In 200 metres, turn right");
    PromptPlanner q;
    assert(q.update(state_at(190), Language::FrenchCanadian).empty());
    assert(q.update(state_at(190), Language::SpanishMexican).empty());
}

void roundabout_and_follow_up() {
    ui::GuidanceState s = state_at(180, ManeuverKind::Roundabout);
    s.maneuver.roundabout_exit = 3;
    CHECK_TEXT(PromptPlanner::maneuver_sentence(s, 2, Language::Ukrainian),
               "Через 200 метрів на кільці з'їдьте на третьому з'їзді");
    s.maneuver.roundabout_exit = 0;
    CHECK_TEXT(PromptPlanner::maneuver_sentence(s, 3, Language::Ukrainian),
               "На кільці виберіть потрібний з'їзд");

    ui::GuidanceState t = state_at(200, ManeuverKind::Left);
    t.has_following_maneuver = true;
    t.following_maneuver.kind = ManeuverKind::KeepRight;
    t.following_maneuver.distance_from_start_metres = 5120.0;
    CHECK_TEXT(PromptPlanner::maneuver_sentence(t, 2, Language::Ukrainian),
               "Через 200 метрів поверніть ліворуч, потім тримайтеся правіше");
    t.following_maneuver.distance_from_start_metres = 5800.0;  // far: not chained
    CHECK_TEXT(PromptPlanner::maneuver_sentence(t, 2, Language::Ukrainian),
               "Через 200 метрів поверніть ліворуч");
}

void reroute_and_arrival() {
    PromptPlanner p;
    ui::GuidanceState s = state_at(600);
    s.recalculating = true;
    auto first = p.update(s, Language::Ukrainian);
    assert(first.size() == 1 && first[0].priority == PromptPriority::Reroute);
    CHECK_TEXT(first[0].text, "Прокладаю новий маршрут");
    assert(p.update(s, Language::Ukrainian).empty());  // once per recalculation
    s.recalculating = false;
    CHECK_TEXT(only(p.update(state_at(480), Language::Ukrainian)),
               "Через 500 метрів поверніть праворуч");

    ui::GuidanceState end = state_at(0, ManeuverKind::Arrive);
    end.arrived = true;
    auto done = p.update(end, Language::Ukrainian);
    assert(done.size() == 1 && done[0].priority == PromptPriority::Arrival);
    CHECK_TEXT(done[0].text, "Ви прибули до місця призначення");
    assert(p.update(end, Language::Ukrainian).empty());
}

void no_position_and_simulation() {
    PromptPlanner p;
    ui::GuidanceState s = state_at(100);
    s.position_available = false;
    assert(p.update(s, Language::Ukrainian).empty());
    s.position_available = true;
    s.simulated = true;
    assert(p.update(s, Language::Ukrainian, false).empty());
    // Staying silent while simulated did not consume the stage.
    assert(p.update(s, Language::Ukrainian, true).size() == 1);
}

} // namespace

int main() {
    distances();
    stages_once();
    joining_late_speaks_only_nearest();
    english_and_unsupported();
    roundabout_and_follow_up();
    reroute_and_arrival();
    no_position_and_simulation();
    std::puts("voice_prompts_test ok");
    return 0;
}
