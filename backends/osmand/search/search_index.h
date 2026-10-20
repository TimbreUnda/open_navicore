#pragma once

#include "search/address_query.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace navicore_osmand::search {

// The address search index built from the OBFs by
// tests/search_proto/build_index.py (layout in its header comment): settlements,
// their streets and the streets' houses, with sorted word dictionaries.
// Memory mapped read-only; a query touches a few small slices of it.
// Used from one thread (the navigation worker).
class SearchIndex {
public:
    struct Point { double latitude = 0.0, longitude = 0.0; };

    struct Hit {
        Kind kind;
        std::string title;    // "вулиця Червоний камінь, 4", or the settlement
        std::string context;  // the settlement of a street or house, else empty
        double latitude = 0.0, longitude = 0.0;
        double score = 0.0;
    };

    SearchIndex() = default;
    ~SearchIndex();
    SearchIndex(const SearchIndex&) = delete;
    SearchIndex& operator=(const SearchIndex&) = delete;

    bool open(const std::string& path);
    bool is_open() const { return base_ != nullptr; }

    // Best hits for a typed query, near `origin` first; places within 100 m
    // of a `recent` destination rank higher.
    std::vector<Hit> search(const std::string& query, Point origin,
                            const std::vector<Point>& recent, std::size_t limit) const;

private:
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

    struct Context;  // one query: words, origin, recents, results

    std::string str(uint32_t off, uint32_t len) const { return std::string(pool_ + off, len); }
    std::pair<uint32_t, uint32_t> token_range(const TokenRow* table, uint32_t n,
                                              const QueryWord& word) const;
    bool correct(QueryWord& word) const;
    bool correct_in_city(const CityRow& city, uint32_t mask, Query& q, int& fixes) const;
    void search_reading(Query q, int typos, bool fix_in_city, Context& ctx) const;
    void add_city(uint32_t c, const Query& q, int exact, int typos, Context& ctx) const;
    void add_street(uint32_t s, const Query& q, uint32_t mask, int city_exact, int typos,
                    Context& ctx) const;

    const char* base_ = nullptr;
    std::size_t size_ = 0;
    uint32_t count_[9] = {};
    const FileRow* files_ = nullptr;
    const CityRow* cities_ = nullptr;
    const StreetRow* streets_ = nullptr;
    const HouseRow* houses_ = nullptr;
    const TokenRow* city_tokens_ = nullptr;
    const TokenRow* street_tokens_ = nullptr;
    const uint32_t* city_postings_ = nullptr;
    const uint32_t* street_postings_ = nullptr;
    const char* pool_ = nullptr;
};

}  // namespace navicore_osmand::search
