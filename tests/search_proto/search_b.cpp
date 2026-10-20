// Search option B: the prebuilt index of build_index.py, memory mapped.
//
//   search_b INDEX QUERIES.tsv
#include "harness.h"

#include <cstring>
#include <fcntl.h>
#include <malloc.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

#pragma pack(push, 1)
struct FileRow { uint32_t name_off, name_len; int32_t min_lat, min_lon, max_lat, max_lon; };
struct CityRow {
    uint16_t file; int16_t subtype; uint32_t title_off; uint16_t title_len, tok_len;
    uint32_t tok_off; int32_t lat, lon; uint32_t street_first, street_count;
};
struct StreetRow {
    uint32_t city, title_off; uint16_t title_len, tok_len; uint32_t tok_off;
    int32_t lat, lon; uint32_t house_first, house_count;
};
struct HouseRow { uint32_t number_off, number_len; int32_t lat, lon; };
struct TokenRow { uint32_t tok_off, tok_len, first, count; };
#pragma pack(pop)
static_assert(sizeof(CityRow) == 32 && sizeof(StreetRow) == 32, "index layout");

struct Index {
    const char* base = nullptr;
    std::size_t size = 0;
    uint32_t count[9] = {};
    const FileRow* files; const CityRow* cities; const StreetRow* streets;
    const HouseRow* houses; const TokenRow* city_tokens; const TokenRow* street_tokens;
    const uint32_t* city_postings; const uint32_t* street_postings; const char* pool;

    bool open(const char* path)
    {
        const int fd = ::open(path, O_RDONLY);
        if (fd < 0) return false;
        struct stat st {};
        ::fstat(fd, &st);
        size = static_cast<std::size_t>(st.st_size);
        void* map = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);
        if (map == MAP_FAILED || std::memcmp(map, "ONSX0001", 8) != 0) return false;
        base = static_cast<const char*>(map);
        uint32_t offset[9];
        std::memcpy(count, base + 8, sizeof(count));
        std::memcpy(offset, base + 8 + sizeof(count), sizeof(offset));
        files = reinterpret_cast<const FileRow*>(base + offset[0]);
        cities = reinterpret_cast<const CityRow*>(base + offset[1]);
        streets = reinterpret_cast<const StreetRow*>(base + offset[2]);
        houses = reinterpret_cast<const HouseRow*>(base + offset[3]);
        city_tokens = reinterpret_cast<const TokenRow*>(base + offset[4]);
        street_tokens = reinterpret_cast<const TokenRow*>(base + offset[5]);
        city_postings = reinterpret_cast<const uint32_t*>(base + offset[6]);
        street_postings = reinterpret_cast<const uint32_t*>(base + offset[7]);
        pool = base + offset[8];
        return true;
    }
    std::string str(uint32_t off, uint32_t len) const { return std::string(pool + off, len); }
};

std::vector<std::string> tokens_of(const std::string& joined)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < joined.size()) {
        std::size_t end = joined.find(' ', start);
        if (end == std::string::npos) end = joined.size();
        out.push_back(joined.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

// Rows of a sorted token table matching a query word (exact, or prefix
// where word_match allows it).
std::pair<uint32_t, uint32_t> token_range(const Index& ix, const TokenRow* table, uint32_t n,
                                         const sq::QueryWord& word)
{
    auto less = [&](const TokenRow& row, const std::string& key) {
        return std::memcmp(ix.pool + row.tok_off, key.data(),
                           std::min<std::size_t>(row.tok_len, key.size())) < 0 ||
               (std::memcmp(ix.pool + row.tok_off, key.data(),
                            std::min<std::size_t>(row.tok_len, key.size())) == 0 &&
                row.tok_len < key.size());
    };
    const TokenRow* first = std::lower_bound(table, table + n, word.text, less);
    const TokenRow* last = first;
    const bool prefix = word.incomplete || word.text.size() >= 6;
    while (last != table + n && last->tok_len >= word.text.size() &&
           std::memcmp(ix.pool + last->tok_off, word.text.data(), word.text.size()) == 0) {
        if (!prefix && last->tok_len != word.text.size()) break;
        ++last;
    }
    return {static_cast<uint32_t>(first - table), static_cast<uint32_t>(last - table)};
}

std::vector<uint32_t> codepoints(const char* s, std::size_t n)
{
    std::vector<uint32_t> out;
    const std::string text(s, n);
    for (std::size_t i = 0; i < text.size();) {
        uint32_t cp;
        sq::decode(text, i, cp);
        out.push_back(cp);
    }
    return out;
}

int bounded_levenshtein(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int bound)
{
    const int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
    if (std::abs(n - m) > bound) return bound + 1;
    std::vector<int> prev(static_cast<std::size_t>(m) + 1), cur(static_cast<std::size_t>(m) + 1);
    for (int j = 0; j <= m; ++j) prev[static_cast<std::size_t>(j)] = j;
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        int row_min = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = a[static_cast<std::size_t>(i - 1)] == b[static_cast<std::size_t>(j - 1)] ? 0 : 1;
            cur[static_cast<std::size_t>(j)] = std::min({prev[static_cast<std::size_t>(j)] + 1,
                                                         cur[static_cast<std::size_t>(j - 1)] + 1,
                                                         prev[static_cast<std::size_t>(j - 1)] + cost});
            row_min = std::min(row_min, cur[static_cast<std::size_t>(j)]);
        }
        if (row_min > bound) return bound + 1;
        std::swap(prev, cur);
    }
    return prev[static_cast<std::size_t>(m)];
}

