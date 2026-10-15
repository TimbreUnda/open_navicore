#include "engine.h"
#include "build_info.h"
#include "camera_controller.h"
#include "cluster_payload.h"
#include "crash_reporter.h"
#include "session.h"
#include "diagnostics.h"
#include "frame_metrics.h"
#include "guidance_tracker.h"
#include "navigation_worker.h"
#include "ui/gl_overlay_compositor.h"
#include "ui/navigation_model.h"
#include "ui/navigation_view.h"
#include "ui/recents_store.h"
#include "vector/vector_map.h"

#include <open_navicore/cluster.h>

#include <OsmAndCore.h>
#include <OsmAndCore/CoreResourcesEmbeddedBundle.h>
#include <OsmAndCore/LatLon.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Utilities.h>
#include <OsmAndCore/Map/AtlasMapRendererConfiguration.h>
#include <OsmAndCore/Map/IMapRenderer.h>

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <array>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__QNXNTO__)
#include <malloc.h>
#endif

namespace navicore_osmand {
namespace {

enum class Phase { Empty, Resources, CoreEntering, CoreReady,
                   RenderingEntering, Ready, Quarantined };

class OsmAndEngine final : public Engine {
public:
    bool initialize(const Config&, const OpenNaviBackendRenderTargetV1&,
                    std::shared_ptr<Wakeup>) override;
    FrameResult frame(const OpenNaviBackendRenderTargetV1&) override;
    int gps(const OpenNaviBackendGpsV1&) override;
    void visibility(const OpenNaviBackendVisibilityV1& event) override;
    void screen(const OpenNaviBackendScreenV1& event) override;
    void vehicle(const OpenNaviBackendVehicleV2& event) override;
    void internationalization(const OpenNaviBackendInternationalizationV1& event) override;
    void cluster_display(const OpenNaviBackendClusterDisplayV1& event) override;
    void driver_distraction(const OpenNaviBackendDriverDistractionV1& event) override;
    void set_outbound(const OpenNaviBackendServicesV2* services) override { outbound_ = services; }
    int pointer(const OpenNaviBackendPointerV2&) override;
    ReleaseResult release(ReleaseMode) override;

private:
    void resize(const OpenNaviBackendRenderTargetV1&);
    // fit: frame the whole route (new route); false keeps the camera
    // (a recalculated route during guidance).
    void show_route(const ui::RouteAlternative&, bool fit = true);
    bool clear_route();
    // Off-route recalculation (G8): request a new route after the
    // position stays off the route.
    void check_off_route(const ui::GuidanceState& state, const ui::GeoPoint& position);
    bool update_guidance();
    bool ensure_navigation_worker();
    void advance_benchmark_autostart();
    // Drains UI map commands, advances the car camera and applies the pose to
    // the renderer. Returns true while the camera still needs frames.
    bool apply_camera();
    Phase phase_ = Phase::Empty;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources_;
    std::shared_ptr<OsmAnd::ObfsCollection> obfs_;
    // OsmAnd's renderer only provides the camera (projection, unprojection)
    // and an empty frame; VectorMap draws the map, route and labels.
    std::shared_ptr<OsmAnd::IMapRenderer> renderer_;
    std::unique_ptr<navicore_vector::VectorMap> vector_map_;
    navicore_vector::MapView vector_view_;
    std::string vector_cache_dir_;
    std::string vector_cache_identity_;
    void sync_vector_guidance(const ui::GuidanceState* state);
    std::chrono::steady_clock::time_point vector_last_frame_{};
    std::chrono::steady_clock::time_point vector_reported_at_{};
    bool render_vector_map(const OpenNaviBackendRenderTargetV1& target);
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
    // LVGL time advances by the real frame interval.
    std::chrono::steady_clock::time_point input_at_{};
    // Finger drag: the 31-bit map point grabbed at the press stays under
    // the finger; drag_target_ is the free camera's centre.
    bool drag_valid_ = false;
    double drag_grab_x_ = 0.0, drag_grab_y_ = 0.0;
    double drag_target_x_ = 0.0, drag_target_y_ = 0.0;
    bool apply_drag(const ui::MapDrag& drag);
    void handle_map_tap(int x, int y);
    void handle_map_touch(const ui::MapTouch& touch);
    // Platform inputs, raw (meanings not qualified): kept for the
    // developer menu and traced when they change; nothing acts on them.
    template <typename T> struct Raw { T value{}; bool has = false; };
    Raw<OpenNaviBackendVisibilityV1> visibility_in_;
    Raw<OpenNaviBackendScreenV1> screen_in_;
    Raw<OpenNaviBackendVehicleV2> vehicle_in_;
    Raw<OpenNaviBackendInternationalizationV1> language_in_;
    Raw<OpenNaviBackendClusterDisplayV1> cluster_display_in_;
    Raw<OpenNaviBackendDriverDistractionV1> distraction_in_;
    // Instrument cluster: the guidance state in the recovered cluster
    // fields, published on change (at most twice a second).
    const OpenNaviBackendServicesV2* outbound_ = nullptr;
    void publish_cluster();
    ClusterPayload cluster_last_;
    bool cluster_published_ = false;
    std::uint32_t cluster_last_status_ = 0;
    std::uint64_t cluster_publishes_ = 0;
    std::chrono::steady_clock::time_point cluster_published_at_{};
    // Developer menu: rows refreshed twice a second while it is open,
    // toggles applied, the vector cache cleared a little each frame.
    void update_dev_menu();
    void apply_dev_action(std::size_t toggle);
    bool step_cache_clear();
    std::chrono::steady_clock::time_point dev_updated_at_{};
    std::array<float, 240> frame_intervals_ms_{};
    std::size_t frame_interval_count_ = 0, frame_interval_next_ = 0;
    std::chrono::steady_clock::time_point last_presented_at_{};
    std::array<std::chrono::steady_clock::time_point, 16> gps_times_{};
    std::size_t gps_time_count_ = 0, gps_time_next_ = 0;
    struct CacheClearDir { std::string path; };
    std::vector<CacheClearDir> cache_clear_;
    std::size_t cache_cleared_files_ = 0;
    bool cache_clear_done_ = false;
    bool apply_pinch(const ui::MapPinch& pinch);
    bool pinch_valid_ = false;
    double pinch_grab_x_ = 0.0, pinch_grab_y_ = 0.0;
    double pinch_target_x_ = 0.0, pinch_target_y_ = 0.0;
    double pinch_distance_ = 1.0, pinch_angle_ = 0.0;
    float pinch_zoom_ = 16.0f, pinch_azimuth_ = 0.0f;
    void drop_pin(int x, int y);
    void update_pin_marker();
    bool unproject_lat_lon(int x, int y, OsmAnd::LatLon& out) const;
    std::string with_distance(std::string text, const OsmAnd::LatLon& location) const;
    std::string pin_id_;                  // the pin whose address is pending
    Config config_;
    std::uint64_t address_revision_ = 0;
    ui::MapTouch map_touch_;
    std::chrono::steady_clock::time_point clock_origin_ =
        std::chrono::steady_clock::now();
    CameraController::Pose applied_pose_{};
    bool pose_applied_ = false;
    bool night_ = false;
    bool overview_pending_ = false;
    bool route_shown_ = false;  // the vector map draws a route
    double overview_box_[4] = {0.0, 0.0, 0.0, 0.0};
    OpenNaviBackendGpsV1 latest_gps_{};
    bool has_valid_position_ = false;
    bool gps_state_observed_ = false;
    bool last_logged_gps_valid_ = false;
    bool gps_dirty_ = false;
    bool guidance_active_ = false;
    bool simulation_enabled_ = false;
    bool simulation_running_ = false;
    bool simulation_arrival_reported_ = false;
    // Off-route recalculation.
    bool off_route_ = false;
    std::chrono::steady_clock::time_point off_route_since_{};
    std::chrono::steady_clock::time_point reroute_retry_at_{};
    // Bench detour (simulate-detour): 0 pending, 1 active, 2 done.
    bool simulate_detour_ = false;
    int detour_phase_ = 0;
    std::chrono::steady_clock::time_point detour_started_{};
    double simulation_progress_metres_ = 0.0;
    double simulation_geometry_length_metres_ = 0.0;
    double simulation_speed_metres_per_second_ = 20.0;
    std::chrono::steady_clock::time_point simulation_updated_at_{};
    // Bench-only: drive search -> route -> guidance without touch input.
    enum class Autostart { Off, Search, Select, Start, Done };
    Autostart autostart_ = Autostart::Off;
    ui::GlOverlayCompositor compositor_;
    ui::OverlayOccupancy overlay_occupancy_;
    std::vector<ui::UploadRect> upload_rects_;
    bool overlay_enabled_ = true;
    FrameMetrics frame_metrics_{false};
    // update_guidance() parts of the current frame (frame log only).
    std::uint32_t guidance_tracker_us_ = 0;
    std::uint32_t guidance_sync_us_ = 0;
    std::uint32_t guidance_apply_us_ = 0;
};

bool failure(const char* message)
{
    trace(message);
    return false;
}

bool valid_coordinates(const OpenNaviBackendGpsV1& gps)
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
                             const OpenNaviBackendRenderTargetV1& target,
                             std::shared_ptr<Wakeup> wakeup)
{
    config_ = config;
    simulation_enabled_ = config.simulate_route;
    simulate_detour_ = config.simulate_detour;
    if (simulate_detour_)
        trace("simulation=detour_enabled");
    autostart_ = config.benchmark_autostart && config.simulate_route
                     ? Autostart::Search : Autostart::Off;
    if (autostart_ != Autostart::Off)
        trace("benchmark_autostart=enabled");
    if (!config.vector_cache.empty()) {
        struct stat map_info {};
        const bool map_known = ::stat(config.map.c_str(), &map_info) == 0;
        // Anything that changes built geometry or labels must change this.
        vector_cache_dir_ = config.vector_cache;
        vector_cache_identity_ = config.map + "|" +
            std::to_string(map_known ? map_info.st_size : 0) + "|" +
            std::to_string(map_known ? map_info.st_mtime : 0) + "|vector-style-2";
    }
    frame_metrics_ = FrameMetrics(config.performance_metrics);
    if (config.performance_metrics)
        trace("performance_metrics=enabled");
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
    obfs_ = std::make_shared<OsmAnd::ObfsCollection>();
    obfs_->addFile(QString::fromLocal8Bit(config.map.c_str()));
    if (obfs_->getObfFiles().isEmpty())
        return failure("error=map");
    trace("init=map");

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

    // GPU API/resources/stages may initialize before _isRenderingInitialized
    // becomes true. releaseRendering refuses to clean that partial state.
    phase_ = Phase::RenderingEntering;
    if (!renderer_->initializeRendering(true))
        return failure("error=initialize_rendering_partial");
    // Rendering now has a valid teardown contract. Mark it before allocations
    // below so an LVGL construction exception can still release OsmAnd.
    phase_ = Phase::Ready;
    trace("init=rendering");
    try {
        vector_map_.reset(new navicore_vector::VectorMap(
            obfs_, resources_, 2, [wakeup]() { (void)wakeup->request(); }));
        if (!vector_map_->init_gl())
            throw std::runtime_error("vector_map gl");
        if (!vector_cache_dir_.empty())
            vector_map_->set_disk_cache(vector_cache_dir_, vector_cache_identity_,
                                        200ull << 20);
        vector_map_->set_night(night_);
        trace("init=vector_map");
    } catch (...) {
        if (vector_map_)
            vector_map_->release_gl(false);
        vector_map_.reset();
        return failure("error=vector_map_init");
    }
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
    navigation_map_ = config.map;
    navigation_routing_xml_.assign(
        routing_xml.constData(), static_cast<std::size_t>(routing_xml.size()));
    trace("init=worker_deferred");
    return true;
}

void OsmAndEngine::resize(const OpenNaviBackendRenderTargetV1& target)
{
    renderer_->setWindowSize(OsmAnd::PointI(
        static_cast<int32_t>(target.width), static_cast<int32_t>(target.height)));
    renderer_->setViewport(OsmAnd::AreaI(
        0, 0, static_cast<int32_t>(target.height), static_cast<int32_t>(target.width)));
    width_ = target.width;
    height_ = target.height;
}

void OsmAndEngine::show_route(const ui::RouteAlternative& route, bool fit)
{
    std::vector<std::pair<std::int32_t, std::int32_t>> route31;
    route31.reserve(route.geometry.size());
    double min_latitude = 90.0;
    double max_latitude = -90.0;
    double min_longitude = 180.0;
    double max_longitude = -180.0;
    simulation_geometry_length_metres_ = 0.0;
    for (const auto& point : route.geometry) {
        const auto point31 = OsmAnd::Utilities::convertLatLonTo31(
            OsmAnd::LatLon(point.latitude, point.longitude));
        route31.emplace_back(point31.x, point31.y);
        min_latitude = std::min(min_latitude, point.latitude);
        max_latitude = std::max(max_latitude, point.latitude);
        min_longitude = std::min(min_longitude, point.longitude);
        max_longitude = std::max(max_longitude, point.longitude);
    }
    for (std::size_t index = 1; index < route.geometry.size(); ++index) {
        simulation_geometry_length_metres_ += geometry_distance(
            route.geometry[index - 1], route.geometry[index]);
    }
    if (vector_map_)
        vector_map_->set_route(route31);
    route_shown_ = true;
    overview_box_[0] = min_latitude;
    overview_box_[1] = min_longitude;
    overview_box_[2] = max_latitude;
    overview_box_[3] = max_longitude;
    overview_pending_ = fit;
    guidance_tracker_.reset();
    off_route_ = false;
    guidance_active_ = false;
    simulation_running_ = false;
    simulation_progress_metres_ = 0.0;
    simulation_arrival_reported_ = false;
}

bool OsmAndEngine::clear_route()
{
    if (!route_shown_)
        return false;
    route_shown_ = false;
    overview_pending_ = false;
    if (vector_map_) {
        vector_map_->set_route({});
        sync_vector_guidance(nullptr);
    }
    // The route overview ends with its route.
    if (camera_.mode() == CameraController::Mode::Overview)
        camera_.recenter();
    trace("route=cleared");
    return true;
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
        const auto now = std::chrono::steady_clock::now();  // also times the detour
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
        // Bench detour: once, past 35% of the route, drive 150 m beside it
        // until a recalculated route arrives (or 30 s pass).
        if (simulate_detour_ && detour_phase_ == 0 &&
            simulation_progress_metres_ > 0.35 * route.distance_metres) {
            detour_phase_ = 1;
            detour_started_ = now;
            trace("simulation=detour_started");
        }
        if (detour_phase_ == 1) {
            if (now - detour_started_ > std::chrono::seconds(30)) {
                detour_phase_ = 2;
            } else {
                position.longitude += 150.0 /
                    (111320.0 * std::cos(position.latitude * 3.14159265358979323846 / 180.0));
            }
        }
        position_available = true;
        simulated = true;
    }
    const auto tracker_started = std::chrono::steady_clock::now();
    const auto since_us = [](std::chrono::steady_clock::time_point started) {
        return static_cast<std::uint32_t>(std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    };
    if (position_available) {
        state = guidance_tracker_.update(route, {
            position.latitude, position.longitude});
        state.simulated = simulated;
        // The route-snapped position drives the camera and vehicle marker.
        motion_.observe(state.position.latitude, state.position.longitude,
                        std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - clock_origin_)
                            .count());
        check_off_route(state, position);
        if (simulated && state.arrived && !simulation_arrival_reported_) {
            simulation_arrival_reported_ = true;
            trace("simulation=arrived");
        }
    } else {
        state = guidance_tracker_.unavailable(route);
    }
    state.recalculating = navigation_->rerouting();
    gps_dirty_ = false;
    guidance_active_ = true;
    guidance_tracker_us_ += since_us(tracker_started);
    const auto sync_started = std::chrono::steady_clock::now();
    sync_vector_guidance(&state);
    guidance_sync_us_ += since_us(sync_started);
    const auto apply_started = std::chrono::steady_clock::now();
    const bool changed = view_->apply_guidance(std::move(state));
    guidance_apply_us_ += since_us(apply_started);
    return changed;
}

