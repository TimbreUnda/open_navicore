#include "search/search_index.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace navicore_osmand::search {
namespace {

static_assert(sizeof(int32_t) == 4, "index layout");

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

std::vector<uint32_t> codepoints(const char* s, std::size_t n)
{
    std::vector<uint32_t> out;
    const std::string text(s, n);
    for (std::size_t i = 0; i < text.size();) {
        uint32_t cp;
        decode(text, i, cp);
        out.push_back(cp);
    }
    return out;
}

int bounded_levenshtein(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int bound)
{
    const std::size_t n = a.size(), m = b.size();
    if ((n > m ? n - m : m - n) > static_cast<std::size_t>(bound)) return bound + 1;
    std::vector<int> prev(m + 1), cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        int row_min = cur[0];
        for (std::size_t j = 1; j <= m; ++j) {
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            row_min = std::min(row_min, cur[j]);
        }
        if (row_min > bound) return bound + 1;
        std::swap(prev, cur);
    }
    return prev[m];
}

}  // namespace

struct SearchIndex::Context {
    Point origin;
    const std::vector<Point>* recent;
    int user_file = -1;
    std::vector<Hit> hits;

    double km_to(double lat, double lon) const
    {
        return km(origin.latitude, origin.longitude, lat, lon);
    }
    bool is_recent(double lat, double lon) const
    {
        for (const auto& r : *recent)
            if (km(r.latitude, r.longitude, lat, lon) < 0.1) return true;
        return false;
    }
};

SearchIndex::~SearchIndex()
{
    if (base_) ::munmap(const_cast<char*>(base_), size_);
}

bool SearchIndex::open(const std::string& path)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size < 8 + 18 * 4) {
        ::close(fd);
        return false;
    }
    const std::size_t size = static_cast<std::size_t>(st.st_size);
    void* map = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) return false;
    const char* base = static_cast<const char*>(map);
    uint32_t offset[9];
    std::memcpy(count_, base + 8, sizeof(count_));
    std::memcpy(offset, base + 8 + sizeof(count_), sizeof(offset));
    bool valid = std::memcmp(base, "ONSX0001", 8) == 0;
    for (uint32_t o : offset) valid = valid && o <= size;
    if (!valid) {
        ::munmap(map, size);
        return false;
    }
    base_ = base;
    size_ = size;
    files_ = reinterpret_cast<const FileRow*>(base + offset[0]);
    cities_ = reinterpret_cast<const CityRow*>(base + offset[1]);
    streets_ = reinterpret_cast<const StreetRow*>(base + offset[2]);
    houses_ = reinterpret_cast<const HouseRow*>(base + offset[3]);
    city_tokens_ = reinterpret_cast<const TokenRow*>(base + offset[4]);
    street_tokens_ = reinterpret_cast<const TokenRow*>(base + offset[5]);
    city_postings_ = reinterpret_cast<const uint32_t*>(base + offset[6]);
    street_postings_ = reinterpret_cast<const uint32_t*>(base + offset[7]);
    pool_ = base + offset[8];
    return true;
}

// Rows of a sorted token table matching a word (exact, or by prefix where
// word_match allows it).
std::pair<uint32_t, uint32_t> SearchIndex::token_range(const TokenRow* table, uint32_t n,
                                                       const QueryWord& word) const
{
    const std::string& key = word.text;
    const TokenRow* first = std::lower_bound(table, table + n, key,
        [this](const TokenRow& row, const std::string& k) {
            const int c = std::memcmp(pool_ + row.tok_off, k.data(),
                                      std::min<std::size_t>(row.tok_len, k.size()));
            return c < 0 || (c == 0 && row.tok_len < k.size());
        });
    const bool prefix = word.incomplete || key.size() >= 6;
    const TokenRow* last = first;
    while (last != table + n && last->tok_len >= key.size() &&
           std::memcmp(pool_ + last->tok_off, key.data(), key.size()) == 0) {
        if (!prefix && last->tok_len != key.size()) break;
        ++last;
    }
    return {static_cast<uint32_t>(first - table), static_cast<uint32_t>(last - table)};
}

