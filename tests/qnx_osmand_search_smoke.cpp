// Destination search timing on the radio's maps: the worker's one-string
// search (addresses, then amenities), and the structured city -> street ->
// house lookup it could use instead.
//
//   osmand_search_smoke MAPDIR QUERY [CITY STREET HOUSE]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <string>
#include <vector>

#include <OsmAndCore.h>
#include <OsmAndCore/CoreResourcesEmbeddedBundle.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Utilities.h>
#include <OsmAndCore/Data/Address.h>
#include <OsmAndCore/Data/Amenity.h>
#include <OsmAndCore/Data/Street.h>
#include <OsmAndCore/Data/StreetGroup.h>
#include <OsmAndCore/Search/AddressesByNameSearch.h>
#include <OsmAndCore/Search/AmenitiesByNameSearch.h>

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string utf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

const char* type_name(OsmAnd::AddressType type)
{
    switch (type) {
    case OsmAnd::AddressType::StreetGroup: return "city";
    case OsmAnd::AddressType::Street: return "street";
    case OsmAnd::AddressType::Building: return "building";
    case OsmAnd::AddressType::StreetIntersection: return "intersection";
    }
    return "?";
}

void print_address(const OsmAnd::Address& address)
{
    const auto point = OsmAnd::Utilities::convert31ToLatLon(address.position31);
    std::printf("    %-12s %s (%.5f, %.5f)\n", type_name(address.addressType),
                utf8(address.nativeName).c_str(), point.latitude, point.longitude);
}

struct Timed {
    std::vector<std::shared_ptr<const OsmAnd::Address>> results;
    double first_ms = -1.0;
    double total_ms = 0.0;
};

// The worker's address step: one name, no area, no parent filter.
Timed addresses(const std::shared_ptr<OsmAnd::ObfsCollection>& obfs,
                const OsmAnd::AddressesByNameSearch::Criteria& criteria)
{
    Timed timed;
    const auto start = Clock::now();
    OsmAnd::AddressesByNameSearch search(obfs);
    search.performSearch(criteria,
        [&timed, start](const OsmAnd::ISearch::Criteria&,
                        const OsmAnd::ISearch::IResultEntry& entry) {
            const auto& result =
                static_cast<const OsmAnd::AddressesByNameSearch::ResultEntry&>(entry);
            if (timed.first_ms < 0.0) timed.first_ms = ms_since(start);
            if (result.address) timed.results.push_back(result.address);
        });
    timed.total_ms = ms_since(start);
    return timed;
}

void report(const char* label, const Timed& timed, std::size_t show = 8)
{
    std::printf("%s: total_ms=%.1f first_ms=%.1f results=%zu\n", label,
                timed.total_ms, timed.first_ms, timed.results.size());
    for (std::size_t i = 0; i < timed.results.size() && i < show; ++i)
        print_address(*timed.results[i]);
}

// area, when set, keeps the search to the OBFs (and POIs) around it.
void worker_search(const std::shared_ptr<OsmAnd::ObfsCollection>& obfs,
                   const QString& query, const char* label,
                   const OsmAnd::AreaI* area = nullptr)
{
    OsmAnd::AddressesByNameSearch::Criteria address_criteria;
    address_criteria.name = query;
    if (area) address_criteria.obfInfoAreaFilter = *area;
    const Timed found = addresses(obfs, address_criteria);
    std::printf("[%s] ", label);
    report("worker_addresses", found);

    const auto start = Clock::now();
    double first_ms = -1.0;
    std::size_t count = 0;
    OsmAnd::AmenitiesByNameSearch amenities(obfs);
    OsmAnd::AmenitiesByNameSearch::Criteria amenity_criteria;
    amenity_criteria.name = query;
    amenity_criteria.xy31 = OsmAnd::Utilities::convertLatLonTo31(
        OsmAnd::LatLon(50.4501, 30.5234));
    if (area) {
        amenity_criteria.obfInfoAreaFilter = *area;
        amenity_criteria.bbox31 = *area;
    }
    amenities.performSearch(amenity_criteria,
        [&](const OsmAnd::ISearch::Criteria&, const OsmAnd::ISearch::IResultEntry&) {
            if (first_ms < 0.0) first_ms = ms_since(start);
            ++count;
        });
    std::printf("[%s] worker_amenities: total_ms=%.1f first_ms=%.1f results=%zu\n",
                label, ms_since(start), first_ms, count);
}