// More than 50 m from the route for 4 s (GPS noise and short parallel
// stretches stay below that) asks for a new route from the position to the
// same destination. One request at a time; a failed one waits 10 s.
void OsmAndEngine::check_off_route(const ui::GuidanceState& state,
                                   const ui::GeoPoint& position)
{
    constexpr double off_route_metres = 50.0;
    constexpr auto off_route_time = std::chrono::seconds(4);
    const auto now = std::chrono::steady_clock::now();
    if (state.arrived || state.off_route_metres <= off_route_metres) {
        off_route_ = false;
        return;
    }
    if (!off_route_) {
        off_route_ = true;
        off_route_since_ = now;
        return;
    }
    if (now - off_route_since_ < off_route_time || navigation_->rerouting() ||
        now < reroute_retry_at_ || !navigation_->destination() ||
        !ensure_navigation_worker())
        return;
    const std::uint64_t revision = navigation_->request_reroute();
    if (!revision)
        return;
    submitted_route_revision_ = revision;
    trace_result("reroute=submit", revision,
                 static_cast<std::size_t>(state.off_route_metres));
    navigation_worker_->submit_route(revision, *navigation_->destination(), position);
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
    if (screen == ui::Screen::Search) {
        vector_view_.tile_zoom_limit = -1.0;
        return false;
    }

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
            if (vector_map_)
                vector_map_->set_night(night_);
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
    bool dragging = false;
    ui::MapDrag drag;
    if (navigation_->take_drag(drag))
        dragging = apply_drag(drag);
    ui::MapPinch pinch;
    if (navigation_->take_pinch(pinch))
        dragging = apply_pinch(pinch) || dragging;
    while (navigation_->take_map_touch(map_touch_))
        handle_map_touch(map_touch_);

    {
        // The coming turn shapes the 3D camera (steeper, a little closer).
        const auto& guidance = navigation_->guidance();
        const auto kind = guidance.maneuver.kind;
        const bool turn = screen == ui::Screen::Guidance && guidance.position_available &&
                          !guidance.arrived && kind != ui::ManeuverKind::Straight &&
                          kind != ui::ManeuverKind::Arrive;
        const bool tight = kind == ui::ManeuverKind::Roundabout ||
                           kind == ui::ManeuverKind::SharpLeft ||
                           kind == ui::ManeuverKind::SharpRight ||
                           kind == ui::ManeuverKind::UTurn;
        camera_.set_maneuver(turn, guidance.distance_to_maneuver_metres, tight);
    }
    const auto vehicle = motion_.predict(now_s);
    const auto pose = camera_.update(vehicle, dt);
    vector_view_.tile_zoom_limit = pose.tile_zoom;
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
    update_pin_marker();

    ui::MapViewState state;
    state.heading_known = camera_.heading_known();
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

    return changed || dragging || (vehicle.valid && vehicle.speed_mps > 0.3);
}