// A word found in neither dictionary: the closest token (1 edit, 2 for
// long words), preferring the most used one.
bool correct(const Index& ix, sq::QueryWord& word)
{
    const auto wanted = codepoints(word.text.data(), word.text.size());
    const int bound = wanted.size() >= 8 ? 2 : 1;
    if (wanted.size() < 4) return false;
    int best_distance = bound + 1;
    uint32_t best_count = 0;
    std::string best;
    for (int dict = 0; dict < 2; ++dict) {
        const TokenRow* table = dict ? ix.street_tokens : ix.city_tokens;
        const uint32_t n = dict ? ix.count[5] : ix.count[4];
        for (uint32_t i = 0; i < n; ++i) {
            const int d = bounded_levenshtein(
                wanted, codepoints(ix.pool + table[i].tok_off, table[i].tok_len), bound);
            if (d == 0) continue;  // the word itself: looking for another
            if (d < best_distance || (d == best_distance && table[i].count > best_count)) {
                best_distance = d;
                best_count = table[i].count;
                best = ix.str(table[i].tok_off, table[i].tok_len);
            }
        }
    }
    if (best_distance > bound) return false;
    word.text = best;
    word.incomplete = false;
    return true;
}

class SearchB {
public:
    explicit SearchB(const Index& ix) : ix_(ix)
    {
        // The user's region: the smallest file box around the position.
        double best_area = 1e30;
        for (uint32_t f = 0; f < ix.count[0]; ++f) {
            const auto& r = ix.files[f];
            const int32_t lat = static_cast<int32_t>(sq::kUserLat * 1e6);
            const int32_t lon = static_cast<int32_t>(sq::kUserLon * 1e6);
            if (lat < r.min_lat || lat > r.max_lat || lon < r.min_lon || lon > r.max_lon) continue;
            const double area = double(r.max_lat - r.min_lat) * double(r.max_lon - r.min_lon);
            if (area < best_area) { best_area = area; user_file_ = static_cast<int>(f); }
        }
    }

    std::vector<sq::Result> operator()(const std::string& text) const
    {
        const sq::Query parsed = sq::parse(text);
        std::vector<sq::Result> out;
        for (const auto& reading : sq::readings(parsed)) search(reading, 0, out);
        // Nothing found: a typo that happens to be a word elsewhere
        // ("камєнь"); replace one word, longest first, by its closest token.
        std::vector<std::size_t> order(parsed.words.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return parsed.words[a].text.size() > parsed.words[b].text.size();
        });
        for (std::size_t w : order) {
            if (!out.empty()) break;
            sq::Query fixed = parsed;
            if (!correct(ix_, fixed.words[w])) continue;
            for (const auto& reading : sq::readings(fixed)) search(reading, 1, out);
        }
        sq::rank(out);
        return out;
    }