// The closest other dictionary word (1 edit, 2 for long words), the most
// used one on a tie.
bool SearchIndex::correct(QueryWord& word) const
{
    const auto wanted = codepoints(word.text.data(), word.text.size());
    if (wanted.size() < 4) return false;
    const int bound = wanted.size() >= 8 ? 2 : 1;
    int best_distance = bound + 1;
    uint32_t best_count = 0;
    std::string best;
    for (int dict = 0; dict < 2; ++dict) {
        const TokenRow* table = dict ? street_tokens_ : city_tokens_;
        const uint32_t n = dict ? count_[5] : count_[4];
        for (uint32_t i = 0; i < n; ++i) {
            const int d = bounded_levenshtein(
                wanted, codepoints(pool_ + table[i].tok_off, table[i].tok_len), bound);
            if (d == 0) continue;
            if (d < best_distance || (d == best_distance && table[i].count > best_count)) {
                best_distance = d;
                best_count = table[i].count;
                best = str(table[i].tok_off, table[i].tok_len);
            }
        }
    }
    if (best_distance > bound) return false;
    word.text = best;
    word.incomplete = false;
    return true;
}

std::vector<SearchIndex::Hit> SearchIndex::search(const std::string& text, Point origin,
                                                  const std::vector<Point>& recent,
                                                  std::size_t limit) const
{
    Context ctx{origin, &recent, -1, {}};
    if (!base_) return {};
    double best_area = 1e30;  // the user's region: the smallest file box around them
    const int32_t lat = static_cast<int32_t>(origin.latitude * 1e6);
    const int32_t lon = static_cast<int32_t>(origin.longitude * 1e6);
    for (uint32_t f = 0; f < count_[0]; ++f) {
        const auto& r = files_[f];
        if (lat < r.min_lat || lat > r.max_lat || lon < r.min_lon || lon > r.max_lon) continue;
        const double area = double(r.max_lat - r.min_lat) * double(r.max_lon - r.min_lon);
        if (area < best_area) { best_area = area; ctx.user_file = static_cast<int>(f); }
    }

    const Query parsed = parse(text);
    for (const auto& reading : readings(parsed)) search_reading(reading, 0, ctx);
    // Nothing found: a typo that is a word elsewhere ("камєнь"); replace one
    // word, longest first, by its closest dictionary word.
    std::vector<std::size_t> order(parsed.words.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return parsed.words[a].text.size() > parsed.words[b].text.size();
    });
    for (std::size_t w : order) {
        if (!ctx.hits.empty()) break;
        Query fixed = parsed;
        if (!correct(fixed.words[w])) continue;
        for (const auto& reading : readings(fixed)) search_reading(reading, 1, ctx);
    }

    std::stable_sort(ctx.hits.begin(), ctx.hits.end(),
                     [](const Hit& a, const Hit& b) { return a.score > b.score; });
    // One entry per place: OSM repeats a house per entrance, a street per way.
    std::vector<Hit> unique;
    for (auto& hit : ctx.hits) {
        bool duplicate = false;
        for (const auto& u : unique)
            if (u.kind == hit.kind &&
                km(u.latitude, u.longitude, hit.latitude, hit.longitude) < 0.15) {
                duplicate = true;
                break;
            }
        if (!duplicate) unique.push_back(std::move(hit));
        if (unique.size() >= limit) break;
    }
    return unique;
}

