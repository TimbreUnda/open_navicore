// String table, plurals, formatters and language detection of the navigation
// UI, plus a check that the compiled-in fonts have a glyph for every
// character of every localized text.
#include "ui/assets/nav_fonts.h"
#include "ui/localization.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

using namespace navicore_osmand::ui;
using loc::Language;
using loc::Str;

namespace {
const Language all_languages[] = {Language::English, Language::Ukrainian,
                                  Language::FrenchCanadian, Language::SpanishMexican};

struct Scoped {
    explicit Scoped(Language language) : previous(loc::language()) { loc::set_language(language); }
    ~Scoped() { loc::set_language(previous); }
    Language previous;
};

std::string write_temp(const std::string& name, const std::string& text)
{
    const std::string path = "/tmp/localization_test_" + std::to_string(::getpid()) + "_" + name;
    std::FILE* file = std::fopen(path.c_str(), "w");
    assert(file);
    std::fputs(text.c_str(), file);
    std::fclose(file);
    return path;
}

void decode(const std::string& text, std::set<char32_t>& out)
{
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        char32_t value = first;
        std::size_t length = 1;
        if (first >= 0xf0u) { value = first & 0x07u; length = 4; }
        else if (first >= 0xe0u) { value = first & 0x0fu; length = 3; }
        else if (first >= 0xc0u) { value = first & 0x1fu; length = 2; }
        for (std::size_t k = 1; k < length; ++k)
            value = (value << 6u) | (static_cast<unsigned char>(text[i + k]) & 0x3fu);
        out.insert(value);
        i += length;
    }
}

void parse_and_detect()
{
    Language language = Language::English;
    assert(loc::parse_language("uk", language) && language == Language::Ukrainian);
    assert(loc::parse_language("UK-ua\n", language) && language == Language::Ukrainian);
    assert(loc::parse_language(" Ukrainian ", language) && language == Language::Ukrainian);
    assert(loc::parse_language("english.cfg\n", language) && language == Language::English);
    assert(loc::parse_language("french.cfg", language) && language == Language::FrenchCanadian);
    assert(loc::parse_language("fr_CA", language) && language == Language::FrenchCanadian);
    assert(loc::parse_language("spanish.cfg", language) && language == Language::SpanishMexican);
    assert(loc::parse_language("es-MX", language) && language == Language::SpanishMexican);
    // Languages the UI has no table for are not guessed.
    language = Language::Ukrainian;
    assert(!loc::parse_language("german.cfg", language));
    assert(!loc::parse_language("russian.cfg", language));
    assert(!loc::parse_language("", language));
    assert(!loc::parse_language("auto", language));
    assert(language == Language::Ukrainian);  // untouched on failure

    const std::string none = "/tmp/localization_test_missing_file";
    const std::string hmi_english = write_temp("hmi_en", "english.cfg\n");
    const std::string hmi_french = write_temp("hmi_fr", "french.cfg");
    const std::string hmi_german = write_temp("hmi_de", "german.cfg\n");
    const std::string marker_uk = write_temp("marker_uk", "uk\n");
    const std::string marker_junk = write_temp("marker_junk", "klingon\n");

    auto found = loc::detect_language(nullptr, none, none);
    assert(found.language == Language::English);
    found = loc::detect_language(nullptr, none, hmi_english);
    assert(found.language == Language::English);
    found = loc::detect_language(nullptr, none, hmi_french);
    assert(found.language == Language::FrenchCanadian);
    // A language without a table, or an unreadable file: English.
    found = loc::detect_language(nullptr, none, hmi_german);
    assert(found.language == Language::English);
    // The bench marker beats the HMI file (the unit's English bank is Ukrainian).
    found = loc::detect_language(nullptr, marker_uk, hmi_english);
    assert(found.language == Language::Ukrainian);
    // An unusable marker falls through to the HMI file.
    found = loc::detect_language(nullptr, marker_junk, hmi_french);
    assert(found.language == Language::FrenchCanadian);
    // The environment beats both.
    found = loc::detect_language("es-MX", marker_uk, hmi_french);
    assert(found.language == Language::SpanishMexican);
    found = loc::detect_language("nonsense", marker_uk, hmi_french);
    assert(found.language == Language::Ukrainian);
    for (const auto& path : {hmi_english, hmi_french, hmi_german, marker_uk, marker_junk})
        std::remove(path.c_str());
}

