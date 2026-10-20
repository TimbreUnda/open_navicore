// Search option A: the OsmAnd OBF data with query understanding. The
// settlements of all regions are cached at start; streets and houses are
// read from the OBF per settlement and cached. Parsing and ranking are
// shared with option B (search_proto/).
//
//   osmand_search_a_smoke MAPDIR QUERIES.tsv
#include <OsmAndCore.h>
#include <OsmAndCore/CoreResourcesEmbeddedBundle.h>
#include <OsmAndCore/ObfDataInterface.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Utilities.h>
#include <OsmAndCore/Data/Building.h>
#include <OsmAndCore/Data/Street.h>
#include <OsmAndCore/Data/StreetGroup.h>

#include "search_proto/harness.h"

#include <cstdlib>
#include <dirent.h>
#include <malloc.h>
#include <map>
#include <memory>
#include <unordered_map>

namespace {

std::string utf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::string title_of(const OsmAnd::Address& address)
{
    const auto it = address.localizedNames.constFind(QStringLiteral("uk"));
    return utf8(it == address.localizedNames.cend() || it->isEmpty() ? address.nativeName : *it);
}

OsmAnd::LatLon latlon(const OsmAnd::PointI& p) { return OsmAnd::Utilities::convert31ToLatLon(p); }

struct HouseEntry { std::string number, folded; double lat, lon; };
struct StreetEntry {
    std::string title;
    std::vector<std::string> tokens;
    double lat, lon;
    std::shared_ptr<const OsmAnd::Street> street;
    bool houses_loaded = false;
    std::vector<HouseEntry> houses;
};
struct CityEntry {
    int file;
    int subtype;
    std::string title;
    std::vector<std::string> tokens;
    double lat, lon;
    std::shared_ptr<const OsmAnd::StreetGroup> group;
    bool streets_loaded = false;
    std::vector<StreetEntry> streets;
};

class SearchA {
public:
    bool open(const std::string& dir)
    {
        DIR* handle = ::opendir(dir.c_str());
        if (!handle) return false;
        std::vector<std::string> names;
        while (const dirent* entry = ::readdir(handle)) {
            const std::string name = entry->d_name;
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".obf") == 0 &&
                name.find("basemap") == std::string::npos)
                names.push_back(name);
        }
        ::closedir(handle);
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            auto obfs = std::make_shared<OsmAnd::ObfsCollection>();
            obfs->addFile(QString::fromLocal8Bit((dir + "/" + name).c_str()));
            files_.push_back({name, obfs->obtainDataInterface(), obfs});
            const int file = static_cast<int>(files_.size()) - 1;
            QList<std::shared_ptr<const OsmAnd::StreetGroup>> groups;
            files_.back().data->loadStreetGroups(&groups, nullptr,
                OsmAnd::ObfAddressStreetGroupTypesMask()
                    .set(OsmAnd::ObfAddressStreetGroupType::CityOrTown)
                    .set(OsmAnd::ObfAddressStreetGroupType::Village));
            double min_lat = 90, min_lon = 180, max_lat = -90, max_lon = -180;
            for (const auto& group : groups) {
                const auto p = latlon(group->position31);
                CityEntry city{file, static_cast<int>(group->type), title_of(*group), {},
                               p.latitude, p.longitude, group, false, {}};
                city.tokens = sq::name_tokens(city.title);
                for (const auto& t : city.tokens) city_tokens_[t].push_back(cities_.size());
                cities_.push_back(std::move(city));
                min_lat = std::min(min_lat, p.latitude); max_lat = std::max(max_lat, p.latitude);
                min_lon = std::min(min_lon, p.longitude); max_lon = std::max(max_lon, p.longitude);
            }
            if (sq::kUserLat >= min_lat && sq::kUserLat <= max_lat && sq::kUserLon >= min_lon &&
                sq::kUserLon <= max_lon) {
                const double area = (max_lat - min_lat) * (max_lon - min_lon);
                if (area < user_area_) { user_area_ = area; user_file_ = file; }
            }
        }
        return !files_.empty();
    }

    std::vector<sq::Result> operator()(const std::string& text)
    {
        const sq::Query parsed = sq::parse(text);
        std::vector<sq::Result> out;
        for (const auto& reading : sq::readings(parsed)) search(reading, 0, out);
        // Typos: only settlement names are in memory to correct against.
        for (std::size_t w = 0; w < parsed.words.size() && out.empty(); ++w) {
            sq::Query fixed = parsed;
            if (!correct_city_word(fixed.words[w])) continue;
            for (const auto& reading : sq::readings(fixed)) search(reading, 1, out);
        }
        sq::rank(out);
        return out;
    }

    std::size_t cached_streets() const
    {
        std::size_t n = 0;
        for (const auto& c : cities_) n += c.streets.size();
        return n;
    }
    std::size_t city_count() const { return cities_.size(); }