void SearchIndex::search_reading(Query q, int typos, Context& ctx) const
{
    if (q.words.empty()) return;
    if (q.words.size() > 31) q.words.resize(31);
    for (auto& word : q.words) {
        const auto c = token_range(city_tokens_, count_[4], word);
        const auto s = token_range(street_tokens_, count_[5], word);
        if (c.first == c.second && s.first == s.second && correct(word)) ++typos;
    }
    int region = -1;
    if (q.region)
        for (uint32_t f = 0; f < count_[0]; ++f)
            if (str(files_[f].name_off, files_[f].name_len).find(q.region) != std::string::npos)
                region = static_cast<int>(f);
    const uint32_t all = (1u << q.words.size()) - 1;

    // A settlement named by some of the words, then its streets.
    std::vector<uint32_t> candidates;
    for (const auto& word : q.words) {
        const auto range = token_range(city_tokens_, count_[4], word);
        for (uint32_t t = range.first; t < range.second; ++t)
            for (uint32_t p = 0; p < city_tokens_[t].count; ++p)
                candidates.push_back(city_postings_[city_tokens_[t].first + p]);
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    for (uint32_t c : candidates) {
        const auto& city = cities_[c];
        if (region >= 0 && city.file != region) continue;
        int exact = 0;
        const uint32_t used = cover_name(q.words, all, tokens_of(str(city.tok_off, city.tok_len)), exact);
        if (!used) continue;
        const uint32_t rest = all & ~used;
        if (!rest) {
            add_city(c, q, exact, typos, ctx);
            continue;
        }
        for (uint32_t s = city.street_first; s < city.street_first + city.street_count; ++s)
            add_street(s, q, rest, exact, typos, ctx);
    }
    // A street without its settlement ("хрещатик 5"): every word in the name.
    int rarest = -1;
    uint32_t rarest_count = ~0u;
    for (std::size_t w = 0; w < q.words.size(); ++w) {
        const auto range = token_range(street_tokens_, count_[5], q.words[w]);
        uint32_t n = 0;
        for (uint32_t t = range.first; t < range.second; ++t) n += street_tokens_[t].count;
        if (n < rarest_count) { rarest_count = n; rarest = static_cast<int>(w); }
    }
    if (rarest < 0 || rarest_count == 0 || rarest_count >= 50000) return;
    const auto range = token_range(street_tokens_, count_[5], q.words[static_cast<std::size_t>(rarest)]);
    for (uint32_t t = range.first; t < range.second; ++t)
        for (uint32_t p = 0; p < street_tokens_[t].count; ++p) {
            const uint32_t s = street_postings_[street_tokens_[t].first + p];
            if (region >= 0 && cities_[streets_[s].city].file != region) continue;
            add_street(s, q, all, 0, typos, ctx);
        }
}

void SearchIndex::add_city(uint32_t c, const Query& q, int exact, int typos, Context& ctx) const
{
    const auto& city = cities_[c];
    const double lat = city.lat / 1e6, lon = city.lon / 1e6;
    ctx.hits.push_back({Kind::City, str(city.title_off, city.title_len), std::string(), lat, lon,
                        score(Kind::City, q, exact, 0, city.subtype, ctx.km_to(lat, lon), 0, typos,
                              city.file == ctx.user_file, ctx.is_recent(lat, lon))});
}

void SearchIndex::add_street(uint32_t s, const Query& q, uint32_t mask, int city_exact, int typos,
                             Context& ctx) const
{
    const auto& street = streets_[s];
    const auto tokens = tokens_of(str(street.tok_off, street.tok_len));
    const int exact = words_in_name(q.words, mask, tokens);
    if (exact < 0) return;
    const auto& city = cities_[street.city];
    const int unmatched = std::max(0, static_cast<int>(tokens.size()) - __builtin_popcount(mask));
    const std::string name = str(street.title_off, street.title_len);
    const std::string place = str(city.title_off, city.title_len);
    const bool home = city.file == ctx.user_file;
    const double lat = street.lat / 1e6, lon = street.lon / 1e6;
    ctx.hits.push_back({Kind::Street, name, place, lat, lon,
                        score(Kind::Street, q, exact + city_exact, unmatched, city.subtype,
                              ctx.km_to(lat, lon), 0, typos, home, ctx.is_recent(lat, lon))});
    if (q.house.empty()) return;
    for (uint32_t h = street.house_first; h < street.house_first + street.house_count; ++h) {
        const auto& house = houses_[h];
        const std::string number = str(house.number_off, house.number_len);
        const int quality = house_match(q.house, house_key(number));
        if (!quality) continue;
        const double hl = house.lat / 1e6, ho = house.lon / 1e6;
        ctx.hits.push_back({Kind::House, name + ", " + number, place, hl, ho,
                            score(Kind::House, q, exact + city_exact, unmatched, city.subtype,
                                  ctx.km_to(hl, ho), quality, typos, home, ctx.is_recent(hl, ho))});
    }
}

}  // namespace navicore_osmand::search
