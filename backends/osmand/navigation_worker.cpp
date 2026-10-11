#include "navigation_worker.h"

#include "diagnostics.h"
#include "session.h"

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

} // namespace

class NavigationWorker::Impl {
public:
    Impl(std::shared_ptr<OsmAnd::ObfsCollection> obfs,
         std::shared_ptr<Wakeup> wakeup,
         std::string map_path,
         std::string routing_xml)
        : obfs_(std::move(obfs)), wakeup_(std::move(wakeup)),
          map_path_(std::move(map_path)), routing_xml_(std::move(routing_xml)),
          thread_(&Impl::run, this)
    {
    }

    ~Impl() { stop(); }

    void submit(std::uint64_t revision, std::string query)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (active_) active_->cancel();
        pending_ = Request::search(revision, std::move(query));
        has_pending_ = true;
        condition_.notify_one();
    }

    void submit_route(std::uint64_t revision, ui::Destination destination)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (active_) active_->cancel();
        pending_ = Request::route(revision, std::move(destination));
        has_pending_ = true;
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
        if (thread_.joinable()) thread_.join();
        if (router_ready_) {
            (void)closeBinaryMapFile(map_path_);
            router_ready_ = false;
        }
    }

private:
    struct Request {
        enum class Kind { Search, Route } kind = Kind::Search;
        std::uint64_t revision = 0;
        std::string query;
        ui::Destination destination;

        static Request search(std::uint64_t revision, std::string query)
        {
            Request request;
            request.kind = Kind::Search;
            request.revision = revision;
            request.query = std::move(query);
            return request;
        }
        static Request route(std::uint64_t revision, ui::Destination destination)
        {
            Request request;
            request.kind = Kind::Route;
            request.revision = revision;
            request.destination = std::move(destination);
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
        const auto segments = front_end.searchRoute(
            context,
            get31TileNumberX(kDemoOrigin.longitude),
            get31TileNumberY(kDemoOrigin.latitude),
            get31TileNumberX(request.destination.longitude),
            get31TileNumberY(request.destination.latitude),
            intermediates_x, intermediates_y, nullptr);
        if (cancel->isAborted() || segments.empty()) return {};

        ui::RouteAlternative alternative;
        alternative.id = "car-fastest";
        alternative.reason = "Fastest";
        alternative.arrival = "--";
        for (const auto& segment : segments) {
            if (!segment) continue;
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

            std::vector<ui::Destination> destinations;
            std::vector<ui::RouteAlternative> routes;
            try {
                if (request.kind == Request::Kind::Search)
                    destinations = search(request, cancel);
                else
                    routes = route(request, cancel);
            } catch (...) {
                trace(request.kind == Request::Kind::Search
                          ? "error=destination_search_exception"
                          : "error=route_calculation_exception");
                // Publish an empty result so the UI leaves its busy state.
                // A genuinely superseded request has already been cancelled
                // by submit(), and remains excluded by the check below.
            }
            bool published = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return;
                if (active_ == cancel && !cancel->superseded()) {
                    if (request.kind == Request::Kind::Search) {
                        result_ = {request.revision, std::move(destinations)};
                        has_result_ = true;
                    } else {
                        route_result_ = {request.revision, std::move(routes)};
                        has_route_result_ = true;
                    }
                    published = true;
                }
                if (active_ == cancel) active_.reset();
            }
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
    std::shared_ptr<Cancellation> active_;
    bool has_pending_ = false;
    bool has_result_ = false;
    bool has_route_result_ = false;
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
                                    ui::Destination destination)
{
    impl_->submit_route(revision, std::move(destination));
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

void NavigationWorker::stop() noexcept { impl_->stop(); }

} // namespace navicore_osmand
