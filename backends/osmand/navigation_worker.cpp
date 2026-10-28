#include "navigation_worker.h"


#include "ch_planner.h"
#include "cluster_payload.h"
#include "config.h"
#include "diagnostics.h"
#include "junction_shape.h"
#include "route_check.h"
#include "route_maneuvers.h"
#include "search/search_index.h"
#include "session.h"
#include "ui/localization.h"

#include <OsmAndCore/Data/Address.h>
#include <OsmAndCore/Data/Amenity.h>
#include <OsmAndCore/IQueryController.h>
#include <OsmAndCore/LatLon.h>
#include <OsmAndCore/Logging.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Search/AddressesByNameSearch.h>
#include <OsmAndCore/Search/AmenitiesByNameSearch.h>
#include <OsmAndCore/Utilities.h>

// Core and core-legacy both ship a Logging.h with the same public names but
// different include guards. The provider uses Core's logger consistently.
#define _OSMAND_LOGGING_H_
#include "binaryRead.h"
#include "commonOsmAndCore.h"
#include "hhRouteDataStructure.h"
#include "hhRoutePlanner.h"
#include "routeCalculationProgress.h"
#include "routePlannerFrontEnd.h"
#include "routeResultPreparation.h"
#include "routeSegmentResult.h"
#include "routingConfiguration.h"

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <stdexcept>
#include <pthread.h>
#include <sched.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace navicore_osmand {
namespace {

constexpr std::size_t kMaxAddressResults = 8;
constexpr std::size_t kMaxAmenityResults = 4;
constexpr std::size_t kMaxRoutes = 3;  // the fastest and up to two alternatives
// Alternatives are off: finding them costs routing time, and the route preview
// then offers only the fastest route. Set to true to bring them back.
constexpr bool kAlternativeRoutes = false;
// Places (amenities) are searched this far around the origin, in degrees
// (about 33 km): the whole country takes seconds.
constexpr double kAmenityRadiusLatitude = 0.3;
constexpr double kAmenityRadiusLongitude = 0.45;

std::string utf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::string distance_text(const OsmAnd::LatLon& origin, const OsmAnd::LatLon& point)
{
    return ui::loc::format_distance(OsmAnd::Utilities::distance(origin, point));
}

std::string address_kind(OsmAnd::AddressType type)
{
    switch (type) {
    case OsmAnd::AddressType::StreetGroup: return ui::loc::tr(ui::loc::Str::AddressPlace);
    case OsmAnd::AddressType::Street: return ui::loc::tr(ui::loc::Str::AddressStreet);
    case OsmAnd::AddressType::Building: return ui::loc::tr(ui::loc::Str::AddressBuilding);
    case OsmAnd::AddressType::StreetIntersection:
        return ui::loc::tr(ui::loc::Str::AddressIntersection);
    }
    return ui::loc::tr(ui::loc::Str::AddressBuilding);
}

// The object's name in the radio's languages, else its own name.
template <typename Object>
std::string localized_name(const Object& object)
{
    for (const std::string& language : ui::loc::name_languages(ui::loc::language())) {
        std::string name = utf8(object.getName(QString::fromStdString(language), false));
        if (!name.empty()) return name;
    }
    return utf8(object.nativeName);
}

ui::Destination destination(const OsmAnd::Address& address, const OsmAnd::LatLon& origin)
{
    const auto coordinates = OsmAnd::Utilities::convert31ToLatLon(address.position31);
    std::string title = localized_name(address);
    return {"address:" + std::to_string(address.position31.x) + ":" +
                std::to_string(address.position31.y),
            std::move(title), address_kind(address.addressType),
            distance_text(origin, coordinates), coordinates.latitude,
            coordinates.longitude};
}

const char* settlement_kind(int subtype)
{
    using ui::loc::Str;
    switch (subtype) {
    case 0: return ui::loc::tr(Str::SettlementCity);
    case 1: return ui::loc::tr(Str::SettlementTown);
    case 2: return ui::loc::tr(Str::SettlementVillage);
    case 3: return ui::loc::tr(Str::SettlementHamlet);
    case 4: return ui::loc::tr(Str::SettlementSuburb);
    default: return ui::loc::tr(Str::AddressPlace);
    }
}

// A search index hit. The subtitle tells same-named places apart: a
// settlement's kind and oblast ("Хутір · Миколаївська обл."), a street's or
// house's settlement and oblast ("Дніпро · Дніпропетровська обл.").
ui::Destination destination(const search::SearchIndex::Hit& hit, const OsmAnd::LatLon& origin)
{
    const OsmAnd::LatLon coordinates(hit.latitude, hit.longitude);
    const auto point = OsmAnd::Utilities::convertLatLonTo31(coordinates);
    const std::string region = ui::loc::region_name(hit.region);
    std::string subtitle = hit.context.empty() ? settlement_kind(hit.settlement_type) : hit.context;
    // Kyiv is a region of its own: "Київ · Kyiv" under its streets says nothing.
    const bool city_region = hit.region == "kyiv-city" && !hit.context.empty();
    if (!region.empty() && !city_region && region != hit.context && region != hit.title)
        subtitle += " · " + region;
    return {"address:" + std::to_string(point.x) + ":" + std::to_string(point.y), hit.title,
            std::move(subtitle), distance_text(origin, coordinates), hit.latitude,
            hit.longitude};
}

ui::Destination destination(const OsmAnd::Amenity& amenity, const OsmAnd::LatLon& origin)
{
    const auto coordinates = OsmAnd::Utilities::convert31ToLatLon(amenity.position31);
    std::string title = localized_name(amenity);
    std::string subtitle = utf8(amenity.subType);
    if (subtitle.empty()) subtitle = utf8(amenity.type);
    if (!subtitle.empty()) subtitle = ui::loc::poi_category(subtitle);
    return {"amenity:" + std::to_string(amenity.position31.x) + ":" +
                std::to_string(amenity.position31.y),
            std::move(title), std::move(subtitle),
            distance_text(origin, coordinates), coordinates.latitude,
            coordinates.longitude};
}

class Cancellation final : public OsmAnd::IQueryController {
public:
    bool isAborted() const override { return reason_.load() != 0; }
    bool superseded() const noexcept { return reason_.load() == 2; }
    void stop_at_limit() noexcept
    {
        int expected = 0;
        (void)reason_.compare_exchange_strong(expected, 1);
    }
    void cancel() noexcept { reason_.store(2); }
private:
    // 0 active, 1 enough results (publish), 2 superseded/stopping (discard).
    std::atomic<int> reason_{0};
};

class RouteProgress final : public RouteCalculationProgress {
public:
    explicit RouteProgress(std::shared_ptr<Cancellation> cancellation)
        : cancellation_(std::move(cancellation)) {}
    bool isCancelled() override { return cancellation_->isAborted(); }
private:
    std::shared_ptr<Cancellation> cancellation_;
};

std::string write_routing_xml(const std::string& xml)
{
    char path[] = "/tmp/open-navicore-routing-XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd < 0) throw std::runtime_error("mkstemp routing.xml failed");
    std::size_t written = 0;
    while (written < xml.size()) {
        const ssize_t count = ::write(fd, xml.data() + written, xml.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            ::close(fd);
            ::unlink(path);
            throw std::runtime_error("write routing.xml failed");
        }
        written += static_cast<std::size_t>(count);
    }
    if (::close(fd) != 0) {
        ::unlink(path);
        throw std::runtime_error("close routing.xml failed");
    }
    return path;
}

std::string distance_label(double metres)
{
    return ui::loc::format_distance(metres);
}

std::string duration_label(double seconds)
{
    return ui::loc::format_duration_minutes(static_cast<int>(std::lround(seconds / 60.0)));
}

// Routing runs for seconds. nv_navigation is pinned to one core, where the
// render thread (priority 20) leaves it about half the CPU: a 15 km route
// took 7.3 s there against 3.7 s free to use either core (route_bench,
// 2026-10-15). At the default priority 10 it shares the other core with its
// services in turn; at 9 it only got their idle time, and a busy Android
// Auto session stretched a route by seconds.
constexpr int kWorkerPriority = 10;
// Opening the routing maps (about 10 s for all of Ukraine, 2026-10-27) runs
// ahead of the first route on idle time only.
constexpr int kWarmupPriority = 9;

void place_worker_thread(int priority = kWorkerPriority, const char* name = "worker=placed")
{
    const unsigned cpus = _syspage_ptr ? _syspage_ptr->num_cpu : 1u;
    const unsigned mask = cpus >= 32u ? ~0u : (1u << cpus) - 1u;
    const bool any_cpu =
        ThreadCtl(_NTO_TCTL_RUNMASK, reinterpret_cast<void*>(static_cast<uintptr_t>(mask))) != -1;
    int policy = 0;
    sched_param parameters{};
    bool lowered = false;
    if (pthread_getschedparam(pthread_self(), &policy, &parameters) == 0) {
        parameters.sched_priority = priority;
        lowered = pthread_setschedparam(pthread_self(), policy, &parameters) == 0;
    }
    char line[80];
    std::snprintf(line, sizeof(line), "%s cpus=%u runmask=%d priority=%d",
                  name, cpus, any_cpu ? 1 : 0, lowered ? priority : -1);
    trace(line);
}

// One trace line per manoeuvre of the main route (bounded), so that a live
// drive can be checked against the cluster picture: the junction
// description, the junction picture (0: none) and the symbol sent.
constexpr std::size_t kJunctionTraceMax = 40;

// Metres between two route points (equirectangular; route points are metres apart).
double metres_between(const ui::GeoPoint& a, const ui::GeoPoint& b)
{
    constexpr double kDegrees = M_PI / 180.0;
    const double mean = (a.latitude + b.latitude) * 0.5 * kDegrees;
    const double dy = (b.latitude - a.latitude) * 111320.0;
    const double dx = (b.longitude - a.longitude) * 111320.0 * std::cos(mean);
    return std::sqrt(dx * dx + dy * dy);
}

void trace_junction(std::size_t number, const ui::Maneuver& maneuver)
{
    const auto& j = maneuver.junction;
    char arms[64] = "";
    std::size_t used = 0;
    for (std::size_t i = 0; i < j.branch_count && used < sizeof(arms); ++i) {
        const int n = std::snprintf(arms + used, sizeof(arms) - used, "%s%.0f", i ? "," : "",
                                    static_cast<double>(j.branches[i]));
        if (n < 0) break;
        used += static_cast<std::size_t>(n);
    }
    char line[200];
    std::snprintf(line, sizeof(line),
                  "worker=junction n=%zu kind=%d osmand_angle=%.0f valid=%d route=%.0f "
                  "arms=%s%s through=%d lanes=%u rec=0x%x change=%d shape=%u symbol=%u",
                  number, static_cast<int>(maneuver.kind), maneuver.turn_angle_degrees,
                  j.valid ? 1 : 0, static_cast<double>(j.route_angle), arms,
                  j.truncated ? "+" : "", j.through_road ? 1 : 0,
                  static_cast<unsigned>(j.lane_count), static_cast<unsigned>(j.recommended_lanes),
                  static_cast<int>(j.lane_change),
                  static_cast<unsigned>(junction_symbol(maneuver)),
                  static_cast<unsigned>(maneuver_symbol(maneuver)));
    trace(line);
}

// What the router returned, as plain legs for the geometry check.
RouteCheck check_segments(const Segments& segments)
{
    std::vector<RouteLeg> legs;
    legs.reserve(segments.size());
    for (const auto& segment : segments) {
        if (!segment || !segment->object) continue;
        const ::LatLon start = segment->getStartPoint();
        const ::LatLon end = segment->getEndPoint();
        legs.push_back({segment->object->getId(), start.lat, start.lon, end.lat, end.lon});
    }
    return check_route_legs(legs);
}

double duration_of(const Segments& segments)
{
    double seconds = 0.0;
    for (const auto& segment : segments)
        if (segment) seconds += segment->segmentTime;
    return seconds;
}

} // namespace

