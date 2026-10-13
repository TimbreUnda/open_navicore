// GPU vector map proof of concept for the radio (QNX 6.6, SGX544, GLES2).
//
// Reads map objects from the OBF through OsmAnd Core (data only: no style
// engine, no Skia), clips and tessellates them per tile on a worker thread,
// and draws them with GLES2. Line widths and colours live in the vertices
// and the GPU extrudes lines, so zoom and rotation never rebuild geometry.
//
// A scripted camera flies through Kyiv: a cold start, a heading-up drive
// with zoom changes, and a rotating overview. Prints timing every 5 s and a
// summary; writes screenshots as BMP.
//
// usage: vector-poc [--seconds N] [--zorder Z] [--shots DIR]
//                   [--resources SO] [--map OBF] [--fonts DIR]
#include <OsmAndCore/QtExtensions.h>

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
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace navicore_vector;
using Clock = std::chrono::steady_clock;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTilePx = 384.0;  // 256 px tiles at density 1.5
constexpr int kMinTileZoom = 11;
constexpr int kMaxTileZoom = 16;    // deeper zooms reuse z16 geometry
constexpr std::size_t kMaxGpuTiles = 220;

double ms_since(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// ---- tiles ---------------------------------------------------------------

struct TileKey {
    int z, x, y;
    bool operator<(const TileKey& o) const
    {
        return z != o.z ? z < o.z : x != o.x ? x < o.x : y < o.y;
    }
    bool operator==(const TileKey& o) const { return z == o.z && x == o.x && y == o.y; }
};

struct BuiltTile {
    TileKey key;
    TileMesh mesh;
    double fetch_ms = 0.0;
    double tessellate_ms = 0.0;
    std::size_t objects = 0;
    std::size_t drawn = 0;
};

struct TagKey {
    const void* mapping;
    std::uint32_t id;
    bool operator==(const TagKey& o) const { return mapping == o.mapping && id == o.id; }
};
struct TagKeyHash {
    std::size_t operator()(const TagKey& k) const
    {
        return std::hash<const void*>()(k.mapping) ^ (std::size_t(k.id) * 0x9E3779B1u);
    }
};

class TileBuilder {
public:
    TileBuilder(std::shared_ptr<OsmAnd::ObfMapObjectsProvider> provider, int threads)
        : provider_(std::move(provider))
    {
        for (int i = 0; i < threads; ++i)
            threads_.emplace_back([this] { run(); });
    }
    ~TileBuilder()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (auto& thread : threads_)
            thread.join();
    }
    // Replaces the wish list (most important first).
    void want(std::vector<TileKey> keys)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            wanted_ = std::move(keys);
        }
        wake_.notify_all();
    }
    std::vector<BuiltTile> take()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BuiltTile> out;
        out.swap(ready_);
        return out;
    }
