// Destination search prototypes (option A: OsmAnd OBF data, option B: a
// prebuilt index): the query parsing, name normalisation and ranking both
// share, so they differ only in how they reach the data.
//
// Normalisation ("fold"): lower case; і ї ы -> и, є э ё -> е, ґ -> г;
// apostrophes, ь and ъ dropped; Latin look-alikes inside a Cyrillic word
// mapped to Cyrillic. tests/search_proto/build_index.py mirrors it.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sq {

inline bool decode(const std::string& s, std::size_t& i, uint32_t& cp)
{
    const auto c = static_cast<unsigned char>(s[i]);
    int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
    if (extra < 0 || i + static_cast<std::size_t>(extra) >= s.size() + (extra ? 0 : 1)) {
        ++i;
        cp = 0xFFFD;
        return true;
    }
    cp = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k <= extra; ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
    i += static_cast<std::size_t>(extra) + 1;
    return true;
}

inline void encode(uint32_t cp, std::string& out)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline bool is_cyrillic(uint32_t cp) { return cp >= 0x400 && cp <= 0x4FF; }

// One code point, lower case and folded; 0 drops it.
inline uint32_t fold_cp(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z') cp += 0x20;
    else if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 0x20;     // Latin-1
    else if (cp >= 0x100 && cp <= 0x17F && cp != 0x130 && cp != 0x138 && cp != 0x149 &&
             cp != 0x17F) {                                          // Latin Extended-A
        // Upper/lower pairs are even/odd, except Ĺ..ň and Ź..ž (odd/even).
        if ((cp >= 0x139 && cp <= 0x148) || cp >= 0x179) {
            if (cp % 2) ++cp;
        } else {
            cp |= 1;
        }
    }
    else if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;
    else if (cp >= 0x400 && cp <= 0x40F) cp += 0x50;
    else if (cp == 0x490) cp = 0x491;
    switch (cp) {
    case 0x456: case 0x457: case 0x44B: return 0x438;          // і ї ы -> и
    case 0x454: case 0x44D: case 0x451: return 0x435;          // є э ё -> е
    case 0x491: return 0x433;                                  // ґ -> г
    case 0x44C: case 0x44A: return 0;                          // ь ъ
    case '\'': case '`': case 0x2019: case 0x2018: case 0x2BC: case 0x2032: return 0;
    default: return cp;
    }
}

inline uint32_t latin_lookalike(uint32_t cp)
{
    switch (cp) {
    case 'a': return 0x430; case 'c': return 0x441; case 'e': return 0x435;
    case 'i': return 0x438; case 'o': return 0x43E; case 'p': return 0x440;
    case 'x': return 0x445; case 'y': return 0x443; case 'k': return 0x43A;
    case 'm': return 0x43C; case 't': return 0x442;
    default: return cp;
    }
}

// Folds one word (no separators inside).
inline std::string fold_word(const std::string& word)
{
    std::vector<uint32_t> cps;
    bool cyrillic = false;
    for (std::size_t i = 0; i < word.size();) {
        uint32_t cp;
        decode(word, i, cp);
        cp = fold_cp(cp);
        if (!cp) continue;
        cyrillic = cyrillic || is_cyrillic(cp);
        cps.push_back(cp);
    }
    std::string out;
    for (uint32_t cp : cps) encode(cyrillic ? latin_lookalike(cp) : cp, out);
    return out;
}