// Keeps the grabbed map point under the finger: each frame the free
// camera centre moves by the ground offset between the grabbed point and
// what is under the finger now. Ground offsets are exact under rotation,
// tilt and fractional zoom because OsmAnd unprojects them.
bool OsmAndEngine::apply_drag(const ui::MapDrag& drag)
{
    const auto unproject = [this](double x, double y, double& x31, double& y31) {
        OsmAnd::PointI64 location;
        if (!renderer_->getLocationFromScreenPoint(OsmAnd::PointD(x, y), location))
            return false;
        x31 = static_cast<double>(location.x);
        y31 = static_cast<double>(location.y);
        return true;
    };
    const auto to_lat_lon = [](double x31, double y31) {
        return OsmAnd::Utilities::convert31ToLatLon(OsmAnd::PointI(
            static_cast<int32_t>(x31), static_cast<int32_t>(y31)));
    };
    if (drag.started) {
        const auto& viewport = camera_.viewport();
        drag_valid_ =
            unproject(drag.start_x, drag.start_y, drag_grab_x_, drag_grab_y_) &&
            unproject(viewport.left + viewport.width / 2.0, viewport.top + viewport.height / 2.0,
                      drag_target_x_, drag_target_y_);
        if (drag_valid_) {
            const auto centre = to_lat_lon(drag_target_x_, drag_target_y_);
            camera_.begin_free(centre.latitude, centre.longitude);
        }
    }
    if (drag_valid_) {
        double under_x = 0.0, under_y = 0.0;
        if (unproject(drag.x, drag.y, under_x, under_y)) {
            drag_target_x_ += drag_grab_x_ - under_x;
            drag_target_y_ += drag_grab_y_ - under_y;
            const auto centre = to_lat_lon(drag_target_x_, drag_target_y_);
            camera_.set_free_location(centre.latitude, centre.longitude);
        }
    }
    if (!drag.active)
        drag_valid_ = false;
    return drag.active;
}

