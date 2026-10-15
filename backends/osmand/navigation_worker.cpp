#include "navigation_worker.h"


#include "diagnostics.h"
#include "session.h"

#include <OsmAndCore/Data/Address.h>
#include <OsmAndCore/Data/Amenity.h>
#include <OsmAndCore/IQueryController.h>
#include <OsmAndCore/LatLon.h>
#include <OsmAndCore/Logging.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/RoadLocator.h>
#include <OsmAndCore/Search/AddressesByNameSearch.h>
#include <OsmAndCore/Search/AmenitiesByNameSearch.h>
#include <OsmAndCore/Search/ReverseGeocoder.h>
#include <OsmAndCore/Utilities.h>

// Core and core-legacy both ship a Logging.h with the same public names but
// different include guards. The provider uses Core's logger consistently.
#define _OSMAND_LOGGING_H_
#include "binaryRead.h"
#include "commonOsmAndCore.h"
#include "routeCalculationProgress.h"
#include "routePlannerFrontEnd.h"
#include "routeSegmentResult.h"
#include "routingConfiguration.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <utility>

namespace navicore_osmand {
namespace {

constexpr std::size_t kMaxAddressResults = 8;
constexpr std::size_t kMaxAmenityResults = 4;
const OsmAnd::LatLon kDemoOrigin(50.4501, 30.5234);

std::string utf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::string distance_text(const OsmAnd::PointI& point)
{
    const auto destination = OsmAnd::Utilities::convert31ToLatLon(point);
    const double metres = OsmAnd::Utilities::distance(kDemoOrigin, destination);
    char text[32] = {};
    if (metres < 1000.0)
        std::snprintf(text, sizeof(text), "%.0f m", metres);
    else
        std::snprintf(text, sizeof(text), "%.1f km", metres / 1000.0);
    return text;
}

std::string address_kind(OsmAnd::AddressType type)
{
    switch (type) {
    case OsmAnd::AddressType::StreetGroup: return "Place";
    case OsmAnd::AddressType::Street: return "Street";
    case OsmAnd::AddressType::Building: return "Address";
    case OsmAnd::AddressType::StreetIntersection: return "Intersection";
    }
    return "Address";
}

ui::Destination destination(const OsmAnd::Address& address)
{
    const auto coordinates = OsmAnd::Utilities::convert31ToLatLon(address.position31);
    std::string title = utf8(address.getName(QStringLiteral("uk"), false));
    if (title.empty()) title = utf8(address.nativeName);
    return {"address:" + std::to_string(address.position31.x) + ":" +
                std::to_string(address.position31.y),
            std::move(title), address_kind(address.addressType),
            distance_text(address.position31), coordinates.latitude,
            coordinates.longitude};
}

ui::Destination destination(const OsmAnd::Amenity& amenity)
{
    const auto coordinates = OsmAnd::Utilities::convert31ToLatLon(amenity.position31);
    std::string title = utf8(amenity.getName(QStringLiteral("uk"), false));
    if (title.empty()) title = utf8(amenity.nativeName);
    std::string subtitle = utf8(amenity.subType);
    if (subtitle.empty()) subtitle = utf8(amenity.type);
    return {"amenity:" + std::to_string(amenity.position31.x) + ":" +
                std::to_string(amenity.position31.y),
            std::move(title), std::move(subtitle),
            distance_text(amenity.position31), coordinates.latitude,
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
    char text[32] = {};
    if (metres < 1000.0) std::snprintf(text, sizeof(text), "%.0f m", metres);
    else std::snprintf(text, sizeof(text), "%.1f km", metres / 1000.0);
    return text;
}

std::string duration_label(double seconds)
{
    const int minutes = static_cast<int>(std::lround(seconds / 60.0));
    char text[32] = {};
    if (minutes < 60) std::snprintf(text, sizeof(text), "%d min", minutes);
    else std::snprintf(text, sizeof(text), "%d h %02d min", minutes / 60, minutes % 60);
    return text;
}

ui::ManeuverKind maneuver_kind(int value)
{
    switch (value) {
    case TurnType::TL: return ui::ManeuverKind::Left;
    case TurnType::TSLL: return ui::ManeuverKind::SlightLeft;
    case TurnType::TSHL: return ui::ManeuverKind::SharpLeft;
    case TurnType::TR: return ui::ManeuverKind::Right;
    case TurnType::TSLR: return ui::ManeuverKind::SlightRight;
    case TurnType::TSHR: return ui::ManeuverKind::SharpRight;
    case TurnType::KL: return ui::ManeuverKind::KeepLeft;
    case TurnType::KR: return ui::ManeuverKind::KeepRight;
    case TurnType::TU:
    case TurnType::TRU: return ui::ManeuverKind::UTurn;
    case TurnType::RNDB:
    case TurnType::RNLB: return ui::ManeuverKind::Roundabout;
    default: return ui::ManeuverKind::Straight;
    }
}

// OsmAnd lane value: bit 0 = the route uses the lane, bits 1-4 its main
// turn, bits 5-9 a second turn (TurnType::getPrimaryTurn/getSecondaryTurn).
std::vector<ui::Lane> lanes_of(const SHARED_PTR<TurnType>& turn)
{
    std::vector<ui::Lane> lanes;
    if (!turn)
        return lanes;
    for (const int value : turn->getLanes()) {
        ui::Lane lane;
        const int primary = TurnType::getPrimaryTurn(value);
        const int secondary = TurnType::getSecondaryTurn(value);
        lane.kind = maneuver_kind(primary ? primary : TurnType::C);
        lane.combined = secondary != 0;
        if (lane.combined)
            lane.also = maneuver_kind(secondary);
        lane.recommended = (value & 1) != 0;
        lanes.push_back(lane);
    }
    return lanes;
}

} // namespace

class NavigationWorker::Impl {
public:
    Impl(std::shared_ptr<OsmAnd::ObfsCollection> obfs,
         std::shared_ptr<Wakeup> wakeup,
         std::string map_path,
         std::string routing_xml)
        : obfs_(std::move(obfs)), wakeup_(std::move(wakeup)),
          map_path_(std::move(map_path)), routing_xml_(std::move(routing_xml))
    {
        // Start only after every request/result flag below has completed
        // construction. Starting in the member initializer races run()
        // against later members such as has_pending_ and stopping_.
        thread_ = std::thread(&Impl::run, this);
        trace("worker=thread_started");
    }

