#include <open_navicore/backend_plugin.h>

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

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <unistd.h>

namespace {

constexpr const char* kDefaultResources =
    "/datastore/navicore-backend/libOsmAndCore_ResourcesBundle_shared.so";
constexpr const char* kDefaultMap =
    "/datastore/navicore-backend/maps/kyiv.obf";
constexpr const char* kDefaultFonts =
    "/datastore/navicore-backend/fonts";
constexpr const char* kTracePath = "/tmp/open-navicore-osmand.log";
constexpr double kKyivLatitude = 50.4501;
constexpr double kKyivLongitude = 30.5234;
constexpr float kInitialZoom = 13.5f;

const char* configured_path(const char* variable, const char* fallback)
{
    const char* value = std::getenv(variable);
    return value != nullptr && value[0] != '\0' ? value : fallback;
}

void trace(const char* message)
{
    const int descriptor = open(kTracePath, O_WRONLY | O_CREAT | O_APPEND,
                                0600);
    if (descriptor < 0)
        return;
    const size_t length = std::strlen(message);
    (void)write(descriptor, message, length);
    (void)write(descriptor, "\n", 1u);
    (void)close(descriptor);
}

struct OsmAndBackend {
    MockNavBackendServicesV1 services{};
    bool started = false;
    bool core_initialized = false;
    bool rendering_initialized = false;
    bool waiting_for_frame_traced = false;
    bool waiting_for_resources_traced = false;
    bool first_frame_rendered = false;
    uint32_t width = 0;
    uint32_t height = 0;

    std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources;
    std::shared_ptr<OsmAnd::MapStylesCollection> styles;
    std::shared_ptr<OsmAnd::ObfsCollection> obfs;
    std::shared_ptr<OsmAnd::MapPresentationEnvironment> presentation;
    std::shared_ptr<OsmAnd::MapPrimitiviser> primitiviser;
    std::shared_ptr<OsmAnd::ObfMapObjectsProvider> map_objects;
    std::shared_ptr<OsmAnd::MapPrimitivesProvider> primitives;
    std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider> symbols;
    std::shared_ptr<OsmAnd::MapRasterLayerProvider_Software> raster;
    std::shared_ptr<OsmAnd::IMapRenderer> renderer;

    void request_frame() const
    {
        if (services.request_frame != nullptr)
            (void)services.request_frame(services.runtime_context);
    }

