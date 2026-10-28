#include "engine.h"
#include "build_info.h"
#include "camera_controller.h"
#include "cluster_payload.h"
#include "dev_menu.h"
#include "gps_history.h"
#include "reroute_state.h"
#include "crash_reporter.h"
#include "session.h"
#include "diagnostics.h"
#include "frame_metrics.h"
#include "guidance_tracker.h"
#include "navigation_worker.h"
#include "voice_link.h"
#include "voice_prompts.h"
#include "ui/gl_overlay_compositor.h"
#include "ui/localization.h"
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

constexpr std::chrono::milliseconds kClusterPublishInterval{500};
constexpr std::chrono::milliseconds kDevMenuRefreshInterval{500};

class OsmAndEngine final : public Engine {
public:
    bool initialize(const Config&, const OpenNaviBackendRenderTargetV1&,
                    std::shared_ptr<Wakeup>) override;
    FrameResult frame(const OpenNaviBackendRenderTargetV1&) override;
    int gps(const OpenNaviBackendGpsV1&) override;
    void visibility(const OpenNaviBackendVisibilityV2& event) override;
    // HMI split screen (Media screen block) from the latest visibility.
    void apply_split();
    void screen(const OpenNaviBackendScreenV1& event) override;
    void vehicle(const OpenNaviBackendVehicleV2& event) override;
    void internationalization(const OpenNaviBackendInternationalizationV1& event) override;
    void cluster_display(const OpenNaviBackendClusterDisplayV1& event) override;
    void driver_distraction(const OpenNaviBackendDriverDistractionV1& event) override;
    void set_outbound(const OpenNaviBackendServicesV2* services) override { outbound_ = services; }
    void set_guidance_outbound(const OpenNaviBackendServicesV3* services) override
    {
        guidance_outbound_ = services;
    }
    void guidance_stop(const OpenNaviBackendGuidanceStopV1& event) override;
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
    void speak_guidance(const ui::GuidanceState& state);
    void reset_voice();
    // Steps of frame().
    void drain_navigation_results();
    void end_guidance();
    bool sync_navigation_to_screen();
    void submit_navigation_requests();
    static void record_vector_timing(const navicore_vector::VectorFrameTiming& vt,
                                     FrameMeasurement& m);
    bool ensure_navigation_worker();
    // Steps of apply_camera().
    void apply_map_commands();
    void update_maneuver_camera(ui::Screen screen);
    bool pose_differs_from_applied(const CameraController::Pose& pose) const;
    void apply_pose(const CameraController::Pose& pose);
    void update_vehicle_marker(const CameraController::Vehicle& vehicle,
                               const CameraController::Pose& pose);
    // Steps of initialize().
    void configure_language();
    void set_vector_cache_identity();
    void log_load_bases();
    bool init_vector_map(const std::shared_ptr<Wakeup>& wakeup);
    bool defer_navigation_worker(std::shared_ptr<Wakeup> wakeup);
    // Where the car is, for search, routes and the map: the position the
    // car's GPS service reports (its own last known one included), else the
    // last one seen since start, else Kyiv.
    ui::GeoPoint current_location() const;
    std::chrono::milliseconds search_debounce() const;
    void advance_benchmark_autostart();
    // Drains UI map commands, advances the car camera and applies the pose to
    // the renderer. Returns true while the camera still needs frames.
    bool apply_camera();
    Phase phase_ = Phase::Empty;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources_;
    std::shared_ptr<OsmAnd::ObfsCollection> obfs_;          // regional maps
    std::shared_ptr<OsmAnd::ObfsCollection> basemap_obfs_;  // world basemap, tiles <= z10
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
    std::vector<std::string> navigation_maps_;
    std::string navigation_routing_xml_;
    bool worker_creation_failed_ = false;
    std::uint64_t submitted_search_revision_ = 0;
    std::uint64_t observed_search_revision_ = 0;
    std::uint64_t submitted_route_revision_ = 0;
    std::chrono::steady_clock::time_point search_revision_observed_at_{};
    bool search_revision_observed_ = false;
    std::chrono::steady_clock::time_point ready_at_{};  // rendering came up
    GuidanceTracker guidance_tracker_;
    // Spoken guidance: the planner decides what to say, the client hands it to navi_voiced.
    PromptPlanner prompt_planner_;
    std::unique_ptr<VoiceClient> voice_;
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
    template <typename T> using Raw = RawInput<T>;
    Raw<OpenNaviBackendVisibilityV2> visibility_in_;
    Raw<OpenNaviBackendScreenV1> screen_in_;
    Raw<OpenNaviBackendVehicleV2> vehicle_in_;
    Raw<OpenNaviBackendInternationalizationV1> language_in_;
    Raw<OpenNaviBackendClusterDisplayV1> cluster_display_in_;
    Raw<OpenNaviBackendDriverDistractionV1> distraction_in_;
    // Route guidance on/off for the platform (backend V6), published when
    // it changes; nv_navigation polls it about once a second.
    const OpenNaviBackendServicesV3* guidance_outbound_ = nullptr;
    void publish_guidance_state();
    bool guidance_state_published_ = false;
    bool guidance_state_last_ = false;
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
    DevMenuSnapshot dev_menu_snapshot(std::chrono::steady_clock::time_point now) const;
    bool step_cache_clear();
    std::chrono::steady_clock::time_point dev_updated_at_{};
    std::array<float, 240> frame_intervals_ms_{};
    std::size_t frame_interval_count_ = 0, frame_interval_next_ = 0;
    std::chrono::steady_clock::time_point last_presented_at_{};
    GpsHistory gps_history_;
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
    bool night_ = true;  // the dark map palette is the default
    bool overview_pending_ = false;
    bool route_shown_ = false;  // the vector map draws a route
    std::size_t shown_route_index_ = 0;  // which of the routes it draws
    double overview_box_[4] = {0.0, 0.0, 0.0, 0.0};
    OpenNaviBackendGpsV1 latest_gps_{};
    bool has_valid_position_ = false;
    ui::GeoPoint last_location_{50.4501, 30.5234};
    bool gps_state_observed_ = false;
    bool last_logged_gps_valid_ = false;
    bool gps_dirty_ = false;
    bool guidance_active_ = false;
    bool simulation_enabled_ = false;
    bool simulation_running_ = false;
    bool simulation_arrival_reported_ = false;
    // Off-route recalculation.
    RerouteState reroute_;
    HeadingMemory heading_memory_;
    BenchDetour bench_detour_;
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

// The parked record: "heading latitude longitude" on one line, written to
// "<path>.tmp" and renamed over the target, as the recents are.
bool load_parked(const std::string& path, HeadingMemory::Parked& parked)
{
    std::FILE* input = std::fopen(path.c_str(), "r");
    if (!input) return false;
    const bool ok = std::fscanf(input, "%lf %lf %lf", &parked.heading_degrees, &parked.latitude,
                                &parked.longitude) == 3;
    std::fclose(input);
    return ok && std::isfinite(parked.heading_degrees) && std::fabs(parked.latitude) <= 90.0 &&
           std::fabs(parked.longitude) <= 180.0;
}

bool save_parked(const std::string& path, const HeadingMemory::Parked& parked)
{
    const std::string temporary = path + ".tmp";
    std::FILE* output = std::fopen(temporary.c_str(), "w");
    if (!output) return false;
    bool ok = std::fprintf(output, "%.1f %.7f %.7f\n", parked.heading_degrees, parked.latitude,
                           parked.longitude) > 0;
    ok = std::fflush(output) == 0 && ok;
    ok = std::fclose(output) == 0 && ok;
    if (!ok) {
        std::remove(temporary.c_str());
        return false;
    }
    return std::rename(temporary.c_str(), path.c_str()) == 0;
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

void OsmAndEngine::configure_language()
{
    const auto language = ui::loc::detect_language(
        std::getenv("OPEN_NAVICORE_OSMAND_LANGUAGE"), config_.language_marker,
        config_.hmi_language_file);
    ui::loc::set_language(language.language);
    trace(("language=" + std::string(ui::loc::language_code(language.language)) +
           " source=" + language.source).c_str());
}

// Anything that changes built geometry or labels must change this.
void OsmAndEngine::set_vector_cache_identity()
{
    vector_cache_dir_ = config_.vector_cache;
    vector_cache_identity_.clear();
    for (const std::string& file : obf_files(config_.map)) {
        struct stat map_info {};
        const bool map_known = ::stat(file.c_str(), &map_info) == 0;
        vector_cache_identity_ += file + "|" +
            std::to_string(map_known ? map_info.st_size : 0) + "|" +
            std::to_string(map_known ? map_info.st_mtime : 0) + ";";
    }
    // Labels are baked into the tiles, so the label languages are part of the identity.
    vector_cache_identity_ += "|labels=";
    for (const std::string& language : ui::loc::name_languages(ui::loc::language()))
        vector_cache_identity_ += language + ",";
    vector_cache_identity_ += "|vector-style-5";
}

// Load bases let a slog fault 'ip' be mapped to a library and symbol.
void OsmAndEngine::log_load_bases()
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

bool OsmAndEngine::init_vector_map(const std::shared_ptr<Wakeup>& wakeup)
{
    try {
        vector_map_.reset(new navicore_vector::VectorMap(
            obfs_, resources_, 2, [wakeup]() { (void)wakeup->request(); },
            basemap_obfs_->getObfFiles().isEmpty() ? nullptr : basemap_obfs_,
            ui::loc::name_languages(ui::loc::language())));
        vector_map_->set_upload_thread(true);
        if (!vector_map_->init_gl())
            throw std::runtime_error("vector_map gl");
        if (!vector_cache_dir_.empty())
            vector_map_->set_disk_cache(vector_cache_dir_, vector_cache_identity_,
                                        200ull << 20);
        vector_map_->set_night(night_);
        trace(vector_map_->upload_thread_active() ? "vector_upload=thread"
                                                  : "vector_upload=render_thread");
        trace("init=vector_map");
    } catch (...) {
        if (vector_map_)
            vector_map_->release_gl(false);
        vector_map_.reset();
        return failure("error=vector_map_init");
    }
    return true;
}

bool OsmAndEngine::defer_navigation_worker(std::shared_ptr<Wakeup> wakeup)
{
    bool resource_ok = false;
    const QByteArray routing_xml = resources_->getResource(
        QStringLiteral("routing/routing.xml"), &resource_ok);
    if (!resource_ok || routing_xml.isEmpty())
        return failure("error=routing_xml");
    trace("init=routing_resource");
    navigation_wakeup_ = std::move(wakeup);
    navigation_maps_.clear();
    for (const std::string& file : obf_files(config_.map))
        if (!is_basemap(file))
            navigation_maps_.push_back(file);
    navigation_routing_xml_.assign(
        routing_xml.constData(), static_cast<std::size_t>(routing_xml.size()));
    trace("init=worker_deferred");
    return true;
}

bool OsmAndEngine::initialize(const Config& config,
                             const OpenNaviBackendRenderTargetV1& target,
                             std::shared_ptr<Wakeup> wakeup)
{
    config_ = config;
    configure_language();
    // Where the car was parked: the map opens there, and a first route from the
    // spot starts the way the car points.
    {
        HeadingMemory::Parked parked;
        if (load_parked(config.parked, parked)) {
            heading_memory_.restore(parked);
            last_location_ = {parked.latitude, parked.longitude};
            trace("parked=restored");
        }
    }
    simulation_enabled_ = config.simulate_route;
    bench_detour_.enable(config.simulate_detour);
    if (config.voice) {
        voice_.reset(new VoiceClient(config.voice_queue));
        trace(("voice=enabled queue=" + config.voice_queue).c_str());
    }
    if (bench_detour_.enabled())
        trace("simulation=detour_enabled");
    autostart_ = config.benchmark_autostart && config.simulate_route
                     ? Autostart::Search : Autostart::Off;
    if (autostart_ != Autostart::Off)
        trace("benchmark_autostart=enabled");
    if (!config.vector_cache.empty())
        set_vector_cache_identity();
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
    log_load_bases();

    obfs_ = std::make_shared<OsmAnd::ObfsCollection>();
    basemap_obfs_ = std::make_shared<OsmAnd::ObfsCollection>();
    for (const std::string& file : obf_files(config.map))
        (is_basemap(file) ? basemap_obfs_ : obfs_)
            ->addFile(QString::fromLocal8Bit(file.c_str()));
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
        OsmAnd::LatLon(last_location_.latitude, last_location_.longitude)));
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
    ready_at_ = std::chrono::steady_clock::now();
    trace("init=rendering");
    if (!init_vector_map(wakeup))
        return false;
    navigation_ = std::unique_ptr<ui::NavigationModel>(new ui::NavigationModel());
    recents_store_.reset(new ui::RecentsStore(config.recents));
    navigation_->set_recents(recents_store_->load());
    saved_recents_revision_ = navigation_->recents_revision();
    view_ = std::unique_ptr<ui::NavigationView>(
        new ui::NavigationView(*navigation_, target.width, target.height));
    trace("init=view");
    apply_split();
    return defer_navigation_worker(std::move(wakeup));
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
    reset_voice();
    reroute_.clear_off_route();
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

// Spoken guidance runs in navi_voiced; here only the decision of what to say and a
// non-blocking hand-over (voice_link.h). A route change drops what is queued.
void OsmAndEngine::speak_guidance(const ui::GuidanceState& state)
{
    if (!voice_)
        return;
    const ui::loc::Language language = ui::loc::language();
    for (const SpokenPrompt& prompt : prompt_planner_.update(state, language))
        voice_->send(prompt, language);
}

void OsmAndEngine::reset_voice()
{
    prompt_planner_.reset();
    if (voice_)
        voice_->cancel();
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
        // Bench detour: drive beside the route until a recalculated one arrives.
        if (bench_detour_.advance(simulation_progress_metres_, route.distance_metres, now))
            trace("simulation=detour_started");
        if (bench_detour_.driving())
            position.longitude += BenchDetour::kOffsetMetres /
                (111320.0 * std::cos(position.latitude * 3.14159265358979323846 / 180.0));
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
    speak_guidance(state);
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
// stretches stay below that), or driving against it for 5 s, asks for a new
// route from the position to the same destination, starting in the driving
// direction. One request at a time; a failed one waits 10 s.
void OsmAndEngine::check_off_route(const ui::GuidanceState& state,
                                   const ui::GeoPoint& position)
{
    const auto now = std::chrono::steady_clock::now();
    // Moving now: a heading from the last two seconds.
    const auto heading = heading_memory_.heading(now, std::chrono::seconds(2));
    std::optional<double> against;
    if (heading && state.route_bearing_known)
        against = *heading - state.route_bearing_degrees;
    const bool off_route =
        reroute_.off_route_long_enough(state.arrived, state.off_route_metres, now);
    const bool wrong_way = reroute_.wrong_way_long_enough(state.arrived, against, now);
    if (!off_route && !wrong_way)
        return;
    if (wrong_way)
        trace_result("reroute=wrong_way", navigation_->route_revision(),
                     static_cast<std::size_t>(std::fabs(std::remainder(*against, 360.0))));
    if (navigation_->rerouting() || reroute_.retry_blocked(now) ||
        !navigation_->destination() || !ensure_navigation_worker())
        return;
    const std::uint64_t revision = navigation_->request_reroute();
    if (!revision)
        return;
    submitted_route_revision_ = revision;
    trace_result("reroute=submit", revision,
                 static_cast<std::size_t>(state.off_route_metres));
    navigation_worker_->submit_route(revision, *navigation_->destination(), position, false,
                                     heading_memory_.start_heading(now, position.latitude,
                                                                   position.longitude));
}

void OsmAndEngine::apply_map_commands()
{
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
}

// The coming turn shapes the 3D camera (steeper, a little closer).
void OsmAndEngine::update_maneuver_camera(ui::Screen screen)
{
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

bool OsmAndEngine::pose_differs_from_applied(const CameraController::Pose& pose) const
{
    return !pose_applied_ ||
        std::fabs(pose.latitude - applied_pose_.latitude) > 1e-8 ||
        std::fabs(pose.longitude - applied_pose_.longitude) > 1e-8 ||
        pose.anchor_x != applied_pose_.anchor_x ||
        pose.anchor_y != applied_pose_.anchor_y ||
        std::fabs(pose.zoom - applied_pose_.zoom) > 0.01f ||
        std::fabs(static_cast<float>(CameraController::shortest_angle_delta(
            applied_pose_.azimuth_degrees, pose.azimuth_degrees))) > 0.25f ||
        pose.elevation_degrees != applied_pose_.elevation_degrees;
}

void OsmAndEngine::apply_pose(const CameraController::Pose& pose)
{
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

void OsmAndEngine::update_vehicle_marker(const CameraController::Vehicle& vehicle,
                                         const CameraController::Pose& pose)
{
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
    {
        // Also while searching: a destination picked there starts this way.
        const auto moving = motion_.predict(now_s);
        const ui::GeoPoint here = current_location();
        heading_memory_.observe(moving.valid, moving.speed_mps, moving.heading_degrees, now,
                                has_valid_position_ ? &here.latitude : nullptr,
                                has_valid_position_ ? &here.longitude : nullptr);
        HeadingMemory::Parked parked;
        if (heading_memory_.take_save(parked))
            trace(save_parked(config_.parked, parked) ? "parked=saved" : "error=parked_save");
    }
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

    apply_map_commands();
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

    update_maneuver_camera(screen);
    const auto vehicle = motion_.predict(now_s);
    if (!vehicle.valid) {
        // No fix: the map rests where the car is believed to be.
        const auto here = current_location();
        camera_.set_resting_location(here.latitude, here.longitude);
    }
    const auto pose = camera_.update(vehicle, dt);
    vector_view_.tile_zoom_limit = pose.tile_zoom;
    const bool changed = pose_differs_from_applied(pose);
    if (changed)
        apply_pose(pose);

    // Vehicle marker: at the anchor while following, projected otherwise.
    update_vehicle_marker(vehicle, pose);
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

void OsmAndEngine::visibility(const OpenNaviBackendVisibilityV2& event)
{
    visibility_in_ = {event, true};
    trace_result("input=visibility", event.visibility, event.interactivity);
    trace_result("input=visibility_area",
                 static_cast<std::uint64_t>(static_cast<std::uint32_t>(event.area_x)) << 32 |
                     static_cast<std::uint32_t>(event.area_y),
                 static_cast<std::size_t>(event.area_width) << 16 | event.area_height);
    apply_split();
}

void OsmAndEngine::apply_split()
{
    if (!view_ || !visibility_in_.has)
        return;
    const auto& event = visibility_in_.value;
    const bool split = event.visibility == OPEN_NAVI_BACKEND_HMI_VISIBLE_SPLIT_SCREEN &&
                       event.area_width > 0 && event.area_height > 0;
    ui::NavigationView::MapArea area;
    area.x = event.area_x;
    area.y = event.area_y;
    area.width = static_cast<int>(std::min<std::uint32_t>(event.area_width, INT32_MAX));
    area.height = static_cast<int>(std::min<std::uint32_t>(event.area_height, INT32_MAX));
    if (view_->set_split_area(split, area))
        trace_result("ui=split_layout", view_->split() ? 1u : 0u, 0u);
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

// The platform asks to end guidance (nv_navigation requestGuidanceStop, e.g.
// when a phone projection takes navigation focus): the same transition as
// the End button, published at once.
void OsmAndEngine::guidance_stop(const OpenNaviBackendGuidanceStopV1& event)
{
    const bool ended = view_ && view_->end_guidance();
    trace_result("input=guidance_stop", event.request_handle, ended ? 1u : 0u);
    publish_guidance_state();
}

void OsmAndEngine::publish_guidance_state()
{
    if (!guidance_outbound_ || !guidance_outbound_->publish_guidance_state)
        return;
    const bool guiding = navigation_ && navigation_->screen() == ui::Screen::Guidance;
    if (guidance_state_published_ && guiding == guidance_state_last_)
        return;
    OpenNaviBackendGuidanceStateV1 state{};
    state.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
    state.struct_size = sizeof(state);
    state.guiding = guiding ? OPEN_NAVI_BACKEND_GUIDANCE_ACTIVE : OPEN_NAVI_BACKEND_GUIDANCE_IDLE;
    const uint32_t status = guidance_outbound_->publish_guidance_state(
        guidance_outbound_->v2.v1.runtime_context, &state);
    guidance_state_published_ = true;
    guidance_state_last_ = guiding;
    trace_result("guidance=publish_state", state.guiding, status);
}

// Instrument cluster: published on change, at most twice a second
// (payload rules in cluster_payload.cpp).
void OsmAndEngine::publish_cluster()
{
    if (!outbound_ || !outbound_->publish_cluster)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (cluster_published_ && now - cluster_published_at_ < kClusterPublishInterval)
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
    // A new road text alone does not redraw the cluster: send it once with
    // a neighbouring symbol; the next publication (500 ms on) restores the
    // real symbol, which redraws again with the new text.
    if (cluster_published_ && apply_text_redraw(payload, cluster_last_))
        trace_result("cluster=text_redraw", payload.cluster.route_guidance_symbol,
                     cluster_last_.cluster.route_guidance_symbol);
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

DevMenuSnapshot OsmAndEngine::dev_menu_snapshot(
    std::chrono::steady_clock::time_point now) const
{
    DevMenuSnapshot snapshot;
    snapshot.now = now;
    snapshot.gps = latest_gps_;
    snapshot.has_valid_position = has_valid_position_;
    snapshot.gps_history = gps_history_;
    snapshot.vehicle = motion_.predict(
        std::chrono::duration<double>(now - clock_origin_).count());
    snapshot.camera_mode = camera_.mode();
    snapshot.zoom = applied_pose_.zoom;
    snapshot.elevation_degrees = applied_pose_.elevation_degrees;
    const auto& guidance = navigation_->guidance();
    snapshot.guiding = navigation_->screen() == ui::Screen::Guidance;
    snapshot.off_route_metres = guidance.off_route_metres;
    snapshot.recalculating = guidance.recalculating;
    snapshot.map_path = config_.map;
    snapshot.vector_cache_dir = config_.vector_cache;
    if (vector_map_) {
        const auto stats = vector_map_->stats();
        snapshot.has_vector_stats = true;
        snapshot.vector_stats = {stats.tiles_built, stats.disk_hits, stats.gpu_tiles,
                                 stats.gpu_bytes, stats.wanted_tiles,
                                 stats.prefetch_tiles, stats.tile_zoom};
    }
    snapshot.frame_intervals_ms.assign(
        frame_intervals_ms_.begin(),
        frame_intervals_ms_.begin() + static_cast<std::ptrdiff_t>(frame_interval_count_));
    snapshot.vehicle_in = vehicle_in_;
    snapshot.visibility_in = visibility_in_;
    snapshot.screen_in = screen_in_;
    snapshot.language_in = language_in_;
    snapshot.distraction_in = distraction_in_;
    snapshot.cluster_display_in = cluster_display_in_;
    snapshot.outbound = outbound_ != nullptr;
    snapshot.cluster_publishes = cluster_publishes_;
    snapshot.cluster_last_status = cluster_last_status_;
    snapshot.backend_git = build_git();
    snapshot.backend_date = build_date();
    snapshot.night = night_;
    snapshot.tilted = camera_.tilted();
    snapshot.cache_clearing = !cache_clear_.empty();
    snapshot.cache_clear_done = cache_clear_done_;
    snapshot.cache_cleared_files = cache_cleared_files_;
    return snapshot;
}

void OsmAndEngine::update_dev_menu()
{
    const bool clearing = step_cache_clear();
    if (!navigation_->dev_menu_open())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (!clearing && dev_updated_at_ != std::chrono::steady_clock::time_point{} &&
        now - dev_updated_at_ < kDevMenuRefreshInterval)
        return;
    dev_updated_at_ = now;
    const DevMenuSnapshot snapshot = dev_menu_snapshot(now);
    (void)view_->set_dev_info(build_dev_sections(snapshot), build_dev_toggles(snapshot));

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
    const std::string distance = ui::loc::format_distance(metres, ui::loc::Rounding::Tens);
    return text.empty() ? distance : text + " · " + distance;
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
    // "amenity_fuel" -> "Fuel" (or "АЗС"): the category in the UI language; a
    // place name shows its type ("city" -> "City").
    const std::string category = ui::loc::poi_category(is_place ? pick.place : pick.icon);
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
    pin.title = ui::loc::tr(ui::loc::Str::DroppedPin);
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

ui::GeoPoint OsmAndEngine::current_location() const
{
    if (has_valid_position_)
        return {static_cast<double>(latest_gps_.latitude_e7) / 1e7,
                static_cast<double>(latest_gps_.longitude_e7) / 1e7};
    return last_location_;
}


// Quiet time after the last key before searching: the index answers in
// milliseconds, so results follow the typing; OsmAnd's search takes seconds.
std::chrono::milliseconds OsmAndEngine::search_debounce() const
{
    const bool fast = navigation_worker_ && navigation_worker_->has_search_index();
    return std::chrono::milliseconds(fast ? 200 : 500);
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
            new NavigationWorker(obfs_, navigation_wakeup_, navigation_maps_,
                                 navigation_routing_xml_, config_.search_index,
                                 config_.ch_sidecar, !config_.osmand_turns));
        trace("worker=ready");
        return true;
    } catch (...) {
        worker_creation_failed_ = true;
        trace("error=worker_creation_exception");
        return false;
    }
}

void OsmAndEngine::drain_navigation_results()
{
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
                    shown_route_index_ = 0;
                    bench_detour_.route_recovered();
                } else {
                    // Keep the old route; try again a little later.
                    trace("reroute=failed");
                    reroute_.request_failed(std::chrono::steady_clock::now());
                }
                gps_dirty_ = true;
            }
        } else {
            const bool accepted = view_->apply_routes(
                route_result.revision, std::move(route_result.routes));
            if (accepted && !navigation_->routes().empty()) {
                show_route(navigation_->routes().front());
                shown_route_index_ = 0;
            }
        }
    }
}

// Guidance is over: stop voice and simulation, keep real GPS following.
void OsmAndEngine::end_guidance()
{
    guidance_active_ = false;
    guidance_tracker_.reset();
    reset_voice();
    simulation_running_ = false;
    // Simulated motion ends with guidance; real GPS keeps following.
    if (!has_valid_position_) motion_.reset();
    pose_applied_ = false;
}

// Brings guidance, the drawn route and the map in line with the screen the
// UI is on. Returns true when the next frame must redraw.
bool OsmAndEngine::sync_navigation_to_screen()
{
    bool changed = false;
    if (navigation_->screen() == ui::Screen::Guidance && !guidance_active_)
        changed = update_guidance() || changed;
    else if (navigation_->screen() != ui::Screen::Guidance &&
             guidance_active_) {
        end_guidance();
        changed = true;
    }
    // The route is drawn in Route preview and Guidance only: End, arrival,
    // preview Back and closing Search clear it.
    if (route_shown_ && navigation_->screen() != ui::Screen::RoutePreview &&
        navigation_->screen() != ui::Screen::Guidance)
        changed = clear_route() || changed;
    // Route preview: the map draws the selected route card's route.
    if (route_shown_ && navigation_->screen() == ui::Screen::RoutePreview &&
        navigation_->selected_route() != shown_route_index_ &&
        navigation_->selected_route() < navigation_->routes().size()) {
        shown_route_index_ = navigation_->selected_route();
        show_route(navigation_->routes()[shown_route_index_], false);
        changed = true;
    }
    return changed;
}

// The worker opens the routing maps in the background (about 10 s for all of
// Ukraine), so it starts before the first route is asked for: when a place
// card or another screen opens, or a while after start-up, out of its way.
constexpr std::chrono::seconds kWorkerWarmupDelay{30};

void OsmAndEngine::submit_navigation_requests()
{
    if (!navigation_worker_ && !worker_creation_failed_ &&
        (navigation_->screen() != ui::Screen::FreeDrive || navigation_->place_card() ||
         std::chrono::steady_clock::now() - ready_at_ >= kWorkerWarmupDelay))
        (void)ensure_navigation_worker();
    if (navigation_->screen() == ui::Screen::Search) {
        const auto revision = navigation_->search_revision();
        const auto now = std::chrono::steady_clock::now();
        if (!search_revision_observed_ || revision != observed_search_revision_) {
            observed_search_revision_ = revision;
            search_revision_observed_at_ = now;
            search_revision_observed_ = true;
        } else if (revision != submitted_search_revision_ && navigation_->search_wanted() &&
                   now - search_revision_observed_at_ >= search_debounce()) {
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
                std::vector<ui::GeoPoint> recent;
                for (const auto& r : navigation_->recents())
                    recent.push_back({r.latitude, r.longitude});
                navigation_worker_->submit_search(submitted_search_revision_,
                                                   navigation_->query(), current_location(),
                                                   std::move(recent));
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
            const ui::GeoPoint origin = current_location();
            navigation_worker_->submit_route(submitted_route_revision_,
                                              *navigation_->destination(), origin, true,
                                              heading_memory_.start_heading(
                                                  std::chrono::steady_clock::now(),
                                                  origin.latitude, origin.longitude));
        }
    }
}

void OsmAndEngine::record_vector_timing(const navicore_vector::VectorFrameTiming& vt,
                                        FrameMeasurement& m)
{
        m.vec_take_us = vt.take_us;
        m.vec_uploads = vt.uploads;
        m.vec_route_us = vt.route_us;
        m.vec_want_us = vt.want_us;
        m.vec_draw_us = vt.draw_us;
        m.vec_labels_us = vt.labels_us;
        m.vec_evict_us = vt.evict_us;
        m.vec_upload_max_us = vt.upload_max_us;
        m.vec_atlas_us = vt.atlas_us;
        m.vec_label_buffers_us = vt.label_buffers_us;
        m.vec_label_collect_us = vt.label_collect_us;
        m.vec_label_candidates = vt.label_candidates;
        m.vec_label_tests = vt.label_tests;
        m.vec_label_reused = vt.label_reused;
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
    drain_navigation_results();
    measurement.results_us = elapsed_us(frame_started);
    auto logic_started = Clock::now();
    bool guidance_changed = false;
    if (navigation_->screen() == ui::Screen::Guidance &&
        (!guidance_active_ || gps_dirty_ ||
         (simulation_enabled_ && simulation_running_))) {
        guidance_changed = update_guidance();
    } else if (navigation_->screen() != ui::Screen::Guidance &&
               guidance_active_) {
        sync_vector_guidance(nullptr);
        end_guidance();
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
    // OsmAnd's renderer has no layers; with the vector map (which clears and
    // covers the frame) its renderFrame() draws nothing visible, and it was
    // seen blocked 20-38 ms in the driver. update()/prepareFrame() above
    // still keep its camera current.
    const bool map_rendered = vector_map_ || renderer_->renderFrame();
    measurement.map_render_us = elapsed_us(stage_started);
    if (!map_rendered)
        return finish({FrameOutcome::Pending, PendingStage::Render, true},
                      MeasuredFrameOutcome::RenderPending);
    bool vector_wants_frames = false;
    if (vector_map_) {
        stage_started = Clock::now();
        vector_wants_frames = render_vector_map(target);
        measurement.vector_us = elapsed_us(stage_started);
        record_vector_timing(vector_map_->frame_timing(), measurement);
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
    guidance_changed = sync_navigation_to_screen() || guidance_changed;
    submit_navigation_requests();
    advance_benchmark_autostart();
    publish_guidance_state();
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
        char message[640];
        std::snprintf(message, sizeof(message),
                      "vector_map frames=%llu incomplete=%llu tiles_built=%llu "
                      "fetch_avg_ms=%.0f fetch_max_ms=%.0f build_avg_ms=%.1f "
                      "labels_avg_ms=%.1f disk_hits=%llu disk_avg_ms=%.1f disk_writes=%llu "
                      "disk_failures=%llu disk_pruned=%llu block_reads=%llu block_hits=%llu "
                      "aborted=%llu "
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
                      static_cast<unsigned long long>(s.disk_pruned),
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
    if (has_valid_position_) last_location_ = current_location();
    gps_dirty_ = true;
    gps_history_.record(std::chrono::steady_clock::now());
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
        // Guidance ends with this engine: tell the platform while the
        // services are still current (stale after stop: ignored).
        if (guidance_state_published_ && guidance_state_last_ && guidance_outbound_ &&
            guidance_outbound_->publish_guidance_state) {
            OpenNaviBackendGuidanceStateV1 state{};
            state.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
            state.struct_size = sizeof(state);
            state.guiding = OPEN_NAVI_BACKEND_GUIDANCE_IDLE;
            (void)guidance_outbound_->publish_guidance_state(
                guidance_outbound_->v2.v1.runtime_context, &state);
        }
        guidance_state_published_ = false;
        guidance_state_last_ = false;
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
        navigation_maps_.clear();
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
        reset_voice();
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
    basemap_obfs_.reset();
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