void table_complete()
{
    for (const Language language : all_languages) {
        for (int key = 0; key < static_cast<int>(Str::Count_); ++key) {
            const char* text = loc::tr(language, static_cast<Str>(key));
            // Only the English roundabout pattern is built in code.
            assert((text && *text) || (key == static_cast<int>(Str::RoundaboutExit) &&
                                       language == Language::English));
        }
        // Printf-style patterns carry their placeholder in every language.
        assert(language == Language::English ||
               std::string(loc::tr(language, Str::RoundaboutExit)).find("%d") != std::string::npos);
        assert(std::string(loc::tr(language, Str::InDistance)).find("%s") != std::string::npos);
    }
    // English is the fallback and the canonical text the model/tests use.
    assert(std::string(loc::tr(Language::English, Str::WhereTo)) == "Where to?");
    assert(std::string(loc::tr(Language::Ukrainian, Str::WhereTo)) == "Куди їдемо?");
    assert(std::string(loc::tr(Language::Ukrainian, Str::Recent)) == "НЕЩОДАВНІ");
    // Out-of-range keys do not crash.
    assert(std::string(loc::tr(Language::English, Str::Count_)).empty());
}

void plurals()
{
    using loc::Plural;
    const auto uk = [](long n) { return loc::plural_form(Language::Ukrainian, n); };
    for (const long n : {1L, 21L, 31L, 101L, 1001L}) assert(uk(n) == Plural::One);
    for (const long n : {2L, 3L, 4L, 22L, 24L, 102L, 134L}) assert(uk(n) == Plural::Few);
    for (const long n : {0L, 5L, 9L, 10L, 11L, 12L, 13L, 14L, 15L, 20L, 25L, 100L, 111L, 112L, 114L})
        assert(uk(n) == Plural::Many);
    assert(loc::plural_form(Language::English, 1) == Plural::One);
    assert(loc::plural_form(Language::English, 2) == Plural::Many);
    assert(loc::plural_form(Language::English, 0) == Plural::Many);
    assert(loc::plural_form(Language::FrenchCanadian, 1) == Plural::One);

    Scoped uk_scope(Language::Ukrainian);
    assert(loc::arrival_countdown_text(1) == "Повернення через 1 секунду");
    assert(loc::arrival_countdown_text(2) == "Повернення через 2 секунди");
    assert(loc::arrival_countdown_text(5) == "Повернення через 5 секунд");
    assert(loc::arrival_countdown_text(8) == "Повернення через 8 секунд");
    assert(loc::arrival_countdown_text(11) == "Повернення через 11 секунд");
    assert(loc::arrival_countdown_text(21) == "Повернення через 21 секунду");
    loc::set_language(Language::English);
    assert(loc::arrival_countdown_text(5) == "Returning to free drive in 5 s");
}