    void release_surface()
    {
        if (renderer && rendering_initialized)
            (void)renderer->releaseRendering(false);
        rendering_initialized = false;
        waiting_for_frame_traced = false;
        waiting_for_resources_traced = false;
        first_frame_rendered = false;
        renderer.reset();
        raster.reset();
        symbols.reset();
        primitives.reset();
        map_objects.reset();
        primitiviser.reset();
        presentation.reset();
        obfs.reset();
        styles.reset();
        if (core_initialized) {
            OsmAnd::ReleaseCore();
            core_initialized = false;
        }
        resources.reset();
        trace("surface_released");
    }
};

OsmAndBackend g_backend;

int backend_start(void* context, const MockNavBackendServicesV1* services)
{
    auto* backend = static_cast<OsmAndBackend*>(context);
    if (backend == nullptr || services == nullptr ||
        services->request_frame == nullptr)
        return -1;
    backend->services = *services;
    backend->started = true;
    trace("start");
    return 0;
}

void backend_stop(void* context)
{
    auto* backend = static_cast<OsmAndBackend*>(context);
    if (backend == nullptr)
        return;
    if (backend->renderer || backend->core_initialized)
        backend->release_surface();
    backend->started = false;
    backend->services = {};
    trace("stop");
}

int backend_surface_created(void* context,
                            const MockNavBackendRenderTargetV1* target)
{
    auto* backend = static_cast<OsmAndBackend*>(context);
    if (backend == nullptr || target == nullptr || !backend->started ||
        target->width == 0u || target->height == 0u)
        return -1;

    try {
        backend->width = target->width;
        backend->height = target->height;
        backend->resources = OsmAnd::CoreResourcesEmbeddedBundle::loadFromLibrary(
            QString::fromLocal8Bit(configured_path(
                "OPEN_NAVICORE_OSMAND_RESOURCES", kDefaultResources)));
        if (!backend->resources) {
            trace("error=resources");
            return -2;
        }

        const char* fonts = configured_path("OPEN_NAVICORE_OSMAND_FONTS",
                                            kDefaultFonts);
        if (OsmAnd::InitializeCore(backend->resources, fonts) == 0) {
            trace("error=initialize_core");
            backend->resources.reset();
            return -3;
        }
        backend->core_initialized = true;

        bool resource_ok = false;
        const QByteArray default_style = backend->resources->getResource(
            QStringLiteral("map/styles/default.render.xml"), &resource_ok);
        backend->styles = std::make_shared<OsmAnd::MapStylesCollection>();
        if (!resource_ok || default_style.isEmpty() ||
            !backend->styles->addStyleFromByteArray(
                default_style, QStringLiteral("default.render.xml"))) {
            trace("error=default_style");
            backend->release_surface();
            return -4;
        }
        const auto style = backend->styles->getResolvedStyleByName(
            QStringLiteral("default"));
        if (!style) {
            trace("error=resolve_style");
            backend->release_surface();
            return -5;
        }

        backend->obfs = std::make_shared<OsmAnd::ObfsCollection>();
        backend->obfs->addFile(QString::fromLocal8Bit(configured_path(
            "OPEN_NAVICORE_OSMAND_MAP", kDefaultMap)));
        if (backend->obfs->getObfFiles().isEmpty()) {
            trace("error=map");
            backend->release_surface();
            return -6;
        }

        backend->presentation =
            std::make_shared<OsmAnd::MapPresentationEnvironment>(
                style, 1.0f, 1.0f, 1.0f);
        backend->presentation->setLocaleLanguageId(QStringLiteral("uk"));
        backend->primitiviser = std::make_shared<OsmAnd::MapPrimitiviser>(
            backend->presentation);
        backend->map_objects =
            std::make_shared<OsmAnd::ObfMapObjectsProvider>(backend->obfs);
        backend->primitives = std::make_shared<OsmAnd::MapPrimitivesProvider>(
            backend->map_objects, backend->primitiviser, 256u);
        backend->symbols =
            std::make_shared<OsmAnd::MapObjectsSymbolsProvider>(
                backend->primitives, 256u);
        backend->raster =
            std::make_shared<OsmAnd::MapRasterLayerProvider_Software>(
                backend->primitives);

        backend->renderer = OsmAnd::createMapRenderer(
            OsmAnd::MapRendererClass::AtlasMapRenderer_OpenGLES2plus);
        if (!backend->renderer) {
            trace("error=create_renderer");
            backend->release_surface();
            return -7;
        }
        OsmAnd::MapRendererSetupOptions setup;
        setup.gpuWorkerThreadEnabled = false;
        setup.frameUpdateRequestCallback = [backend](const OsmAnd::IMapRenderer*) {
            backend->request_frame();
        };
        if (!backend->renderer->setup(setup)) {
            trace("error=setup_renderer");
            backend->release_surface();
            return -8;
        }

        const auto configuration =
            std::static_pointer_cast<OsmAnd::AtlasMapRendererConfiguration>(
                backend->renderer->getConfiguration());
        configuration->referenceTileSizeOnScreenInPixels = 256u;
        backend->renderer->setConfiguration(configuration);
        backend->renderer->setWindowSize(
            OsmAnd::PointI(static_cast<int32_t>(target->width),
                           static_cast<int32_t>(target->height)));
        backend->renderer->setViewport(
            OsmAnd::AreaI(0, 0, static_cast<int32_t>(target->height),
                          static_cast<int32_t>(target->width)));
        backend->renderer->setTarget(OsmAnd::Utilities::convertLatLonTo31(
            OsmAnd::LatLon(kKyivLatitude, kKyivLongitude)));
        backend->renderer->setZoom(kInitialZoom);
        backend->renderer->setAzimuth(0.0f);
        backend->renderer->setElevationAngle(90.0f);
        backend->renderer->addSymbolsProvider(backend->symbols);
        backend->renderer->setMapLayerProvider(0, backend->raster);
        if (!backend->renderer->initializeRendering(true)) {
            trace("error=initialize_rendering");
            backend->release_surface();
            return -9;
        }
        backend->rendering_initialized = true;
        trace("surface_created=kyiv");
        backend->request_frame();
        return 0;
    } catch (...) {
        trace("error=exception_surface_created");
        backend->release_surface();
        return -10;
    }
}

void backend_surface_destroyed(void* context)
{
    auto* backend = static_cast<OsmAndBackend*>(context);
    if (backend != nullptr)
        backend->release_surface();
}

template <typename T>
int accept_event(void*, const T*)
{
    return 0;
}

int backend_gps(void* context, const MockNavBackendGpsV1* gps)
{
    auto* backend = static_cast<OsmAndBackend*>(context);
    if (backend == nullptr || gps == nullptr)
        return -1;
    /* Keep the first integration milestone pinned to Kyiv. Live positioning
       belongs to the navigation-state layer, after rendering is proven. */
    return 0;
}

int backend_render(void* context, const MockNavBackendRenderTargetV1* target,
                   const MockNavBackendFrameV1*)
{
    auto* backend = static_cast<OsmAndBackend*>(context);
    if (backend == nullptr || target == nullptr || !backend->renderer ||
        !backend->rendering_initialized)
        return -1;
    try {
        if (target->width != backend->width || target->height != backend->height) {
            backend->width = target->width;
            backend->height = target->height;
            backend->renderer->setWindowSize(OsmAnd::PointI(
                static_cast<int32_t>(target->width),
                static_cast<int32_t>(target->height)));
            backend->renderer->setViewport(OsmAnd::AreaI(
                0, 0, static_cast<int32_t>(target->height),
                static_cast<int32_t>(target->width)));
        }
        if (!backend->renderer->update()) {
            trace("error=update");
            return -2;
        }
        const bool prepared = backend->renderer->prepareFrame();
        if (!prepared) {
            if (!backend->waiting_for_frame_traced) {
                trace("frame_waiting");
                backend->waiting_for_frame_traced = true;
            }
            if (!backend->renderer->isIdle())
                backend->request_frame();
            return 0;
        }
        const bool rendered = backend->renderer->renderFrame();
        if (!rendered) {
            if (!backend->waiting_for_resources_traced) {
                trace("frame_waiting_resources");
                backend->waiting_for_resources_traced = true;
            }
            backend->request_frame();
            return 0;
        }
        if (!backend->renderer->isIdle())
            backend->request_frame();
        if (!backend->first_frame_rendered) {
            trace("frame_rendered=kyiv");
            backend->first_frame_rendered = true;
        }
        return 0;
    } catch (...) {
        trace("error=exception_render");
        return -4;
    }
}

void fill_v1_query(MockNavBackendPluginV1* output)
{
    static const uint8_t build_id[MOCK_NAV_PLUGIN_BUILD_ID_BYTES] =
        MOCK_NAV_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER;
    std::memset(output, 0, sizeof(*output));
    output->magic0 = MOCK_NAV_PLUGIN_MAGIC0;
    output->magic1 = MOCK_NAV_PLUGIN_MAGIC1;
    output->query_abi_version = MOCK_NAV_PLUGIN_QUERY_ABI_V1;
    output->struct_size = sizeof(*output);
    output->endian_tag = MOCK_NAV_PLUGIN_ENDIAN_TAG;
    output->pointer_bytes = sizeof(void*);
    output->backend_abi_version = MOCK_NAV_BACKEND_ABI_V2;
    output->backend_v1_size = sizeof(MockNavBackendV1);
    output->backend_v2_size = sizeof(MockNavBackendV2);
    output->render_target_v1_size = sizeof(MockNavBackendRenderTargetV1);
    output->frame_v1_size = sizeof(MockNavBackendFrameV1);
    output->gps_v1_size = sizeof(MockNavBackendGpsV1);
    output->visibility_v1_size = sizeof(MockNavBackendVisibilityV1);
    output->screen_v1_size = sizeof(MockNavBackendScreenV1);
    output->pointer_v1_size = sizeof(MockNavBackendPointerV1);
    output->services_v1_size = sizeof(MockNavBackendServicesV1);
    output->vehicle_v2_size = sizeof(MockNavBackendVehicleV2);
    output->gps_interval_v2_size = sizeof(MockNavBackendGpsIntervalV2);
    output->audio_v2_size = sizeof(MockNavBackendAudioV2);
    output->capabilities = MOCK_NAV_PLUGIN_REQUIRED_CAPABILITIES;
    std::memcpy(output->build_id, build_id, sizeof(build_id));
    output->backend.v1.abi_version = MOCK_NAV_BACKEND_ABI_V2;
    output->backend.v1.struct_size = sizeof(MockNavBackendV2);
    output->backend.v1.backend_context = &g_backend;
    output->backend.v1.start = backend_start;
    output->backend.v1.stop = backend_stop;
    output->backend.v1.surface_created = backend_surface_created;
    output->backend.v1.surface_destroyed = backend_surface_destroyed;
    output->backend.v1.visibility_changed =
        accept_event<MockNavBackendVisibilityV1>;
    output->backend.v1.screen_changed = accept_event<MockNavBackendScreenV1>;
    output->backend.v1.gps_changed = backend_gps;
    output->backend.v1.pointer_changed = accept_event<MockNavBackendPointerV1>;
    output->backend.v1.render_frame = backend_render;
    output->backend.vehicle_changed = accept_event<MockNavBackendVehicleV2>;
    output->backend.gps_interval_changed =
        accept_event<MockNavBackendGpsIntervalV2>;
    output->backend.audio_changed = accept_event<MockNavBackendAudioV2>;
}

}  // namespace

extern "C" MOCK_NAV_PLUGIN_EXPORT int mock_nav_backend_query_v1(
    uint32_t requested_query_abi, uint32_t output_size,
    MockNavBackendPluginV1* output)
{
    if (requested_query_abi != MOCK_NAV_PLUGIN_QUERY_ABI_V1 ||
        output_size != sizeof(*output) || output == nullptr)
        return -1;
    fill_v1_query(output);
    return 0;
}
