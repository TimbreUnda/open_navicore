// GPU vector map proof of concept for the radio (QNX 6.6, SGX544, GLES2).
//
// Drives navicore_vector::VectorMap (the same renderer the navigation
// backend uses) with a scripted camera through Kyiv: a cold start, a
// heading-up drive with zoom changes, and a rotating overview. Prints
// timing every 5 s and a summary; writes screenshots as BMP.
//
// usage: vector-poc [--seconds N] [--zorder Z] [--shots DIR]
//                   [--resources SO] [--map OBF] [--fonts DIR]
//                   [--dump z,lat,lon]
#include <OsmAndCore/QtExtensions.h>

#include "vector_map.h"
#include "vector_style.h"
#include "vector_tessellator.h"

#include <OsmAndCore.h>
#include <OsmAndCore/CoreResourcesEmbeddedBundle.h>
#include <OsmAndCore/FunctorQueryController.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Data/MapObject.h>
#include <OsmAndCore/Map/IMapObjectsProvider.h>
#include <OsmAndCore/Map/ObfMapObjectsProvider.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <screen/screen.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace navicore_vector;
using Clock = std::chrono::steady_clock;

namespace {

constexpr double kPi = 3.14159265358979323846;

double ms_since(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Prints what one tile holds: area/line counts, and the most common first
// tag=value pairs of objects the style draws and skips.
void dump_tile(OsmAnd::ObfMapObjectsProvider& provider, int z, double lat, double lon)
{
    const double n = std::ldexp(1.0, z);
    const double r = lat * kPi / 180.0;
    const int x = static_cast<int>((lon + 180.0) / 360.0 * n);
    const int y = static_cast<int>((1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / kPi) / 2.0 * n);
    OsmAnd::IMapTiledDataProvider::Request request;
    request.tileId = OsmAnd::TileId::fromXY(x, y);
    request.zoom = static_cast<OsmAnd::ZoomLevel>(z);
    request.detailedZoom = request.zoom;
    request.queryController = std::make_shared<OsmAnd::FunctorQueryController>(
        [](const OsmAnd::FunctorQueryController*) { return false; });
    std::shared_ptr<OsmAnd::IMapObjectsProvider::Data> data;
    provider.obtainTiledMapObjects(request, data);
    if (!data) {
        std::printf("dump %d/%d/%d: no data\n", z, x, y);
        return;
    }
    std::map<std::string, int> undrawn, drawn;
    int areas = 0, lines = 0;
    for (const auto& object : data->mapObjects) {
        Tags tags;
        std::string first;
        for (int i = 0; i < object->attributeIds.size(); ++i)
            if (const auto* tv = object->resolveAttributeByIndex(static_cast<std::uint32_t>(i))) {
                tags.emplace_back(tv->tag.toStdString(), tv->value.toStdString());
                if (first.empty()) first = tags.back().first + "=" + tags.back().second;
            }
        for (int i = 0; i < object->additionalAttributeIds.size(); ++i)
            if (const auto* tv = object->resolveAttributeByIndex(static_cast<std::uint32_t>(i), true))
                tags.emplace_back(tv->tag.toStdString(), tv->value.toStdString());
        const bool area = treat_as_area(
            tags, object->isArea,
            object->points31.size() > 3 &&
                object->points31.first() == object->points31.last());
        (area ? areas : lines)++;
        const auto style = classify(tags, area, z);
        const std::string label = std::string(area ? "A " : "L ") + first +
            (tags.size() > 1 ? " +" + tags[1].first + "=" + tags[1].second : "");
        ((style.has_area || style.has_line) ? drawn : undrawn)[label]++;
    }
    std::printf("dump %d/%d/%d objects=%d areas=%d lines=%d\n", z, x, y,
                data->mapObjects.size(), areas, lines);
    const auto top = [](const std::map<std::string, int>& m, const char* name) {
        std::vector<std::pair<int, std::string>> v;
        for (const auto& e : m) v.emplace_back(e.second, e.first);
        std::sort(v.rbegin(), v.rend());
        for (std::size_t i = 0; i < v.size() && i < 25; ++i)
            std::printf("  %s %4d %s\n", name, v[i].first, v[i].second.c_str());
    };
    top(drawn, "drawn");
    top(undrawn, "skip ");
}

// ---- camera --------------------------------------------------------------

struct Camera {
    double x31, y31;   // centre (vehicle) position
    double zoom;       // continuous
    double azimuth;    // degrees clockwise from north, at the top of the screen
};

void lat_lon_to_31(double lat, double lon, double& x31, double& y31)
{
    const double n = std::ldexp(1.0, 31);
    x31 = (lon + 180.0) / 360.0 * n;
    const double r = lat * kPi / 180.0;
    y31 = (1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / kPi) / 2.0 * n;
}

struct Waypoint {
    double lat, lon;
};
// Maidan -> Khreshchatyk -> Bessarabska -> Lesi Ukrainky -> Lavra -> Patona
// bridge -> left bank.
const Waypoint kRoute[] = {
    {50.45010, 30.52410}, {50.44730, 30.52230}, {50.44180, 30.52150},
    {50.43960, 30.52490}, {50.43650, 30.53700}, {50.43470, 30.55710},
    {50.42550, 30.57700}, {50.42850, 30.60100},
};

// Scripted flight. 0..cold: hold north-up at z16 (cold load). Then drive
// heading-up along the route with the zoom breathing between 15 and 17.
// The last 20%: overview, zoom out to 13 and spin.
Camera camera_at(double t, double duration)
{
    const double cold = 4.0;
    const double overview_start = duration * 0.8;
    std::vector<std::pair<double, double>> points;
    for (const auto& w : kRoute) {
        double x, y;
        lat_lon_to_31(w.lat, w.lon, x, y);
        points.emplace_back(x, y);
    }
    std::vector<double> lengths{0.0};
    for (std::size_t i = 1; i < points.size(); ++i)
        lengths.push_back(lengths.back() + std::hypot(points[i].first - points[i - 1].first,
                                                      points[i].second - points[i - 1].second));
    const auto along = [&](double fraction, double& x, double& y, double& heading) {
        const double target = std::clamp(fraction, 0.0, 1.0) * lengths.back();
        std::size_t i = 1;
        while (i + 1 < points.size() && lengths[i] < target)
            ++i;
        const double f = (target - lengths[i - 1]) / (lengths[i] - lengths[i - 1]);
        x = points[i - 1].first + f * (points[i].first - points[i - 1].first);
        y = points[i - 1].second + f * (points[i].second - points[i - 1].second);
        heading = std::atan2(points[i].first - points[i - 1].first,
                             -(points[i].second - points[i - 1].second)) * 180.0 / kPi;
    };
    Camera c{};
    double heading = 0.0;
    if (t < cold) {
        along(0.0, c.x31, c.y31, heading);
        c.zoom = 16.0;
        c.azimuth = 0.0;
        return c;
    }
    const double drive_t = std::min(t, overview_start) - cold;
    const double drive_len = overview_start - cold;
    along(drive_t / drive_len, c.x31, c.y31, heading);
    // Smooth heading: average a little ahead and behind.
    double hx = 0, hy = 0;
    for (int k = -3; k <= 3; ++k) {
        double x, y, h;
        along((drive_t + k * 0.4) / drive_len, x, y, h);
        hx += std::sin(h * kPi / 180.0);
        hy += std::cos(h * kPi / 180.0);
    }
    heading = std::atan2(hx, hy) * 180.0 / kPi;
    c.zoom = 16.0 + std::sin(drive_t / drive_len * 2.0 * kPi) * 1.0;
    c.azimuth = heading;
    if (t > overview_start) {
        const double f = std::min((t - overview_start) / (duration - overview_start), 1.0);
        c.zoom = 16.0 - 3.0 * std::sin(std::min(f * 2.0, 1.0) * kPi / 2.0);
        c.azimuth = heading + 360.0 * f;
    }
    return c;
}

// ---- screenshots -----------------------------------------------------------

void write_bmp(const char* path, int width, int height, const std::vector<std::uint8_t>& rgba)
{
    FILE* f = std::fopen(path, "wb");
    if (!f)
        return;
    const std::uint32_t data = static_cast<std::uint32_t>(width * height * 4);
    std::uint8_t header[54] = {'B', 'M'};
    const auto le32 = [&](int at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) header[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    le32(2, 54 + data);
    le32(10, 54);
    le32(14, 40);
    le32(18, static_cast<std::uint32_t>(width));
    le32(22, static_cast<std::uint32_t>(height));  // bottom-up, as GL reads
    header[26] = 1;
    header[28] = 32;
    le32(34, data);
    std::fwrite(header, 1, sizeof(header), f);
    std::vector<std::uint8_t> bgra(rgba.size());
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        bgra[i] = rgba[i + 2];
        bgra[i + 1] = rgba[i + 1];
        bgra[i + 2] = rgba[i];
        bgra[i + 3] = 255;
    }
    std::fwrite(bgra.data(), 1, bgra.size(), f);
    std::fclose(f);
}

double mean(const std::vector<double>& values)
{
    double sum = 0.0;
    for (const double v : values)
        sum += v;
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double percentile(std::vector<double> values, double p)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1,
                           static_cast<std::size_t>(p * static_cast<double>(values.size())))];
}

} // namespace

