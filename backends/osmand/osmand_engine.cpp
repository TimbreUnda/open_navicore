#include "engine.h"
#include "baked_symbols_raster_provider.h"
#include "camera_controller.h"
#include "crash_reporter.h"
#include "caching_raster_provider.h"
#include "precache_worker.h"
#include "tile_cache.h"
#include "bounded_symbols_provider.h"
#include "session.h"
#include "diagnostics.h"
#include "frame_metrics.h"
#include "gpu_upload_context.h"
#include "guidance_tracker.h"
#include "navigation_worker.h"
#include "retained_symbol_layer.h"
#include "ui/gl_overlay_compositor.h"
#include "ui/navigation_model.h"
#include "ui/navigation_view.h"
#include "ui/recents_store.h"

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
#include <QHash>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <dlfcn.h>
#include <sys/stat.h>

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
    void update_render_shootout();
    bool ensure_navigation_worker();
    void advance_benchmark_autostart();
    void apply_style_settings();
    QHash<QString, QString> style_settings() const;
    std::shared_ptr<TileCache> make_tile_cache() const;
    void start_precache(const Config& config);
    // Drains UI map commands, advances the car camera and applies the pose to
    // the renderer. Returns true while the camera still needs frames.
    bool apply_camera();
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
    // Outlives OsmAnd's GPU worker thread; destroyed after releaseRendering().
    std::shared_ptr<GpuUploadContext> upload_context_;
    std::unique_ptr<RetainedSymbolLayer> retained_symbols_;
    bool retained_symbols_creation_attempted_ = false;
    std::unique_ptr<ui::NavigationModel> navigation_;
    std::unique_ptr<ui::NavigationView> view_;
    std::unique_ptr<NavigationWorker> navigation_worker_;
    std::unique_ptr<ui::RecentsStore> recents_store_;
    std::uint64_t saved_recents_revision_ = 0;
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
    CameraController camera_;
    MotionEstimator motion_;
    std::vector<ui::MapCommand> map_commands_;
    std::chrono::steady_clock::time_point camera_updated_at_{};
    std::chrono::steady_clock::time_point clock_origin_ =
        std::chrono::steady_clock::now();
    CameraController::Pose applied_pose_{};
    bool pose_applied_ = false;
    bool car_style_ = false;
    bool night_ = false;
    bool overview_pending_ = false;
    std::string production_attempt_;
    std::chrono::steady_clock::time_point production_started_at_{};
    std::shared_ptr<CachingRasterProvider> caching_raster_;
    std::shared_ptr<const OsmAnd::ResolvedMapStyle> style_;
    std::unique_ptr<PrecacheWorker> precache_;
    std::string cache_identity_;
    std::string tile_cache_runtime_;
    std::string tile_cache_base_;
    std::uint64_t tile_cache_budget_ = 0;
    bool rgb565_tiles_ = false;
    double overview_box_[4] = {0.0, 0.0, 0.0, 0.0};
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
    bool render_shootout_enabled_ = false;
    bool sustained_basemap_benchmark_ = false;
    bool baked_map_symbols_ = false;
    bool bounded_map_symbols_ = false;
    bool retained_map_symbols_ = false;
    bool render_shootout_active_ = false;
    // Bench-only: drive search -> route -> guidance without touch input.
    enum class Autostart { Off, Search, Select, Start, Done };
    Autostart autostart_ = Autostart::Off;
    bool render_shootout_complete_ = false;
    unsigned int render_shootout_phase_ = 0u;
    std::chrono::steady_clock::time_point render_shootout_started_at_{};
    ui::GlOverlayCompositor compositor_;
    ui::OverlayOccupancy overlay_occupancy_;
    std::vector<ui::UploadRect> upload_rects_;
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
    autostart_ = config.benchmark_autostart && config.simulate_route
                     ? Autostart::Search : Autostart::Off;
    if (autostart_ != Autostart::Off)
        trace("benchmark_autostart=enabled");
    sustained_basemap_benchmark_ = config.sustained_basemap_benchmark;
    baked_map_symbols_ = config.baked_map_symbols;
    bounded_map_symbols_ = config.bounded_map_symbols;
    retained_map_symbols_ = config.retained_map_symbols;
    render_shootout_enabled_ = config.render_shootout &&
                               !sustained_basemap_benchmark_;
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
    if (config.production_profile)
        trace("production_profile=enabled");
    if (config.production_profile_suppressed)
        trace("production_profile=suppressed_after_unstable_run");
    production_attempt_ = config.production_profile
        ? config.production_attempt : std::string();
    production_started_at_ = std::chrono::steady_clock::now();
    if (render_shootout_enabled_)
        trace("render_shootout=enabled");
    if (sustained_basemap_benchmark_)
        trace("sustained_basemap_benchmark=enabled");
    if (baked_map_symbols_)
        trace("baked_map_symbols=enabled");
    if (bounded_map_symbols_)
        trace("bounded_map_symbols=enabled groups_per_tile=12");
    if (retained_map_symbols_)
        trace("retained_map_symbols=enabled atlas=512 symbols=32");
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
    install_crash_reporter("/tmp/open-navicore-crash.log");
    // Load bases let a slog fault 'ip' be mapped to a library and symbol.
    {
        const auto report = [](const char* name, const void* address) {
            Dl_info info{};
            if (dladdr(const_cast<void*>(address), &info) == 0 ||
                !info.dli_fbase)
                return;
            char message[256] = {};
            (void)std::snprintf(message, sizeof(message),
                                "load_base %s=%p %s", name, info.dli_fbase,
                                info.dli_fname ? info.dli_fname : "?");
            trace(message);
        };
        report("backend", reinterpret_cast<const void*>(&make_engine));
        report("osmand_core",
               reinterpret_cast<const void*>(&OsmAnd::InitializeCore));
        report("qt_core", reinterpret_cast<const void*>(&qVersion));
    }

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
    style_ = style;

    obfs_ = std::make_shared<OsmAnd::ObfsCollection>();
    obfs_->addFile(QString::fromLocal8Bit(config.map.c_str()));
    if (obfs_->getObfFiles().isEmpty())
        return failure("error=map");
    trace("init=map");

    presentation_ = std::make_shared<OsmAnd::MapPresentationEnvironment>(
        style, 1.0f, 1.0f, 1.0f);
    presentation_->setLocaleLanguageId(QStringLiteral("uk"));
    car_style_ = config.car_style;
    apply_style_settings();
    if (car_style_)
        trace("car_style=enabled");
    primitiviser_ = std::make_shared<OsmAnd::MapPrimitiviser>(presentation_);
    map_objects_ = std::make_shared<OsmAnd::ObfMapObjectsProvider>(obfs_);
    primitives_ = std::make_shared<OsmAnd::MapPrimitivesProvider>(
        map_objects_, primitiviser_, 256u);
    if (bounded_map_symbols_)
        symbols_ = std::make_shared<BoundedSymbolsProvider>(primitives_, 12u);
    else
        symbols_ = std::make_shared<OsmAnd::MapObjectsSymbolsProvider>(
            primitives_, 256u);

    renderer_ = OsmAnd::createMapRenderer(
        OsmAnd::MapRendererClass::AtlasMapRenderer_OpenGLES2plus);
    if (!renderer_)
        return failure("error=create_renderer");
    if (baked_map_symbols_) {
        raster_ = std::make_shared<BakedSymbolsRasterProvider>(
            primitives_, symbols_, [this]() {
                return renderer_->getMapState();
            });
    } else if (config.tile_cache) {
        rgb565_tiles_ = config.rgb565_tiles;
        tile_cache_runtime_ = config.tile_cache_runtime;
        tile_cache_base_ = config.tile_cache_base;
        tile_cache_budget_ =
            static_cast<std::uint64_t>(config.tile_cache_megabytes) << 20;
        struct stat map_info {};
        const bool map_known = ::stat(config.map.c_str(), &map_info) == 0;
        // Anything that changes rasterized pixels must change this string.
        cache_identity_ = config.map + "|" +
            std::to_string(map_known ? map_info.st_size : 0) + "|" +
            std::to_string(map_known ? map_info.st_mtime : 0) +
            "|default|uk|256|1.0|cache-v1";
        caching_raster_ = std::make_shared<CachingRasterProvider>(primitives_);
        caching_raster_->set_cache(make_tile_cache());
        raster_ = caching_raster_;
        trace("tile_cache=enabled");
    } else {
        raster_ = std::make_shared<OsmAnd::MapRasterLayerProvider_Software>(
            primitives_);
    }

    OsmAnd::MapRendererSetupOptions setup;
    setup.gpuWorkerThreadEnabled = false;
    // Without a worker OsmAnd uploads at most one resource per frame on the
    // render thread, which delays cold tiles. The worker needs its own context
    // sharing objects with the provider's; fall back if one cannot be bound.
    if (config.gpu_upload_worker) {
        auto upload = std::make_shared<GpuUploadContext>();
        if (upload->create(
                reinterpret_cast<EGLDisplay>(target.native_egl_display),
                reinterpret_cast<EGLContext>(target.native_egl_context))) {
            upload_context_ = upload;
            setup.gpuWorkerThreadEnabled = true;
            setup.gpuWorkerThreadPrologue =
                [upload](const OsmAnd::IMapRenderer*) noexcept {
                    if (!upload->make_current())
                        trace("error=gpu_upload_context_bind");
                };
            setup.gpuWorkerThreadEpilogue =
                [upload](const OsmAnd::IMapRenderer*) noexcept {
                    upload->release_current();
                };
            trace("gpu_upload_worker=enabled");
        } else {
            char message[64] = {};
            (void)std::snprintf(message, sizeof(message),
                                "gpu_upload_worker=fallback egl_error=0x%04x",
                                static_cast<unsigned int>(upload->error()));
            trace(message);
        }
    }
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
    // RGB565 halves tile upload size and texture bandwidth on the shared bus.
    configuration->limitTextureColorDepthBy16bits = config.rgb565_tiles;
    if (config.rgb565_tiles)
        trace("rgb565_tiles=enabled");
    // Keep the proven setup order: providers and camera precede GL init.
    renderer_->setConfiguration(configuration);
    resize(target);
    renderer_->setTarget(OsmAnd::Utilities::convertLatLonTo31(
        OsmAnd::LatLon(50.4501, 30.5234)));
    renderer_->setZoom(13.5f);
    renderer_->setAzimuth(0.0f);
    renderer_->setElevationAngle(90.0f);
    route_lines_ = std::make_shared<OsmAnd::VectorLinesCollection>();
    if ((!sustained_basemap_benchmark_ || bounded_map_symbols_) &&
        !retained_map_symbols_ &&
        !baked_map_symbols_)
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
    recents_store_.reset(new ui::RecentsStore(config.recents));
    navigation_->set_recents(recents_store_->load());
    saved_recents_revision_ = navigation_->recents_revision();
    view_ = std::unique_ptr<ui::NavigationView>(
        new ui::NavigationView(*navigation_, target.width, target.height));
    trace("init=view");
    const QByteArray routing_xml = resources_->getResource(
        QStringLiteral("routing/routing.xml"), &resource_ok);
    if (!resource_ok || routing_xml.isEmpty())
        return failure("error=routing_xml");
    trace("init=routing_resource");
    navigation_wakeup_ = std::move(wakeup);
    start_precache(config);
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
    overview_box_[0] = min_latitude;
    overview_box_[1] = min_longitude;
    overview_box_[2] = max_latitude;
    overview_box_[3] = max_longitude;
    overview_pending_ = true;
    guidance_tracker_.reset();
    guidance_active_ = false;
    simulation_running_ = false;
    simulation_progress_metres_ = 0.0;
    simulation_arrival_reported_ = false;
    render_shootout_active_ = false;
    render_shootout_complete_ = false;
    render_shootout_phase_ = 0u;
}