// A word typed in Latin (Ukrainian national romanization, reversed) to
// Cyrillic, so that "dnipro" finds "Дніпро". Adjective "-yi" is "-ий".
inline std::string transliterate(const std::string& word)
{
    for (char c : word)
        if (static_cast<unsigned char>(c) >= 0x80) return word;
    std::string w;
    for (char c : word) w += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 0x20 : c);
    static const std::vector<std::pair<const char*, const char*>> table = {
        {"shch", "щ"}, {"zgh", "зг"}, {"zh", "ж"}, {"kh", "х"}, {"ts", "ц"}, {"ch", "ч"},
        {"sh", "ш"}, {"yu", "ю"}, {"ya", "я"}, {"ye", "є"}, {"yi", "ї"}, {"ie", "є"},
        {"iu", "ю"}, {"ia", "я"}, {"a", "а"}, {"b", "б"}, {"v", "в"}, {"w", "в"}, {"h", "г"},
        {"g", "ґ"}, {"d", "д"}, {"e", "е"}, {"z", "з"}, {"y", "и"}, {"i", "і"}, {"j", "й"},
        {"k", "к"}, {"l", "л"}, {"m", "м"}, {"n", "н"}, {"o", "о"}, {"p", "п"}, {"r", "р"},
        {"s", "с"}, {"t", "т"}, {"u", "у"}, {"f", "ф"}, {"c", "ц"}, {"x", "кс"}, {"q", "к"}};
    bool letters = false;
    for (char c : w) letters = letters || (c >= 'a' && c <= 'z');
    if (!letters) return word;
    std::string out;
    for (std::size_t i = 0; i < w.size();) {
        const bool end2 = i + 2 == w.size();
        if (end2 && (w.compare(i, 2, "yi") == 0 || w.compare(i, 2, "iy") == 0 ||
                     w.compare(i, 2, "yy") == 0)) {
            out += "ий";
            break;
        }
        bool hit = false;
        for (const auto& entry : table) {
            const std::size_t n = std::string(entry.first).size();
            if (w.compare(i, n, entry.first) == 0) {
                out += entry.second;
                i += n;
                hit = true;
                break;
            }
        }
        if (!hit) out += w[i++];
    }
    return out;
}

inline bool is_separator(uint32_t cp, bool keep_slash_dash)
{
    if (keep_slash_dash && (cp == '/' || cp == '-')) return false;
    return cp == ' ' || cp == '\t' || cp == ',' || cp == ';' || cp == '.' || cp == '-' ||
           cp == '/' || cp == '(' || cp == ')' || cp == '"' || cp == 0xAB || cp == 0xBB ||
           cp == 0x2013 || cp == 0x2014 || cp == ':' || cp == 0x2116 /* № */;
}

inline bool has_digit(const std::string& s)
{
    for (char c : s) if (c >= '0' && c <= '9') return true;
    return false;
}

