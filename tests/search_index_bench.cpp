// Times the navigation's index search (search/search_index.h) on a query
// set, user at Kyiv: search_index_bench INDEX QUERIES.tsv
#include "search/search_index.h"
#include "search_proto/harness.h"

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: search_index_bench INDEX QUERIES.tsv\n");
        return 2;
    }
    const double start = sq::now_ms();
    navicore_osmand::search::SearchIndex index;
    if (!index.open(argv[1])) return 3;
    std::printf("index open_ms=%.1f\n", sq::now_ms() - start);
    sq::run_queries("index", argv[2], [&](const std::string& query) {
        std::vector<sq::Result> results;
        for (const auto& hit : index.search(query, {sq::kUserLat, sq::kUserLon}, {}, 8))
            results.push_back({hit.kind,
                               hit.context.empty() ? hit.title : hit.context + ", " + hit.title,
                               hit.latitude, hit.longitude, hit.score});
        return results;
    });
    return 0;
}