class NavigationWorker::Impl {
public:
    Impl(std::shared_ptr<OsmAnd::ObfsCollection> obfs,
         std::shared_ptr<Wakeup> wakeup,
         std::vector<std::string> map_paths,
         std::string routing_xml,
         const std::string& search_index,
         std::string ch_sidecar,
         bool organic_turns)
        : obfs_(std::move(obfs)), wakeup_(std::move(wakeup)),
          map_paths_(std::move(map_paths)), routing_xml_(std::move(routing_xml)),
          ch_sidecar_(std::move(ch_sidecar)), organic_turns_(organic_turns)
    {
        trace(organic_turns_ ? "worker=turns source=organic_maps" : "worker=turns source=osmand");
        // Opened here, read only by the worker thread started below.
        trace(index_.open(search_index) ? "worker=search_index" : "worker=no_search_index");
        // Start only after every request/result flag below has completed
        // construction. Starting in the member initializer races run()
        // against later members such as has_pending_ and stopping_.
        thread_ = std::thread(&Impl::run, this);
        trace("worker=thread_started");
        // The first route would otherwise wait for every map to open.
        warmup_ = std::thread([this] {
            place_worker_thread(kWarmupPriority, "worker=warmup_placed");
            (void)prepare_router();
        });
    }

    ~Impl() { stop(); }