private:
    using TagCache =
        std::unordered_map<TagKey, std::pair<std::string, std::string>, TagKeyHash>;

    void run()
    {
        TagCache cache;
        for (;;) {
            TileKey key{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] {
                    if (stopping_) return true;
                    for (const auto& k : wanted_)
                        if (!done_.count(k)) return true;
                    return false;
                });
                if (stopping_)
                    return;
                for (const auto& k : wanted_)
                    if (!done_.count(k)) {
                        key = k;
                        break;
                    }
                done_[key] = true;
            }
            BuiltTile tile = build(key, cache);
            std::lock_guard<std::mutex> lock(mutex_);
            ready_.push_back(std::move(tile));
        }
    }

    static const std::pair<std::string, std::string>& tag(
        TagCache& tag_cache_, const OsmAnd::MapObject& object, std::uint32_t index,
        bool additional)
    {
        const auto& ids = additional ? object.additionalAttributeIds : object.attributeIds;
        const TagKey key{object.attributeMapping.get(), ids[static_cast<int>(index)]};
        auto it = tag_cache_.find(key);
        if (it != tag_cache_.end())
            return it->second;
        std::pair<std::string, std::string> value;
        if (const auto* resolved = object.resolveAttributeByIndex(index, additional)) {
            value.first = resolved->tag.toStdString();
            value.second = resolved->value.toStdString();
        }
        return tag_cache_.emplace(key, std::move(value)).first->second;
    }

    BuiltTile build(const TileKey& key, TagCache& cache)
    {
        BuiltTile out;
        out.key = key;
        const auto fetch_started = Clock::now();
        OsmAnd::IMapTiledDataProvider::Request request;
        request.tileId = OsmAnd::TileId::fromXY(key.x, key.y);
        request.zoom = static_cast<OsmAnd::ZoomLevel>(key.z);
        request.detailedZoom = request.zoom;
        request.queryController = controller_;
        std::shared_ptr<OsmAnd::IMapObjectsProvider::Data> data;
        provider_->obtainTiledMapObjects(request, data);
        out.fetch_ms = ms_since(fetch_started);
        if (!data)
            return out;

        const auto tess_started = Clock::now();
        const double size31 = std::ldexp(1.0, 31 - key.z);
        const double origin_x = key.x * size31;
        const double origin_y = key.y * size31;
        const double to_local = kTileExtent / size31;
        const auto local = [&](const QVector<OsmAnd::PointI>& points31, Ring& ring) {
            ring.clear();
            ring.reserve(points31.size());
            for (const auto& p : points31)
                ring.push_back({static_cast<float>((p.x - origin_x) * to_local),
                                static_cast<float>((p.y - origin_y) * to_local)});
        };
        TileTessellator tessellator;
        Tags tags;
        Ring outer;
        std::vector<Ring> holes;
        out.objects = static_cast<std::size_t>(data->mapObjects.size());
        for (const auto& object : data->mapObjects) {
            if (!object || object->isCoastline || object->points31.size() < 2)
                continue;
            tags.clear();
            for (int i = 0; i < object->attributeIds.size(); ++i)
                tags.push_back(tag(cache, *object, static_cast<std::uint32_t>(i), false));
            for (int i = 0; i < object->additionalAttributeIds.size(); ++i)
                tags.push_back(tag(cache, *object, static_cast<std::uint32_t>(i), true));
            const bool area = treat_as_area(
                tags, object->isArea,
                object->points31.size() > 3 &&
                    object->points31.first() == object->points31.last());
            const FeatureStyle style = classify(tags, area, key.z);
            if (!style.has_area && !style.has_line)
                continue;
            ++out.drawn;
            local(object->points31, outer);
            if (style.has_area) {
                holes.resize(static_cast<std::size_t>(object->innerPolygonsPoints31.size()));
                for (int i = 0; i < object->innerPolygonsPoints31.size(); ++i)
                    local(object->innerPolygonsPoints31[i], holes[static_cast<std::size_t>(i)]);
                tessellator.add_area(outer, holes, style.area);
            }
            if (style.has_line) {
                if (area && outer.size() > 2 && !(outer.front() == outer.back()))
                    outer.push_back(outer.front());
                tessellator.add_line(outer, style.line);
            }
        }
        out.mesh = tessellator.finish();
        out.tessellate_ms = ms_since(tess_started);
        return out;
    }

    std::shared_ptr<OsmAnd::ObfMapObjectsProvider> provider_;
    std::shared_ptr<OsmAnd::FunctorQueryController> controller_ =
        std::make_shared<OsmAnd::FunctorQueryController>(
            [](const OsmAnd::FunctorQueryController*) { return false; });
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<TileKey> wanted_;
    std::map<TileKey, bool> done_;
    std::vector<BuiltTile> ready_;
    bool stopping_ = false;
    std::vector<std::thread> threads_;
};

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

// ---- GL ------------------------------------------------------------------

GLuint compile(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader: %s\n", log);
        std::exit(10);
    }
    return shader;
}

GLuint program(const char* vs, const char* fs,
               std::initializer_list<const char*> attributes)
{
    const GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    GLuint index = 0;
    for (const char* name : attributes)
        glBindAttribLocation(p, index++, name);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::fprintf(stderr, "link failed\n");
        std::exit(11);
    }
    return p;
}

const char* kAreaVs =
    "attribute vec2 a_pos; attribute vec4 a_color;"
    "uniform mat2 u_lin; uniform vec2 u_off; varying lowp vec4 v_color;"
    "void main(){ v_color = a_color; gl_Position = vec4(u_lin * a_pos + u_off, 0.0, 1.0); }";
const char* kAreaFs =
    "varying lowp vec4 v_color; void main(){ gl_FragColor = v_color; }";
