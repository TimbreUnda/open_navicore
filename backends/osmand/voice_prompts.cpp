#include "voice_prompts.h"

#include <cmath>
#include <cstdio>

namespace navicore_osmand {

namespace {

using ui::ManeuverKind;
using ui::loc::Language;
using ui::loc::Plural;

constexpr double kFollowingGapMetres = 200.0;

bool supported(Language language) noexcept {
    return language == Language::Ukrainian || language == Language::English;
}

bool is_uk(Language language) noexcept { return language == Language::Ukrainian; }

// The first character of an ASCII or UTF-8 Cyrillic sentence upper-cased.
// Cyrillic capitals are the lower-case code point minus 0x20 in the range
// U+0430..U+044F (0xD0 0xB0.. / 0xD1 0x80..) and minus 0x50 for U+0450..U+045F;
// the Ukrainian letters ґ є і ї (U+0491, U+0454, U+0456, U+0457) are handled.
std::string capitalise(std::string text) {
    if (text.empty()) return text;
    unsigned char c0 = static_cast<unsigned char>(text[0]);
    if (c0 >= 'a' && c0 <= 'z') {
        text[0] = static_cast<char>(c0 - 'a' + 'A');
    } else if (c0 == 0xD0 && text.size() >= 2) {
        unsigned char c1 = static_cast<unsigned char>(text[1]);
        if (c1 >= 0xB0 && c1 <= 0xBF) text[1] = static_cast<char>(c1 - 0x20);  // а..п
    } else if (c0 == 0xD1 && text.size() >= 2) {
        unsigned char c1 = static_cast<unsigned char>(text[1]);
        if (c1 >= 0x80 && c1 <= 0x8F) {  // р..я -> Р..Я (0xD0 0xA0..0xAF)
            text[0] = static_cast<char>(0xD0);
            text[1] = static_cast<char>(c1 + 0x20);
        } else if (c1 == 0x94) {         // є -> Є (U+0404)
            text[0] = static_cast<char>(0xD0);
            text[1] = static_cast<char>(0x84);
        } else if (c1 == 0x96) {         // і -> І (U+0406)
            text[0] = static_cast<char>(0xD0);
            text[1] = static_cast<char>(0x86);
        } else if (c1 == 0x97) {         // ї -> Ї (U+0407)
            text[0] = static_cast<char>(0xD0);
            text[1] = static_cast<char>(0x87);
        }
    }
    return text;
}

std::string action(ManeuverKind kind, int roundabout_exit, Language language) {
    if (is_uk(language)) {
        switch (kind) {
        case ManeuverKind::Left: return "поверніть ліворуч";
        case ManeuverKind::SlightLeft: return "поверніть злегка ліворуч";
        case ManeuverKind::SharpLeft: return "поверніть різко ліворуч";
        case ManeuverKind::Right: return "поверніть праворуч";
        case ManeuverKind::SlightRight: return "поверніть злегка праворуч";
        case ManeuverKind::SharpRight: return "поверніть різко праворуч";
        case ManeuverKind::KeepLeft: return "тримайтеся лівіше";
        case ManeuverKind::KeepRight: return "тримайтеся правіше";
        case ManeuverKind::UTurn: return "розвернітеся";
        case ManeuverKind::Roundabout: {
            static const char* const ordinal[] = {
                "першому", "другому", "третьому", "четвертому",
                "п'ятому", "шостому", "сьомому", "восьмому"};
            if (roundabout_exit >= 1 && roundabout_exit <= 8)
                return std::string("на кільці з'їдьте на ") + ordinal[roundabout_exit - 1] +
                       " з'їзді";
            return "на кільці виберіть потрібний з'їзд";
        }
        case ManeuverKind::Arrive: return "ви прибудете до місця призначення";
        case ManeuverKind::Straight: return "продовжуйте прямо";
        }
        return {};
    }
    switch (kind) {
    case ManeuverKind::Left: return "turn left";
    case ManeuverKind::SlightLeft: return "turn slightly left";
    case ManeuverKind::SharpLeft: return "turn sharp left";
    case ManeuverKind::Right: return "turn right";
    case ManeuverKind::SlightRight: return "turn slightly right";
    case ManeuverKind::SharpRight: return "turn sharp right";
    case ManeuverKind::KeepLeft: return "keep left";
    case ManeuverKind::KeepRight: return "keep right";
    case ManeuverKind::UTurn: return "make a U-turn";
    case ManeuverKind::Roundabout: {
        if (roundabout_exit >= 1 && roundabout_exit <= 8) {
            static const char* const ordinal[] = {"first", "second", "third", "fourth",
                                                  "fifth", "sixth", "seventh", "eighth"};
            return std::string("at the roundabout take the ") + ordinal[roundabout_exit - 1] +
                   " exit";
        }
        return "at the roundabout take the exit";
    }
    case ManeuverKind::Arrive: return "you will arrive at your destination";
    case ManeuverKind::Straight: return "continue straight";
    }
    return {};
}

std::string number(long value) { return std::to_string(value); }

} // namespace

std::string PromptPlanner::spoken_distance(double metres, Language language) {
    if (!(metres > 0.0)) metres = 0.0;
    if (metres < 950.0) {
        long m = static_cast<long>(std::lround(metres / 50.0)) * 50;
        if (m < 50) m = 50;
        if (is_uk(language)) {
            const char* unit = "метрів";
            switch (ui::loc::plural_form(language, m)) {
            case Plural::One: unit = "метр"; break;
            case Plural::Few: unit = "метри"; break;
            case Plural::Many: unit = "метрів"; break;
            }
            return number(m) + " " + unit;
        }
        return number(m) + (m == 1 ? " metre" : " metres");
    }
    // Kilometres: whole numbers from 10 km, otherwise halves.
    double km = metres / 1000.0;
    double rounded = km >= 10.0 ? std::round(km) : std::round(km * 2.0) / 2.0;
    long whole = static_cast<long>(rounded);
    bool half = rounded - static_cast<double>(whole) > 0.25;
    if (is_uk(language)) {
        if (half) return number(whole) + ",5 кілометра";
        const char* unit = "кілометрів";
        switch (ui::loc::plural_form(language, whole)) {
        case Plural::One: unit = "кілометр"; break;
        case Plural::Few: unit = "кілометри"; break;
        case Plural::Many: unit = "кілометрів"; break;
        }
        return number(whole) + " " + unit;
    }
    if (half) return number(whole) + ".5 kilometres";
    return number(whole) + (whole == 1 ? " kilometre" : " kilometres");
}

std::string PromptPlanner::maneuver_sentence(const ui::GuidanceState& state, std::size_t stage,
                                             Language language) {
    if (!supported(language) || stage >= kStageCount) return {};
    const ui::Maneuver& m = state.maneuver;
    std::string what = action(m.kind, m.roundabout_exit, language);
    std::string text;
    if (stage + 1 < kStageCount) {
        // "Через 200 метрів поверніть праворуч" / "In 200 metres, turn right".
        std::string distance = spoken_distance(state.distance_to_maneuver_metres, language);
        text = is_uk(language) ? "Через " + distance + " " + what
                               : "In " + distance + ", " + what;
        if (!is_uk(language)) text = capitalise(text);
    } else {
        text = capitalise(what);
        if (m.kind == ManeuverKind::Arrive) text = is_uk(language) ? "Ви майже прибули"
                                                                    : "You have almost arrived";
    }
    // A close follow-up turn is announced with the first so the driver can prepare.
    if (stage >= 2 && state.has_following_maneuver &&
        state.following_maneuver.kind != ManeuverKind::Straight &&
        state.following_maneuver.distance_from_start_metres - m.distance_from_start_metres <
            kFollowingGapMetres) {
        text += (is_uk(language) ? ", потім " : ", then ") +
                action(state.following_maneuver.kind, state.following_maneuver.roundabout_exit,
                       language);
    }
    return text;
}

void PromptPlanner::reset() noexcept {
    has_maneuver_ = false;
    kind_ = ManeuverKind::Straight;
    geometry_index_ = 0;
    for (bool& done : stage_done_) done = false;
    was_recalculating_ = false;
    was_arrived_ = false;
}

std::vector<SpokenPrompt> PromptPlanner::update(const ui::GuidanceState& state,
                                                Language language, bool speak_simulated) {
    std::vector<SpokenPrompt> out;
    if (!supported(language)) return out;
    if (!state.position_available || (state.simulated && !speak_simulated)) return out;

    if (state.arrived) {
        if (!was_arrived_) {
            was_arrived_ = true;
            has_maneuver_ = false;
            out.push_back({is_uk(language) ? "Ви прибули до місця призначення"
                                           : "You have arrived at your destination",
                           PromptPriority::Arrival, "arrived"});
        }
        return out;
    }
    was_arrived_ = false;

    if (state.recalculating) {
        if (!was_recalculating_) {
            was_recalculating_ = true;
            has_maneuver_ = false;  // the new route has new manoeuvres
            out.push_back({is_uk(language) ? "Прокладаю новий маршрут" : "Recalculating route",
                           PromptPriority::Reroute, "reroute"});
        }
        return out;
    }
    was_recalculating_ = false;

    const ui::Maneuver& m = state.maneuver;
    if (m.kind == ManeuverKind::Straight) return out;
    if (!has_maneuver_ || kind_ != m.kind || geometry_index_ != m.geometry_index) {
        has_maneuver_ = true;
        kind_ = m.kind;
        geometry_index_ = m.geometry_index;
        for (bool& done : stage_done_) done = false;
    }

    double distance = std::isfinite(state.distance_to_maneuver_metres)
                          ? state.distance_to_maneuver_metres
                          : 1.0e9;
    if (distance < 0.0) distance = 0.0;
    // Every stage the vehicle is inside is consumed, but only the nearest one is
    // spoken: joining the route at 300 m must not read out 1000 m and 500 m too.
    std::size_t speak = kStageCount;
    for (std::size_t i = 0; i < kStageCount; ++i) {
        if (distance <= kStages[i] && !stage_done_[i]) {
            stage_done_[i] = true;
            speak = i;
        }
    }
    if (speak < kStageCount) {
        ui::GuidanceState spoken = state;
        spoken.distance_to_maneuver_metres = distance;
        out.push_back({maneuver_sentence(spoken, speak, language), PromptPriority::Maneuver,
                       "m" + std::to_string(geometry_index_) + "-s" + std::to_string(speak)});
    }
    return out;
}

} // namespace navicore_osmand