    bool has_index() const { return index_.is_open(); }

    void submit(std::uint64_t revision, std::string query, ui::GeoPoint origin,
                std::vector<ui::GeoPoint> recent)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (active_) active_->cancel();
        pending_ = Request::search(revision, std::move(query));
        pending_.origin = origin;
        pending_.recent = std::move(recent);
        has_pending_ = true;
        trace_result("worker=search_queued", revision, pending_.query.size());
        condition_.notify_one();
    }

    void submit_route(std::uint64_t revision, ui::Destination destination,
                      ui::GeoPoint origin, bool alternatives,
                      std::optional<double> heading_degrees)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (active_) active_->cancel();
        pending_ = Request::route(revision, std::move(destination), origin);
        pending_.alternatives = alternatives;
        pending_.heading_degrees = heading_degrees;
        has_pending_ = true;
        trace_result("worker=route_queued", revision, 1u);
        condition_.notify_one();
    }

    bool take(SearchResult& output)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_result_) return false;
        output = std::move(result_);
        has_result_ = false;
        return true;
    }

    bool take(RouteResult& output)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_route_result_) return false;
        output = std::move(route_result_);
        has_route_result_ = false;
        return true;
    }

    void stop() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            stopping_ = true;
            if (active_) active_->cancel();
            condition_.notify_one();
        }
        stop_warmup_ = true;
        if (warmup_.joinable()) warmup_.join();
        if (thread_.joinable()) thread_.join();
        if (router_ready_) {
            for (const std::string& path : map_paths_)
                (void)closeBinaryMapFile(path);
            router_ready_ = false;
        }
    }