void OsmAndEngine::update_render_shootout()
{
    if (!render_shootout_enabled_ || render_shootout_complete_ ||
        !simulation_running_)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (!render_shootout_active_) {
        render_shootout_active_ = true;
        render_shootout_started_at_ = now;
        render_shootout_phase_ = 0u;
        trace("render_shootout=full_map");
        return;
    }
    constexpr auto phase_duration = std::chrono::seconds(15);
    const auto desired = static_cast<unsigned int>(
        (now - render_shootout_started_at_) / phase_duration);
    if (desired <= render_shootout_phase_)
        return;

    if (desired == 1u) {
        if (!renderer_->removeSymbolsProvider(symbols_, true))
            trace("error=render_shootout_remove_symbols");
        trace("render_shootout=raster_without_symbols");
    } else if (desired == 2u) {
        if (!renderer_->resetMapLayerProvider(0, true))
            trace("error=render_shootout_remove_raster");
        trace("render_shootout=route_only_gpu_floor");
    } else {
        if (!renderer_->setMapLayerProvider(0, raster_, true))
            trace("error=render_shootout_restore_raster");
        if (!renderer_->addSymbolsProvider(symbols_, true))
            trace("error=render_shootout_restore_symbols");
        trace("render_shootout=complete_full_map_restored");
        render_shootout_complete_ = true;
        render_shootout_active_ = false;
        render_shootout_phase_ = 3u;
        return;
    }

    render_shootout_phase_ = desired;
    simulation_progress_metres_ = 0.0;
    simulation_updated_at_ = now;
    simulation_arrival_reported_ = false;
    guidance_tracker_.reset();
    gps_dirty_ = true;
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
        // The route-snapped position drives the camera and vehicle marker.
        motion_.observe(state.position.latitude, state.position.longitude,
                        std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - clock_origin_)
                            .count());
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

