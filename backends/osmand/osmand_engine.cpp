#include "engine.h"
#include "session.h"
#include "diagnostics.h"
#include "frame_metrics.h"
#include "guidance_tracker.h"
#include "navigation_worker.h"
#include "ui/gl_overlay_compositor.h"
#include "ui/navigation_model.h"
#include "ui/navigation_view.h"

#include <OsmAndCore.h>
#include <OsmAndCore/CoreResourcesEmbeddedBundle.h>
#include <OsmAndCore/LatLon.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Utilities.h>
#include <OsmAndCore/Map/AtlasMapRendererConfiguration.h>
#include <OsmAndCore/Map/IMapRenderer.h>
#include <OsmAndCore/Map/MapObjectsSymbolsProvider.h>
#include <OsmAndCore/Map/MapPresentationEnvironment.h>
#include <OsmAndCore/Map/MapPrimitivesProvider.h>
#include <OsmAndCore/Map/MapPrimitiviser.h>
#include <OsmAndCore/Map/MapRasterLayerProvider_Software.h>
#include <OsmAndCore/Map/MapStylesCollection.h>
#include <OsmAndCore/Map/ObfMapObjectsProvider.h>
#include <OsmAndCore/Map/VectorLineBuilder.h>
#include <OsmAndCore/Map/VectorLinesCollection.h>

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace navicore_osmand {
namespace {

enum class Phase { Empty, Resources, CoreEntering, CoreReady,
                   RenderingEntering, Ready, Quarantined };

class OsmAndEngine final : public Engine {
public:
    bool initialize(const Config&, const MockNavBackendRenderTargetV1&,
                    std::shared_ptr<Wakeup>) override;
    FrameResult frame(const MockNavBackendRenderTargetV1&) override;
    int gps(const MockNavBackendGpsV1&) override;
    int pointer(const MockNavBackendPointerV1&) override;
    ReleaseResult release(ReleaseMode) override;

private:
    void resize(const MockNavBackendRenderTargetV1&);
    void show_route(const ui::RouteAlternative&);
    bool update_guidance();
    bool ensure_navigation_worker();
    Phase phase_ = Phase::Empty;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources_;
    std::shared_ptr<OsmAnd::MapStylesCollection> styles_;
    std::shared_ptr<OsmAnd::ObfsCollection> obfs_;
    std::shared_ptr<OsmAnd::MapPresentationEnvironment> presentation_;
    std::shared_ptr<OsmAnd::MapPrimitiviser> primitiviser_;
    std::shared_ptr<OsmAnd::ObfMapObjectsProvider> map_objects_;
    std::shared_ptr<OsmAnd::MapPrimitivesProvider> primitives_;
    std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider> symbols_;
    std::shared_ptr<OsmAnd::MapRasterLayerProvider_Software> raster_;
    std::shared_ptr<OsmAnd::IMapRenderer> renderer_;
    std::shared_ptr<OsmAnd::VectorLinesCollection> route_lines_;
    std::unique_ptr<ui::NavigationModel> navigation_;
    std::unique_ptr<ui::NavigationView> view_;
    std::unique_ptr<NavigationWorker> navigation_worker_;
    std::shared_ptr<Wakeup> navigation_wakeup_;
    std::string navigation_map_;
    std::string navigation_routing_xml_;
    bool worker_creation_failed_ = false;
    std::uint64_t submitted_search_revision_ = 0;
    std::uint64_t observed_search_revision_ = 0;
    std::uint64_t submitted_route_revision_ = 0;
    std::chrono::steady_clock::time_point search_revision_observed_at_{};
    bool search_revision_observed_ = false;
    GuidanceTracker guidance_tracker_;
    MockNavBackendGpsV1 latest_gps_{};
    bool has_valid_position_ = false;
    bool gps_state_observed_ = false;
    bool last_logged_gps_valid_ = false;
    bool gps_dirty_ = false;
    bool guidance_active_ = false;
    bool simulation_enabled_ = false;
    bool simulation_running_ = false;
    bool simulation_arrival_reported_ = false;
    double simulation_progress_metres_ = 0.0;
    double simulation_geometry_length_metres_ = 0.0;
    double simulation_speed_metres_per_second_ = 20.0;
    std::chrono::steady_clock::time_point simulation_updated_at_{};
    ui::GlOverlayCompositor compositor_;
    bool overlay_enabled_ = true;
    FrameMetrics frame_metrics_{false};
};

bool failure(const char* message)
{
    trace(message);
    return false;
}

bool valid_coordinates(const MockNavBackendGpsV1& gps)
{
    constexpr int32_t latitude_limit = 900000000;
    constexpr int32_t longitude_limit = 1800000000;
    return gps.latitude_e7 >= -latitude_limit &&
           gps.latitude_e7 <= latitude_limit &&
           gps.longitude_e7 >= -longitude_limit &&
           gps.longitude_e7 <= longitude_limit &&
           (gps.latitude_e7 != 0 || gps.longitude_e7 != 0);
}

double geometry_distance(const ui::GeoPoint& a, const ui::GeoPoint& b)
{
    constexpr double metres_per_degree = 111320.0;
    constexpr double radians_per_degree =
        3.14159265358979323846 / 180.0;
    const double latitude = (a.latitude + b.latitude) * 0.5 *
                            radians_per_degree;
    const double x = (b.longitude - a.longitude) * metres_per_degree *
                     std::cos(latitude);
    const double y = (b.latitude - a.latitude) * metres_per_degree;
    return std::hypot(x, y);
}

ui::GeoPoint simulated_position(const ui::RouteAlternative& route,
                                double progress_metres,
                                double geometry_length)
{
    if (route.geometry.empty())
        return {};
    if (route.geometry.size() == 1 || route.distance_metres <= 0.0)
        return route.geometry.front();
    if (geometry_length <= 0.0)
        return route.geometry.front();
    const double wanted = std::clamp(
        progress_metres / route.distance_metres, 0.0, 1.0) * geometry_length;
    double traversed = 0.0;
    for (std::size_t index = 1; index < route.geometry.size(); ++index) {
        const auto& a = route.geometry[index - 1];
        const auto& b = route.geometry[index];
        const double segment = geometry_distance(a, b);
        if (traversed + segment >= wanted && segment > 0.0) {
            const double fraction = (wanted - traversed) / segment;
            return {a.latitude + (b.latitude - a.latitude) * fraction,
                    a.longitude + (b.longitude - a.longitude) * fraction};
        }
        traversed += segment;
    }
    return route.geometry.back();
}

bool seeded_destinations(const std::string& query,
                         std::vector<ui::Destination>& output)
{
    if (query != "kyiv" && query != "Kyiv" && query != "KYIV")
        return false;
    output = {
        {"kyiv-lavra", "Kyiv Pechersk Lavra", "Historic landmark",
         "3.2 km", 50.4346, 30.5573},
        {"kyiv-station", "Kyiv Railway Station", "Transport",
         "2.8 km", 50.4413, 30.4898},
        {"kyiv-podil", "Podil", "District",
         "2.0 km", 50.4660, 30.5169},
    };
    return true;
}

bool OsmAndEngine::initialize(const Config& config,
                             const MockNavBackendRenderTargetV1& target,
                             std::shared_ptr<Wakeup> wakeup)
{
    simulation_enabled_ = config.simulate_route;
    frame_metrics_ = FrameMetrics(config.performance_metrics);
    OsmAnd::enablePerformanceMetrics(config.detailed_tile_metrics);
    if (config.performance_metrics)
        trace("performance_metrics=enabled");
    if (config.detailed_tile_metrics)
        trace("detailed_tile_metrics=enabled");
    simulation_speed_metres_per_second_ =
        config.simulation_speed_metres_per_second;
    if (simulation_enabled_)
        trace("simulation=enabled");
    resources_ = OsmAnd::CoreResourcesEmbeddedBundle::loadFromLibrary(
        QString::fromLocal8Bit(config.resources.c_str()));
    if (!resources_)
        return failure("error=resources");
    phase_ = Phase::Resources;
    trace("init=resources");

    // InitializeCore starts threads/mutates globals before its SKIA/ICU
    // failure returns. ReleaseCore has no partial-initialization contract.
    phase_ = Phase::CoreEntering;
    if (OsmAnd::InitializeCore(resources_, config.fonts.c_str()) == 0)
        return failure("error=initialize_core_partial");
    phase_ = Phase::CoreReady;
    trace("init=core");

    bool resource_ok = false;
    const QByteArray default_style = resources_->getResource(
        QStringLiteral("map/styles/default.render.xml"), &resource_ok);
    styles_ = std::make_shared<OsmAnd::MapStylesCollection>();
    if (!resource_ok || default_style.isEmpty() ||
        !styles_->addStyleFromByteArray(default_style,
                                       QStringLiteral("default.render.xml")))
        return failure("error=default_style");
    const auto style = styles_->getResolvedStyleByName(QStringLiteral("default"));
    if (!style)
        return failure("error=resolve_style");
    trace("init=style");

    obfs_ = std::make_shared<OsmAnd::ObfsCollection>();
    obfs_->addFile(QString::fromLocal8Bit(config.map.c_str()));
    if (obfs_->getObfFiles().isEmpty())
        return failure("error=map");
    trace("init=map");

    presentation_ = std::make_shared<OsmAnd::MapPresentationEnvironment>(
        style, 1.0f, 1.0f, 1.0f);
    presentation_->setLocaleLanguageId(QStringLiteral("uk"));
    primitiviser_ = std::make_shared<OsmAnd::MapPrimitiviser>(presentation_);
    map_objects_ = std::make_shared<OsmAnd::ObfMapObjectsProvider>(obfs_);
    primitives_ = std::make_shared<OsmAnd::MapPrimitivesProvider>(
        map_objects_, primitiviser_, 256u);
    symbols_ = std::make_shared<OsmAnd::MapObjectsSymbolsProvider>(primitives_, 256u);
    raster_ = std::make_shared<OsmAnd::MapRasterLayerProvider_Software>(primitives_);

    renderer_ = OsmAnd::createMapRenderer(
        OsmAnd::MapRendererClass::AtlasMapRenderer_OpenGLES2plus);
    if (!renderer_)
        return failure("error=create_renderer");
    OsmAnd::MapRendererSetupOptions setup;
    setup.gpuWorkerThreadEnabled = false;
    setup.frameUpdateRequestCallback = [wakeup](const OsmAnd::IMapRenderer*) noexcept {
        (void)wakeup->request();
    };
    if (!renderer_->setup(setup))
        return failure("error=setup_renderer");
    trace("init=renderer_setup");

    const auto configuration =
        std::static_pointer_cast<OsmAnd::AtlasMapRendererConfiguration>(
            renderer_->getConfiguration());
    configuration->referenceTileSizeOnScreenInPixels = 256u;
    // Keep the proven setup order: providers and camera precede GL init.
    renderer_->setConfiguration(configuration);
    resize(target);
    renderer_->setTarget(OsmAnd::Utilities::convertLatLonTo31(
        OsmAnd::LatLon(50.4501, 30.5234)));
    renderer_->setZoom(13.5f);
    renderer_->setAzimuth(0.0f);
    renderer_->setElevationAngle(90.0f);
    route_lines_ = std::make_shared<OsmAnd::VectorLinesCollection>();
    renderer_->addSymbolsProvider(symbols_);
    renderer_->addSymbolsProvider(route_lines_);
    renderer_->setMapLayerProvider(0, raster_);

    // GPU API/resources/stages may initialize before _isRenderingInitialized
    // becomes true. releaseRendering refuses to clean that partial state.
    phase_ = Phase::RenderingEntering;
    if (!renderer_->initializeRendering(true))
        return failure("error=initialize_rendering_partial");
    // Rendering now has a valid teardown contract. Mark it before allocations
    // below so an LVGL construction exception can still release OsmAnd.
    phase_ = Phase::Ready;
    trace("init=rendering");
    navigation_ = std::unique_ptr<ui::NavigationModel>(new ui::NavigationModel());
    view_ = std::unique_ptr<ui::NavigationView>(
        new ui::NavigationView(*navigation_, target.width, target.height));
    trace("init=view");
    const QByteArray routing_xml = resources_->getResource(
        QStringLiteral("routing/routing.xml"), &resource_ok);
    if (!resource_ok || routing_xml.isEmpty())
        return failure("error=routing_xml");
    trace("init=routing_resource");
    navigation_wakeup_ = std::move(wakeup);
    navigation_map_ = config.map;
    navigation_routing_xml_.assign(
        routing_xml.constData(), static_cast<std::size_t>(routing_xml.size()));
    trace("init=worker_deferred");
    return true;
}

void OsmAndEngine::resize(const MockNavBackendRenderTargetV1& target)
{
    renderer_->setWindowSize(OsmAnd::PointI(
        static_cast<int32_t>(target.width), static_cast<int32_t>(target.height)));
    renderer_->setViewport(OsmAnd::AreaI(
        0, 0, static_cast<int32_t>(target.height), static_cast<int32_t>(target.width)));
    width_ = target.width;
    height_ = target.height;
}

void OsmAndEngine::show_route(const ui::RouteAlternative& route)
{
    route_lines_->removeAllLines();
    QVector<OsmAnd::PointI> points;
    points.reserve(static_cast<int>(route.geometry.size()));
    double min_latitude = 90.0;
    double max_latitude = -90.0;
    double min_longitude = 180.0;
    double max_longitude = -180.0;
    simulation_geometry_length_metres_ = 0.0;
    for (const auto& point : route.geometry) {
        points.push_back(OsmAnd::Utilities::convertLatLonTo31(
            OsmAnd::LatLon(point.latitude, point.longitude)));
        min_latitude = std::min(min_latitude, point.latitude);
        max_latitude = std::max(max_latitude, point.latitude);
        min_longitude = std::min(min_longitude, point.longitude);
        max_longitude = std::max(max_longitude, point.longitude);
    }
    for (std::size_t index = 1; index < route.geometry.size(); ++index) {
        simulation_geometry_length_metres_ += geometry_distance(
            route.geometry[index - 1], route.geometry[index]);
    }
    if (points.size() < 2) return;
    OsmAnd::VectorLineBuilder()
        .setLineId(1)
        .setBaseOrder(-100000)
        .setLineWidth(10.0)
        .setOutlineWidth(3.0)
        .setOutlineColor(OsmAnd::FColorARGB(1.0f, 0.04f, 0.10f, 0.18f))
        .setFillColor(OsmAnd::FColorARGB(1.0f, 0.10f, 0.55f, 1.0f))
        .setPoints(points)
        .buildAndAddToCollection(route_lines_);
    renderer_->setTarget(OsmAnd::Utilities::convertLatLonTo31(OsmAnd::LatLon(
        (min_latitude + max_latitude) * 0.5,
        (min_longitude + max_longitude) * 0.5)));
    renderer_->setZoom(12.5f);
    guidance_tracker_.reset();
    guidance_active_ = false;
    simulation_running_ = false;
    simulation_progress_metres_ = 0.0;
    simulation_arrival_reported_ = false;
}

bool OsmAndEngine::update_guidance()
{
    if (navigation_->screen() != ui::Screen::Guidance ||
        navigation_->selected_route() >= navigation_->routes().size())
        return false;
    const auto& route = navigation_->routes()[navigation_->selected_route()];
    ui::GuidanceState state;
    ui::GeoPoint position;
    bool position_available = false;
    bool simulated = false;
    if (has_valid_position_) {
        position = {
            static_cast<double>(latest_gps_.latitude_e7) / 10000000.0,
            static_cast<double>(latest_gps_.longitude_e7) / 10000000.0};
        position_available = true;
        simulation_running_ = false;
    } else if (simulation_enabled_ && route.geometry.size() >= 2) {
        const auto now = std::chrono::steady_clock::now();
        if (!simulation_running_) {
            simulation_running_ = true;
            simulation_updated_at_ = now;
            simulation_progress_metres_ = 0.0;
            simulation_arrival_reported_ = false;
            trace("simulation=route_started");
        } else {
            const double seconds = std::chrono::duration<double>(
                now - simulation_updated_at_).count();
            simulation_progress_metres_ = std::min(
                route.distance_metres,
                simulation_progress_metres_ +
                    seconds * simulation_speed_metres_per_second_);
            simulation_updated_at_ = now;
        }
        position = simulated_position(route, simulation_progress_metres_,
                                      simulation_geometry_length_metres_);
        position_available = true;
        simulated = true;
    }
    if (position_available) {
        state = guidance_tracker_.update(route, {
            position.latitude, position.longitude});
        state.simulated = simulated;
        const auto location = OsmAnd::Utilities::convertLatLonTo31(OsmAnd::LatLon(
            state.position.latitude, state.position.longitude));
        const int32_t rail = width_ >= 600u ? 336 : 0;
        const int32_t top = height_ >= 700u ? 72 : 0;
        const int32_t bottom = height_ >= 700u ? 122 : 0;
        const int32_t map_x = rail + (static_cast<int32_t>(width_) - rail) / 2;
        const int32_t map_y = top +
            (static_cast<int32_t>(height_) - top - bottom) / 2;
        (void)renderer_->setMapTarget(OsmAnd::PointI(map_x, map_y), location);
        (void)renderer_->setZoom(16.0f);
        if (simulated && state.arrived && !simulation_arrival_reported_) {
            simulation_arrival_reported_ = true;
            trace("simulation=arrived");
        }
    } else {
        state = guidance_tracker_.unavailable(route);
    }
    gps_dirty_ = false;
    guidance_active_ = true;
    return view_->apply_guidance(std::move(state));
}

bool OsmAndEngine::ensure_navigation_worker()
{
    if (navigation_worker_)
        return true;
    if (worker_creation_failed_)
        return false;
    try {
        trace("worker=create");
        navigation_worker_ = std::unique_ptr<NavigationWorker>(
            new NavigationWorker(obfs_, navigation_wakeup_, navigation_map_,
                                 navigation_routing_xml_));
        trace("worker=ready");
        return true;
    } catch (...) {
        worker_creation_failed_ = true;
        trace("error=worker_creation_exception");
        return false;
    }
}

FrameResult OsmAndEngine::frame(const MockNavBackendRenderTargetV1& target)
{
    using Clock = std::chrono::steady_clock;
    const auto frame_started = Clock::now();
    FrameMeasurement measurement;
    const auto elapsed_us = [](Clock::time_point started) {
        const auto value = std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - started).count();
        return static_cast<std::uint32_t>(std::min<std::int64_t>(
            value, static_cast<std::int64_t>(UINT32_MAX)));
    };
    const auto finish = [&](FrameResult result,
                            MeasuredFrameOutcome outcome) -> FrameResult {
        measurement.outcome = outcome;
        measurement.total_us = elapsed_us(frame_started);
        frame_metrics_.record(measurement);
        return result;
    };
    if (phase_ != Phase::Ready)
        return finish({FrameOutcome::Fault}, MeasuredFrameOutcome::Fault);
    NavigationWorker::SearchResult search_result;
    if (navigation_worker_ && navigation_worker_->take_search_result(search_result))
    {
        trace_result("engine=search_result", search_result.revision,
                     search_result.destinations.size());
        (void)view_->apply_search_results(search_result.revision,
                                          std::move(search_result.destinations));
    }
    NavigationWorker::RouteResult route_result;
    if (navigation_worker_ && navigation_worker_->take_route_result(route_result)) {
        trace_result("engine=route_result", route_result.revision,
                     route_result.routes.size());
        const bool accepted = view_->apply_routes(
            route_result.revision, std::move(route_result.routes));
        if (accepted && !navigation_->routes().empty())
            show_route(navigation_->routes().front());
    }
    bool guidance_changed = false;
    if (navigation_->screen() == ui::Screen::Guidance &&
        (!guidance_active_ || gps_dirty_ ||
         (simulation_enabled_ && simulation_running_))) {
        guidance_changed = update_guidance();
    } else if (navigation_->screen() != ui::Screen::Guidance &&
               guidance_active_) {
        guidance_active_ = false;
        guidance_tracker_.reset();
        simulation_running_ = false;
        (void)renderer_->resetMapTarget();
    }
    if (target.width != width_ || target.height != height_)
        resize(target);
    auto stage_started = Clock::now();
    const bool update_ok = renderer_->update();
    measurement.update_us = elapsed_us(stage_started);
    if (!update_ok) {
        trace("error=update");
        return finish({FrameOutcome::Fault}, MeasuredFrameOutcome::Fault);
    }
    stage_started = Clock::now();
    const bool prepared = renderer_->prepareFrame();
    measurement.prepare_us = elapsed_us(stage_started);
    if (!prepared)
        return finish(
            {FrameOutcome::Pending, PendingStage::Prepare, !renderer_->isIdle()},
            MeasuredFrameOutcome::PreparePending);
    // The bool API collapses StageResult::Wait and ::Fail. Preserve the live
    // resource-wait behavior, but let Session report sustained lack of progress.
    stage_started = Clock::now();
    const bool map_rendered = renderer_->renderFrame();
    measurement.map_render_us = elapsed_us(stage_started);
    if (!map_rendered)
        return finish({FrameOutcome::Pending, PendingStage::Render, true},
                      MeasuredFrameOutcome::RenderPending);
    view_->resize(target.width, target.height);
    stage_started = Clock::now();
    const bool ui_dirty = overlay_enabled_ && view_->render(16u);
    measurement.ui_render_us = elapsed_us(stage_started);
    if (overlay_enabled_) {
        const auto dirty = view_->dirty_region();
        if (ui_dirty)
            measurement.ui_upload_bytes = dirty.width * dirty.height *
                                          sizeof(std::uint32_t);
        stage_started = Clock::now();
        const bool presented = compositor_.present(
            view_->pixels(), view_->width(), view_->height(),
            ui_dirty ? dirty.x : 0u, ui_dirty ? dirty.y : 0u,
            ui_dirty ? dirty.width : 0u, ui_dirty ? dirty.height : 0u);
        measurement.ui_composite_us = elapsed_us(stage_started);
        if (!presented) {
            trace("error=ui_composite");
            compositor_.release(false);
            overlay_enabled_ = false;
        }
    }
    // A Start/End click is dispatched by LVGL above. Apply the new camera and
    // guidance model now, then request the frame that presents that rebuild.
    if (navigation_->screen() == ui::Screen::Guidance && !guidance_active_)
        guidance_changed = update_guidance() || guidance_changed;
    else if (navigation_->screen() != ui::Screen::Guidance &&
             guidance_active_) {
        guidance_active_ = false;
        guidance_tracker_.reset();
        simulation_running_ = false;
        (void)renderer_->resetMapTarget();
        guidance_changed = true;
    }
    if (navigation_->screen() == ui::Screen::Search) {
        const auto revision = navigation_->search_revision();
        const auto now = std::chrono::steady_clock::now();
        if (!search_revision_observed_ || revision != observed_search_revision_) {
            observed_search_revision_ = revision;
            search_revision_observed_at_ = now;
            search_revision_observed_ = true;
        } else if (revision != submitted_search_revision_ &&
                   now - search_revision_observed_at_ >=
                       std::chrono::milliseconds(500)) {
            submitted_search_revision_ = revision;
            if (!navigation_->query().empty()) {
            trace_result("engine=search_submit", submitted_search_revision_,
                         navigation_->query().size());
                std::vector<ui::Destination> seeded;
                if (seeded_destinations(navigation_->query(), seeded)) {
                    trace_result("engine=seeded_results",
                                 submitted_search_revision_, seeded.size());
                    (void)view_->apply_search_results(
                        submitted_search_revision_, std::move(seeded));
                } else if (ensure_navigation_worker()) {
                navigation_worker_->submit_search(submitted_search_revision_,
                                                   navigation_->query());
                }
            }
        }
    } else {
        search_revision_observed_ = false;
    }
    if (navigation_->screen() == ui::Screen::Search &&
        navigation_->destination() &&
        navigation_->route_revision() != submitted_route_revision_) {
        submitted_route_revision_ = navigation_->route_revision();
        trace_result("engine=route_submit", submitted_route_revision_, 1u);
        if (ensure_navigation_worker()) {
            ui::GeoPoint origin{50.4501, 30.5234};
            if (has_valid_position_) {
                origin.latitude = static_cast<double>(latest_gps_.latitude_e7) /
                                  10000000.0;
                origin.longitude = static_cast<double>(latest_gps_.longitude_e7) /
                                   10000000.0;
            }
            navigation_worker_->submit_route(submitted_route_revision_,
                                              *navigation_->destination(), origin);
        }
    }
    return finish({FrameOutcome::Presented, PendingStage::None,
                   guidance_changed || !renderer_->isIdle()},
                  MeasuredFrameOutcome::Presented);
}

int OsmAndEngine::gps(const MockNavBackendGpsV1& event)
{
    latest_gps_ = event;
    has_valid_position_ = valid_coordinates(event);
    gps_dirty_ = true;
    if (!gps_state_observed_ || has_valid_position_ != last_logged_gps_valid_) {
        trace(has_valid_position_ ? "gps=position_received"
                                  : "gps=position_unavailable");
        gps_state_observed_ = true;
        last_logged_gps_valid_ = has_valid_position_;
    }
    return navigation_wakeup_ ? navigation_wakeup_->request() : 0;
}

int OsmAndEngine::pointer(const MockNavBackendPointerV1& event)
{
    if (phase_ != Phase::Ready || !view_)
        return -1;
    trace_pointer("received", event.action, event.x, event.y);
    view_->pointer(static_cast<int>(event.action), event.x, event.y);
    return 0;
}

ReleaseResult OsmAndEngine::release(ReleaseMode mode)
{
    if (phase_ == Phase::CoreEntering || phase_ == Phase::RenderingEntering ||
        phase_ == Phase::Quarantined) {
        phase_ = Phase::Quarantined;
        return ReleaseResult::Quarantined;
    }
    if (phase_ == Phase::Ready) {
        // Set the failure state BEFORE invoking dependency code that may throw
        // or fail after tearing down only part of the resource graph.
        phase_ = Phase::Quarantined;
        if (navigation_worker_) navigation_worker_->stop();
        navigation_worker_.reset();
        navigation_wakeup_.reset();
        navigation_map_.clear();
        navigation_routing_xml_.clear();
        worker_creation_failed_ = false;
        latest_gps_ = {};
        has_valid_position_ = false;
        gps_state_observed_ = false;
        last_logged_gps_valid_ = false;
        gps_dirty_ = false;
        guidance_active_ = false;
        simulation_enabled_ = false;
        simulation_running_ = false;
        simulation_progress_metres_ = 0.0;
        simulation_geometry_length_metres_ = 0.0;
        simulation_arrival_reported_ = false;
        guidance_tracker_.reset();
        compositor_.release(mode == ReleaseMode::ContextLost);
        view_.reset();
        navigation_.reset();
        overlay_enabled_ = true;
        submitted_search_revision_ = 0;
        observed_search_revision_ = 0;
        submitted_route_revision_ = 0;
        search_revision_observed_ = false;
        if (!renderer_->releaseRendering(mode == ReleaseMode::ContextLost))
            return ReleaseResult::Quarantined;
        phase_ = Phase::CoreReady;
    }
    renderer_.reset();
    raster_.reset();
    symbols_.reset();
    route_lines_.reset();
    primitives_.reset();
    map_objects_.reset();
    primitiviser_.reset();
    presentation_.reset();
    obfs_.reset();
    styles_.reset();
    if (phase_ == Phase::CoreReady) {
        phase_ = Phase::Quarantined;
        OsmAnd::ReleaseCore();
    }
    resources_.reset();
    phase_ = Phase::Empty;
    return ReleaseResult::Released;
}

} // namespace

std::unique_ptr<Engine> make_engine()
{
    return std::unique_ptr<Engine>(new OsmAndEngine());
}
} // namespace navicore_osmand