void formatters()
{
    {
        Scoped scope(Language::English);
        assert(loc::format_distance(0.0) == "0 m");
        assert(loc::format_distance(287.0) == "287 m");
        assert(loc::format_distance(287.0, loc::Rounding::Coarse) == "290 m");
        assert(loc::format_distance(42.0, loc::Rounding::Coarse) == "40 m");
        assert(loc::format_distance(43.0, loc::Rounding::Coarse) == "45 m");
        assert(loc::format_distance(287.0, loc::Rounding::Tens) == "290 m");
        assert(loc::format_distance(6240.0) == "6.2 km");
        assert(loc::format_distance(1000.0) == "1.0 km");
        assert(loc::format_distance(997.0, loc::Rounding::Coarse) == "1.0 km");
        assert(loc::format_distance(-5.0) == "0 m");
        assert(loc::format_duration_minutes(0) == "0 min");
        assert(loc::format_duration_minutes(12) == "12 min");
        assert(loc::format_duration_minutes(60) == "1 h 00 min");
        assert(loc::format_duration_minutes(125) == "2 h 05 min");
        assert(loc::format_in_distance(290.0) == "290 m");
        assert(loc::roundabout_exit_text(1) == "At the roundabout, take the 1st exit");
        assert(loc::roundabout_exit_text(2) == "At the roundabout, take the 2nd exit");
        assert(loc::roundabout_exit_text(3) == "At the roundabout, take the 3rd exit");
        assert(loc::roundabout_exit_text(4) == "At the roundabout, take the 4th exit");
        assert(loc::roundabout_exit_text(11) == "At the roundabout, take the 11th exit");
        assert(loc::roundabout_exit_text(21) == "At the roundabout, take the 21st exit");
        assert(std::string(loc::maneuver_action(ManeuverKind::Right)) == "Turn right");
        assert(std::string(loc::maneuver_action(ManeuverKind::UTurn)) == "Make a U-turn");
    }
    {
        Scoped scope(Language::Ukrainian);
        assert(loc::format_distance(287.0, loc::Rounding::Coarse) == "290 м");
        assert(loc::format_distance(6240.0) == "6,2 км");  // decimal comma
        assert(loc::format_duration_minutes(12) == "12 хв");
        assert(loc::format_duration_minutes(65) == "1 год 05 хв");
        assert(loc::format_in_distance(290.0) == "Через 290 м");
        assert(loc::format_in_distance(1500.0) == "Через 1,5 км");
        assert(std::string(loc::maneuver_action(ManeuverKind::Right)) == "Поверніть праворуч");
        assert(std::string(loc::maneuver_action(ManeuverKind::Left)) == "Поверніть ліворуч");
        assert(loc::roundabout_exit_text(2) == "На кільці — 2-й виїзд");
    }
    {
        Scoped scope(Language::FrenchCanadian);
        assert(loc::format_distance(6240.0) == "6,2 km");
        assert(loc::format_in_distance(290.0) == "Dans 290 m");
        assert(loc::format_duration_minutes(65) == "1 h 05 min");
        assert(loc::roundabout_exit_text(3) == "Au carrefour giratoire, prenez la sortie 3");
    }
    {
        Scoped scope(Language::SpanishMexican);
        assert(loc::format_distance(6240.0) == "6.2 km");  // Mexico uses the point
        assert(loc::format_in_distance(290.0) == "En 290 m");
        assert(std::string(loc::maneuver_action(ManeuverKind::UTurn)) == "Dé vuelta en U");
    }
}

void categories_and_labels()
{
    {
        Scoped scope(Language::English);
        assert(loc::poi_category("amenity_fuel") == "Fuel");
        assert(loc::poi_category("fuel") == "Fuel");
        assert(loc::poi_category("charging_station") == "Charging station");
        assert(loc::poi_category("aeroway_aerodrome") == "Aerodrome");
        assert(loc::poi_category("railway_station") == "Railway station");
        assert(loc::poi_category("tourism_guest_house") == "Guest house");
        assert(loc::poi_category("city") == "City");
        // Unknown keys read as English without the OSM key prefix.
        assert(loc::poi_category("amenity_foo_bar") == "Foo bar");
        assert(loc::poi_category("something_else") == "Something else");
        assert(loc::poi_category("").empty());
        assert(loc::route_reason("Fastest") == "Fastest");
        assert(loc::model_message("No offline results") == "No offline results");
    }
    {
        Scoped scope(Language::Ukrainian);
        assert(loc::poi_category("amenity_fuel") == "АЗС");
        assert(loc::poi_category("charging_station") == "Зарядна станція");
        assert(loc::poi_category("aeroway_aerodrome") == "Аеродром");
        assert(loc::poi_category("city") == "Місто");
        assert(loc::poi_category("amenity_foo_bar") == "Foo bar");  // unknown: English
        assert(loc::route_reason("Fastest") == "Найшвидший");
        assert(loc::route_reason("Alternative") == "Альтернативний");
        assert(loc::route_reason("Shorter") == "Коротший");
        assert(loc::route_reason("Custom label") == "Custom label");
        assert(loc::model_message("Calculating route…") == "Прокладання маршруту…");
        assert(loc::model_message("something else") == "something else");
    }
    {
        // A language with a gap in a category falls back to English.
        Scoped scope(Language::FrenchCanadian);
        assert(loc::poi_category("compressed_air") == "Compressed air");
        assert(loc::poi_category("amenity_pharmacy") == "Pharmacie");
    }
}

