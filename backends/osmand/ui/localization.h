#pragma once

// Navigation UI language: a small string table (no i18n library). The radio's
// own HMI is localized, so the navigation texts follow it. The language is
// read once at start (detect_language) and set with set_language; texts are
// looked up at the point of use, so data that is stored or passed around
// (route labels, recents) can be produced in any language.
//
// Languages: English (default and fallback for every missing entry),
// Ukrainian, French (Canada), Spanish (Mexico). Distances stay metric.

#include "navigation_model.h"

#include <string>
#include <vector>

namespace navicore_osmand::ui::loc {

enum class Language { English, Ukrainian, FrenchCanadian, SpanishMexican };

Language language() noexcept;
void set_language(Language language) noexcept;
// "uk", "uk-UA", "ukrainian", "fr-CA", "french.cfg" (the radio's HMI file),
// "es_MX", ... Case-insensitive; false for anything else (e.g. german.cfg).
bool parse_language(const std::string& text, Language& out);
// Short code for traces: "en", "uk", "fr-CA", "es-MX".
const char* language_code(Language language) noexcept;
// OSM name languages to try for map labels and search results, best first
// ("uk"; "fr", "en"; ...). The caller falls back to the object's own name.
std::vector<std::string> name_languages(Language language);

struct Detected {
    Language language = Language::English;
    const char* source = "default";  // "environment", "marker", "hmi", "default"
};
// First that names a supported language wins:
//   1. `environment` (value of OPEN_NAVICORE_OSMAND_LANGUAGE, may be null),
//   2. the marker file `marker_path` (bench override, first line),
//   3. the radio's `hmi_path` (/datastore/dc_hmi_language.txt, "english.cfg"),
//   4. English. Unreadable files are ignored.
Detected detect_language(const char* environment, const std::string& marker_path,
                         const std::string& hmi_path);

enum class Str {
    WhereTo, Recent, NoRecent, NoRecentHint,
    Close, Navigate, Back, Start, End, Done,
    SearchPlaceholder, TypingPaused, TypingPausedHint, KeySpace, TypeToSearch,
    MsgSearching, MsgNoResults, MsgCalculating, MsgNoRoute,
    DestinationFallback, RouteFallback, Arrived, Recalculating, OffRoute,
    SimulationTag, NoGps, NoGpsBody, NextOnRoute, EtaPaused,
    ReasonFastest, ReasonShorter, ReasonAlternative, ReasonShortest, ReasonFewerTurns,
    DroppedPin,
    TurnLeft, TurnSlightLeft, TurnSharpLeft, TurnRight, TurnSlightRight, TurnSharpRight,
    KeepLeft, KeepRight, UTurn, Roundabout, Arrive, Straight,
    RoundaboutExit,   // printf %d: the exit number
    InDistance,       // printf %s: a formatted distance ("In 290 m")
    AddressPlace, AddressStreet, AddressBuilding, AddressIntersection,
    UnitMetre, UnitKilometre, UnitMinute, UnitHour,
    Then,             // split-screen card: "Then" before the following turn
    OpenNavigation,   // split-screen banner when no route is being guided
    // Settlement kinds in search results (OSM place=city/town/village/...).
    SettlementCity, SettlementTown, SettlementVillage, SettlementHamlet, SettlementSuburb,
    Count_
};

// The text for `key` in the current language (English when it has none).
const char* tr(Str key) noexcept;
const char* tr(Language language, Str key) noexcept;

// The oblast of a map file key ("kharkiv" from Ukraine_kharkiv_europe_2.obf):
// "Харківська обл." / "Kharkiv Oblast"; empty for an unknown key.
std::string region_name(const std::string& file_key);

const char* maneuver_action(ManeuverKind kind) noexcept;
std::string roundabout_exit_text(int exit);

// Which form a count takes: Ukrainian has one (1, 21, ...), few (2-4, 22-24)
// and many (0, 5-20, ...); the other languages use one for 1 and many else.
enum class Plural { One, Few, Many };
Plural plural_form(Language language, long count) noexcept;

// Metres as "290 m" / "6.2 km" ("290 м" / "6,2 км"). Under 1 km the value is
// rounded as `rounding` says; 1 km and more shows one decimal.
enum class Rounding { None, Coarse /* 5 m below 100 m, else 10 m */, Tens };
std::string format_distance(double metres, Rounding rounding = Rounding::None);
// "12 min" / "1 h 05 min" ("12 хв" / "1 год 05 хв").
std::string format_duration_minutes(int minutes);
// "In 290 m" ("Через 290 м"): the distance to the next manoeuvre.
std::string format_in_distance(double metres);
// "Returning to free drive in 5 s" / "Повернення до карти через 5 секунд".
std::string arrival_countdown_text(int seconds);

// "amenity_fuel", "charging_station", "city" -> "Fuel", "Charging station",
// "City" in the current language; an unknown key is shown as readable
// English ("amenity_foo_bar" -> "Foo bar").
std::string poi_category(const std::string& key);

// Route labels and model messages that arrive as English text: the
// localized text, or the input itself when it is not a known label.
std::string route_reason(const std::string& english);
std::string model_message(const std::string& english);

} // namespace navicore_osmand::ui::loc