private:
    struct Request {
        enum class Kind { Search, Route } kind = Kind::Search;
        std::chrono::steady_clock::time_point queued = std::chrono::steady_clock::now();
        std::uint64_t revision = 0;
        std::string query;
        ui::Destination destination;
        ui::GeoPoint origin;               // route start; search: "near"
        std::vector<ui::GeoPoint> recent;  // search: destinations picked before
        bool alternatives = false;  // route: also look for alternatives
        std::optional<double> heading_degrees;  // route: driving direction at the origin

        static Request search(std::uint64_t revision, std::string query)
        {
            Request request;
            request.kind = Kind::Search;
            request.revision = revision;
            request.query = std::move(query);
            return request;
        }
        static Request route(std::uint64_t revision,
                             ui::Destination destination,
                             ui::GeoPoint origin)
        {
            Request request;
            request.kind = Kind::Route;
            request.revision = revision;
            request.destination = std::move(destination);
            request.origin = origin;
            return request;
        }
    };

    std::vector<ui::Destination> search(const Request& request,
                                        const std::shared_ptr<Cancellation>& cancel)
    {
        std::vector<ui::Destination> output;
        if (request.query.empty()) return output;
        const QString query = QString::fromUtf8(request.query.data(),
                                                static_cast<int>(request.query.size()));
        const OsmAnd::LatLon origin(request.origin.latitude, request.origin.longitude);

        if (index_.is_open()) {
            std::vector<search::SearchIndex::Point> recent;
            for (const auto& point : request.recent)
                recent.push_back({point.latitude, point.longitude});
            const auto hits = index_.search(request.query,
                                            {origin.latitude, origin.longitude}, recent,
                                            kMaxAddressResults);
            for (const auto& hit : hits) output.push_back(destination(hit, origin));
            trace_result("worker=index_hits", request.revision, output.size());
            // An address with a house number needs no places; otherwise the
            // addresses show now and nearby places follow.
            if (!search::parse(request.query).house.empty() || cancel->isAborted())
                return output;
            publish_early(request, cancel, output);
            const auto centre = OsmAnd::Utilities::convertLatLonTo31(origin);
            const OsmAnd::AreaI area(
                OsmAnd::Utilities::get31TileNumberY(origin.latitude + kAmenityRadiusLatitude),
                OsmAnd::Utilities::get31TileNumberX(origin.longitude - kAmenityRadiusLongitude),
                OsmAnd::Utilities::get31TileNumberY(origin.latitude - kAmenityRadiusLatitude),
                OsmAnd::Utilities::get31TileNumberX(origin.longitude + kAmenityRadiusLongitude));
            const std::size_t limit = output.size() + kMaxAmenityResults;
            OsmAnd::AmenitiesByNameSearch amenities(obfs_);
            OsmAnd::AmenitiesByNameSearch::Criteria criteria;
            criteria.name = query;
            criteria.xy31 = centre;
            criteria.bbox31 = area;
            criteria.obfInfoAreaFilter = area;
            amenities.performSearch(criteria,
                [&output, &origin, cancel, limit](const OsmAnd::ISearch::Criteria&,
                                                  const OsmAnd::ISearch::IResultEntry& entry) {
                    const auto& result = static_cast<const OsmAnd::AmenitiesByNameSearch::ResultEntry&>(entry);
                    if (result.amenity && output.size() < limit)
                        output.push_back(destination(*result.amenity, origin));
                    if (output.size() >= limit) cancel->stop_at_limit();
                }, cancel);
            return output;
        }

        OsmAnd::AddressesByNameSearch addresses(obfs_);
        OsmAnd::AddressesByNameSearch::Criteria address_criteria;
        address_criteria.name = query;
        addresses.performSearch(address_criteria,
            [&output, &origin, cancel](const OsmAnd::ISearch::Criteria&,
                                       const OsmAnd::ISearch::IResultEntry& entry) {
                const auto& result = static_cast<const OsmAnd::AddressesByNameSearch::ResultEntry&>(entry);
                if (result.address && output.size() < kMaxAddressResults)
                    output.push_back(destination(*result.address, origin));
                if (output.size() >= kMaxAddressResults) cancel->stop_at_limit();
            }, cancel);

        if (cancel->isAborted()) return output;
        OsmAnd::AmenitiesByNameSearch amenities(obfs_);
        OsmAnd::AmenitiesByNameSearch::Criteria amenity_criteria;
        amenity_criteria.name = query;
        amenity_criteria.xy31 = OsmAnd::Utilities::convertLatLonTo31(origin);
        amenities.performSearch(amenity_criteria,
            [&output, &origin, cancel](const OsmAnd::ISearch::Criteria&,
                                       const OsmAnd::ISearch::IResultEntry& entry) {
                const auto& result = static_cast<const OsmAnd::AmenitiesByNameSearch::ResultEntry&>(entry);
                if (result.amenity && output.size() < kMaxAddressResults + kMaxAmenityResults)
                    output.push_back(destination(*result.amenity, origin));
                if (output.size() >= kMaxAddressResults + kMaxAmenityResults)
                    cancel->stop_at_limit();
            }, cancel);
        return output;
    }

    // Once, by the warm-up thread or the first route, whichever comes first;
    // the other waits for it.
    bool prepare_router()
    {
        std::lock_guard<std::mutex> lock(router_mutex_);
        if (router_ready_) return true;
        if (router_failed_ || routing_xml_.empty() || map_paths_.empty()) return false;
        router_failed_ = !open_router();
        return !router_failed_;
    }

    bool open_router()
    {
        const auto started = std::chrono::steady_clock::now();
        const std::string path = write_routing_xml(routing_xml_);
        // fileName namespaces imported configurations.  A non-empty value
        // would rename "car" and make build("car") return an empty router.
        auto builder = parseRoutingConfigurationFromXml(path.c_str(), "");
        (void)::unlink(path.c_str());
        if (!builder) {
            trace("error=routing_config_parse_failed");
            return false;
        }
        if (builder->routers.find("car") == builder->routers.end()) {
            trace("error=routing_profile_car_missing");
            return false;
        }
        MAP_STR_STR parameters;
        routing_config_ = builder->build("car", 64, parameters);
        if (!routing_config_ || !routing_config_->router) return false;
        // Every region is opened: the router searches all open map files, so
        // a route may cross regions. Files without a routing index are closed.
        std::size_t routable = 0;
        std::size_t with_hh = 0;
        std::vector<std::string> routable_maps;
        std::vector<std::string> opened;
        for (const std::string& path : map_paths_) {
            if (stop_warmup_) {
                // Shutting down: close what is open, route never.
                for (const std::string& done : opened) (void)closeBinaryMapFile(done);
                trace("worker=router_stopped");
                return false;
            }
            opened.push_back(path);
            const auto map = initBinaryMapFile(path, false, true);
            if (map && !map->routingIndexes.empty()) {
                ++routable;
                if (!is_basemap(path)) routable_maps.push_back(path);   // a world basemap is never routed on
                const bool car_hh = std::any_of(map->hhIndexes.begin(), map->hhIndexes.end(),
                    [](const SHARED_PTR<HHRouteIndex>& index) { return index->profile == "car"; });
                if (car_hh) ++with_hh;
            } else {
                (void)closeBinaryMapFile(path);
            }
        }
        if (routable == 0) {
            trace("error=map_has_no_routing_index");
            return false;
        }
        // HH only when every routable file carries car HH data: a route that
        // crosses into a file without it would have no network there.
        has_hh_ = with_hh == routable;
        {
            char line[128];
            std::snprintf(line, sizeof(line), "worker=router hh=%d routable=%zu hh_car=%zu open_ms=%lld",
                          has_hh_ ? 1 : 0, routable, with_hh,
                          static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started).count()));
            trace(line);
        }
        // Contraction-hierarchy routing: only with a sidecar built from exactly these maps.
        if (!ch_sidecar_.empty()) {
            std::string error;
            ch_ = ChPlanner::open(ch_sidecar_, routable_maps, error);
            trace(ch_ ? ("worker=ch " + ch_->describe()).c_str()
                      : ("worker=ch_off why=" + error).c_str());
        }
        router_ready_ = true;
        return true;
    }

    bool accept_route(const Segments& segments, const char* which)
    {
        const RouteCheck check = check_segments(segments);
        if (check.ok()) return true;
        char line[160];
        std::snprintf(line, sizeof(line),
                      "worker=route_rejected route=%s why=straight_legs legs=%zu filler=%zu gaps=%zu "
                      "max_gap_m=%.0f",
                      which, check.legs, check.filler_legs, check.gaps, check.max_gap_metres);
        trace(line);
        return false;
    }

    // Highway-hierarchy routing over the map's precomputed HH sections, with
    // alternatives when asked. Plain A* over the same 15 km took 15.4 s on
    // a free core and HH 3.2 s, the same route (route_bench, 2026-10-15).
    // Empty when the map has no HH data or HH finds nothing.
    std::vector<Segments> route_hh(RoutePlannerFrontEnd& front_end,
                                   const SHARED_PTR<RoutingContext>& context,
                                   int sx, int sy, int ex, int ey, bool alternatives)
    {
        std::vector<Segments> routes;
        std::unique_ptr<HHRoutingConfig> config(HHRoutingConfig::astar(0));
        config->calcDetailed(HHRoutingConfig::CALCULATE_ALL_DETAILED);
        if (alternatives) config->calcAlternative();
        config->INITIAL_DIRECTION = context->config->initialDirection;
        HHNetworkRouteRes* result = nullptr;
        {
            HHRoutePlanner planner(context.get());
            result = planner.runRouting(sx, sy, ex, ey, config.get());
        }
        if (!result) return routes;
        // The result owns neither its alternatives nor itself.
        std::unique_ptr<HHNetworkRouteRes> main(result);
        std::vector<std::unique_ptr<HHNetworkRouteRes>> others;
        for (auto* alt : main->altRoutes) others.emplace_back(alt);
        main->altRoutes.clear();
        if (!main->error.empty() || main->detailed.empty()) {
            char line[160];
            std::snprintf(line, sizeof(line), "worker=hh_rejected why=%s",
                          main->error.empty() ? "no_route" : main->error.c_str());
            trace(line);
            return routes;
        }
        // What RoutePlannerFrontEnd does with an HH result.
        prepareResult(context.get(), main->detailed);
        front_end.makeStartEndPointsPrecise(context.get(), main->detailed, sx, sy, ex, ey);
        // Where a leg's road geometry could not be loaded the core joins the
        // network nodes with a straight line; such a route crosses water and
        // forest. A bad main route fails the request; a bad
        // alternative is dropped.
        if (!accept_route(main->detailed, "main")) return routes;
        routes.push_back(std::move(main->detailed));
        for (auto& alt : others) {
            if (alt->detailed.empty()) continue;
            prepareResult(context.get(), alt->detailed);
            front_end.makeStartEndPointsPrecise(context.get(), alt->detailed, sx, sy, ex, ey);
            if (!accept_route(alt->detailed, "alternative")) continue;
            routes.push_back(std::move(alt->detailed));
        }
        return routes;
    }

    std::vector<ui::RouteAlternative> route(
        const Request& request, const std::shared_ptr<Cancellation>& cancel)
    {
        const auto preparing = std::chrono::steady_clock::now();
        if (!prepare_router() || cancel->isAborted()) return {};
        {
            char line[160];
            std::snprintf(line, sizeof(line), "worker=route_ready revision=%llu wait_ms=%lld",
                static_cast<unsigned long long>(request.revision),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - preparing).count()));
            trace(line);
        }
        const auto started = std::chrono::steady_clock::now();
        const int sx = get31TileNumberX(request.origin.longitude);
        const int sy = get31TileNumberY(request.origin.latitude);
        const int ex = get31TileNumberX(request.destination.longitude);
        const int ey = get31TileNumberY(request.destination.latitude);
        RoutePlannerFrontEnd front_end;
        std::vector<Segments> found;
        const char* method = "ch";
        // The routers read the start direction from the shared configuration
        // (radians clockwise from north, as RouteDataObject::directionRoute),
        // and HH clears it when done, so it is set again before each router.
        const auto new_context = [&] {
            routing_config_->initialDirection = request.heading_degrees
                ? static_cast<float>(*request.heading_degrees * M_PI / 180.0) : NO_DIRECTION;
            auto context = front_end.buildRoutingContext(routing_config_);
            context->progress = std::make_shared<RouteProgress>(cancel);
            return context;
        };
        if (request.heading_degrees)
            trace_result("worker=route_heading", request.revision,
                         static_cast<std::size_t>(*request.heading_degrees));
        // Organic Maps' turns for each route found, while its routing context (the
        // roads at every node) is alive; parallel to |found|.
        std::vector<omaps::Result> found_turns;
        const auto annotate = [&](RoutingContext& context) {
            while (found_turns.size() < found.size()) {
                if (!organic_turns_) {
                    found_turns.emplace_back();
                    continue;
                }
                const auto annotate_started = std::chrono::steady_clock::now();
                omaps::Result result = omaps::annotate(
                    context, found[found_turns.size()], request.origin.latitude,
                    request.origin.longitude, request.destination.latitude,
                    request.destination.longitude);
                const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - annotate_started).count();
                char line[200];
                if (result.ok)
                    std::snprintf(line, sizeof(line), "worker=omaps turns=%zu ms=%lld",
                                  result.turns.size(), ms);
                else
                    std::snprintf(line, sizeof(line), "worker=omaps_error why=%.150s ms=%lld",
                                  result.error.c_str(), ms);
                trace(line);
                found_turns.push_back(std::move(result));
            }
        };
        // A configured CH sidecar is the selected router. A missing/stale index
        // or failed query must be visible, not silently trigger slower routers.
        // Empty ch_sidecar_ is the explicit switch to OsmAnd's HH/A* router.
        if (!ch_sidecar_.empty() && !ch_) {
            trace("worker=route_failed method=ch why=sidecar_unavailable");
            return {};
        }
        if (ch_) {
            auto context = new_context();
            std::string why;
            Segments segments = ch_->route(front_end, context, sx, sy, ex, ey, why,
                                           request.heading_degrees.value_or(-1.0));
            if (!segments.empty() && accept_route(segments, "ch")) {
                const ChPlanner::Timing& timing = ch_->last_timing();
                char line[200];
                std::snprintf(line, sizeof(line),
                              "worker=ch_route snap=%s snap_ms=%.0f search_ms=%.1f roads_ms=%.0f prepare_ms=%.0f "
                              "runs=%zu edges=%zu settled=%zu",
                              timing.direct ? "same_road" : timing.road_snap ? "road" : "node",
                              timing.snap_ms, timing.search_ms, timing.roads_ms, timing.prepare_ms,
                              timing.road_runs, timing.base_edges, timing.settled);
                trace(line);
                found.push_back(std::move(segments));
                annotate(*context);
            } else if (!cancel->isAborted()) {
                trace(("worker=route_failed method=ch why=" +
                       (why.empty() ? std::string("route_rejected") : why)).c_str());
                return {};
            }
        }
        if (!ch_ && has_hh_ && !cancel->isAborted()) {
            method = "hh";
            auto context = new_context();
            found = route_hh(front_end, context, sx, sy, ex, ey,
                             request.alternatives && kAlternativeRoutes);
            annotate(*context);
        }
        if (!ch_ && !has_hh_ && !cancel->isAborted()) {
            // Explicit OsmAnd mode, maps without HH data: select A* directly.
            method = "astar";
            trace("worker=route_method method=astar why=no_hh_data");
            auto context = new_context();
            std::vector<int> intermediates_x;
            std::vector<int> intermediates_y;
            auto segments = front_end.searchRoute(context, sx, sy, ex, ey,
                                                  intermediates_x, intermediates_y, nullptr);
            if (!segments.empty()) {
                found.push_back(std::move(segments));
                annotate(*context);
            }
        }
        if (cancel->isAborted()) return {};
        if (found.empty()) {
            trace((std::string("worker=route_failed method=") + method + " why=no_usable_route").c_str());
            return {};
        }
        {
            char line[96];
            std::snprintf(line, sizeof(line), "worker=route_calculated method=%s routes=%zu ms=%lld",
                          method, found.size(),
                          static_cast<long long>(std::chrono::duration_cast<
                              std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                         started).count()));
            trace(line);
        }
        // Fastest first; the others are labelled against it.
        std::vector<std::size_t> order(found.size());
        for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return duration_of(found[a]) < duration_of(found[b]);
        });
        {
            std::vector<Segments> sorted;
            std::vector<omaps::Result> sorted_turns;
            for (const std::size_t index : order) {
                sorted.push_back(std::move(found[index]));
                sorted_turns.push_back(std::move(found_turns[index]));
            }
            found = std::move(sorted);
            found_turns = std::move(sorted_turns);
        }
        std::vector<ui::RouteAlternative> output;
        for (std::size_t index = 0; index < found.size() && index < kMaxRoutes; ++index) {
            // Organic Maps switched off: OsmAnd's own manoeuvres (no Result).
            const omaps::Result* turns = organic_turns_ ? &found_turns[index] : nullptr;
            ui::RouteAlternative alternative = convert(request, found, index, turns);
            if (alternative.geometry.size() < 2) continue;
            output.push_back(std::move(alternative));
        }
        if (output.size() > 1) {
            const double fastest_metres = output.front().distance_metres;
            for (std::size_t index = 1; index < output.size(); ++index)
                output[index].reason =
                    output[index].distance_metres < fastest_metres ? "Shorter" : "Alternative";
        }
        if (!output.empty())
            trace_result("worker=route_maneuvers", request.revision,
                         output.front().maneuvers.size());
        return output;
    }

    // One routing result as a UI route; geometry shorter than two points
    // means unusable.
    // turns: Organic Maps' turns for this route (omaps_turns.h); null or failed:
    // OsmAnd's own manoeuvres.
    ui::RouteAlternative convert(const Request& request, std::vector<Segments>& found,
                                 std::size_t route_index, const omaps::Result* turns)
    {
        Segments& segments = found[route_index];  // getStreetName takes it non-const
        ui::RouteAlternative alternative;
        alternative.id = route_index == 0 ? "car-fastest"
                                          : "car-alternative-" + std::to_string(route_index);
        alternative.reason = "Fastest";
        alternative.arrival = "--";
        // Per segment: route distance and geometry index at its start, its road.
        std::vector<double> distance_at(segments.size(), 0.0);
        std::vector<std::size_t> geometry_at(segments.size(), 0);
        std::vector<std::string> road_of(segments.size());
        for (std::size_t segment_index = 0; segment_index < segments.size();
             ++segment_index) {
            const auto& segment = segments[segment_index];
            distance_at[segment_index] = alternative.distance_metres;
            geometry_at[segment_index] =
                alternative.geometry.empty() ? 0 : alternative.geometry.size() - 1u;
            if (!segment) continue;
            const double distance_at_start = alternative.distance_metres;
            // Native road names (the design fonts include Cyrillic).
            std::string road = segment->getStreetName(
                "", false, segments, static_cast<int>(segment_index));
            road_of[segment_index] = road;
            int limit = 0;
            if (segment->object) {
                const float metres_per_second =
                    segment->object->getMaximumSpeed(segment->isForwardDirection());
                if (metres_per_second > 0.0f)
                    limit = static_cast<int>(std::lround(metres_per_second * 3.6f));
            }
            if (alternative.roads.empty() || alternative.roads.back().name != road ||
                alternative.roads.back().speed_limit_kmh != limit)
                alternative.roads.push_back({distance_at_start, road, limit});
            alternative.distance_metres += segment->distance;
            alternative.duration_seconds += segment->segmentTime;
            const int start = segment->getStartPointIndex();
            const int end = segment->getEndPointIndex();
            const int step = start <= end ? 1 : -1;
            for (int index = start;; index += step) {
                const ::LatLon point = segment->getPoint(index);
                if (alternative.geometry.empty() ||
                    alternative.geometry.back().latitude != point.lat ||
                    alternative.geometry.back().longitude != point.lon)
                    alternative.geometry.push_back({point.lat, point.lon});
                if (index == end) break;
            }
        }
        alternative.duration = duration_label(alternative.duration_seconds);
        alternative.distance = distance_label(alternative.distance_metres);
        if (alternative.geometry.size() < 2) return alternative;
        // The manoeuvres: at their route point, a segment's start or (Organic Maps'
        // turns) any point in it; the distance along the geometry from the segment start.
        for (auto& placed : route_maneuvers(segments, turns)) {
            if (placed.segment >= segments.size()) continue;
            ui::Maneuver maneuver = std::move(placed.maneuver);
            const std::size_t at = std::min(placed.route_point, alternative.geometry.size() - 1u);
            const std::size_t from = std::min(geometry_at[placed.segment], at);
            double metres = distance_at[placed.segment];
            for (std::size_t i = from; i < at; ++i)
                metres += metres_between(alternative.geometry[i], alternative.geometry[i + 1]);
            maneuver.road = road_of[placed.segment];
            maneuver.distance_from_start_metres = std::min(metres, alternative.distance_metres);
            maneuver.geometry_index = at;
            if (route_index == 0 && alternative.maneuvers.size() < kJunctionTraceMax)
                trace_junction(alternative.maneuvers.size(), maneuver);
            alternative.maneuvers.push_back(std::move(maneuver));
        }
        ui::Maneuver arrive;
        arrive.kind = ui::ManeuverKind::Arrive;
        arrive.road = request.destination.title;
        arrive.distance_from_start_metres = alternative.distance_metres;
        arrive.geometry_index = alternative.geometry.size() - 1u;
        alternative.maneuvers.push_back(std::move(arrive));
        return alternative;
    }

    // Search results before the request is done (index addresses ahead of
    // the slower place search); the final publish replaces them.
    void publish_early(const Request& request, const std::shared_ptr<Cancellation>& cancel,
                       const std::vector<ui::Destination>& destinations)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || active_ != cancel || cancel->superseded()) return;
            result_ = {request.revision, destinations};
            has_result_ = true;
        }
        trace_result("worker=search_published_early", request.revision, destinations.size());
        if (wakeup_) (void)wakeup_->request();
    }

    void run()
    {
        place_worker_thread();
        for (;;) {
            Request request;
            std::shared_ptr<Cancellation> cancel;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || has_pending_; });
                if (stopping_) return;
                request = std::move(pending_);
                has_pending_ = false;
                active_ = std::make_shared<Cancellation>();
                cancel = active_;
            }

            using Kind = Request::Kind;
            const auto begun = std::chrono::steady_clock::now();
            const auto queue_ms = std::chrono::duration_cast<std::chrono::milliseconds>(begun - request.queued).count();
            const char* kind = request.kind == Kind::Search ? "search"
                : "route";
            {
                char line[160];
                std::snprintf(line, sizeof(line), "worker=request_begin kind=%s revision=%llu queue_ms=%lld",
                    kind, static_cast<unsigned long long>(request.revision), static_cast<long long>(queue_ms));
                trace(line);
            }
            trace_result(request.kind == Kind::Search ? "worker=search_begin"
                         : "worker=route_begin",
                         request.revision,
                         request.kind == Kind::Search ? request.query.size() : 1u);

            std::vector<ui::Destination> destinations;
            std::vector<ui::RouteAlternative> routes;
            try {
                if (request.kind == Kind::Search)
                    destinations = search(request, cancel);
                else
                    routes = route(request, cancel);
            } catch (...) {
                trace(request.kind == Kind::Search ? "error=destination_search_exception"
                      : "error=route_calculation_exception");
                // Publish an empty result so the UI leaves its busy state.
                // A genuinely superseded request has already been cancelled
                // by submit(), and remains excluded by the check below.
            }
            {
                char line[200];
                const auto work_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - begun).count();
                std::snprintf(line, sizeof(line),
                    "worker=request_done kind=%s revision=%llu work_ms=%lld total_ms=%lld cancelled=%d",
                    kind, static_cast<unsigned long long>(request.revision),
                    static_cast<long long>(work_ms), static_cast<long long>(queue_ms + work_ms),
                    cancel->superseded() ? 1 : 0);
                trace(line);
            }
            bool published = false;
            std::size_t published_count = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return;
                if (active_ == cancel && !cancel->superseded()) {
                    if (request.kind == Kind::Search) {
                        result_ = {request.revision, std::move(destinations)};
                        published_count = result_.destinations.size();
                        has_result_ = true;
                    } else {
                        route_result_ = {request.revision, std::move(routes)};
                        published_count = route_result_.routes.size();
                        has_route_result_ = true;
                    }
                    published = true;
                }
                if (active_ == cancel) active_.reset();
            }
            if (published)
                trace_result(request.kind == Kind::Search ? "worker=search_published"
                             : "worker=route_published",
                             request.revision,
                             published_count);
            if (published && wakeup_) (void)wakeup_->request();
        }
    }

    search::SearchIndex index_;  // read by the worker thread only, after open
    std::shared_ptr<OsmAnd::ObfsCollection> obfs_;
    std::shared_ptr<Wakeup> wakeup_;
    std::vector<std::string> map_paths_;
    std::string routing_xml_;
    std::string ch_sidecar_;  // empty: contraction-hierarchy routing is off
    // Turn instructions from Organic Maps' generator (omaps_turns.h); false: OsmAnd's.
    bool organic_turns_ = true;
    std::unique_ptr<ChPlanner> ch_;
    SHARED_PTR<RoutingConfiguration> routing_config_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    std::thread warmup_;                      // opens the routing maps once
    std::mutex router_mutex_;                 // prepare_router: warm-up or first route
    std::atomic<bool> stop_warmup_{false};
    bool router_failed_ = false;              // under router_mutex_
    Request pending_;
    SearchResult result_;
    RouteResult route_result_;
    std::shared_ptr<Cancellation> active_;
    bool has_pending_ = false;
    bool has_result_ = false;
    bool has_route_result_ = false;
    bool stopping_ = false;
    bool router_ready_ = false;
    bool has_hh_ = false;  // the map has highway-hierarchy routing sections
};

