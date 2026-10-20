// Runs a query set through a search function and prints timings and
// pass/fail (top result of the expected kind within tolerance).
//
// Query file (TSV): query  lat  lon  kind(C|S|H)  tolerance_km
#pragma once

#include "address_query.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace sq {

constexpr double kUserLat = 50.4501, kUserLon = 30.5234;  // Kyiv, as the app's demo origin

using Search = std::function<std::vector<Result>(const std::string& query)>;

inline double now_ms()
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline const char* kind_name(Kind kind)
{
    return kind == Kind::City ? "C" : kind == Kind::Street ? "S" : "H";
}

inline int run_queries(const char* label, const char* path, const Search& search)
{
    std::ifstream in(path);
    std::string line;
    int passed = 0, total = 0;
    double sum_ms = 0, max_ms = 0, sum_first = 0, max_first = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string query, lat, lon, kind, tolerance;
        std::getline(fields, query, '\t');
        std::getline(fields, lat, '\t');
        std::getline(fields, lon, '\t');
        std::getline(fields, kind, '\t');
        std::getline(fields, tolerance, '\t');
        std::vector<Result> results;
        std::vector<double> times;
        for (int run = 0; run < 3; ++run) {
            const double start = now_ms();
            results = search(query);
            times.push_back(now_ms() - start);
        }
        const double first = times[0];  // A: before its caches hold this city
        std::sort(times.begin(), times.end());
        const double ms = times[1];
        bool ok = false;
        if (!results.empty()) {
            const auto& top = results.front();
            ok = kind == kind_name(top.kind) &&
                 km(top.lat, top.lon, std::stod(lat), std::stod(lon)) <= std::stod(tolerance);
        }
        ++total;
        passed += ok;
        sum_ms += ms;
        max_ms = std::max(max_ms, ms);
        sum_first += first;
        max_first = std::max(max_first, first);
        std::printf("%s %-4s first %8.1f ms median %8.1f ms  \"%s\"  -> ", label,
                    ok ? "PASS" : "FAIL", first, ms, query.c_str());
        if (results.empty()) std::printf("(none)\n");
        for (std::size_t i = 0; i < results.size() && i < 3; ++i)
            std::printf("%s%s %s (%.4f,%.4f) %.0f\n", i ? "        " : "",
                        kind_name(results[i].kind), results[i].title.c_str(),
                        results[i].lat, results[i].lon, results[i].score);
    }
    std::printf("%s SUMMARY passed=%d/%d first_avg_ms=%.1f first_max_ms=%.1f "
                "median_avg_ms=%.1f median_max_ms=%.1f\n", label, passed, total,
                total ? sum_first / total : 0.0, max_first, total ? sum_ms / total : 0.0, max_ms);
    return passed;
}

inline void rank(std::vector<Result>& results, std::size_t keep = 8)
{
    std::stable_sort(results.begin(), results.end(),
                     [](const Result& a, const Result& b) { return a.score > b.score; });
    // One entry per place: OSM repeats a house per entrance and a street per way.
    std::vector<Result> unique;
    for (const auto& r : results) {
        bool dup = false;
        for (const auto& u : unique)
            if (u.kind == r.kind && km(u.lat, u.lon, r.lat, r.lon) < 0.15) { dup = true; break; }
        if (!dup) unique.push_back(r);
        if (unique.size() >= keep) break;
    }
    results.swap(unique);
}

}  // namespace sq