namespace {
const char* const kMarkerDir = "/datastore/navicore-backend/";
// Bench markers shown as switches; read at backend (or provider) start.
struct DevMarker { const char* label; const char* file; };
constexpr DevMarker kDevMarkers[] = {
    {"Route simulation", "enable-route-simulation"},
    {"Performance metrics", "enable-performance-metrics"},
    {"Input trace", "enable-input-trace"},
    {"Benchmark autostart", "enable-benchmark-autostart"},
    {"Simulate detour", "simulate-detour"},
};
constexpr std::size_t kDevToggleNight = 0, kDevToggleTilt = 1, kDevMarkerFirst = 2;
constexpr std::size_t kDevClearCache = kDevMarkerFirst + sizeof(kDevMarkers) / sizeof(kDevMarkers[0]);

bool file_exists(const std::string& path)
{
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

std::string printf_string(const char* format, ...)
{
    char text[160] = {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    return text;
}
} // namespace

void OsmAndEngine::visibility(const OpenNaviBackendVisibilityV1& event)
{
    visibility_in_ = {event, true};
    trace_result("input=visibility", event.visibility, event.interactivity);
}

void OsmAndEngine::screen(const OpenNaviBackendScreenV1& event)
{
    screen_in_ = {event, true};
    trace_result("input=screen_type", event.screen_type, event.animation);
}

void OsmAndEngine::vehicle(const OpenNaviBackendVehicleV2& event)
{
    const auto before = vehicle_in_;
    vehicle_in_ = {event, true};
    if (!before.has || before.value.day_night_status != event.day_night_status)
        trace_result("input=day_night_status", event.day_night_status, event.valid_fields);
    if (!before.has || before.value.day_night_setting != event.day_night_setting)
        trace_result("input=day_night_setting", event.day_night_setting, event.valid_fields);
}

void OsmAndEngine::internationalization(const OpenNaviBackendInternationalizationV1& event)
{
    language_in_ = {event, true};
    trace_result("input=language", event.language_raw, event.settings_present);
}

void OsmAndEngine::cluster_display(const OpenNaviBackendClusterDisplayV1& event)
{
    cluster_display_in_ = {event, true};
    trace_result("input=cluster_display", event.enabled_raw, 0u);
}

void OsmAndEngine::driver_distraction(const OpenNaviBackendDriverDistractionV1& event)
{
    const bool changed = !distraction_in_.has ||
        distraction_in_.value.distraction_status_raw != event.distraction_status_raw;
    distraction_in_ = {event, true};
    if (changed)
        trace_result("input=driver_distraction", event.distraction_status_raw, 0u);
}

// Instrument cluster: published on change, at most twice a second
// (payload rules in cluster_payload.cpp).
void OsmAndEngine::publish_cluster()
{
    if (!outbound_ || !outbound_->publish_cluster)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (cluster_published_ && now - cluster_published_at_ < std::chrono::milliseconds(500))
        return;
    ClusterInputs inputs;
    inputs.screen = navigation_->screen();
    inputs.calculating_route = navigation_->calculating_route();
    inputs.guidance = navigation_->guidance();
    const auto vehicle = motion_.predict(
        std::chrono::duration<double>(now - clock_origin_).count());
    inputs.heading_valid = vehicle.valid && vehicle.speed_mps > 1.0;
    inputs.heading_degrees = vehicle.heading_degrees;
    inputs.now = std::time(nullptr);
    ClusterPayload payload = build_cluster_payload(inputs);
    if (cluster_published_ && payload.same_as(cluster_last_))
        return;
    const uint32_t status =
        outbound_->publish_cluster(outbound_->v1.runtime_context, &payload.cluster);
    cluster_last_ = std::move(payload);
    cluster_last_.bind();
    cluster_published_ = true;
    cluster_published_at_ = now;
    ++cluster_publishes_;
    if (status != cluster_last_status_ || cluster_publishes_ == 1)
        trace_result("cluster=publish_status", status, cluster_last_.cluster.valid_attributes);
    cluster_last_status_ = status;
}

void OsmAndEngine::update_dev_menu()
{
    const bool clearing = step_cache_clear();
    if (!navigation_->dev_menu_open())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (!clearing && dev_updated_at_ != std::chrono::steady_clock::time_point{} &&
        now - dev_updated_at_ < std::chrono::milliseconds(500))
        return;
    dev_updated_at_ = now;
    std::vector<ui::DevSection> sections;

    // GPS: several fields are raw values without qualified units
    // (api-ui-restrictions.md), shown raw and labelled so.
    ui::DevSection gps{"GPS", {}};
    const auto& g = latest_gps_;
    gps.rows.push_back({"Fix (raw)", printf_string("%u%s", g.fix_type,
                                                   has_valid_position_ ? " · position" : " · none")});
    gps.rows.push_back({"Satellites", printf_string("%u used / %u in view", g.satellites_used,
                                                    g.satellites_in_view)});
    gps.rows.push_back({"DOP (raw)", printf_string("H %u · P %u · V %u", g.hdop_raw, g.pdop_raw,
                                                   g.vdop_raw)});
    gps.rows.push_back({"Accuracy (raw)", printf_string("%u", g.accuracy_raw)});
    gps.rows.push_back({"Position", printf_string("%.6f, %.6f", g.latitude_e7 / 1e7,
                                                  g.longitude_e7 / 1e7)});
    gps.rows.push_back({"Course (raw)", printf_string("hdg %u · spd %u", g.heading_raw,
                                                      g.speed_raw_masked)});
    gps.rows.push_back({"Quality / sync", printf_string("%u / %u", g.quality_status,
                                                        g.sync_counter)});
    if (gps_time_count_ == 0) {
        gps.rows.push_back({"Last fix", "never"});
        gps.rows.push_back({"Rate", "-"});
    } else {
        const auto last = gps_times_[(gps_time_next_ + gps_times_.size() - 1) % gps_times_.size()];
        const auto first = gps_times_[gps_time_count_ < gps_times_.size() ? 0 : gps_time_next_];
        gps.rows.push_back({"Last fix", printf_string("%.1f s ago",
            std::chrono::duration<double>(now - last).count())});
        const double span = std::chrono::duration<double>(last - first).count();
        gps.rows.push_back({"Rate", gps_time_count_ > 1 && span > 0.0
            ? printf_string("%.1f Hz", (gps_time_count_ - 1) / span) : std::string("-")});
    }
    sections.push_back(std::move(gps));

    ui::DevSection position{"POSITION AND CAMERA", {}};
    const double now_s = std::chrono::duration<double>(now - clock_origin_).count();
    const auto vehicle = motion_.predict(now_s);
    position.rows.push_back({"Motion", vehicle.valid
        ? printf_string("%.0f km/h · %.0f°", vehicle.speed_mps * 3.6, vehicle.heading_degrees)
        : std::string("no position")});
    const char* mode = "free";
    switch (camera_.mode()) {
    case CameraController::Mode::HeadingUp: mode = "heading-up"; break;
    case CameraController::Mode::NorthUp: mode = "north-up"; break;
    case CameraController::Mode::Free: mode = "free"; break;
    case CameraController::Mode::Overview: mode = "overview"; break;
    }
    position.rows.push_back({"Camera", printf_string("%s · z%.2f · %.0f°", mode,
        applied_pose_.zoom, applied_pose_.elevation_degrees)});
    const auto& guidance = navigation_->guidance();
    position.rows.push_back({"Off route", navigation_->screen() == ui::Screen::Guidance
        ? printf_string("%.0f m%s", guidance.off_route_metres,
                        guidance.recalculating ? " · recalculating" : "")
        : std::string("not guiding")});
    sections.push_back(std::move(position));

    ui::DevSection map{"MAP DATA", {}};
    {
        struct stat info {};
        const auto slash = config_.map.rfind('/');
        const std::string name = slash == std::string::npos ? config_.map : config_.map.substr(slash + 1);
        map.rows.push_back({"Map file", ::stat(config_.map.c_str(), &info) == 0
            ? printf_string("%s · %.0f MB", name.c_str(), info.st_size / 1048576.0)
            : name + " · missing"});
        struct statvfs space {};
        map.rows.push_back({"Cache free", !config_.vector_cache.empty() &&
                                ::statvfs(config_.vector_cache.c_str(), &space) == 0
            ? printf_string("%.0f MB", static_cast<double>(space.f_bavail) * space.f_frsize / 1048576.0)
            : std::string("off")});
    }
    if (vector_map_) {
        const auto stats = vector_map_->stats();
        map.rows.push_back({"Tiles", printf_string("%llu new · %llu disk · %zu GPU",
            static_cast<unsigned long long>(stats.tiles_built),
            static_cast<unsigned long long>(stats.disk_hits), stats.gpu_tiles)});
        map.rows.push_back({"GPU memory", printf_string("%.1f MB", stats.gpu_bytes / 1048576.0)});
        map.rows.push_back({"Wanted", printf_string("%zu · prefetch %zu · z%d",
            stats.wanted_tiles, stats.prefetch_tiles, stats.tile_zoom)});
    }
    if (frame_interval_count_ > 0) {
        std::array<float, 240> sorted{};
        std::copy_n(frame_intervals_ms_.begin(), frame_interval_count_, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(frame_interval_count_));
        float total = 0.0f;
        for (std::size_t i = 0; i < frame_interval_count_; ++i)
            total += sorted[i];
        const float p99 = sorted[std::min(frame_interval_count_ - 1,
                                          frame_interval_count_ * 99 / 100)];
        map.rows.push_back({"Frames", printf_string("%.1f fps · 1%% low %.1f fps",
            1000.0f * frame_interval_count_ / std::max(total, 1.0f), 1000.0f / std::max(p99, 1.0f))});
    }
#if defined(__QNXNTO__)
    {
        const struct mallinfo heap = ::mallinfo();
        map.rows.push_back({"Heap", printf_string("%.1f MB used · %.1f MB arena",
            heap.uordblks / 1048576.0, heap.arena / 1048576.0)});
    }
#endif
    sections.push_back(std::move(map));

    ui::DevSection platform{"CAR AND HMI (RAW)", {}};
    const auto raw = [&](bool has, std::string text) { return has ? text : std::string("-"); };
    const auto& v = vehicle_in_.value;
    float displayed_speed = 0.0f;
    static_assert(sizeof(displayed_speed) == sizeof(v.displayed_vehicle_speed_bits), "float bits");
    std::memcpy(&displayed_speed, &v.displayed_vehicle_speed_bits, sizeof(displayed_speed));
    platform.rows.push_back({"Day/night", raw(vehicle_in_.has, printf_string(
        "status %u · setting %u · fields 0x%x", v.day_night_status, v.day_night_setting,
        v.valid_fields))});
    platform.rows.push_back({"Car speed", raw(vehicle_in_.has &&
        (v.valid_fields & OPEN_NAVI_BACKEND_VEHICLE_SPEED) != 0,
        printf_string("%.1f (GPS %.1f km/h)", displayed_speed, vehicle.valid ? vehicle.speed_mps * 3.6 : 0.0))});
    platform.rows.push_back({"Visibility", raw(visibility_in_.has, printf_string(
        "%u · interactive %u · screen %u", visibility_in_.value.visibility,
        visibility_in_.value.interactivity, screen_in_.value.screen_type))});
    platform.rows.push_back({"Language", raw(language_in_.has, printf_string(
        "%u · date %u · time %u", language_in_.value.language_raw,
        language_in_.value.date_format_raw, language_in_.value.time_format_raw))});
    platform.rows.push_back({"Distraction", raw(distraction_in_.has, printf_string(
        "%u", distraction_in_.value.distraction_status_raw))});
    platform.rows.push_back({"Cluster", outbound_ ? printf_string(
        "%llu sent · status %u · display %s", static_cast<unsigned long long>(cluster_publishes_),
        cluster_last_status_, cluster_display_in_.has
            ? printf_string("%u", cluster_display_in_.value.enabled_raw).c_str() : "-")
        : std::string("no outbound services")});
    sections.push_back(std::move(platform));

    ui::DevSection build{"BUILD", {}};
    build.rows.push_back({"Backend", build_git()});
    build.rows.push_back({"Built", build_date()});
    build.rows.push_back({"Interfaces", outbound_ ? "backend V5 · contacts" : "backend V2 · contacts"});
    sections.push_back(std::move(build));

    std::vector<ui::DevToggle> toggles;
    toggles.push_back({"Night map", night_, ""});
    toggles.push_back({"3D view", camera_.tilted(), ""});
    for (const auto& marker : kDevMarkers)
        toggles.push_back({marker.label, file_exists(std::string(kMarkerDir) + marker.file),
                           "applies at next start"});
    toggles.push_back({"Clear vector cache", false,
                       !cache_clear_.empty() ? printf_string("clearing · %zu files", cache_cleared_files_)
                       : cache_clear_done_ ? printf_string("cleared %zu files", cache_cleared_files_)
                                           : std::string("rebuilds tiles from the map")});
    (void)view_->set_dev_info(std::move(sections), std::move(toggles));

    std::size_t action = 0;
    while (navigation_->take_dev_action(action))
        apply_dev_action(action);
}

void OsmAndEngine::apply_dev_action(std::size_t toggle)
{
    dev_updated_at_ = {};  // show the result at once
    if (toggle == kDevToggleNight) {
        navigation_->push_map_command(ui::MapCommand::ToggleNight);
    } else if (toggle == kDevToggleTilt) {
        navigation_->push_map_command(ui::MapCommand::ToggleTilt);
    } else if (toggle >= kDevMarkerFirst && toggle < kDevClearCache) {
        const std::string path = std::string(kMarkerDir) + kDevMarkers[toggle - kDevMarkerFirst].file;
        if (file_exists(path)) {
            (void)::unlink(path.c_str());
        } else {
            const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT, 0644);
            if (fd >= 0)
                (void)::close(fd);
        }
        trace_result("dev=marker", toggle, file_exists(path) ? 1u : 0u);
    } else if (toggle == kDevClearCache && cache_clear_.empty() && !config_.vector_cache.empty()) {
        cache_clear_.push_back({config_.vector_cache});
        cache_cleared_files_ = 0;
        cache_clear_done_ = false;
        trace("dev=clear_vector_cache");
    }
}

// Removes the vector cache a few files per frame (it can be thousands of
// files on flash); the cache writer recreates directories as needed. The
// cache root itself stays.
bool OsmAndEngine::step_cache_clear()
{
    if (cache_clear_.empty())
        return false;
    std::size_t budget = 32;
    while (budget > 0 && !cache_clear_.empty()) {
        const std::string path = cache_clear_.back().path;
        DIR* directory = ::opendir(path.c_str());
        if (!directory) {
            cache_clear_.pop_back();
            continue;
        }
        std::vector<std::string> children;
        std::size_t removed = 0;
        while (const dirent* entry = ::readdir(directory)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..")
                continue;
            const std::string full = path + "/" + name;
            struct stat info {};
            if (::lstat(full.c_str(), &info) != 0)
                continue;
            if (S_ISDIR(info.st_mode)) {
                children.push_back(full);
            } else if (removed < budget && ::unlink(full.c_str()) == 0) {
                ++removed;
            }
        }
        ::closedir(directory);
        cache_cleared_files_ += removed;
        budget -= std::min(budget, removed + 1);
        if (!children.empty()) {
            for (auto& child : children)
                cache_clear_.push_back({std::move(child)});
        } else if (removed == 0) {
            if (cache_clear_.size() > 1)
                (void)::rmdir(path.c_str());
            cache_clear_.pop_back();
        }
    }
    if (cache_clear_.empty()) {
        cache_clear_done_ = true;
        trace_result("dev=vector_cache_cleared", cache_cleared_files_, 0u);
    }
    return true;
}

// Two fingers: their distance zooms (log2 of the ratio), their angle
// rotates, and the ground point grabbed under their midpoint is kept under
// it the same way a drag keeps its point (corrected each frame).
bool OsmAndEngine::apply_pinch(const ui::MapPinch& pinch)
{
    constexpr double degrees = 180.0 / 3.14159265358979323846;
    const auto unproject = [this](double x, double y, double& x31, double& y31) {
        OsmAnd::PointI64 location;
        if (!renderer_->getLocationFromScreenPoint(OsmAnd::PointD(x, y), location))
            return false;
        x31 = static_cast<double>(location.x);
        y31 = static_cast<double>(location.y);
        return true;
    };
    const auto to_lat_lon = [](double x31, double y31) {
        return OsmAnd::Utilities::convert31ToLatLon(OsmAnd::PointI(
            static_cast<int32_t>(x31), static_cast<int32_t>(y31)));
    };
    const double mid_x = (pinch.x0 + pinch.x1) / 2.0;
    const double mid_y = (pinch.y0 + pinch.y1) / 2.0;
    const double distance = std::max(1.0, std::hypot(pinch.x1 - pinch.x0, pinch.y1 - pinch.y0));
    const double angle = std::atan2(pinch.y1 - pinch.y0, pinch.x1 - pinch.x0) * degrees;
    if (pinch.started) {
        const auto& viewport = camera_.viewport();
        pinch_valid_ =
            unproject(mid_x, mid_y, pinch_grab_x_, pinch_grab_y_) &&
            unproject(viewport.left + viewport.width / 2.0, viewport.top + viewport.height / 2.0,
                      pinch_target_x_, pinch_target_y_);
        if (pinch_valid_) {
            const auto centre = to_lat_lon(pinch_target_x_, pinch_target_y_);
            camera_.begin_free(centre.latitude, centre.longitude);
            pinch_distance_ = distance;
            pinch_angle_ = angle;
            pinch_zoom_ = camera_.zoom();
            pinch_azimuth_ = camera_.azimuth();
            trace("map_gesture=pinch");
        }
    }
    if (pinch_valid_ && pinch.active) {
        // Screen angles grow clockwise (y down); turning the fingers
        // clockwise turns the map with them, which lowers the azimuth.
        camera_.set_free_view(
            pinch_zoom_ + static_cast<float>(std::log2(distance / pinch_distance_)),
            pinch_azimuth_ - static_cast<float>(CameraController::shortest_angle_delta(
                                 pinch_angle_, angle)));
        double under_x = 0.0, under_y = 0.0;
        if (unproject(mid_x, mid_y, under_x, under_y)) {
            pinch_target_x_ += pinch_grab_x_ - under_x;
            pinch_target_y_ += pinch_grab_y_ - under_y;
            const auto centre = to_lat_lon(pinch_target_x_, pinch_target_y_);
            camera_.set_free_location(centre.latitude, centre.longitude);
        }
    }
    if (!pinch.active)
        pinch_valid_ = false;
    return pinch.active;
}

bool OsmAndEngine::unproject_lat_lon(int x, int y, OsmAnd::LatLon& out) const
{
    OsmAnd::PointI64 location;
    if (!renderer_->getLocationFromScreenPoint(OsmAnd::PointD(x, y), location))
        return false;
    out = OsmAnd::Utilities::convert31ToLatLon(OsmAnd::PointI(
        static_cast<int32_t>(location.x), static_cast<int32_t>(location.y)));
    return true;
}

// "Fuel · 0.4 km": appends the straight-line distance from the car when
// the car's position is known.
std::string OsmAndEngine::with_distance(std::string text, const OsmAnd::LatLon& location) const
{
    const auto vehicle = motion_.predict(
        std::chrono::duration<double>(std::chrono::steady_clock::now() - clock_origin_).count());
    if (!vehicle.valid)
        return text;
    const double metres = OsmAnd::Utilities::distance(
        OsmAnd::LatLon(vehicle.latitude, vehicle.longitude), location);
    char distance[32] = {};
    if (metres < 1000.0)
        std::snprintf(distance, sizeof(distance), "%.0f m", std::round(metres / 10.0) * 10.0);
    else
        std::snprintf(distance, sizeof(distance), "%.1f km", metres / 1000.0);
    return text.empty() ? std::string(distance) : text + " · " + distance;
}

void OsmAndEngine::handle_map_touch(const ui::MapTouch& touch)
{
    switch (touch.gesture) {
    case ui::MapGesture::Tap: handle_map_tap(touch.x, touch.y); break;
    case ui::MapGesture::DoubleTap: {
        // Zoom in one step around the tapped point (following the car it
        // zooms like the + button and keeps following).
        OsmAnd::LatLon point;
        if (navigation_->screen() != ui::Screen::Search &&
            unproject_lat_lon(touch.x, touch.y, point)) {
            camera_.zoom_at(1.0f, point.latitude, point.longitude);
            trace("map_gesture=double_tap");
        }
        break;
    }
    case ui::MapGesture::LongPress: drop_pin(touch.x, touch.y); break;
    }
}

// A tap on a POI or a place name of the vector map opens its place card
// (free drive); Navigate there routes to it. A tap on bare map closes it.
void OsmAndEngine::handle_map_tap(int x, int y)
{
    if (!vector_map_ || navigation_->screen() != ui::Screen::FreeDrive)
        return;
    navicore_vector::PoiPick pick;
    if (!vector_map_->pick_poi(static_cast<float>(x), static_cast<float>(y), 24.0f, pick)) {
        trace_result("map_tap=miss", static_cast<std::size_t>(x), static_cast<std::size_t>(y));
        (void)view_->close_place_card();
        return;
    }
    const auto location = OsmAnd::Utilities::convert31ToLatLon(OsmAnd::PointI(
        static_cast<int32_t>(pick.x31), static_cast<int32_t>(pick.y31)));
    const bool is_place = pick.icon.empty() && !pick.place.empty();
    ui::Destination place;
    place.id = (is_place ? "place:" : "poi:") +
               std::to_string(static_cast<long long>(pick.x31)) + "," +
               std::to_string(static_cast<long long>(pick.y31));
    place.title = pick.name;
    // "amenity_fuel" -> "Fuel": the category after the key, readable; a
    // place name shows its type ("city" -> "City").
    std::string category = is_place ? pick.place : pick.icon;
    const auto underscore = category.find('_');
    if (!is_place && underscore != std::string::npos && underscore + 1 < category.size())
        category = category.substr(underscore + 1);
    for (auto& c : category)
        if (c == '_')
            c = ' ';
    if (!category.empty())
        category[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(category[0])));
    place.latitude = location.latitude;
    place.longitude = location.longitude;
    place.subtitle = with_distance(category, location);
    if (view_->show_place_card(std::move(place), pick.icon))
        trace(is_place ? "map_tap=place_name_card" : "map_tap=place_card");
}