    ~Impl() { stop(); }

    void submit(std::uint64_t revision, std::string query)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (active_) active_->cancel();
        pending_ = Request::search(revision, std::move(query));
        has_pending_ = true;
        trace_result("worker=search_queued", revision, pending_.query.size());
        condition_.notify_one();
    }

    void submit_route(std::uint64_t revision, ui::Destination destination,
                      ui::GeoPoint origin)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (active_) active_->cancel();
        pending_ = Request::route(revision, std::move(destination), origin);
        has_pending_ = true;
        trace_result("worker=route_queued", revision, 1u);
        condition_.notify_one();
    }

    void submit_address(std::uint64_t revision, ui::GeoPoint point)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (active_) active_->cancel();
        pending_ = Request::address(revision, point);
        has_pending_ = true;
        trace_result("worker=address_queued", revision, 1u);
        condition_.notify_one();
    }

    bool take(AddressResult& output)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_address_result_) return false;
        output = std::move(address_result_);
        has_address_result_ = false;
        return true;
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
        if (thread_.joinable()) thread_.join();
        if (router_ready_) {
            (void)closeBinaryMapFile(map_path_);
            router_ready_ = false;
        }
    }

private:
    struct Request {
        enum class Kind { Search, Route, Address } kind = Kind::Search;
        std::uint64_t revision = 0;
        std::string query;
        ui::Destination destination;
        ui::GeoPoint origin;

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
        static Request address(std::uint64_t revision, ui::GeoPoint point)
        {
            Request request;
            request.kind = Kind::Address;
            request.revision = revision;
            request.origin = point;
            return request;
        }
    };

    // Nearest address of a point: "street, house number", or the street
    // alone; empty when there is no road nearby.
    std::string address(const Request& request)
    {
        if (!road_locator_)
            road_locator_ = std::make_shared<OsmAnd::RoadLocator>(obfs_);
        OsmAnd::ReverseGeocoder geocoder(obfs_, road_locator_);
        OsmAnd::ReverseGeocoder::Criteria criteria;
        criteria.latLon = OsmAnd::LatLon(request.origin.latitude, request.origin.longitude);
        const auto entry = geocoder.performSearch(criteria);
        if (!entry) return {};
        const auto name = [](const OsmAnd::Address& address) {
            std::string text = utf8(address.getName(QStringLiteral("uk"), false));
            return text.empty() ? utf8(address.nativeName) : text;
        };
        std::string street = entry->street ? name(*entry->street) : utf8(entry->streetName);
        if (street.empty()) return {};
        if (entry->building) {
            const std::string number = name(*entry->building);
            if (!number.empty()) street += ", " + number;
        }
        return street;
    }

    std::vector<ui::Destination> search(const Request& request,
                                        const std::shared_ptr<Cancellation>& cancel)
    {
        std::vector<ui::Destination> output;
        if (request.query.empty()) return output;
        const QString query = QString::fromUtf8(request.query.data(),
                                                static_cast<int>(request.query.size()));

        OsmAnd::AddressesByNameSearch addresses(obfs_);
        OsmAnd::AddressesByNameSearch::Criteria address_criteria;
        address_criteria.name = query;
        addresses.performSearch(address_criteria,
            [&output, cancel](const OsmAnd::ISearch::Criteria&,
                              const OsmAnd::ISearch::IResultEntry& entry) {
                const auto& result = static_cast<const OsmAnd::AddressesByNameSearch::ResultEntry&>(entry);
                if (result.address && output.size() < kMaxAddressResults)
                    output.push_back(destination(*result.address));
                if (output.size() >= kMaxAddressResults) cancel->stop_at_limit();
            }, cancel);

        if (cancel->isAborted()) return output;
        OsmAnd::AmenitiesByNameSearch amenities(obfs_);
        OsmAnd::AmenitiesByNameSearch::Criteria amenity_criteria;
        amenity_criteria.name = query;
        amenity_criteria.xy31 = OsmAnd::Utilities::convertLatLonTo31(kDemoOrigin);
        amenities.performSearch(amenity_criteria,
            [&output, cancel](const OsmAnd::ISearch::Criteria&,
                              const OsmAnd::ISearch::IResultEntry& entry) {
                const auto& result = static_cast<const OsmAnd::AmenitiesByNameSearch::ResultEntry&>(entry);
                if (result.amenity && output.size() < kMaxAddressResults + kMaxAmenityResults)
                    output.push_back(destination(*result.amenity));
                if (output.size() >= kMaxAddressResults + kMaxAmenityResults)
                    cancel->stop_at_limit();
            }, cancel);
        return output;
    }

    bool prepare_router()
    {
        if (router_ready_) return true;
        if (routing_xml_.empty() || map_path_.empty()) return false;
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
        const auto map = initBinaryMapFile(map_path_, false, true);
        if (!map || map->routingIndexes.empty()) {
            (void)closeBinaryMapFile(map_path_);
            trace("error=map_has_no_routing_index");
            return false;
        }
        router_ready_ = true;
        return true;
    }

    std::vector<ui::RouteAlternative> route(
        const Request& request, const std::shared_ptr<Cancellation>& cancel)
    {
        if (!prepare_router() || cancel->isAborted()) return {};
        RoutePlannerFrontEnd front_end;
        auto context = front_end.buildRoutingContext(routing_config_);
        context->progress = std::make_shared<RouteProgress>(cancel);
        std::vector<int> intermediates_x;
        std::vector<int> intermediates_y;
        auto segments = front_end.searchRoute(
            context,
            get31TileNumberX(request.origin.longitude),
            get31TileNumberY(request.origin.latitude),
            get31TileNumberX(request.destination.longitude),
            get31TileNumberY(request.destination.latitude),
            intermediates_x, intermediates_y, nullptr);
        if (cancel->isAborted() || segments.empty()) return {};

        ui::RouteAlternative alternative;
        alternative.id = "car-fastest";
        alternative.reason = "Fastest";
        alternative.arrival = "--";
        for (std::size_t segment_index = 0; segment_index < segments.size();
             ++segment_index) {
            const auto& segment = segments[segment_index];
            if (!segment) continue;
            const double distance_at_start = alternative.distance_metres;
            const std::size_t geometry_at_start = alternative.geometry.size();
            // Native road names (the design fonts include Cyrillic).
            std::string road = segment->getStreetName(
                "", false, segments, static_cast<int>(segment_index));
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
            if (segment->turnType &&
                segment->turnType->getValue() != TurnType::C) {
                ui::Maneuver maneuver;
                maneuver.kind = maneuver_kind(segment->turnType->getValue());
                maneuver.road = road;
                maneuver.distance_from_start_metres = distance_at_start;
                maneuver.geometry_index = geometry_at_start;
                maneuver.roundabout_exit = segment->turnType->getExitOut();
                maneuver.lanes = lanes_of(segment->turnType);
                alternative.maneuvers.push_back(std::move(maneuver));
            }
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
        if (alternative.geometry.size() < 2) return {};
        alternative.maneuvers.push_back({
            ui::ManeuverKind::Arrive, request.destination.title,
            alternative.distance_metres, alternative.geometry.size() - 1u, 0, {}});
        trace_result("worker=route_maneuvers", request.revision,
                     alternative.maneuvers.size());
        return {std::move(alternative)};
    }

    void run()
    {
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
            trace_result(request.kind == Kind::Search ? "worker=search_begin"
                         : request.kind == Kind::Route ? "worker=route_begin"
                                                       : "worker=address_begin",
                         request.revision,
                         request.kind == Kind::Search ? request.query.size() : 1u);

            std::vector<ui::Destination> destinations;
            std::vector<ui::RouteAlternative> routes;
            std::string address_text;
            try {
                if (request.kind == Kind::Search)
                    destinations = search(request, cancel);
                else if (request.kind == Kind::Route)
                    routes = route(request, cancel);
                else
                    address_text = address(request);
            } catch (...) {
                trace(request.kind == Kind::Search ? "error=destination_search_exception"
                      : request.kind == Kind::Route ? "error=route_calculation_exception"
                                                    : "error=address_exception");
                // Publish an empty result so the UI leaves its busy state.
                // A genuinely superseded request has already been cancelled
                // by submit(), and remains excluded by the check below.
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
                    } else if (request.kind == Kind::Route) {
                        route_result_ = {request.revision, std::move(routes)};
                        published_count = route_result_.routes.size();
                        has_route_result_ = true;
                    } else {
                        published_count = address_text.empty() ? 0u : 1u;
                        address_result_ = {request.revision, std::move(address_text)};
                        has_address_result_ = true;
                    }
                    published = true;
                }
                if (active_ == cancel) active_.reset();
            }
            if (published)
                trace_result(request.kind == Kind::Search ? "worker=search_published"
                             : request.kind == Kind::Route ? "worker=route_published"
                                                           : "worker=address_published",
                             request.revision,
                             published_count);
            if (published && wakeup_) (void)wakeup_->request();
        }
    }

    std::shared_ptr<OsmAnd::ObfsCollection> obfs_;
    std::shared_ptr<Wakeup> wakeup_;
    std::string map_path_;
    std::string routing_xml_;
    SHARED_PTR<RoutingConfiguration> routing_config_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    Request pending_;
    SearchResult result_;
    RouteResult route_result_;
    AddressResult address_result_;
    std::shared_ptr<const OsmAnd::IRoadLocator> road_locator_;  // worker thread
    std::shared_ptr<Cancellation> active_;
    bool has_pending_ = false;
    bool has_result_ = false;
    bool has_route_result_ = false;
    bool has_address_result_ = false;
    bool stopping_ = false;
    bool router_ready_ = false;
};