private:
    struct File {
        std::string name;
        std::shared_ptr<OsmAnd::ObfDataInterface> data;
        std::shared_ptr<OsmAnd::ObfsCollection> obfs;
    };

    void search(sq::Query q, int typos, std::vector<sq::Result>& out)
    {
        if (q.words.empty()) return;
        if (q.words.size() > 31) q.words.resize(31);
        int region = -1;
        if (q.region)
            for (std::size_t f = 0; f < files_.size(); ++f)
                if (files_[f].name.find(q.region) != std::string::npos) region = static_cast<int>(f);
        const uint32_t all = (1u << q.words.size()) - 1;

        std::vector<std::size_t> candidates;
        for (const auto& word : q.words)
            for (auto it = city_tokens_.lower_bound(word.text); it != city_tokens_.end(); ++it) {
                if (sq::word_match(word, it->first) == 0) break;
                candidates.insert(candidates.end(), it->second.begin(), it->second.end());
            }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        for (std::size_t c : candidates) {
            CityEntry& city = cities_[c];
            if (region >= 0 && city.file != region) continue;
            int exact = 0;
            const uint32_t used = sq::cover_name(q.words, all, city.tokens, exact);
            if (!used) continue;
            const uint32_t rest = all & ~used;
            if (!rest) {
                out.push_back({sq::Kind::City, city.title, city.lat, city.lon,
                               sq::score(sq::Kind::City, q, exact, 0, city.subtype,
                                         sq::km(sq::kUserLat, sq::kUserLon, city.lat, city.lon),
                                         0, typos, city.file == user_file_)});
                continue;
            }
            load_streets(city);
            for (auto& street : city.streets) add_street(city, street, q, rest, exact, typos, out);
        }

        // Street alone: OsmAnd's name index, the user's (or named) region
        // first, every region when that finds nothing.
        std::size_t longest = 0;
        for (std::size_t w = 1; w < q.words.size(); ++w)
            if (q.words[w].text.size() > q.words[longest].text.size()) longest = w;
        const std::size_t before = out.size();
        const bool city_found = !out.empty();
        const int first_file = region >= 0 ? region : user_file_;
        for (int pass = 0; pass < 2 && out.size() == before; ++pass) {
            if (pass == 1 && (region >= 0 || city_found)) break;  // all regions: last resort
            for (std::size_t f = 0; f < files_.size(); ++f) {
                if ((pass == 0) != (static_cast<int>(f) == first_file)) continue;
                files_[f].data->scanAddressesByName(
                    QString::fromUtf8(q.words[longest].text.c_str()),
                    OsmAnd::StringMatcherMode::CHECK_STARTS_FROM_SPACE, nullptr, nullptr,
                    OsmAnd::ObfAddressStreetGroupTypesMask(), true, false,
                    [&](const std::shared_ptr<const OsmAnd::Address>& address) {
                        if (address->addressType != OsmAnd::AddressType::Street) return false;
                        const auto street = std::static_pointer_cast<const OsmAnd::Street>(address);
                        CityEntry* city = city_of(street->streetGroup);
                        if (!city) return false;
                        StreetEntry entry = street_entry(street);
                        add_street(*city, entry, q, all, 0, typos, out);
                        return false;
                    });
            }
        }
    }

    CityEntry* city_of(const std::shared_ptr<const OsmAnd::StreetGroup>& group)
    {
        if (!group) return nullptr;
        const auto key = std::make_pair(group->obfSection.get(), group->dataOffset);
        if (by_group_.empty())
            for (auto& c : cities_)
                by_group_[{c.group->obfSection.get(), c.group->dataOffset}] = &c;
        const auto it = by_group_.find(key);
        return it == by_group_.end() ? nullptr : it->second;
    }

    static StreetEntry street_entry(const std::shared_ptr<const OsmAnd::Street>& street)
    {
        const auto p = latlon(street->position31);
        StreetEntry entry{title_of(*street), {}, p.latitude, p.longitude, street, false, {}};
        entry.tokens = sq::name_tokens(entry.title);
        return entry;
    }

    void load_streets(CityEntry& city)
    {
        if (city.streets_loaded) return;
        city.streets_loaded = true;
        QList<std::shared_ptr<const OsmAnd::StreetGroup>> groups{city.group};
        files_[static_cast<std::size_t>(city.file)].data->loadStreetsFromGroups(
            groups, nullptr, nullptr, [&](const std::shared_ptr<const OsmAnd::Street>& street) {
                city.streets.push_back(street_entry(street));
                return false;
            });
    }

    void load_houses(const CityEntry& city, StreetEntry& street)
    {
        if (street.houses_loaded) return;
        street.houses_loaded = true;
        QList<std::shared_ptr<const OsmAnd::Street>> streets{street.street};
        files_[static_cast<std::size_t>(city.file)].data->loadBuildingsFromStreets(
            streets, nullptr, nullptr, [&](const std::shared_ptr<const OsmAnd::Building>& b) {
                const auto p = latlon(b->position31);
                const std::string number = utf8(b->nativeName);
                street.houses.push_back({number, sq::house_key(number), p.latitude, p.longitude});
                return false;
            });
    }

    void add_street(const CityEntry& city, StreetEntry& street, const sq::Query& q,
                    uint32_t mask, int city_exact, int typos, std::vector<sq::Result>& out)
    {
        const int exact = sq::words_in_name(q.words, mask, street.tokens);
        if (exact < 0) return;
        const int unmatched = std::max(0, static_cast<int>(street.tokens.size()) -
                                              __builtin_popcount(mask));
        const std::string title = city.title + ", " + street.title;
        const bool home = city.file == user_file_;
        out.push_back({sq::Kind::Street, title, street.lat, street.lon,
                       sq::score(sq::Kind::Street, q, exact + city_exact, unmatched, city.subtype,
                                 sq::km(sq::kUserLat, sq::kUserLon, street.lat, street.lon), 0,
                                 typos, home)});
        if (q.house.empty()) return;
        load_houses(city, street);
        for (const auto& house : street.houses) {
            const int quality = sq::house_match(q.house, house.folded);
            if (!quality) continue;
            out.push_back({sq::Kind::House, title + ", " + house.number, house.lat, house.lon,
                           sq::score(sq::Kind::House, q, exact + city_exact, unmatched,
                                     city.subtype,
                                     sq::km(sq::kUserLat, sq::kUserLon, house.lat, house.lon),
                                     quality, typos, home)});
        }
    }

    // The closest settlement-name token (1 edit, 2 for long words).
    bool correct_city_word(sq::QueryWord& word) const
    {
        std::vector<uint32_t> wanted;
        for (std::size_t i = 0; i < word.text.size();) {
            uint32_t cp;
            sq::decode(word.text, i, cp);
            wanted.push_back(cp);
        }
        if (wanted.size() < 4) return false;
        const int bound = wanted.size() >= 8 ? 2 : 1;
        int best_distance = bound + 1;
        std::string best;
        for (const auto& entry : city_tokens_) {
            std::vector<uint32_t> token;
            for (std::size_t i = 0; i < entry.first.size();) {
                uint32_t cp;
                sq::decode(entry.first, i, cp);
                token.push_back(cp);
            }
            if (std::abs(static_cast<int>(token.size()) - static_cast<int>(wanted.size())) > bound)
                continue;
            std::vector<int> prev(token.size() + 1), cur(token.size() + 1);
            for (std::size_t j = 0; j <= token.size(); ++j) prev[j] = static_cast<int>(j);
            for (std::size_t i = 1; i <= wanted.size(); ++i) {
                cur[0] = static_cast<int>(i);
                for (std::size_t j = 1; j <= token.size(); ++j)
                    cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
                                       prev[j - 1] + (wanted[i - 1] == token[j - 1] ? 0 : 1)});
                std::swap(prev, cur);
            }
            const int d = prev[token.size()];
            if (d > 0 && d < best_distance) { best_distance = d; best = entry.first; }
        }
        if (best_distance > bound) return false;
        word.text = best;
        word.incomplete = false;
        return true;
    }

    std::vector<File> files_;
    std::vector<CityEntry> cities_;
    std::map<std::string, std::vector<std::size_t>> city_tokens_;
    std::map<std::pair<const void*, uint32_t>, CityEntry*> by_group_;
    int user_file_ = -1;
    double user_area_ = 1e30;
};

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: osmand_search_a_smoke MAPDIR QUERIES.tsv\n");
        return 2;
    }
    const auto resources = OsmAnd::CoreResourcesEmbeddedBundle::loadFromLibrary(
        QStringLiteral("/open-navicore/libOsmAndCore_ResourcesBundle_shared.so"));
    if (!resources || OsmAnd::InitializeCore(resources, "/datastore/navicore-backend/fonts") == 0)
        return 4;
    const double start = sq::now_ms();
    SearchA search;
    if (!search.open(argv[1])) return 3;
    std::printf("A init_ms=%.1f cities=%zu heap_bytes=%d\n", sq::now_ms() - start,
                search.city_count(), mallinfo().uordblks);
    sq::run_queries("A", argv[2], [&](const std::string& q) { return search(q); });
    std::printf("A heap_bytes_after=%d cached_streets=%zu\n", mallinfo().uordblks,
                search.cached_streets());
    std::fflush(stdout);
    std::_Exit(0);  // OsmAnd's static teardown aborts without ReleaseCore
}
