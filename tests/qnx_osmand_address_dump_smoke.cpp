// Dumps the address data of OBF files as TSV on stdout, for building an
// offline search index on the workstation (search option B).
//
//   osmand_address_dump_smoke MAP.obf...  > addresses.tsv
//
//   F <file> <name>
//   C <city> <file> <subtype> <native> <uk> <lat> <lon>
//   S <street> <city> <native> <uk> <lat> <lon>
//   B <street> <number> <lat> <lon>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include <OsmAndCore.h>
#include <OsmAndCore/CoreResourcesEmbeddedBundle.h>
#include <OsmAndCore/ObfDataInterface.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Utilities.h>
#include <OsmAndCore/Data/Building.h>
#include <OsmAndCore/Data/Street.h>
#include <OsmAndCore/Data/StreetGroup.h>

namespace {

// One field: UTF-8 without the TSV separators.
std::string field(const QString& value)
{
    QByteArray bytes = value.toUtf8();
    for (char& c : bytes)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::string uk(const OsmAnd::Address& address)
{
    const auto it = address.localizedNames.constFind(QStringLiteral("uk"));
    return it == address.localizedNames.cend() ? std::string() : field(*it);
}

void point(const OsmAnd::PointI& position31)
{
    const auto p = OsmAnd::Utilities::convert31ToLatLon(position31);
    std::printf("\t%.6f\t%.6f\n", p.latitude, p.longitude);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: osmand_address_dump_smoke MAP.obf...\n");
        return 2;
    }
    const auto resources = OsmAnd::CoreResourcesEmbeddedBundle::loadFromLibrary(
        QStringLiteral("/open-navicore/libOsmAndCore_ResourcesBundle_shared.so"));
    if (!resources || OsmAnd::InitializeCore(resources, "/datastore/navicore-backend/fonts") == 0)
        return 4;
    static char buffer[1 << 16];
    std::setvbuf(stdout, buffer, _IOFBF, sizeof(buffer));

    unsigned long next_city = 0, next_street = 0, buildings = 0;
    for (int file = 1; file < argc; ++file) {
        std::printf("F\t%d\t%s\n", file, argv[file]);
        auto obfs = std::make_shared<OsmAnd::ObfsCollection>();
        obfs->addFile(QString::fromLocal8Bit(argv[file]));
        const auto data = obfs->obtainDataInterface();
        if (!data) continue;

        QList<std::shared_ptr<const OsmAnd::StreetGroup>> groups;
        data->loadStreetGroups(&groups, nullptr, OsmAnd::ObfAddressStreetGroupTypesMask()
            .set(OsmAnd::ObfAddressStreetGroupType::CityOrTown)
            .set(OsmAnd::ObfAddressStreetGroupType::Village));
        // Batches keep the loaded streets and buildings small.
        constexpr int kBatch = 64;
        for (int first = 0; first < groups.size(); first += kBatch) {
            QList<std::shared_ptr<const OsmAnd::StreetGroup>> batch = groups.mid(first, kBatch);
            QHash<const OsmAnd::StreetGroup*, unsigned long> city_ids;
            for (const auto& group : batch) {
                city_ids.insert(group.get(), ++next_city);
                std::printf("C\t%lu\t%d\t%d\t%s\t%s", next_city, file,
                            static_cast<int>(group->type), field(group->nativeName).c_str(),
                            uk(*group).c_str());
                point(group->position31);
            }
            QList<std::shared_ptr<const OsmAnd::Street>> streets;
            QHash<const OsmAnd::Street*, unsigned long> street_ids;
            data->loadStreetsFromGroups(batch, nullptr, nullptr,
                [&](const std::shared_ptr<const OsmAnd::Street>& street) {
                    street_ids.insert(street.get(), ++next_street);
                    std::printf("S\t%lu\t%lu\t%s\t%s", next_street,
                                city_ids.value(street->streetGroup.get()),
                                field(street->nativeName).c_str(), uk(*street).c_str());
                    point(street->position31);
                    streets.push_back(street);
                    return false;  // printed; nothing to keep in the result
                });
            data->loadBuildingsFromStreets(streets, nullptr, nullptr,
                [&](const std::shared_ptr<const OsmAnd::Building>& building) {
                    std::printf("B\t%lu\t%s", street_ids.value(building->street.get()),
                                field(building->nativeName).c_str());
                    point(building->position31);
                    ++buildings;
                    return false;
                });
        }
        std::fprintf(stderr, "file %d %s cities=%lu streets=%lu buildings=%lu\n", file,
                     argv[file], next_city, next_street, buildings);
    }
    std::fflush(stdout);
    std::_Exit(0);  // OsmAnd's static teardown aborts without ReleaseCore
}