void structured_search(const std::shared_ptr<OsmAnd::ObfsCollection>& obfs,
                       const QString& city, const QString& street,
                       const QString& house, const char* label,
                       const OsmAnd::AreaI* area = nullptr)
{
    const auto start = Clock::now();
    OsmAnd::AddressesByNameSearch::Criteria city_criteria;
    city_criteria.name = city;
    city_criteria.includeStreets = false;
    city_criteria.streetGroupTypesMask = OsmAnd::ObfAddressStreetGroupTypesMask()
        .set(OsmAnd::ObfAddressStreetGroupType::CityOrTown);
    if (area) city_criteria.obfInfoAreaFilter = *area;
    const Timed cities = addresses(obfs, city_criteria);
    std::printf("[%s] ", label);
    report("city", cities, 4);

    std::vector<std::shared_ptr<const OsmAnd::Address>> found_streets;
    for (const auto& candidate : cities.results) {
        if (candidate->addressType != OsmAnd::AddressType::StreetGroup) continue;
        if (candidate->nativeName.compare(city, Qt::CaseInsensitive) != 0) continue;
        OsmAnd::AddressesByNameSearch::Criteria street_criteria;
        street_criteria.name = street;
        street_criteria.addressFilter = candidate;
        const Timed streets = addresses(obfs, street_criteria);
        std::printf("[%s] ", label);
        report("street_in_city", streets, 4);
        if (!streets.results.empty()) {
            found_streets = streets.results;
            break;
        }
    }
    // OSM often holds one street as several ways with different names
    // ("вул.", "вулиця", "житловий масив"): the house can be on any of them.
    for (const auto& found_street : found_streets) {
        if (found_street->addressType != OsmAnd::AddressType::Street) continue;
        OsmAnd::AddressesByNameSearch::Criteria house_criteria;
        house_criteria.name = house;
        house_criteria.addressFilter = found_street;
        const Timed houses = addresses(obfs, house_criteria);
        std::printf("[%s] %s ", label, utf8(found_street->nativeName).c_str());
        report("house_on_street", houses, 4);
    }
    std::printf("[%s] structured_total_ms=%.1f\n", label, ms_since(start));
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 6) {
        std::fprintf(stderr,
                     "usage: osmand_search_smoke MAPDIR QUERY [CITY STREET HOUSE]\n");
        return 2;
    }
    // The collator behind name matching needs the initialized core (ICU).
    const auto resources = OsmAnd::CoreResourcesEmbeddedBundle::loadFromLibrary(
        QStringLiteral("/open-navicore/libOsmAndCore_ResourcesBundle_shared.so"));
    if (!resources || OsmAnd::InitializeCore(resources, "/datastore/navicore-backend/fonts") == 0)
        return 4;
    const std::string dir = argv[1];
    auto obfs = std::make_shared<OsmAnd::ObfsCollection>();
    // MAPDIR may also be one .obf: a region picked by position.
    DIR* handle = ::opendir(dir.c_str());
    int files = 0;
    if (!handle) {
        obfs->addFile(QString::fromLocal8Bit(dir.c_str()));
        ++files;
    }
    while (const dirent* entry = handle ? ::readdir(handle) : nullptr) {
        const std::string name = entry->d_name;
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".obf") != 0) continue;
        if (name.find("basemap") != std::string::npos) continue;  // like the engine
        obfs->addFile(QString::fromLocal8Bit((dir + "/" + name).c_str()));
        ++files;
    }
    if (handle) ::closedir(handle);
    std::printf("files=%d\n", files);

    const QString query = QString::fromUtf8(argv[2]);
    worker_search(obfs, query, "cold");
    worker_search(obfs, query, "warm");
    if (argc == 6) {
        const QString city = QString::fromUtf8(argv[3]);
        const QString street = QString::fromUtf8(argv[4]);
        const QString house = QString::fromUtf8(argv[5]);
        structured_search(obfs, city, street, house, "cold");
        structured_search(obfs, city, street, house, "warm");
        worker_search(obfs, street, "street_only");

        // The Dnipro area, as a position-near filter would give it.
        const OsmAnd::AreaI dnipro(
            OsmAnd::Utilities::get31TileNumberY(48.62),
            OsmAnd::Utilities::get31TileNumberX(34.75),
            OsmAnd::Utilities::get31TileNumberY(48.33),
            OsmAnd::Utilities::get31TileNumberX(35.25));
        worker_search(obfs, query, "area_full_query", &dnipro);
        worker_search(obfs, street, "area_street_only", &dnipro);
        structured_search(obfs, city, street, house, "area_structured", &dnipro);
    }
    // OsmAnd's static teardown aborts without ReleaseCore; the numbers are out.
    std::fflush(stdout);
    std::_Exit(0);
}