// Every character the UI can show from the table must be in each font
// (without its Montserrat fallback, which has no Cyrillic).
void font_coverage()
{
    std::set<char32_t> needed;
    for (const Language language : all_languages) {
        Scoped scope(language);
        for (int key = 0; key < static_cast<int>(Str::Count_); ++key)
            decode(loc::tr(language, static_cast<Str>(key)), needed);
        for (const char* category :
             {"fuel", "charging_station", "services", "rest_area", "hospital", "speed_camera",
              "parking", "parking_entrance", "car_repair", "tyres", "car_wash", "compressed_air",
              "vehicle_inspection", "car_parts", "car", "police", "pharmacy", "aerodrome",
              "airport", "station", "ferry_terminal", "toilets", "motel", "hotel", "guest_house",
              "camp_site", "supermarket", "convenience", "mall", "atm", "bank", "fast_food",
              "restaurant", "cafe", "clinic", "doctors", "bus_station", "subway_entrance",
              "marketplace", "museum", "attraction", "theatre", "cinema", "monument", "memorial",
              "castle", "ruins", "viewpoint", "park", "school", "university", "library",
              "post_office", "fire_station", "place_of_worship", "townhall", "bar", "pub",
              "bakery", "city", "town", "village", "hamlet", "suburb", "neighbourhood",
              "quarter", "borough", "locality", "isolated_dwelling", "farm", "square"})
            decode(loc::poi_category(category), needed);
        for (const char* english : {"Fastest", "Shorter", "Alternative", "Shortest", "Fewer turns"})
            decode(loc::route_reason(english), needed);
        for (int seconds : {1, 2, 5})
            decode(loc::arrival_countdown_text(seconds), needed);
        decode(loc::format_in_distance(1500.0), needed);
        decode(loc::format_duration_minutes(125), needed);
        decode(loc::roundabout_exit_text(2), needed);
    }
    // The Ukrainian letters that Russian-only subsets lack.
    for (const char32_t letter : {U'ї', U'Ї', U'є', U'Є', U'і', U'І', U'ґ', U'Ґ', U'’'})
        needed.insert(letter);
    // Accents of French and Spanish.
    for (const char32_t letter : {U'é', U'è', U'ê', U'à', U'â', U'ç', U'î', U'ô', U'û', U'ù',
                                  U'É', U'À', U'Ç', U'ñ', U'Ñ', U'á', U'í', U'ó', U'ú', U'ü',
                                  U'¿', U'¡'})
        needed.insert(letter);

    const lv_font_t* fonts[] = {
        &nav_font_14_400, &nav_font_14_600, &nav_font_14_700, &nav_font_14_800,
        &nav_font_20_400, &nav_font_20_600, &nav_font_20_700,
        &nav_font_24_400, &nav_font_24_600, &nav_font_24_700, &nav_font_24_800,
        &nav_font_28_700, &nav_font_28_800};
    int missing = 0;
    for (const lv_font_t* font : fonts) {
        for (const char32_t code : needed) {
            if (code < 0x20u)
                continue;
            lv_font_glyph_dsc_t glyph{};
            if (!font->get_glyph_dsc(font, &glyph, code, 0)) {
                std::fprintf(stderr, "font %p lacks U+%04X\n", static_cast<const void*>(font),
                             static_cast<unsigned>(code));
                ++missing;
            }
        }
    }
    assert(missing == 0);
}
} // namespace

void name_language_chain()
{
    using Names = std::vector<std::string>;
    assert(loc::name_languages(Language::Ukrainian) == Names{"uk"});
    assert(loc::name_languages(Language::English) == Names{"en"});
    assert((loc::name_languages(Language::FrenchCanadian) == Names{"fr", "en"}));
    assert((loc::name_languages(Language::SpanishMexican) == Names{"es", "en"}));
}

int main()
{
    name_language_chain();
    parse_and_detect();
    table_complete();
    plurals();
    formatters();
    categories_and_labels();
    font_coverage();
    std::puts("localization_test: ok");
    return 0;
}