inline std::vector<std::string> split(const std::string& text, bool keep_slash_dash)
{
    std::vector<std::string> out;
    std::string current;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        uint32_t cp;
        decode(text, i, cp);
        if (is_separator(cp, keep_slash_dash)) {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current.append(text, start, i - start);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

// Street-type and admin words: never required to match.
inline bool is_stopword(const std::string& folded)
{
    static const std::vector<std::string> words = [] {
        std::vector<std::string> list;
        for (const char* w : {"вул", "вулиця", "вулица", "ул", "улица", "просп", "проспект",
                              "пр", "пров", "провулок", "переулок", "пл", "площа", "площадь",
                              "бульв", "бульвар", "б", "р", "н", "шосе", "узвіз", "набережна",
                              "тупик", "проїзд", "житловий", "масив", "жм", "ж", "мкр",
                              "мікрорайон", "область", "обл", "район", "рн", "місто", "м",
                              "село", "с", "смт", "селище", "україна"})
            list.push_back(fold_word(w));
        return list;
    }();
    for (const auto& w : words) if (w == folded) return true;
    return false;
}

// Name -> folded tokens without stopwords (for index and matching).
inline std::vector<std::string> name_tokens(const std::string& name)
{
    std::vector<std::string> out;
    for (const auto& part : split(name, false)) {
        std::string folded = fold_word(part);
        if (!folded.empty() && !is_stopword(folded)) out.push_back(folded);
    }
    return out;
}

// Oblast adjectives ("Харківська") -> the region file key.
struct Region { const char* stem; const char* file_key; };
inline const std::vector<Region>& regions()
{
    static const std::vector<Region> table = {
        {"харківськ", "kharkiv"}, {"дніпропетровськ", "dnipro"}, {"київськ", "kyiv_"},
        {"львівськ", "lviv"}, {"одеськ", "odesa"}, {"запорізьк", "zaporizhzhya"},
        {"полтавськ", "poltava"}, {"сумськ", "sumy"}, {"чернігівськ", "chernihiv"},
        {"черкаськ", "cherkasy"}, {"чернівецьк", "chernivtsi"}, {"житомирськ", "zhytomyr"},
        {"вінницьк", "vinnytsya"}, {"хмельницьк", "khmelnytskyy"}, {"тернопільськ", "ternopil"},
        {"рівненськ", "rivne"}, {"волинськ", "volyn"}, {"закарпатськ", "transcarpathia"},
        {"франківськ", "ivano-frankivsk"}, {"кіровоградськ", "kropyvnytskyi"},
        {"миколаївськ", "mykolayiv"}, {"херсонськ", "kherson"}, {"донецьк", "donetsk"},
        {"луганськ", "luhansk"}, {"кримськ", "crimea"}};
    return table;
}

inline const char* region_of(const std::string& folded)
{
    for (const auto& region : regions()) {
        const std::string stem = fold_word(region.stem);
        if (folded.size() <= stem.size() || folded.compare(0, stem.size(), stem) != 0)
            continue;
        const std::string ending = folded.substr(stem.size());
        for (const char* e : {"а", "ой", "ий", "у", "ои"})
            if (ending == fold_word(e)) return region.file_key;
    }
    return nullptr;
}

// House number key: folded, only letters, digits and '/' ("4-Г" -> "4г",
// "184, 4" -> "1844").
inline std::string house_key(const std::string& number)
{
    std::string out;
    const std::string folded = fold_word(number);
    for (std::size_t i = 0; i < folded.size();) {
        const std::size_t start = i;
        uint32_t cp;
        decode(folded, i, cp);
        if ((cp >= '0' && cp <= '9') || cp == '/' || (cp >= 'a' && cp <= 'z') || is_cyrillic(cp))
            out.append(folded, start, i - start);
    }
    return out;
}

struct QueryWord {
    std::string text;      // folded
    bool incomplete;       // last word while typing: prefix match only
};

struct Query {
    std::vector<QueryWord> words;  // name words
    std::string house;             // folded house number, may be empty
    const char* region = nullptr;  // region file key named in the query
    std::string region_word;       // the oblast adjective as typed (folded)
    bool region_explicit = false;  // followed by "область"/"обл"
    double bonus = 0;              // added to every result of this reading
};

inline Query parse(const std::string& text)
{
    Query query;
    const bool ends_open = !text.empty() && text.back() != ' ' && text.back() != ',';
    const auto parts = split(text, true);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const bool last = i + 1 == parts.size();
        if (has_digit(parts[i])) {
            query.house += house_key(parts[i]);  // "184, 4" arrives as two parts
            continue;
        }
        const std::size_t words_before = query.words.size();
        for (const auto& piece : split(parts[i], false)) {
            const std::string folded = fold_word(transliterate(piece));
            if (!query.region_word.empty() && query.region_explicit == false &&
                (folded == fold_word("область") || folded == fold_word("обл")))
                query.region_explicit = true;
            if (folded.empty() || is_stopword(folded)) continue;
            if (const char* region = region_of(folded); region && !query.region) {
                query.region = region;
                query.region_word = folded;
                continue;
            }
            query.words.push_back({folded, false});
        }
        if (last && ends_open && query.words.size() > words_before)
            query.words.back().incomplete = true;
    }
    return query;
}

// Readings of a query: an oblast adjective ("Сумська") is also a street
// name, so without "область" both are searched and ranked together.
inline std::vector<Query> readings(const Query& parsed)
{
    std::vector<Query> out{parsed};
    if (!parsed.region) return out;
    out[0].bonus = parsed.region_explicit ? 60 : 10;
    Query plain = parsed;
    plain.region = nullptr;
    plain.words.push_back({parsed.region_word, false});
    out.push_back(plain);
    return out;
}

// How a query word matches a name token: 2 exact, 1 prefix, 0 none.
inline int word_match(const QueryWord& word, const std::string& token)
{
    if (word.text == token) return 2;
    const std::size_t min_prefix = word.incomplete ? 1u : 6u;  // bytes (3 Cyrillic letters)
    if (word.text.size() >= min_prefix && token.size() > word.text.size() &&
        token.compare(0, word.text.size(), word.text) == 0)
        return 1;
    return 0;
}

// A name covered by query words: every name token matched by a distinct
// word (used for cities: "Лозова" must not take "Лозова Друга").
// Returns the matched words as a bit mask, or 0.
inline uint32_t cover_name(const std::vector<QueryWord>& words, uint32_t available,
                           const std::vector<std::string>& tokens, int& exact)
{
    if (tokens.empty()) return 0;
    uint32_t used = 0;
    exact = 0;
    for (const auto& token : tokens) {
        int best = 0;
        std::size_t best_word = 0;
        for (std::size_t w = 0; w < words.size() && w < 32; ++w) {
            if (!(available & (1u << w)) || (used & (1u << w))) continue;
            const int m = word_match(words[w], token);
            if (m > best) { best = m; best_word = w; }
        }
        if (!best) return 0;
        used |= 1u << best_word;
        exact += best == 2;
    }
    return used;
}

// The remaining query words all found in a name (streets: "шевченка" finds
// "Тараса Шевченка"). Returns exact-match count or -1.
inline int words_in_name(const std::vector<QueryWord>& words, uint32_t mask,
                         const std::vector<std::string>& tokens)
{
    int exact = 0;
    for (std::size_t w = 0; w < words.size() && w < 32; ++w) {
        if (!(mask & (1u << w))) continue;
        int best = 0;
        for (const auto& token : tokens) best = std::max(best, word_match(words[w], token));
        if (!best) return -1;
        exact += best == 2;
    }
    return exact;
}

// House number match: 2 equal, 1 the number with a suffix ("4" -> "4г").
inline int house_match(const std::string& wanted, const std::string& folded_number)
{
    if (wanted.empty()) return 0;
    if (folded_number == wanted) return 2;
    if (folded_number.size() > wanted.size() &&
        folded_number.compare(0, wanted.size(), wanted) == 0 &&
        !(folded_number[wanted.size()] >= '0' && folded_number[wanted.size()] <= '9'))
        return 1;
    return 0;
}

inline double km(double lat1, double lon1, double lat2, double lon2)
{
    const double r = 6371.0, d = 3.14159265358979 / 180.0;
    const double a = std::sin((lat2 - lat1) * d / 2), b = std::sin((lon2 - lon1) * d / 2);
    const double h = a * a + std::cos(lat1 * d) * std::cos(lat2 * d) * b * b;
    return 2 * r * std::asin(std::sqrt(h));
}

// OsmAnd city subtypes: City 0, Town 1, Village 2, Hamlet 3, Suburb 4.
inline double subtype_bonus(int subtype)
{
    switch (subtype) {
    case 0: return 30; case 1: return 20; case 2: return 10; case 3: return 5; case 4: return 8;
    default: return 0;
    }
}

enum class Kind { City, Street, House };

struct Result {
    Kind kind;
    std::string title;  // "Дніпро, вулиця Червоний камінь, 4"
    double lat, lon;
    double score;
};

// Shared ranking: kind fit, exact words, settlement size, distance, typos.
inline double score(Kind kind, const Query& q, int exact_words, int unmatched_tokens,
                    int subtype, double km_from_user, int house_quality, int typos,
                    bool in_user_region)
{
    // A settlement named by the whole query beats a street of that name.
    double s = kind == Kind::House ? 300 : kind == Kind::Street ? 200 : 220;
    if (!q.house.empty() && kind != Kind::House) s -= 120;  // asked for a house
    // Every query word is matched; the ones only by prefix cost.
    s += 20.0 * exact_words - 15.0 * (static_cast<double>(q.words.size()) - exact_words) -
         3.0 * unmatched_tokens + q.bonus;
    if (kind == Kind::City) s += subtype_bonus(subtype);
    s -= 10.0 * std::log10(1.0 + km_from_user);
    if (house_quality == 1) s -= 10;
    s -= 25.0 * typos;
    if (in_user_region) s += 15;
    return s;
}

}  // namespace sq
