// Address search index: parsing, matching and ranking on a small index
// built from fixtures/search_index.tsv by search_proto/build_index.py.
#include "search/search_index.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using navicore_osmand::search::Kind;
using navicore_osmand::search::SearchIndex;

namespace {

int failures = 0;

const SearchIndex::Point kKyiv{50.4501, 30.5234};
const SearchIndex::Point kDnipro{48.4680, 35.0418};

void expect_top(const SearchIndex& index, const char* query, SearchIndex::Point origin,
                Kind kind, double lat, double lon,
                const std::vector<SearchIndex::Point>& recent = {})
{
    const auto hits = index.search(query, origin, recent, 8);
    // A street is several ways along its length: any of them will do.
    const double tolerance = kind == Kind::Street ? 0.01 : 0.002;
    const bool ok = !hits.empty() && hits[0].kind == kind &&
                    std::fabs(hits[0].latitude - lat) < tolerance &&
                    std::fabs(hits[0].longitude - lon) < tolerance;
    if (!ok) {
        ++failures;
        std::printf("FAIL \"%s\": ", query);
        if (hits.empty()) std::printf("no hits\n");
        for (std::size_t i = 0; i < hits.size() && i < 3; ++i)
            std::printf("%s[%d %s / %s %.4f,%.4f %.0f]\n", i ? "     " : "",
                        static_cast<int>(hits[i].kind), hits[i].title.c_str(),
                        hits[i].context.c_str(), hits[i].latitude, hits[i].longitude,
                        hits[i].score);
    }
}

}  // namespace

int main()
{
    SearchIndex index;
    if (!index.open(SEARCH_FIXTURE)) {
        std::printf("FAIL open %s\n", SEARCH_FIXTURE);
        return 1;
    }
    SearchIndex missing;
    if (missing.open("/nonexistent/search.idx") || missing.is_open()) ++failures;

    // City, street and house in any order, any spelling the radio keyboard gives.
    expect_top(index, "дніпро червоний камінь 4", kKyiv, Kind::House, 48.487316, 34.934635);
    expect_top(index, "червоний камінь 4 дніпро", kKyiv, Kind::House, 48.487316, 34.934635);
    expect_top(index, "днипро червоний камінь 4", kKyiv, Kind::House, 48.487316, 34.934635);
    expect_top(index, "dnipro chervonyi kamin 4", kKyiv, Kind::House, 48.487316, 34.934635);
    expect_top(index, "Дніпро, вул. Червоний Камінь, 4-г", kKyiv, Kind::House, 48.487610, 34.936290);
    // A typo that is no word of the index, and one that is.
    expect_top(index, "дніпро червоний камєнь 4", kKyiv, Kind::House, 48.487316, 34.934635);
    expect_top(index, "дніпро червоний камнь 4", kKyiv, Kind::House, 48.487316, 34.934635);
    // A settlement before streets of its name; the town before villages.
    expect_top(index, "Лозова", kKyiv, Kind::City, 48.884243, 36.316025);
    expect_top(index, "Лозова Харківська область", kKyiv, Kind::City, 48.884243, 36.316025);
    expect_top(index, "Лозова Друга", kKyiv, Kind::City, 49.869120, 37.558758);
    // "Сумська" is an oblast and a street: both readings are ranked.
    expect_top(index, "харків сумська 10", kKyiv, Kind::House, 49.994429, 36.233168);
    // A street without its town, and while typing.
    expect_top(index, "хрещатик 5", kKyiv, Kind::House, 50.451151, 30.527444);
    expect_top(index, "дніпро червоний кам", kKyiv, Kind::Street, 48.487117, 34.934442);
    // Near the user first; a destination picked before beats distance.
    expect_top(index, "шевченка 1", kKyiv, Kind::House, 50.444100, 30.510100);
    expect_top(index, "шевченка 1", kDnipro, Kind::House, 48.470100, 35.040100);
    expect_top(index, "шевченка 1", kKyiv, Kind::House, 48.470100, 35.040100,
               {{48.470100, 35.040100}});

    // "(Київ)" in an OSM street name is not repeated in the result title.
    {
        const auto hits = index.search("прорізна 2", kKyiv, {}, 8);
        if (hits.empty() || hits[0].title != "Прорізна вулиця, 2" || hits[0].context != "Київ") {
            ++failures;
            std::printf("FAIL \"прорізна 2\": %s\n", hits.empty() ? "no hits" : hits[0].title.c_str());
        }
    }

    if (!index.search("", kKyiv, {}, 8).empty()) ++failures;
    if (!index.search("ыыыыыыыыы", kKyiv, {}, 8).empty()) ++failures;

    std::printf("search_index_test: %s (%d failures)\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