// Casing and fill passes share the mesh; u_casing selects width and colour.
const char* kLineVs =
    "attribute vec2 a_pos; attribute vec2 a_normal; attribute float a_side;"
    "attribute float a_width; attribute float a_cwidth;"
    "attribute vec4 a_fill; attribute vec4 a_casing;"
    "uniform mat2 u_lin; uniform vec2 u_off; uniform mat2 u_nrm;"
    "uniform float u_wscale; uniform float u_casing;"
    "varying mediump float v_dist; varying mediump float v_half; varying lowp vec4 v_color;"
    "void main(){"
    "  float w = mix(a_width, a_cwidth, u_casing);"
    "  float hw = w > 0.0 ? max(w * u_wscale, 1.0) * 0.5 + 0.5 : 0.0;"
    "  v_color = mix(a_fill, a_casing, u_casing);"
    "  v_dist = a_side * hw; v_half = hw;"
    "  gl_Position = vec4(u_lin * a_pos + u_off + u_nrm * (a_normal * hw), 0.0, 1.0);"
    "}";
const char* kLineFs =
    "precision mediump float;"
    "varying mediump float v_dist; varying mediump float v_half; varying lowp vec4 v_color;"
    "void main(){ float a = clamp(v_half - abs(v_dist), 0.0, 1.0);"
    "  gl_FragColor = vec4(v_color.rgb, v_color.a * a); }";

struct GpuTile {
    GLuint buffers[4] = {};  // area vbo, area ibo, line vbo, line ibo
    GLsizei area_count = 0;
    GLsizei line_count = 0;
    std::size_t bytes = 0;
    std::uint64_t last_used = 0;
};