int main(int argc, char** argv)
{
    double duration = 40.0;
    int zorder = 2000;
    std::string shots = "/tmp/vector-poc";
    std::string resources_path = "/datastore/navicore-backend/libOsmAndCore_ResourcesBundle_shared.so";
    std::string map_path = "/datastore/navicore-backend/maps/kyiv.obf";
    std::string fonts = "/datastore/navicore-backend/fonts";
    std::string dump;  // "z,lat,lon": print one tile's contents and exit
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--seconds") duration = std::atof(argv[i + 1]);
        else if (a == "--zorder") zorder = std::atoi(argv[i + 1]);
        else if (a == "--shots") shots = argv[i + 1];
        else if (a == "--resources") resources_path = argv[i + 1];
        else if (a == "--map") map_path = argv[i + 1];
        else if (a == "--fonts") fonts = argv[i + 1];
        else if (a == "--dump") dump = argv[i + 1];
    }

    const auto resources = OsmAnd::CoreResourcesEmbeddedBundle::loadFromLibrary(
        QString::fromLocal8Bit(resources_path.c_str()));
    if (!resources || OsmAnd::InitializeCore(resources, fonts.c_str()) == 0) {
        std::fprintf(stderr, "OsmAnd core init failed\n");
        return 2;
    }
    auto obfs = std::make_shared<OsmAnd::ObfsCollection>();
    obfs->addFile(QString::fromLocal8Bit(map_path.c_str()));
    if (!dump.empty()) {
        OsmAnd::ObfMapObjectsProvider provider(
            obfs, OsmAnd::ObfMapObjectsProvider::Mode::OnlyBinaryMapObjects);
        int z = 16;
        double lat = 0, lon = 0;
        if (std::sscanf(dump.c_str(), "%d,%lf,%lf", &z, &lat, &lon) == 3)
            dump_tile(provider, z, lat, lon);
        std::fflush(stdout);
        std::_Exit(0);
    }

    // ---- Screen + EGL: a full-screen window, above the HMI by default.
    if (!std::getenv("GRAPHICS_ROOT"))
        setenv("GRAPHICS_ROOT", "/proc/boot/usr/lib/graphics/jacinto6", 0);
    screen_context_t sctx;
    screen_window_t win;
    if (screen_create_context(&sctx, SCREEN_APPLICATION_CONTEXT) != 0 ||
        screen_create_window(&win, sctx) != 0) {
        std::perror("screen");
        return 3;
    }
    int usage = SCREEN_USAGE_OPENGL_ES2;
    int format = SCREEN_FORMAT_RGBX8888;
    int transparency = SCREEN_TRANSPARENCY_NONE;
    int size[2] = {1024, 768};
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &format);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_TRANSPARENCY, &transparency);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_BUFFER_SIZE, size);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &zorder);
    screen_create_window_buffers(win, 2);

    EGLDisplay egl = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(egl, nullptr, nullptr);
    EGLConfig config;
    EGLint n = 0;
    const EGLint msaa_attr[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
                                EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                EGL_BLUE_SIZE, 8, EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, 4,
                                EGL_NONE};
    const EGLint plain_attr[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
                                 EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                 EGL_BLUE_SIZE, 8, EGL_NONE};
    const bool msaa = std::getenv("VECTOR_POC_NO_MSAA") == nullptr &&
                      eglChooseConfig(egl, msaa_attr, &config, 1, &n) && n == 1;
    if (!msaa && !(eglChooseConfig(egl, plain_attr, &config, 1, &n) && n == 1)) {
        std::fprintf(stderr, "no EGL config\n");
        return 4;
    }
    const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLSurface surface = eglCreateWindowSurface(egl, config, (EGLNativeWindowType)win, nullptr);
    EGLContext ctx = eglCreateContext(egl, config, EGL_NO_CONTEXT, ctx_attr);
    if (surface == EGL_NO_SURFACE || !eglMakeCurrent(egl, surface, surface, ctx)) {
        std::fprintf(stderr, "EGL surface failed 0x%x\n", eglGetError());
        return 5;
    }
    const int W = size[0], H = size[1];
    std::printf("vector-poc: %dx%d msaa=%d zorder=%d seconds=%.0f\n", W, H, msaa ? 4 : 0,
                zorder, duration);

    VectorMap map(obfs, resources, 2, nullptr);
    if (!map.init_gl()) {
        std::fprintf(stderr, "vector map GL init failed\n");
        return 6;
    }
    {
        // The flight path doubles as a route line.
        std::vector<std::pair<std::int32_t, std::int32_t>> route;
        for (const auto& w : kRoute) {
            double x, y;
            lat_lon_to_31(w.lat, w.lon, x, y);
            route.emplace_back(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
        }
        map.set_route(route);
    }

    std::vector<double> cpu_ms, swap_ms, frame_ms, window_cpu, window_swap, window_frame;
    std::size_t incomplete = 0, window_incomplete = 0, window_frames = 0;
    double first_complete_s = -1.0;
    auto window_start = Clock::now();
    const double shot_times[] = {3.5, duration * 0.3, duration * 0.55, duration * 0.95};
    std::size_t next_shot = 0;
    std::vector<std::uint8_t> pixels;
    std::uint64_t frame = 0;
    const auto start = Clock::now();
    auto last_frame = start;
    double last_interval_ms = 16.7;
    for (;;) {
        const double t = ms_since(start) / 1000.0;
        if (t > duration)
            break;
        const auto frame_started = Clock::now();
        const Camera cam = camera_at(t, duration);
        MapView view;
        view.width = W;
        view.height = H;
        VectorMap::set_flat_camera(view, cam.x31, cam.y31, cam.zoom, cam.azimuth, W / 2.0,
                                   H * 0.70);
        (void)map.render(view, std::min(last_interval_ms, 100.0) / 1000.0, true);
        const bool missing = map.last_frame_incomplete();

        if (next_shot < 4 && t >= shot_times[next_shot]) {
            glFinish();
            pixels.resize(static_cast<std::size_t>(W * H * 4));
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            char path[256];
            std::snprintf(path, sizeof(path), "%s-%zu.bmp", shots.c_str(), next_shot + 1);
            write_bmp(path, W, H, pixels);
            std::printf("shot %s t=%.1f zoom=%.2f azimuth=%.0f incomplete=%d\n", path, t,
                        cam.zoom, cam.azimuth, missing ? 1 : 0);
            ++next_shot;
        }

        const double cpu = ms_since(frame_started);
        const auto swap_started = Clock::now();
        eglSwapBuffers(egl, surface);
        const double swap = ms_since(swap_started);
        const double interval = ms_since(last_frame);
        last_frame = Clock::now();
        last_interval_ms = interval;
        if (frame > 0) {
            cpu_ms.push_back(cpu);
            swap_ms.push_back(swap);
            frame_ms.push_back(interval);
            window_cpu.push_back(cpu);
            window_swap.push_back(swap);
            window_frame.push_back(interval);
        }
        ++window_frames;
        if (missing) {
            ++incomplete;
            ++window_incomplete;
        } else if (first_complete_s < 0.0) {
            first_complete_s = t;
        }
        ++frame;

        if (ms_since(window_start) >= 5000.0) {
            const double seconds = ms_since(window_start) / 1000.0;
            const VectorMapStats s = map.stats();
            std::printf("t=%5.1f fps=%5.1f cpu_p50=%.1f cpu_p95=%.1f swap_avg=%.1f "
                        "frame_p95=%.1f frame_max=%.1f incomplete=%zu/%zu zoom=%.2f "
                        "tiles_gpu=%zu gpu_mb=%.1f built=%llu\n",
                        t, window_frames / seconds, percentile(window_cpu, 0.5),
                        percentile(window_cpu, 0.95), mean(window_swap),
                        percentile(window_frame, 0.95), percentile(window_frame, 1.0),
                        window_incomplete, window_frames, cam.zoom, s.gpu_tiles,
                        s.gpu_bytes / 1048576.0, static_cast<unsigned long long>(s.tiles_built));
            std::fflush(stdout);
            window_cpu.clear();
            window_swap.clear();
            window_frame.clear();
            window_incomplete = window_frames = 0;
            window_start = Clock::now();
        }
    }

    const double seconds = ms_since(start) / 1000.0;
    std::size_t over50 = 0;
    for (double f : frame_ms)
        if (f > 50.0) ++over50;
    const VectorMapStats s = map.stats();
    const double built = s.tiles_built ? static_cast<double>(s.tiles_built) : 1.0;
    std::printf("summary frames=%llu seconds=%.1f fps=%.1f cpu_p50=%.1f cpu_p95=%.1f "
                "cpu_max=%.1f swap_avg=%.1f frame_p95=%.1f frame_max=%.1f over50=%zu "
                "incomplete=%zu first_complete_s=%.2f calls_avg=%.0f\n",
                static_cast<unsigned long long>(frame), seconds, frame / seconds,
                percentile(cpu_ms, 0.5), percentile(cpu_ms, 0.95), percentile(cpu_ms, 1.0),
                mean(swap_ms), percentile(frame_ms, 0.95), percentile(frame_ms, 1.0), over50,
                incomplete, first_complete_s,
                s.frames ? static_cast<double>(s.draw_calls) / s.frames : 0.0);
    std::printf("tiles built=%llu fetch_avg=%.1f fetch_max=%.1f build_avg=%.1f "
                "labels_avg=%.1f gpu_tiles=%zu gpu_mb=%.1f label_quads_avg=%.1f "
                "label_place_max=%.1f atlas_uploads=%zu atlas_resets=%zu\n",
                static_cast<unsigned long long>(s.tiles_built), s.fetch_ms_total / built,
                s.fetch_ms_max, s.build_ms_total / built, s.labels_ms_total / built,
                s.gpu_tiles, s.gpu_bytes / 1048576.0,
                s.frames ? static_cast<double>(s.label_quads) / s.frames : 0.0,
                s.label_place_ms_max, s.atlas_uploads, s.atlas_resets);
    std::fflush(stdout);

    map.release_gl(false);
    eglMakeCurrent(egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl, surface);
    eglDestroyContext(egl, ctx);
    eglTerminate(egl);
    screen_destroy_window(win);
    screen_destroy_context(sctx);
    // Skip ReleaseCore: the process exits.
    std::_Exit(0);
}
