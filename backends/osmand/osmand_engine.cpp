#include "engine.h"
#include "session.h"
#include "diagnostics.h"
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

#include <chrono>

namespace navicore_osmand {
namespace {

enum class Phase { Empty, Resources, CoreEntering, CoreReady,
                   RenderingEntering, Ready, Quarantined };

class OsmAndEngine final : public Engine {
public:
    bool initialize(const Config&, const MockNavBackendRenderTargetV1&,
                    std::shared_ptr<Wakeup>) override;
    FrameResult frame(const MockNavBackendRenderTargetV1&) override;
    int pointer(const MockNavBackendPointerV1&) override;
    ReleaseResult release(ReleaseMode) override;

private:
    void resize(const MockNavBackendRenderTargetV1&);
    void show_route(const ui::RouteAlternative&);
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
    ui::GlOverlayCompositor compositor_;
    bool overlay_enabled_ = true;
};

bool failure(const char* message)
{
    trace(message);
    return false;
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
    for (const auto& point : route.geometry) {
        points.push_back(OsmAnd::Utilities::convertLatLonTo31(
            OsmAnd::LatLon(point.latitude, point.longitude)));
        min_latitude = std::min(min_latitude, point.latitude);
        max_latitude = std::max(max_latitude, point.latitude);
        min_longitude = std::min(min_longitude, point.longitude);
        max_longitude = std::max(max_longitude, point.longitude);
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
    if (phase_ != Phase::Ready)
        return {FrameOutcome::Fault};
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
    if (target.width != width_ || target.height != height_)
        resize(target);
    if (!renderer_->update()) {
        trace("error=update");
        return {FrameOutcome::Fault};
    }
    if (!renderer_->prepareFrame())
        return {FrameOutcome::Pending, PendingStage::Prepare, !renderer_->isIdle()};
    // The bool API collapses StageResult::Wait and ::Fail. Preserve the live
    // resource-wait behavior, but let Session report sustained lack of progress.
    if (!renderer_->renderFrame())
        return {FrameOutcome::Pending, PendingStage::Render, true};
    view_->resize(target.width, target.height);
    if (overlay_enabled_ && view_->render(16u) &&
        !compositor_.present(view_->pixels(), view_->width(), view_->height())) {
        trace("error=ui_composite");
        compositor_.release(false);
        overlay_enabled_ = false;
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
        if (ensure_navigation_worker())
            navigation_worker_->submit_route(submitted_route_revision_,
                                              *navigation_->destination());
    }
    return {FrameOutcome::Presented, PendingStage::None, !renderer_->isIdle()};
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
