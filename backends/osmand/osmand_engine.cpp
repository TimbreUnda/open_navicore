#include "engine.h"
#include "session.h"
#include "diagnostics.h"

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

#include <QByteArray>
#include <QString>

namespace navicore_osmand {
namespace {

enum class Phase { Empty, Resources, CoreEntering, CoreReady,
                   RenderingEntering, Ready, Quarantined };

class OsmAndEngine final : public Engine {
public:
    bool initialize(const Config&, const MockNavBackendRenderTargetV1&,
                    std::shared_ptr<Wakeup>) override;
    FrameResult frame(const MockNavBackendRenderTargetV1&) override;
    ReleaseResult release(ReleaseMode) override;

private:
    void resize(const MockNavBackendRenderTargetV1&);
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
};

bool failure(const char* message)
{
    trace(message);
    return false;
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

    // InitializeCore starts threads/mutates globals before its SKIA/ICU
    // failure returns. ReleaseCore has no partial-initialization contract.
    phase_ = Phase::CoreEntering;
    if (OsmAnd::InitializeCore(resources_, config.fonts.c_str()) == 0)
        return failure("error=initialize_core_partial");
    phase_ = Phase::CoreReady;

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

    obfs_ = std::make_shared<OsmAnd::ObfsCollection>();
    obfs_->addFile(QString::fromLocal8Bit(config.map.c_str()));
    if (obfs_->getObfFiles().isEmpty())
        return failure("error=map");

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
    renderer_->addSymbolsProvider(symbols_);
    renderer_->setMapLayerProvider(0, raster_);

    // GPU API/resources/stages may initialize before _isRenderingInitialized
    // becomes true. releaseRendering refuses to clean that partial state.
    phase_ = Phase::RenderingEntering;
    if (!renderer_->initializeRendering(true))
        return failure("error=initialize_rendering_partial");
    phase_ = Phase::Ready;
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

FrameResult OsmAndEngine::frame(const MockNavBackendRenderTargetV1& target)
{
    if (phase_ != Phase::Ready)
        return {FrameOutcome::Fault};
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
    return {FrameOutcome::Presented, PendingStage::None, !renderer_->isIdle()};
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
        if (!renderer_->releaseRendering(mode == ReleaseMode::ContextLost))
            return ReleaseResult::Quarantined;
        phase_ = Phase::CoreReady;
    }
    renderer_.reset();
    raster_.reset();
    symbols_.reset();
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