// Long press on the map: a pin there with its card. The subtitle shows
// the coordinates until the worker finds the nearest address.
void OsmAndEngine::drop_pin(int x, int y)
{
    OsmAnd::LatLon location;
    if (navigation_->screen() != ui::Screen::FreeDrive || !unproject_lat_lon(x, y, location))
        return;
    char coordinates[48] = {};
    std::snprintf(coordinates, sizeof(coordinates), "%.5f, %.5f", location.latitude,
                  location.longitude);
    ui::Destination pin;
    pin.id = std::string(ui::NavigationModel::pin_prefix) + coordinates;
    pin.title = "Dropped pin";
    pin.latitude = location.latitude;
    pin.longitude = location.longitude;
    pin.subtitle = with_distance(coordinates, location);
    pin_id_ = pin.id;
    if (!view_->show_place_card(std::move(pin), "pin"))
        return;
    trace("map_gesture=pin");
    if (ensure_navigation_worker())
        navigation_worker_->submit_address(++address_revision_,
                                           {location.latitude, location.longitude});
}

// The pin follows the map while its card is open.
void OsmAndEngine::update_pin_marker()
{
    const ui::Destination* place = navigation_->place_card();
    if (!navigation_->place_card_is_pin() || !place) {
        view_->set_pin_marker(false, 0, 0);
        return;
    }
    OsmAnd::PointI screen;
    const bool visible = renderer_->obtainScreenPointFromPosition(
        OsmAnd::Utilities::convertLatLonTo31(OsmAnd::LatLon(place->latitude, place->longitude)),
        screen, true);
    view_->set_pin_marker(visible, screen.x, screen.y);
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

FrameResult OsmAndEngine::frame(const OpenNaviBackendRenderTargetV1& target)
{
    using Clock = std::chrono::steady_clock;
    const auto frame_started = Clock::now();
    FrameMeasurement measurement;
    // Render-thread CPU time: wall time far above it means the thread was
    // preempted or blocked, not busy.
    const auto thread_cpu_us = [] {
        timespec now{};
        (void)clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
        return static_cast<std::int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
    };
    const std::int64_t cpu_started = thread_cpu_us();
    const auto elapsed_us = [](Clock::time_point started) {
        const auto value = std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - started).count();
        return static_cast<std::uint32_t>(std::min<std::int64_t>(
            value, static_cast<std::int64_t>(UINT32_MAX)));
    };
    const auto finish = [&](FrameResult result,
                            MeasuredFrameOutcome outcome) -> FrameResult {
        if (outcome == MeasuredFrameOutcome::Presented) {
            const auto now = Clock::now();
            if (last_presented_at_ != Clock::time_point{}) {
                frame_intervals_ms_[frame_interval_next_] =
                    std::chrono::duration<float, std::milli>(now - last_presented_at_).count();
                frame_interval_next_ = (frame_interval_next_ + 1) % frame_intervals_ms_.size();
                frame_interval_count_ = std::min(frame_interval_count_ + 1,
                                                 frame_intervals_ms_.size());
            }
            last_presented_at_ = now;
        }
        measurement.outcome = outcome;
        measurement.total_us = elapsed_us(frame_started);
        measurement.cpu_us = static_cast<std::uint32_t>(
            std::max<std::int64_t>(0, thread_cpu_us() - cpu_started));
        frame_metrics_.record(measurement);
        return result;
    };
    if (phase_ != Phase::Ready)
        return finish({FrameOutcome::Fault}, MeasuredFrameOutcome::Fault);
    guidance_tracker_us_ = guidance_sync_us_ = guidance_apply_us_ = 0;
    NavigationWorker::SearchResult search_result;
    if (navigation_worker_ && navigation_worker_->take_search_result(search_result))
    {
        trace_result("engine=search_result", search_result.revision,
                     search_result.destinations.size());
        (void)view_->apply_search_results(search_result.revision,
                                          std::move(search_result.destinations));
    }
    NavigationWorker::AddressResult address_result;
    if (navigation_worker_ && navigation_worker_->take_address_result(address_result) &&
        address_result.revision == address_revision_) {
        trace_result("engine=address_result", address_result.revision,
                     address_result.address.size());
        const ui::Destination* pin = navigation_->place_card();
        if (!address_result.address.empty() && pin && pin->id == pin_id_)
            (void)view_->set_place_subtitle(
                pin_id_, with_distance(address_result.address,
                                       OsmAnd::LatLon(pin->latitude, pin->longitude)));
    }
    NavigationWorker::RouteResult route_result;
    if (navigation_worker_ && navigation_worker_->take_route_result(route_result)) {
        trace_result("engine=route_result", route_result.revision,
                     route_result.routes.size());
        if (navigation_->screen() == ui::Screen::Guidance) {
            const bool found = !route_result.routes.empty();
            if (navigation_->apply_reroute(route_result.revision,
                                           std::move(route_result.routes))) {
                if (found) {
                    trace("reroute=applied");
                    show_route(navigation_->routes().front(), false);
                    if (detour_phase_ == 1) detour_phase_ = 2;
                } else {
                    // Keep the old route; try again a little later.
                    trace("reroute=failed");
                    reroute_retry_at_ = std::chrono::steady_clock::now() +
                                        std::chrono::seconds(10);
                }
                gps_dirty_ = true;
            }
        } else {
            const bool accepted = view_->apply_routes(
                route_result.revision, std::move(route_result.routes));
            if (accepted && !navigation_->routes().empty())
                show_route(navigation_->routes().front());
        }
    }
    measurement.results_us = elapsed_us(frame_started);
    auto logic_started = Clock::now();
    bool guidance_changed = false;
    if (navigation_->screen() == ui::Screen::Guidance &&
        (!guidance_active_ || gps_dirty_ ||
         (simulation_enabled_ && simulation_running_))) {
        guidance_changed = update_guidance();
    } else if (navigation_->screen() != ui::Screen::Guidance &&
               guidance_active_) {
        guidance_active_ = false;
        sync_vector_guidance(nullptr);
        guidance_tracker_.reset();
        simulation_running_ = false;
        // Simulated motion ends with guidance; real GPS keeps following.
        if (!has_valid_position_) motion_.reset();
        pose_applied_ = false;
    }
    measurement.guidance_us = elapsed_us(logic_started);
    if (target.width != width_ || target.height != height_)
        resize(target);
    logic_started = Clock::now();
    if (overlay_enabled_) {
        const auto input_now = std::chrono::steady_clock::now();
        const auto elapsed_ms = input_at_ == std::chrono::steady_clock::time_point{} ? 16 :
            std::chrono::duration_cast<std::chrono::milliseconds>(input_now - input_at_).count();
        input_at_ = input_now;
        view_->process_input(static_cast<uint32_t>(std::clamp<long long>(elapsed_ms, 0, 250)));
    }
    measurement.input_us = elapsed_us(logic_started);
    logic_started = Clock::now();
    const bool camera_animating = apply_camera();
    measurement.camera_us = elapsed_us(logic_started);
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
    bool vector_wants_frames = false;
    if (vector_map_) {
        stage_started = Clock::now();
        vector_wants_frames = render_vector_map(target);
        measurement.vector_us = elapsed_us(stage_started);
        const auto vt = vector_map_->frame_timing();
        measurement.vec_take_us = vt.take_us;
        measurement.vec_uploads = vt.uploads;
        measurement.vec_route_us = vt.route_us;
        measurement.vec_want_us = vt.want_us;
        measurement.vec_draw_us = vt.draw_us;
        measurement.vec_labels_us = vt.labels_us;
        measurement.vec_evict_us = vt.evict_us;
    }
    view_->resize(target.width, target.height);
    stage_started = Clock::now();
    // process_input() already advanced LVGL time for this frame.
    const bool ui_dirty = overlay_enabled_ && view_->render(0u);
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
    const auto post_started = Clock::now();
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
    // The route is drawn in Route preview and Guidance only: End, arrival,
    // preview Back and closing Search clear it.
    if (route_shown_ && navigation_->screen() != ui::Screen::RoutePreview &&
        navigation_->screen() != ui::Screen::Guidance)
        guidance_changed = clear_route() || guidance_changed;
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
    // A destination picked in Search, or directly from free drive (place
    // card, recent destination).
    if ((navigation_->screen() == ui::Screen::Search ||
         navigation_->direct_routing()) &&
        navigation_->calculating_route() &&
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
    advance_benchmark_autostart();
    publish_cluster();
    update_dev_menu();
    // Recents change only on guidance start; save outside the hot path.
    if (recents_store_ &&
        navigation_->recents_revision() != saved_recents_revision_) {
        saved_recents_revision_ = navigation_->recents_revision();
        if (!recents_store_->save(navigation_->recents()))
            trace("error=recents_save");
    }
    measurement.post_us = elapsed_us(post_started);
    measurement.tracker_us = guidance_tracker_us_;
    measurement.route_sync_us = guidance_sync_us_;
    measurement.guidance_ui_us = guidance_apply_us_;
    return finish({FrameOutcome::Presented, PendingStage::None,
                   guidance_changed || camera_animating || vector_wants_frames ||
                       (overlay_enabled_ && view_->wants_frames()) ||
                       navigation_->dev_menu_open() || !cache_clear_.empty() ||
                       !renderer_->isIdle() ||
                       (autostart_ != Autostart::Off &&
                        autostart_ != Autostart::Done)},
                  MeasuredFrameOutcome::Presented);
}

// The camera comes from OsmAnd: four screen points are unprojected onto the
// ground and the vector map uses the homography through them, which is
// exact for OsmAnd's flat ground in 2D and tilted 3D.
// Route progress (grey behind the vehicle, prefetch ahead) and the turn
// arrow for the next manoeuvre, shown from 400 m out.
void OsmAndEngine::sync_vector_guidance(const ui::GuidanceState* state)
{
    if (!vector_map_)
        return;
    if (!state || !state->position_available) {
        vector_map_->set_route_progress(false, 0, 0);
        vector_map_->set_maneuver(false, 0);
        return;
    }
    const auto position = OsmAnd::Utilities::convertLatLonTo31(
        OsmAnd::LatLon(state->position.latitude, state->position.longitude));
    vector_map_->set_route_progress(true, position.x, position.y);
    const auto kind = state->maneuver.kind;
    const bool turn = kind != ui::ManeuverKind::Straight && kind != ui::ManeuverKind::Arrive;
    vector_map_->set_maneuver(turn && !state->arrived &&
                                  state->distance_to_maneuver_metres <= 400.0,
                              state->maneuver.geometry_index);
}

bool OsmAndEngine::render_vector_map(const OpenNaviBackendRenderTargetV1& target)
{
    // Reused every frame so the blocked list keeps its capacity.
    auto& view = vector_view_;
    view.blocked.clear();
    view.dot_x = view.dot_y = 0.0f;
    view.width = static_cast<int>(target.width);
    view.height = static_cast<int>(target.height);
    const auto state = renderer_->getMapState();
    view.origin_x31 = state.target31.x;
    view.origin_y31 = state.target31.y;
    const double w = view.width, h = view.height;
    const double screen[4][2] = {{w * 0.15, h * 0.55}, {w * 0.85, h * 0.55},
                                 {w * 0.85, h * 0.95}, {w * 0.15, h * 0.95}};
    double world[4][2];
    for (int i = 0; i < 4; ++i) {
        OsmAnd::PointI64 location;
        if (!renderer_->getLocationFromScreenPoint(
                OsmAnd::PointD(screen[i][0], screen[i][1]), location))
            return false;
        world[i][0] = static_cast<double>(location.x);
        world[i][1] = static_cast<double>(location.y);
    }
    if (!navicore_vector::VectorMap::set_from_correspondences(view, world, screen))
        return false;
    const auto visible = renderer_->getVisibleBBox31();
    view.min_x31 = visible.left();
    view.min_y31 = visible.top();
    view.max_x31 = visible.right();
    view.max_y31 = visible.bottom();
    // Tilted, OsmAnd's box can stop short of the screen's near corners;
    // widen it with every screen edge point that lies on the ground.
    for (const auto& p : {std::pair<double, double>{0, h}, {w, h}, {w / 2, h}, {0, h / 2},
                          {w, h / 2}, {0, 0}, {w, 0}, {w / 2, 0}}) {
        OsmAnd::PointI64 location;
        if (!renderer_->getLocationFromScreenPoint(OsmAnd::PointD(p.first, p.second), location))
            continue;
        view.min_x31 = std::min(view.min_x31, static_cast<double>(location.x));
        view.min_y31 = std::min(view.min_y31, static_cast<double>(location.y));
        view.max_x31 = std::max(view.max_x31, static_cast<double>(location.x));
        view.max_y31 = std::max(view.max_y31, static_cast<double>(location.y));
    }
    // The LVGL view draws the vehicle marker; keep labels clear of it.
    view.location_dot = false;
    if (overlay_enabled_)
        for (const auto& rect : overlay_occupancy_.regions())
            view.blocked.push_back({static_cast<float>(rect.x), static_cast<float>(rect.y),
                                    static_cast<float>(rect.width),
                                    static_cast<float>(rect.height)});
    OsmAnd::PointI marker;
    if (renderer_->obtainScreenPointFromPosition(state.target31, marker, false)) {
        view.dot_x = static_cast<float>(marker.x);
        view.dot_y = static_cast<float>(marker.y);
    }
    const auto now = std::chrono::steady_clock::now();
    const double dt = vector_last_frame_ == std::chrono::steady_clock::time_point{}
        ? 0.016
        : std::min(0.1, std::chrono::duration<double>(now - vector_last_frame_).count());
    vector_last_frame_ = now;
    const bool wants_more = vector_map_->render(view, dt, true);
    if (now - vector_reported_at_ >= std::chrono::seconds(5)) {
        vector_reported_at_ = now;
        const auto s = vector_map_->stats();
        const double built = s.tiles_built ? static_cast<double>(s.tiles_built) : 1.0;
        const double disk = s.disk_hits ? static_cast<double>(s.disk_hits) : 1.0;
        char message[512];
        std::snprintf(message, sizeof(message),
                      "vector_map frames=%llu incomplete=%llu tiles_built=%llu "
                      "fetch_avg_ms=%.0f fetch_max_ms=%.0f build_avg_ms=%.1f "
                      "labels_avg_ms=%.1f disk_hits=%llu disk_avg_ms=%.1f disk_writes=%llu "
                      "disk_failures=%llu block_reads=%llu block_hits=%llu aborted=%llu "
                      "empty=%llu wanted=%zu prefetch=%zu gpu_tiles=%zu gpu_mb=%.1f "
                      "calls_avg=%.0f label_quads_avg=%.1f label_place_max_ms=%.1f "
                      "zoom=%.2f tile_zoom=%d..%d",
                      static_cast<unsigned long long>(s.frames),
                      static_cast<unsigned long long>(s.incomplete_frames),
                      static_cast<unsigned long long>(s.tiles_built),
                      s.fetch_ms_total / built, s.fetch_ms_max, s.build_ms_total / built,
                      s.labels_ms_total / std::max(1.0, static_cast<double>(s.tiles_built + s.disk_hits)),
                      static_cast<unsigned long long>(s.disk_hits), s.disk_ms_total / disk,
                      static_cast<unsigned long long>(s.disk_writes),
                      static_cast<unsigned long long>(s.disk_failures),
                      static_cast<unsigned long long>(s.block_reads),
                      static_cast<unsigned long long>(s.block_hits),
                      static_cast<unsigned long long>(s.aborted),
                      static_cast<unsigned long long>(s.empty_tiles), s.wanted_tiles,
                      s.prefetch_tiles, s.gpu_tiles, s.gpu_bytes / 1048576.0,
                      s.frames ? static_cast<double>(s.draw_calls) / s.frames : 0.0,
                      s.frames ? static_cast<double>(s.label_quads) / s.frames : 0.0,
                      s.label_place_ms_max, s.zoom, s.min_tile_zoom, s.tile_zoom);
        trace(message);
    }
    return wants_more;
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

int OsmAndEngine::gps(const OpenNaviBackendGpsV1& event)
{
    latest_gps_ = event;
    has_valid_position_ = valid_coordinates(event);
    gps_dirty_ = true;
    gps_times_[gps_time_next_] = std::chrono::steady_clock::now();
    gps_time_next_ = (gps_time_next_ + 1) % gps_times_.size();
    gps_time_count_ = std::min(gps_time_count_ + 1, gps_times_.size());
    if (!gps_state_observed_ || has_valid_position_ != last_logged_gps_valid_) {
        trace(has_valid_position_ ? "gps=position_received"
                                  : "gps=position_unavailable");
        gps_state_observed_ = true;
        last_logged_gps_valid_ = has_valid_position_;
    }
    return navigation_wakeup_ ? navigation_wakeup_->request() : 0;
}

int OsmAndEngine::pointer(const OpenNaviBackendPointerV2& event)
{
    if (phase_ != Phase::Ready || !view_)
        return -1;
    trace_pointer("received", event.action, event.x, event.y);
    view_->pointer(static_cast<int>(event.action), event.x, event.y,
                   static_cast<int>(event.contact_id));
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
        if (vector_map_) {
            vector_map_->release_gl(mode == ReleaseMode::ContextLost);
            vector_map_.reset();
        }
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
        phase_ = Phase::CoreReady;
    }
    renderer_.reset();
    obfs_.reset();
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