NavigationWorker::NavigationWorker(std::shared_ptr<OsmAnd::ObfsCollection> obfs,
                                   std::shared_ptr<Wakeup> wakeup,
                                   std::string map_path,
                                   std::string routing_xml)
    : impl_(new Impl(std::move(obfs), std::move(wakeup), std::move(map_path),
                     std::move(routing_xml)))
{
}

void NavigationWorker::submit_route(std::uint64_t revision,
                                    ui::Destination destination,
                                    ui::GeoPoint origin)
{
    impl_->submit_route(revision, std::move(destination), origin);
}

NavigationWorker::~NavigationWorker() = default;

void NavigationWorker::submit_search(std::uint64_t revision, std::string query)
{
    impl_->submit(revision, std::move(query));
}

bool NavigationWorker::take_route_result(RouteResult& output)
{
    return impl_->take(output);
}

bool NavigationWorker::take_search_result(SearchResult& output)
{
    return impl_->take(output);
}

void NavigationWorker::submit_address(std::uint64_t revision, ui::GeoPoint point)
{
    impl_->submit_address(revision, point);
}

bool NavigationWorker::take_address_result(AddressResult& output)
{
    return impl_->take(output);
}

void NavigationWorker::stop() noexcept { impl_->stop(); }

} // namespace navicore_osmand