GpuTile upload(const TileMesh& mesh)
{
    GpuTile tile;
    glGenBuffers(4, tile.buffers);
    glBindBuffer(GL_ARRAY_BUFFER, tile.buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, mesh.area_vertices.size() * sizeof(AreaVertex),
                 mesh.area_vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile.buffers[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.area_indices.size() * 4,
                 mesh.area_indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, tile.buffers[2]);
    glBufferData(GL_ARRAY_BUFFER, mesh.line_vertices.size() * sizeof(LineVertex),
                 mesh.line_vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile.buffers[3]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.line_indices.size() * 4,
                 mesh.line_indices.data(), GL_STATIC_DRAW);
    tile.area_count = static_cast<GLsizei>(mesh.area_indices.size());
    tile.line_count = static_cast<GLsizei>(mesh.line_indices.size());
    tile.bytes = mesh.area_vertices.size() * sizeof(AreaVertex) +
                 mesh.line_vertices.size() * sizeof(LineVertex) +
                 (mesh.area_indices.size() + mesh.line_indices.size()) * 4;
    return tile;
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
    auto provider = std::make_shared<OsmAnd::ObfMapObjectsProvider>(
        obfs, OsmAnd::ObfMapObjectsProvider::Mode::OnlyBinaryMapObjects);
    if (!dump.empty()) {
        int z = 16;
        double lat = 0, lon = 0;
        if (std::sscanf(dump.c_str(), "%d,%lf,%lf", &z, &lat, &lon) == 3)
            dump_tile(*provider, z, lat, lon);
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
    bool msaa = std::getenv("VECTOR_POC_NO_MSAA") == nullptr &&
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

    const GLuint area_prog = program(kAreaVs, kAreaFs, {"a_pos", "a_color"});
    const GLuint line_prog = program(kLineVs, kLineFs,
                                     {"a_pos", "a_normal", "a_side", "a_width",
                                      "a_cwidth", "a_fill", "a_casing"});
    const GLint area_lin = glGetUniformLocation(area_prog, "u_lin");
    const GLint area_off = glGetUniformLocation(area_prog, "u_off");
    const GLint line_lin = glGetUniformLocation(line_prog, "u_lin");
    const GLint line_off = glGetUniformLocation(line_prog, "u_off");
    const GLint line_nrm = glGetUniformLocation(line_prog, "u_nrm");
    const GLint line_wscale = glGetUniformLocation(line_prog, "u_wscale");
    const GLint line_casing = glGetUniformLocation(line_prog, "u_casing");

    TileBuilder builder(provider, 2);
    std::map<TileKey, GpuTile> gpu;
    std::size_t gpu_bytes = 0;
    std::uint64_t frame = 0;

    // Stats.
    std::vector<double> cpu_ms, swap_ms, frame_ms, fetch_ms, tess_ms, upload_ms;
    std::size_t built = 0, incomplete_frames = 0, total_calls = 0;
    double first_complete_s = -1.0;
    std::size_t vertices_total = 0;
    std::vector<double> window_cpu, window_swap, window_frame;
    std::size_t window_incomplete = 0, window_frames = 0, window_calls = 0;
    auto window_start = Clock::now();
    const double shot_times[] = {3.5, duration * 0.3, duration * 0.55, duration * 0.95};
    std::size_t next_shot = 0;
    std::vector<std::uint8_t> pixels;

    const Rgba land = land_colour();
    const auto start = Clock::now();
    auto last_frame = start;
    for (;;) {
        const double t = ms_since(start) / 1000.0;
        if (t > duration)
            break;
        const auto frame_started = Clock::now();
        const Camera cam = camera_at(t, duration);

        // Upload finished tiles.
        for (auto& tile : builder.take()) {
            ++built;
            fetch_ms.push_back(tile.fetch_ms);
            tess_ms.push_back(tile.tessellate_ms);
            vertices_total += tile.mesh.area_vertices.size() + tile.mesh.line_vertices.size();
            const auto upload_started = Clock::now();
            GpuTile g = upload(tile.mesh);
            upload_ms.push_back(ms_since(upload_started));
            gpu_bytes += g.bytes;
            g.last_used = frame;
            gpu[tile.key] = g;
        }

        // Projection: tile-local -> clip is a 2x2 linear part plus offset.
        const double scale = kTilePx * std::pow(2.0, cam.zoom) / std::ldexp(1.0, 31);
        const double phi = -cam.azimuth * kPi / 180.0;
        const double cs = std::cos(phi), sn = std::sin(phi);
        const double dx = 2.0 / W, dy = -2.0 / H;
        const double anchor_x = 0.0, anchor_y = 0.70;  // vehicle at 70% height
        // screen offset from anchor in px -> clip
        const double anchor_clip_x = anchor_x * 2.0 - 0.0;
        const double anchor_clip_y = 1.0 - anchor_y * 2.0;
        const float nrm[4] = {static_cast<float>(dx * cs), static_cast<float>(dy * sn),
                              static_cast<float>(dx * -sn), static_cast<float>(dy * cs)};

        // Visible area: invert the four screen corners to 31-space.
        const int tz = std::clamp(static_cast<int>(std::floor(cam.zoom)), kMinTileZoom, kMaxTileZoom);
        double min_x = 1e300, min_y = 1e300, max_x = -1e300, max_y = -1e300;
        for (const auto& corner : {std::pair<double, double>{0, 0}, {double(W), 0},
                                   {0, double(H)}, {double(W), double(H)}}) {
            const double sx = corner.first - W / 2.0;
            const double sy = corner.second - H * anchor_y;
            // inverse rotation
            const double wx = (cs * sx + sn * sy) / scale;
            const double wy = (-sn * sx + cs * sy) / scale;
            min_x = std::min(min_x, cam.x31 + wx);
            max_x = std::max(max_x, cam.x31 + wx);
            min_y = std::min(min_y, cam.y31 + wy);
            max_y = std::max(max_y, cam.y31 + wy);
        }
        const double size31 = std::ldexp(1.0, 31 - tz);
        std::vector<TileKey> wanted;
        for (int y = static_cast<int>(min_y / size31); y <= static_cast<int>(max_y / size31); ++y)
            for (int x = static_cast<int>(min_x / size31); x <= static_cast<int>(max_x / size31); ++x)
                wanted.push_back({tz, x, y});
        std::sort(wanted.begin(), wanted.end(), [&](const TileKey& a, const TileKey& b) {
            const auto d = [&](const TileKey& k) {
                return std::hypot((k.x + 0.5) * size31 - cam.x31, (k.y + 0.5) * size31 - cam.y31);
            };
            return d(a) < d(b);
        });
        builder.want(wanted);

        // Draw list: wanted tiles, or their nearest ready ancestor / children.
        std::vector<TileKey> draw;
        std::size_t missing = 0;
        for (const auto& key : wanted) {
            if (gpu.count(key)) {
                draw.push_back(key);
                continue;
            }
            ++missing;
            bool covered = false;
            for (int up = 1; up <= 3 && !covered; ++up) {
                const TileKey parent{key.z - up, key.x >> up, key.y >> up};
                if (gpu.count(parent)) {
                    draw.push_back(parent);
                    covered = true;
                }
            }
            if (!covered) {
                for (int cy = 0; cy < 2; ++cy)
                    for (int cx = 0; cx < 2; ++cx) {
                        const TileKey child{key.z + 1, key.x * 2 + cx, key.y * 2 + cy};
                        if (gpu.count(child))
                            draw.push_back(child);
                    }
            }
        }
        std::sort(draw.begin(), draw.end());
        draw.erase(std::unique(draw.begin(), draw.end()), draw.end());
        // Coarser fallbacks first so the sharper tiles land on top.
        std::stable_sort(draw.begin(), draw.end(),
                         [](const TileKey& a, const TileKey& b) { return a.z < b.z; });

        glViewport(0, 0, W, H);
        glClearColor(land.r / 255.0f, land.g / 255.0f, land.b / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        std::size_t calls = 0;
        const auto tile_matrix = [&](const TileKey& key, float lin[4], float off[2]) {
            const double tile31 = std::ldexp(1.0, 31 - key.z);
            const double k = tile31 / kTileExtent * scale;
            lin[0] = static_cast<float>(dx * cs * k);
            lin[1] = static_cast<float>(dy * sn * k);
            lin[2] = static_cast<float>(dx * -sn * k);
            lin[3] = static_cast<float>(dy * cs * k);
            const double ox = (key.x * tile31 - cam.x31) * scale;
            const double oy = (key.y * tile31 - cam.y31) * scale;
            off[0] = static_cast<float>(dx * (cs * ox - sn * oy) + anchor_clip_x);
            off[1] = static_cast<float>(dy * (sn * ox + cs * oy) + anchor_clip_y);
        };
        float lin[4], off[2];

        glDisable(GL_BLEND);
        glUseProgram(area_prog);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        for (const auto& key : draw) {
            auto& tile = gpu[key];
            tile.last_used = frame;
            if (!tile.area_count)
                continue;
            tile_matrix(key, lin, off);
            glUniformMatrix2fv(area_lin, 1, GL_FALSE, lin);
            glUniform2fv(area_off, 1, off);
            glBindBuffer(GL_ARRAY_BUFFER, tile.buffers[0]);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile.buffers[1]);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(AreaVertex), nullptr);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(AreaVertex),
                                  reinterpret_cast<void*>(8));
            glDrawElements(GL_TRIANGLES, tile.area_count, GL_UNSIGNED_INT, nullptr);
            ++calls;
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(line_prog);
        for (GLuint a = 0; a < 7; ++a)
            glEnableVertexAttribArray(a);
        glUniformMatrix2fv(line_nrm, 1, GL_FALSE, nrm);
        glUniform1f(line_wscale, static_cast<float>(std::pow(
                                     2.0, (cam.zoom - 16.0) * (cam.zoom < 16.0 ? 0.5 : 0.75))));
        for (int pass = 0; pass < 2; ++pass) {
            glUniform1f(line_casing, pass == 0 ? 1.0f : 0.0f);
            for (const auto& key : draw) {
                const auto& tile = gpu[key];
                if (!tile.line_count)
                    continue;
                tile_matrix(key, lin, off);
                glUniformMatrix2fv(line_lin, 1, GL_FALSE, lin);
                glUniform2fv(line_off, 1, off);
                glBindBuffer(GL_ARRAY_BUFFER, tile.buffers[2]);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile.buffers[3]);
                const GLsizei stride = sizeof(LineVertex);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8));
                glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(16));
                glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(20));
                glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(24));
                glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(28));
                glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(32));
                glDrawElements(GL_TRIANGLES, tile.line_count, GL_UNSIGNED_INT, nullptr);
                ++calls;
            }
        }
        for (GLuint a = 0; a < 7; ++a)
            glDisableVertexAttribArray(a);

        // Vehicle marker: a blue dot with a white ring, drawn as a fat
        // zero-length line would need caps; use a scissored clear instead.
        glEnable(GL_SCISSOR_TEST);
        const int mx = W / 2, my = static_cast<int>(H * (1.0 - anchor_y));
        glScissor(mx - 9, my - 9, 18, 18);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(mx - 6, my - 6, 12, 12);
        glClearColor(0.10f, 0.45f, 0.91f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);

        if (next_shot < 4 && t >= shot_times[next_shot]) {
            glFinish();
            pixels.resize(static_cast<std::size_t>(W * H * 4));
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            char path[256];
            std::snprintf(path, sizeof(path), "%s-%zu.bmp", shots.c_str(), next_shot + 1);
            write_bmp(path, W, H, pixels);
            std::printf("shot %s t=%.1f zoom=%.2f azimuth=%.0f tiles=%zu missing=%zu\n",
                        path, t, cam.zoom, cam.azimuth, draw.size(), missing);
            ++next_shot;
        }

        const double cpu = ms_since(frame_started);
        const auto swap_started = Clock::now();
        eglSwapBuffers(egl, surface);
        const double swap = ms_since(swap_started);
        const double interval = ms_since(last_frame);
        last_frame = Clock::now();
        if (frame > 0) {
            cpu_ms.push_back(cpu);
            swap_ms.push_back(swap);
            frame_ms.push_back(interval);
            window_cpu.push_back(cpu);
            window_swap.push_back(swap);
            window_frame.push_back(interval);
        }
        total_calls += calls;
        window_calls += calls;
        ++window_frames;
        if (missing) {
            ++incomplete_frames;
            ++window_incomplete;
        } else if (first_complete_s < 0.0) {
            first_complete_s = t;
        }
        ++frame;

        // Evict least recently used tiles.
        while (gpu.size() > kMaxGpuTiles) {
            auto oldest = gpu.begin();
            for (auto it = gpu.begin(); it != gpu.end(); ++it)
                if (it->second.last_used < oldest->second.last_used)
                    oldest = it;
            if (oldest->second.last_used == frame - 1)
                break;
            glDeleteBuffers(4, oldest->second.buffers);
            gpu_bytes -= oldest->second.bytes;
            gpu.erase(oldest);
        }

        if (ms_since(window_start) >= 5000.0) {
            const double seconds = ms_since(window_start) / 1000.0;
            std::printf("t=%5.1f fps=%5.1f cpu_p50=%.1f cpu_p95=%.1f swap_avg=%.1f "
                        "frame_p95=%.1f frame_max=%.1f incomplete=%zu/%zu calls=%zu "
                        "zoom=%.2f tiles_gpu=%zu gpu_mb=%.1f built=%zu\n",
                        t, window_frames / seconds, percentile(window_cpu, 0.5),
                        percentile(window_cpu, 0.95),
                        mean(window_swap),
                        percentile(window_frame, 0.95), percentile(window_frame, 1.0),
                        window_incomplete, window_frames,
                        window_frames ? window_calls / window_frames : 0, cam.zoom,
                        gpu.size(), gpu_bytes / 1048576.0, built);
            std::fflush(stdout);
            window_cpu.clear();
            window_swap.clear();
            window_frame.clear();
            window_incomplete = window_frames = window_calls = 0;
            window_start = Clock::now();
        }
    }

    const double seconds = ms_since(start) / 1000.0;
    std::size_t over50 = 0;
    for (double f : frame_ms)
        if (f > 50.0) ++over50;
    std::printf("summary frames=%llu seconds=%.1f fps=%.1f cpu_p50=%.1f cpu_p95=%.1f "
                "cpu_max=%.1f swap_avg=%.1f frame_p95=%.1f frame_max=%.1f over50=%zu "
                "incomplete=%zu first_complete_s=%.2f calls_avg=%.0f\n",
                static_cast<unsigned long long>(frame), seconds, frame / seconds,
                percentile(cpu_ms, 0.5), percentile(cpu_ms, 0.95), percentile(cpu_ms, 1.0),
                mean(swap_ms), percentile(frame_ms, 0.95), percentile(frame_ms, 1.0), over50,
                incomplete_frames, first_complete_s,
                frame ? static_cast<double>(total_calls) / frame : 0.0);
    std::printf("tiles built=%zu fetch_avg=%.1f fetch_p95=%.1f fetch_max=%.1f "
                "tess_avg=%.1f tess_p95=%.1f tess_max=%.1f upload_avg=%.2f upload_max=%.2f "
                "vertices_avg=%.0f gpu_tiles=%zu gpu_mb=%.1f\n",
                built, mean(fetch_ms), percentile(fetch_ms, 0.95), percentile(fetch_ms, 1.0),
                mean(tess_ms), percentile(tess_ms, 0.95), percentile(tess_ms, 1.0),
                mean(upload_ms), percentile(upload_ms, 1.0),
                built ? static_cast<double>(vertices_total) / built : 0.0, gpu.size(),
                gpu_bytes / 1048576.0);
    std::fflush(stdout);

    for (auto& entry : gpu)
        glDeleteBuffers(4, entry.second.buffers);
    eglMakeCurrent(egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl, surface);
    eglDestroyContext(egl, ctx);
    eglTerminate(egl);
    screen_destroy_window(win);
    screen_destroy_context(sctx);
    // Skip ReleaseCore: the builder thread is gone, the process exits.
    std::_Exit(0);
}