void OsmAndEngine::apply_style_settings()
{
    presentation_->setSettings(style_settings());
}

QHash<QString, QString> OsmAndEngine::style_settings() const
{
    QHash<QString, QString> settings;
    if (car_style_) {
        // OsmAnd's own car profile plus switches for detail a driver does not
        // use; fewer primitives means less rasterization and fewer symbols.
        settings = {
            {QStringLiteral("appMode"), QStringLiteral("car")},
            {QStringLiteral("baseAppMode"), QStringLiteral("car")},
            {QStringLiteral("hideHouseNumbers"), QStringLiteral("true")},
            {QStringLiteral("hideUnderground"), QStringLiteral("true")},
            {QStringLiteral("hideMilitaryAreas"), QStringLiteral("true")},
            {QStringLiteral("noAdminboundaries"), QStringLiteral("true")},
            {QStringLiteral("transportStops"), QStringLiteral("false")},
            {QStringLiteral("streetLighting"), QStringLiteral("false")},
            {QStringLiteral("natureReserves"), QStringLiteral("false")},
            {QStringLiteral("showProposed"), QStringLiteral("false")},
            // POI captions (churches, libraries, shops) are noise while
            // driving; icons stay so fuel/charging/parking remain visible.
            {QStringLiteral("hidePOILabels"), QStringLiteral("true")},
        };
    }
    settings.insert(QStringLiteral("nightMode"),
                    night_ ? QStringLiteral("true") : QStringLiteral("false"));
    return settings;
}