private:
    void search(sq::Query q, int typos, std::vector<sq::Result>& out) const
    {
        if (q.words.empty()) return;
        if (q.words.size() > 31) q.words.resize(31);
        for (auto& word : q.words) {
            const auto c = token_range(ix_, ix_.city_tokens, ix_.count[4], word);
            const auto s = token_range(ix_, ix_.street_tokens, ix_.count[5], word);
            if (c.first == c.second && s.first == s.second && correct(ix_, word)) ++typos;
        }
        int region = -1;
        if (q.region)
            for (uint32_t f = 0; f < ix_.count[0]; ++f)
                if (ix_.str(ix_.files[f].name_off, ix_.files[f].name_len).find(q.region) !=
                    std::string::npos)
                    region = static_cast<int>(f);
        const uint32_t all = (1u << q.words.size()) - 1;

        // City first: cities named by some of the words.
        std::vector<uint32_t> candidates;
        for (const auto& word : q.words) {
            const auto range = token_range(ix_, ix_.city_tokens, ix_.count[4], word);
            for (uint32_t t = range.first; t < range.second; ++t)
                for (uint32_t p = 0; p < ix_.city_tokens[t].count; ++p)
                    candidates.push_back(ix_.city_postings[ix_.city_tokens[t].first + p]);
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        for (uint32_t c : candidates) {
            const auto& city = ix_.cities[c];
            if (region >= 0 && city.file != region) continue;
            int exact = 0;
            const uint32_t used = sq::cover_name(q.words, all,
                                                 tokens_of(ix_.str(city.tok_off, city.tok_len)), exact);
            if (!used) continue;
            const uint32_t rest = all & ~used;
            if (!rest) {
                add_city(c, q, exact, typos, out);
                continue;
            }
            for (uint32_t s = city.street_first; s < city.street_first + city.street_count; ++s)
                add_street(s, q, rest, exact, typos, out);
        }
        // Street alone ("хрещатик 5"): every word in the street name.
        int rarest = -1;
        uint32_t rarest_count = ~0u;
        for (std::size_t w = 0; w < q.words.size(); ++w) {
            const auto range = token_range(ix_, ix_.street_tokens, ix_.count[5], q.words[w]);
            uint32_t n = 0;
            for (uint32_t t = range.first; t < range.second; ++t) n += ix_.street_tokens[t].count;
            if (n < rarest_count) { rarest_count = n; rarest = static_cast<int>(w); }
        }
        if (rarest >= 0 && rarest_count > 0 && rarest_count < 50000) {
            const auto range = token_range(ix_, ix_.street_tokens, ix_.count[5],
                                           q.words[static_cast<std::size_t>(rarest)]);
            for (uint32_t t = range.first; t < range.second; ++t)
                for (uint32_t p = 0; p < ix_.street_tokens[t].count; ++p) {
                    const uint32_t s = ix_.street_postings[ix_.street_tokens[t].first + p];
                    if (region >= 0 && ix_.cities[ix_.streets[s].city].file != region) continue;
                    add_street(s, q, all, 0, typos, out);
                }
        }
    }

    void add_city(uint32_t c, const sq::Query& q, int exact, int typos,
                  std::vector<sq::Result>& out) const
    {
        const auto& city = ix_.cities[c];
        const double lat = city.lat / 1e6, lon = city.lon / 1e6;
        out.push_back({sq::Kind::City, ix_.str(city.title_off, city.title_len), lat, lon,
                       sq::score(sq::Kind::City, q, exact, 0, city.subtype,
                                 sq::km(sq::kUserLat, sq::kUserLon, lat, lon), 0, typos,
                                 city.file == user_file_)});
    }

    void add_street(uint32_t s, const sq::Query& q, uint32_t mask, int city_exact, int typos,
                    std::vector<sq::Result>& out) const
    {
        const auto& street = ix_.streets[s];
        const auto tokens = tokens_of(ix_.str(street.tok_off, street.tok_len));
        const int exact = sq::words_in_name(q.words, mask, tokens);
        if (exact < 0) return;
        const auto& city = ix_.cities[street.city];
        const int words = __builtin_popcount(mask);
        const int unmatched = std::max(0, static_cast<int>(tokens.size()) - words);
        const std::string title = ix_.str(city.title_off, city.title_len) + ", " +
                                  ix_.str(street.title_off, street.title_len);
        const bool home = city.file == user_file_;
        const double lat = street.lat / 1e6, lon = street.lon / 1e6;
        out.push_back({sq::Kind::Street, title, lat, lon,
                       sq::score(sq::Kind::Street, q, exact + city_exact, unmatched, city.subtype,
                                 sq::km(sq::kUserLat, sq::kUserLon, lat, lon), 0, typos, home)});
        if (q.house.empty()) return;
        for (uint32_t h = street.house_first; h < street.house_first + street.house_count; ++h) {
            const auto& house = ix_.houses[h];
            const std::string number = ix_.str(house.number_off, house.number_len);
            const int quality = sq::house_match(q.house, sq::fold_word(number));
            if (!quality) continue;
            const double hl = house.lat / 1e6, ho = house.lon / 1e6;
            out.push_back({sq::Kind::House, title + ", " + number, hl, ho,
                           sq::score(sq::Kind::House, q, exact + city_exact, unmatched,
                                     city.subtype, sq::km(sq::kUserLat, sq::kUserLon, hl, ho),
                                     quality, typos, home)});
        }
    }

    const Index& ix_;
    int user_file_ = -1;
};

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: search_b INDEX QUERIES.tsv\n");
        return 2;
    }
    const double start = sq::now_ms();
    Index ix;
    if (!ix.open(argv[1])) return 3;
    SearchB search(ix);
    std::printf("B init_ms=%.1f index_bytes=%zu heap_bytes=%d\n", sq::now_ms() - start, ix.size,
                mallinfo().uordblks);
    sq::run_queries("B", argv[2], [&](const std::string& q) { return search(q); });
    std::printf("B heap_bytes_after=%d\n", mallinfo().uordblks);
    return 0;
}