NavigationWorker::NavigationWorker(std::shared_ptr<OsmAnd::ObfsCollection> obfs,
                                   std::shared_ptr<Wakeup> wakeup,
                                   std::vector<std::string> map_paths,
                                   std::string routing_xml,
                                   std::string search_index,
                                   std::string ch_sidecar,
                                   bool organic_turns)
    : impl_(new Impl(std::move(obfs), std::move(wakeup), std::move(map_paths),
                     std::move(routing_xml), search_index, std::move(ch_sidecar),
                     organic_turns))
{
}

bool NavigationWorker::has_search_index() const
{
    return impl_->has_index();
}

void NavigationWorker::submit_route(std::uint64_t revision,
                                    ui::Destination destination,
                                    ui::GeoPoint origin, bool alternatives,
                                    std::optional<double> heading_degrees)
{
    impl_->submit_route(revision, std::move(destination), origin, alternatives,
                        heading_degrees);
}

NavigationWorker::~NavigationWorker() = default;

void NavigationWorker::submit_search(std::uint64_t revision, std::string query,
                                     ui::GeoPoint origin, std::vector<ui::GeoPoint> recent)
{
    impl_->submit(revision, std::move(query), origin, std::move(recent));
}

bool NavigationWorker::take_route_result(RouteResult& output)
{
    return impl_->take(output);
}

bool NavigationWorker::take_search_result(SearchResult& output)
{
    return impl_->take(output);
}

void NavigationWorker::stop() noexcept { impl_->stop(); }

} // namespace navicore_osmand