std::shared_ptr<TileCache> OsmAndEngine::make_tile_cache() const
{
    if (cache_identity_.empty())
        return nullptr;
    const auto format = rgb565_tiles_ ? TileFormat::Rgb565
                                      : TileFormat::Rgba8888;
    std::string description = cache_identity_;
    description += car_style_ ? "|car" : "|default";
    description += night_ ? "|night" : "|day";
    description += rgb565_tiles_ ? "|565" : "|8888";
    return std::make_shared<TileCache>(
        tile_cache_base_, tile_cache_runtime_,
        TileCache::namespace_hash(description), format, 256u,
        tile_cache_budget_);
}

void OsmAndEngine::start_precache(const Config& config)
{
    if (cache_identity_.empty())
        return;
    std::FILE* file = std::fopen(config.precache_manifest.c_str(), "r");
    if (!file)
        return;
    std::string text;
    char buffer[4096];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        text.append(buffer, got);
    std::fclose(file);
    std::size_t errors = 0;
    auto ranges = parse_precache_manifest(text, &errors);
    if (errors)
        trace_result("precache=manifest_errors", errors, ranges.size());
    if (ranges.empty())
        return;
    // The shipped base is written for the default (day) style namespace.
    std::string description = cache_identity_;
    description += car_style_ ? "|car|day" : "|default|day";
    description += rgb565_tiles_ ? "|565" : "|8888";
    PrecacheWorker::Job job;
    job.manifest_path = config.precache_manifest;
    job.ranges = std::move(ranges);
    job.obfs = obfs_;
    job.style = style_;
    job.locale = QStringLiteral("uk");
    // Called from initialize(), before any night toggle: day settings.
    job.style_settings = style_settings();
    job.store.root = tile_cache_base_ + "/" +
                     TileCache::namespace_hash(description);
    job.store.writable = true;
    job.store.format = rgb565_tiles_ ? TileFormat::Rgb565 : TileFormat::Rgba8888;
    job.store.tile_size = 256u;
    job.store.compression_level = 9;
    try {
        precache_.reset(new PrecacheWorker(std::move(job)));
        trace("precache=worker_started");
    } catch (...) {
        trace("error=precache_thread_start");
    }
}

bool OsmAndEngine::apply_camera()
{
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const double dt = camera_updated_at_ == Clock::time_point{} ? 0.0 :
        std::chrono::duration<double>(now - camera_updated_at_).count();
    camera_updated_at_ = now;
    const double now_s =
        std::chrono::duration<double>(now - clock_origin_).count();
    const auto screen = navigation_->screen();
    if (screen == ui::Screen::Search)
        return false;

    const auto area = view_->map_area();
    camera_.set_viewport({area.x, area.y, area.width, area.height});

    // Outside guidance, raw GPS fixes drive free-drive following.
    if (screen != ui::Screen::Guidance && has_valid_position_ && gps_dirty_) {
        motion_.observe(static_cast<double>(latest_gps_.latitude_e7) / 1e7,
                        static_cast<double>(latest_gps_.longitude_e7) / 1e7,
                        now_s);
        gps_dirty_ = false;
    }
    if (overview_pending_) {
        camera_.overview(overview_box_[0], overview_box_[1],
                         overview_box_[2], overview_box_[3]);
        overview_pending_ = false;
    }

    navigation_->take_map_commands(map_commands_);
    for (const auto command : map_commands_) {
        switch (command) {
        case ui::MapCommand::ZoomIn: camera_.zoom_by(1.0f); break;
        case ui::MapCommand::ZoomOut: camera_.zoom_by(-1.0f); break;
        case ui::MapCommand::Recenter: camera_.recenter(); break;
        case ui::MapCommand::ToggleOrientation:
            camera_.toggle_orientation();
            break;
        case ui::MapCommand::ToggleTilt:
            camera_.set_tilt(!camera_.tilted());
            break;
        case ui::MapCommand::ToggleNight:
            night_ = !night_;
            apply_style_settings();
            if (caching_raster_)
                caching_raster_->set_cache(make_tile_cache());
            if (retained_symbols_) {
                retained_symbols_->stop();
                retained_symbols_->release(false);
                retained_symbols_.reset();
                retained_symbols_creation_attempted_ = false;
            }
            // Every cached tile was rasterized for the old palette.
            renderer_->reloadEverything();
            trace(night_ ? "style=night" : "style=day");
            break;
        case ui::MapCommand::Overview: {
            const auto& routes = navigation_->routes();
            const auto index = navigation_->selected_route();
            if (index < routes.size() && !routes[index].geometry.empty()) {
                double box[4] = {90.0, 180.0, -90.0, -180.0};
                for (const auto& point : routes[index].geometry) {
                    box[0] = std::min(box[0], point.latitude);
                    box[1] = std::min(box[1], point.longitude);
                    box[2] = std::max(box[2], point.latitude);
                    box[3] = std::max(box[3], point.longitude);
                }
                camera_.overview(box[0], box[1], box[2], box[3]);
            }
            break;
        }
        }
    }
    int dx = 0;
    int dy = 0;
    if (navigation_->take_pan(dx, dy))
        camera_.pan(dx, dy);

    const auto vehicle = motion_.predict(now_s);
    const auto pose = camera_.update(vehicle, dt);
    const bool changed = !pose_applied_ ||
        std::fabs(pose.latitude - applied_pose_.latitude) > 1e-8 ||
        std::fabs(pose.longitude - applied_pose_.longitude) > 1e-8 ||
        pose.anchor_x != applied_pose_.anchor_x ||
        pose.anchor_y != applied_pose_.anchor_y ||
        std::fabs(pose.zoom - applied_pose_.zoom) > 0.01f ||
        std::fabs(static_cast<float>(CameraController::shortest_angle_delta(
            applied_pose_.azimuth_degrees, pose.azimuth_degrees))) > 0.25f ||
        pose.elevation_degrees != applied_pose_.elevation_degrees;
    if (changed) {
        const auto location = OsmAnd::Utilities::convertLatLonTo31(
            OsmAnd::LatLon(pose.latitude, pose.longitude));
        (void)renderer_->setMapTarget(
            OsmAnd::PointI(pose.anchor_x, pose.anchor_y), location);
        (void)renderer_->setZoom(pose.zoom);
        (void)renderer_->setAzimuth(pose.azimuth_degrees);
        (void)renderer_->setElevationAngle(pose.elevation_degrees);
        applied_pose_ = pose;
        pose_applied_ = true;
    }

    // Vehicle marker: at the anchor while following, projected otherwise.
    if (vehicle.valid) {
        OsmAnd::PointI marker(pose.anchor_x, pose.anchor_y);
        bool visible = pose.following;
        if (!visible) {
            visible = renderer_->obtainScreenPointFromPosition(
                OsmAnd::Utilities::convertLatLonTo31(
                    OsmAnd::LatLon(vehicle.latitude, vehicle.longitude)),
                marker, true);
        }
        float rotation = static_cast<float>(CameraController::shortest_angle_delta(
            pose.azimuth_degrees, vehicle.heading_degrees));
        // Heading-up: the arrow points up; small residuals from azimuth
        // smoothing would otherwise redraw the rotated marker every frame.
        if (pose.following &&
            camera_.follow_mode() == CameraController::Mode::HeadingUp &&
            std::fabs(rotation) < 15.0f)
            rotation = 0.0f;
        view_->set_vehicle_marker(visible, marker.x, marker.y, rotation);
    } else {
        view_->set_vehicle_marker(false, 0, 0, 0.0f);
    }

    ui::MapViewState state;
    state.following = pose.following || !vehicle.valid;
    state.heading_up =
        camera_.follow_mode() == CameraController::Mode::HeadingUp;
    state.tilted = camera_.tilted();
    state.night = night_;
    if (navigation_->set_map_view(state))
        view_->apply_map_view();
    // Hysteresis so the typing lock does not flicker at walking speed.
    const bool moving = navigation_->moving()
        ? vehicle.valid && vehicle.speed_mps > 1.0
        : vehicle.valid && vehicle.speed_mps > 2.5;
    (void)view_->set_moving(moving);
    if (precache_)
        precache_->set_throttled(moving);

    return changed || (vehicle.valid && vehicle.speed_mps > 0.3);
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
        // Simulated motion ends with guidance; real GPS keeps following.
        if (!has_valid_position_) motion_.reset();
        pose_applied_ = false;
    }
    if (target.width != width_ || target.height != height_)
        resize(target);
    update_render_shootout();
    const bool camera_animating = apply_camera();
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
    if (retained_symbols_) {
        retained_symbols_->request(renderer_->getMapState());
        if (!retained_symbols_->present(*renderer_, target.width, target.height)) {
            trace("error=retained_symbols");
            retained_symbols_->release(false);
            retained_symbols_.reset();
        }
    }
    view_->resize(target.width, target.height);
    stage_started = Clock::now();
    const bool ui_dirty = overlay_enabled_ && view_->render(16u);
    measurement.ui_render_us = elapsed_us(stage_started);
    if (overlay_enabled_) {
        stage_started = Clock::now();
        upload_rects_.clear();
        if (ui_dirty) {
            for (const auto& dirty : view_->dirty_regions()) {
                measurement.ui_upload_bytes += dirty.width * dirty.height *
                                               sizeof(std::uint32_t);
                overlay_occupancy_.update(view_->pixels(), view_->width(),
                                          view_->height(), dirty.y,
                                          dirty.height);
                upload_rects_.push_back(
                    {dirty.x, dirty.y, dirty.width, dirty.height});
            }
        }
        const bool presented = compositor_.present(
            view_->pixels(), view_->width(), view_->height(), upload_rects_,
            overlay_occupancy_);
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
        // Simulated motion ends with guidance; real GPS keeps following.
        if (!has_valid_position_) motion_.reset();
        pose_applied_ = false;
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
    // OsmAnd's worker graph is still settling after initializeRendering().
    // Start our independent symbol worker only after an entire map/UI frame
    // has rendered successfully. The atlas will first appear on a later frame.
    if (retained_map_symbols_ && !retained_symbols_ &&
        !retained_symbols_creation_attempted_) {
        retained_symbols_creation_attempted_ = true;
        try {
            SymbolSource source;
            source.obfs = obfs_;
            source.style = style_;
            source.style_settings = style_settings();
            source.locale = QStringLiteral("uk");
            // Warm provider: same primitives cache as the raster tiles.
            // In retained mode the renderer itself never uses symbols_.
            source.shared_provider = symbols_;
            retained_symbols_ = std::make_unique<RetainedSymbolLayer>(
                std::move(source), navigation_wakeup_);
            trace("init=retained_symbols_after_first_frame");
        } catch (...) {
            retained_map_symbols_ = false;
            trace("error=retained_symbols_thread_start");
        }
    }
    advance_benchmark_autostart();
    // Crash guard: two minutes of presented frames clears the attempt.
    if (!production_attempt_.empty() &&
        std::chrono::steady_clock::now() - production_started_at_ >=
            std::chrono::minutes(2)) {
        std::remove(production_attempt_.c_str());
        production_attempt_.clear();
        trace("production_profile=stable");
    }
    // Recents change only on guidance start; save outside the hot path.
    if (recents_store_ &&
        navigation_->recents_revision() != saved_recents_revision_) {
        saved_recents_revision_ = navigation_->recents_revision();
        if (!recents_store_->save(navigation_->recents()))
            trace("error=recents_save");
    }
    return finish({FrameOutcome::Presented, PendingStage::None,
                   guidance_changed || camera_animating ||
                       !renderer_->isIdle() ||
                       (autostart_ != Autostart::Off &&
                        autostart_ != Autostart::Done)},
                  MeasuredFrameOutcome::Presented);
}

void OsmAndEngine::advance_benchmark_autostart()
{
    // Uses the same seeded search and route worker as touch input; each step
    // waits for the previous asynchronous result to land in the model.
    switch (autostart_) {
    case Autostart::Search:
        view_->open_search();
        (void)view_->set_query("kyiv");
        autostart_ = Autostart::Select;
        trace("benchmark_autostart=search");
        break;
    case Autostart::Select:
        if (navigation_->screen() == ui::Screen::Search &&
            !navigation_->results().empty() && !navigation_->destination()) {
            (void)view_->select_result(0u);
            autostart_ = Autostart::Start;
            trace("benchmark_autostart=select");
        }
        break;
    case Autostart::Start:
        if (navigation_->screen() == ui::Screen::RoutePreview &&
            !navigation_->routes().empty()) {
            if (view_->start_guidance(0u))
                trace("benchmark_autostart=guidance");
            else
                trace("error=benchmark_autostart_guidance");
            autostart_ = Autostart::Done;
        }
        break;
    case Autostart::Off:
    case Autostart::Done:
        break;
    }
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
        if (precache_) precache_->stop();
        precache_.reset();
        navigation_worker_.reset();
        if (retained_symbols_) {
            retained_symbols_->stop();
            retained_symbols_->release(mode == ReleaseMode::ContextLost);
            retained_symbols_.reset();
        }
        retained_symbols_creation_attempted_ = false;
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
        render_shootout_enabled_ = false;
        sustained_basemap_benchmark_ = false;
        render_shootout_active_ = false;
        render_shootout_complete_ = false;
        render_shootout_phase_ = 0u;
        autostart_ = Autostart::Off;
        simulation_arrival_reported_ = false;
        guidance_tracker_.reset();
        compositor_.release(mode == ReleaseMode::ContextLost);
        overlay_occupancy_ = ui::OverlayOccupancy();
        view_.reset();
        navigation_.reset();
        recents_store_.reset();
        camera_ = CameraController();
        motion_.reset();
        pose_applied_ = false;
        night_ = false;
        overview_pending_ = false;
        camera_updated_at_ = {};
        overlay_enabled_ = true;
        submitted_search_revision_ = 0;
        observed_search_revision_ = 0;
        submitted_route_revision_ = 0;
        search_revision_observed_ = false;
        if (!renderer_->releaseRendering(mode == ReleaseMode::ContextLost))
            return ReleaseResult::Quarantined;
        // releaseRendering() joined the GPU worker, so nothing binds it now.
        if (upload_context_) upload_context_->destroy();
        upload_context_.reset();
        phase_ = Phase::CoreReady;
    }
    renderer_.reset();
    raster_.reset();
    caching_raster_.reset();
    style_.reset();
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
