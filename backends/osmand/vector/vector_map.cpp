#include <OsmAndCore/QtExtensions.h>

#include "vector_map.h"

#include "vector_style.h"
#include "vector_tessellator.h"
#include "backends/osmand/label_geometry.h"
#include "backends/osmand/thread_priority.h"

#include <OsmAndCore.h>
#include <OsmAndCore/CoreResourcesEmbeddedBundle.h>
#include <OsmAndCore/FunctorQueryController.h>
#include <OsmAndCore/IObfsCollection.h>
#include <OsmAndCore/SkiaUtilities.h>
#include <OsmAndCore/TextRasterizer.h>
#include <OsmAndCore/Data/MapObject.h>
#include <OsmAndCore/Map/IMapObjectsProvider.h>
#include <OsmAndCore/Map/ObfMapObjectsProvider.h>

#include <OsmAndCore/ignore_warnings_on_external_includes.h>
#include <SkImage.h>
#include <SkImageInfo.h>
#include <OsmAndCore/restore_internal_warnings.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace navicore_vector {
namespace {

using Clock = std::chrono::steady_clock;
using navicore_osmand::OrientedBox;
using navicore_osmand::ScreenPoint;

constexpr int kMinTileZoom = 11;
constexpr int kMaxTileZoom = 16;  // deeper zooms reuse z16 geometry
constexpr std::size_t kMaxGpuTiles = 220;
constexpr std::size_t kMaxWantedTiles = 64;

double ms_since(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// ---- tiles and labels --------------------------------------------------------

struct TileKey {
    int z, x, y;
    bool operator<(const TileKey& o) const
    {
        return z != o.z ? z < o.z : x != o.x ? x < o.x : y < o.y;
    }
    bool operator==(const TileKey& o) const { return z == o.z && x == o.x && y == o.y; }
};

// Premultiplied RGBA bitmap of a label or icon.
struct LabelImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};
using LabelImagePtr = std::shared_ptr<const LabelImage>;

LabelImagePtr image_from_skia(const sk_sp<SkImage>& image)
{
    if (!image || image->width() <= 0 || image->height() <= 0)
        return nullptr;
    auto out = std::make_shared<LabelImage>();
    out->width = image->width();
    out->height = image->height();
    out->rgba.resize(static_cast<std::size_t>(out->width) * out->height * 4u);
    const auto info = SkImageInfo::Make(out->width, out->height,
                                        SkColorType::kRGBA_8888_SkColorType,
                                        SkAlphaType::kPremul_SkAlphaType);
    if (!image->readPixels(nullptr, info, out->rgba.data(),
                           static_cast<std::size_t>(out->width) * 4u, 0, 0,
                           SkImage::kDisallow_CachingHint))
        return nullptr;
    return out;
}

// Shared by the workers: each distinct text/icon is rasterized once.
class ImageCache {
public:
    LabelImagePtr get(const std::string& key, const std::function<LabelImagePtr()>& make)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = images_.find(key);
            if (it != images_.end())
                return it->second;
        }
        LabelImagePtr image = make();
        std::lock_guard<std::mutex> lock(mutex_);
        return images_.emplace(key, image).first->second;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, LabelImagePtr> images_;
};

LabelImagePtr rasterize_text(const QString& text, float size, bool bold, std::uint32_t argb)
{
    OsmAnd::TextRasterizer::Style style;
    style.setSize(size)
        .setBold(bold)
        .setColor(OsmAnd::ColorARGB(argb))
        .setHaloColor(OsmAnd::ColorARGB(0xFFFFFFFFu))
        .setHaloRadius(3);
    return image_from_skia(OsmAnd::TextRasterizer::getDefault()->rasterize(text, style));
}

// Google-style POI marker: category-coloured disc, white rim, OsmAnd's
// white SVG pictogram.
LabelImagePtr make_poi_icon(const OsmAnd::CoreResourcesEmbeddedBundle& resources,
                            const char* icon, Rgba colour)
{
    constexpr int kSize = 30;
    auto out = std::make_shared<LabelImage>();
    out->width = out->height = kSize;
    out->rgba.assign(kSize * kSize * 4, 0);
    const float centre = (kSize - 1) / 2.0f;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const float d = std::hypot(x - centre, y - centre);
            const float outer = std::clamp(14.5f - d, 0.0f, 1.0f);
            const float inner = std::clamp(12.8f - d, 0.0f, 1.0f);
            const float r = 255.0f + (colour.r - 255.0f) * inner;
            const float g = 255.0f + (colour.g - 255.0f) * inner;
            const float b = 255.0f + (colour.b - 255.0f) * inner;
            auto* px = &out->rgba[(y * kSize + x) * 4];
            px[0] = static_cast<std::uint8_t>(r * outer);
            px[1] = static_cast<std::uint8_t>(g * outer);
            px[2] = static_cast<std::uint8_t>(b * outer);
            px[3] = static_cast<std::uint8_t>(255.0f * outer);
        }
    bool ok = false;
    const QByteArray svg = resources.getResource(
        QStringLiteral("map/icons/%1.svg").arg(QString::fromLatin1(icon)), &ok);
    if (ok && !svg.isEmpty()) {
        constexpr int kIcon = 16;
        const auto glyph = image_from_skia(
            OsmAnd::SkiaUtilities::createImageFromVectorData(svg, kIcon, kIcon));
        if (glyph) {
            const int ox = (kSize - glyph->width) / 2, oy = (kSize - glyph->height) / 2;
            for (int y = 0; y < glyph->height; ++y)
                for (int x = 0; x < glyph->width; ++x) {
                    const auto* src = &glyph->rgba[(y * glyph->width + x) * 4];
                    auto* dst = &out->rgba[((y + oy) * kSize + x + ox) * 4];
                    const int inv = 255 - src[3];
                    for (int c = 0; c < 4; ++c)
                        dst[c] = static_cast<std::uint8_t>(src[c] + dst[c] * inv / 255);
                }
        }
    }
    return out;
}

// Google-style location dot: soft blue halo, white rim, blue centre.
LabelImagePtr make_location_dot()
{
    constexpr int kSize = 44;
    auto out = std::make_shared<LabelImage>();
    out->width = out->height = kSize;
    out->rgba.assign(kSize * kSize * 4, 0);
    const float c = (kSize - 1) / 2.0f;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const float d = std::hypot(x - c, y - c);
            const float halo = std::clamp(21.5f - d, 0.0f, 1.0f) * 0.22f;
            const float rim = std::clamp(10.5f - d, 0.0f, 1.0f);
            const float core = std::clamp(8.0f - d, 0.0f, 1.0f);
            float r = 26 * halo, g = 115 * halo, b = 232 * halo, a = halo;
            r = r * (1 - rim) + 255 * rim;
            g = g * (1 - rim) + 255 * rim;
            b = b * (1 - rim) + 255 * rim;
            a = a * (1 - rim) + rim;
            r = r * (1 - core) + 26 * core;
            g = g * (1 - core) + 115 * core;
            b = b * (1 - core) + 232 * core;
            a = a * (1 - core) + core;
            auto* px = &out->rgba[(y * kSize + x) * 4];
            px[0] = static_cast<std::uint8_t>(r);
            px[1] = static_cast<std::uint8_t>(g);
            px[2] = static_cast<std::uint8_t>(b);
            px[3] = static_cast<std::uint8_t>(a * 255.0f);
        }
    return out;
}

struct StreetLabel {
    std::string key;     // atlas key (text + style)
    std::string id;      // one map object: name + first point
    std::string name;
    LabelImagePtr image;
    int rank = 0;
    Ring path;           // tile-local, not clipped
};
struct PoiLabel {
    std::string text_key, icon_key, name, id;
    LabelImagePtr text, icon;
    int rank = 0;
    int min_zoom = 15;
    Point pos;
};
struct PlaceLabel {
    std::string key, place;
    LabelImagePtr image;
    Point pos;
};
struct TileLabels {
    std::vector<StreetLabel> streets;
    std::vector<PoiLabel> pois;
    std::vector<PlaceLabel> places;
};

struct BuiltTile {
    bool ok = false;
    TileKey key;
    TileMesh mesh;
    TileLabels labels;
    double fetch_ms = 0.0;
    double build_ms = 0.0;
    double labels_ms = 0.0;
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

const std::string* tag_value(const Tags& tags, const char* name)
{
    for (const auto& entry : tags)
        if (entry.first == name)
            return &entry.second;
    return nullptr;
}

class TileBuilder {
public:
    TileBuilder(std::shared_ptr<const OsmAnd::IObfsCollection> obfs,
                std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources,
                int threads, std::function<void()> on_ready)
        : provider_(std::make_shared<OsmAnd::ObfMapObjectsProvider>(
              obfs, OsmAnd::ObfMapObjectsProvider::Mode::OnlyBinaryMapObjects)),
          resources_(std::move(resources)), on_ready_(std::move(on_ready))
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
    // An evicted tile may be wanted again later.
    void forget(const TileKey& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done_.erase(key);
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
        (void)navicore_osmand::lower_current_thread_priority(2);
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
            BuiltTile tile;
            try {
                tile = build(key, cache);
            } catch (...) {
                tile = BuiltTile{};
                tile.key = key;
            }
            if (!tile.ok) {
                // No data yet (map still opening) or a failure: retry later
                // instead of caching an empty tile.
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                std::lock_guard<std::mutex> lock(mutex_);
                done_.erase(key);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ready_.push_back(std::move(tile));
            }
            if (on_ready_)
                on_ready_();
        }
    }

    static const std::pair<std::string, std::string>& tag(
        TagCache& cache, const OsmAnd::MapObject& object, std::uint32_t index,
        bool additional)
    {
        const auto& ids = additional ? object.additionalAttributeIds : object.attributeIds;
        const TagKey key{object.attributeMapping.get(), ids[static_cast<int>(index)]};
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
        std::pair<std::string, std::string> value;
        if (const auto* resolved = object.resolveAttributeByIndex(index, additional)) {
            value.first = resolved->tag.toStdString();
            value.second = resolved->value.toStdString();
        }
        return cache.emplace(key, std::move(value)).first->second;
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

        const auto build_started = Clock::now();
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
        double labels_ms = 0.0;
        for (const auto& object : data->mapObjects) {
            if (!object || object->isCoastline || object->points31.isEmpty())
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
            {
                const auto label_started = Clock::now();
                collect_labels(*object, tags, area, origin_x, origin_y, to_local, out.labels);
                labels_ms += ms_since(label_started);
            }
            if (object->points31.size() < 2)
                continue;
            const FeatureStyle style = classify(tags, area, key.z);
            if (!style.has_area && !style.has_line)
                continue;
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
        out.ok = true;
        out.labels_ms = labels_ms;
        out.build_ms = ms_since(build_started) - labels_ms;
        return out;
    }

    // Street names (with the road's whole path), POIs and place names.
    // Point-like labels are kept only by the tile that contains them.
    void collect_labels(const OsmAnd::MapObject& object, const Tags& tags, bool area,
                        double origin_x, double origin_y, double to_local,
                        TileLabels& out)
    {
        const QString caption = object.getCaptionInNativeLanguage();
        if (caption.isEmpty())
            return;
        const std::string name = caption.toStdString();
        const auto anchor = [&]() {
            double x, y;
            if (object.points31.size() == 1) {
                x = object.points31[0].x;
                y = object.points31[0].y;
            } else if (object.isLabelCoordinatesSpecified()) {
                x = object.getLabelCoordinateX();
                y = object.getLabelCoordinateY();
            } else {
                double sx = 0, sy = 0;
                for (const auto& p : object.points31) {
                    sx += p.x;
                    sy += p.y;
                }
                x = sx / object.points31.size();
                y = sy / object.points31.size();
            }
            return Point{static_cast<float>((x - origin_x) * to_local),
                         static_cast<float>((y - origin_y) * to_local)};
        };
        const auto inside = [](const Point& p) {
            return p[0] >= 0.0f && p[0] < kTileExtent && p[1] >= 0.0f && p[1] < kTileExtent;
        };

        const PoiStyle poi = classify_poi(tags, 99);
        if (poi.show) {
            const Point pos = anchor();
            if (!inside(pos))
                return;
            PoiLabel label;
            label.name = name;
            label.rank = poi.rank;
            label.min_zoom = poi.min_zoom;
            label.pos = pos;
            const std::uint32_t argb = 0xFF000000u | (poi.colour.r << 16) |
                                       (poi.colour.g << 8) | poi.colour.b;
            label.text_key = "p|" + std::to_string(argb) + "|" + name;
            label.id = label.text_key + "@" +
                       std::to_string(static_cast<long long>(origin_x + pos[0] / to_local)) + "," +
                       std::to_string(static_cast<long long>(origin_y + pos[1] / to_local));
            label.text = images_.get(label.text_key, [&] {
                return rasterize_text(caption, 17.0f, true, argb);
            });
            label.icon_key = std::string("i|") + poi.icon + "|" + std::to_string(argb);
            label.icon = images_.get(label.icon_key, [&] {
                return make_poi_icon(*resources_, poi.icon, poi.colour);
            });
            if (label.text && label.icon)
                out.pois.push_back(std::move(label));
            return;
        }
        if (const auto* place = tag_value(tags, "place")) {
            bool shown = false;
            for (int z = 10; z <= 18 && !shown; ++z)
                shown = place_label_rank(*place, z) >= 0;
            const Point pos = anchor();
            if (!shown || !inside(pos))
                return;
            PlaceLabel label;
            label.place = *place;
            label.pos = pos;
            label.key = "c|" + name;
            label.image = images_.get(label.key, [&] {
                return rasterize_text(caption, 20.0f, true, 0xFF70757Au);
            });
            if (label.image)
                out.places.push_back(std::move(label));
            return;
        }
        const auto* highway = tag_value(tags, "highway");
        if (!highway || area || object.points31.size() < 2)
            return;
        const int rank = street_label_rank(*highway);
        if (rank < 0)
            return;
        StreetLabel label;
        label.name = name;
        label.rank = rank;
        const std::uint32_t argb = rank <= 3 ? 0xFF3C4043u : 0xFF5F6368u;
        label.key = "s|" + std::to_string(argb) + "|" + name;
        label.id = name + "@" + std::to_string(object.points31[0].x) + "," +
                   std::to_string(object.points31[0].y);
        label.image = images_.get(label.key, [&] {
            return rasterize_text(caption, 17.0f, false, argb);
        });
        if (!label.image)
            return;
        label.path.reserve(static_cast<std::size_t>(object.points31.size()));
        for (const auto& p : object.points31)
            label.path.push_back({static_cast<float>((p.x - origin_x) * to_local),
                                  static_cast<float>((p.y - origin_y) * to_local)});
        out.streets.push_back(std::move(label));
    }

    std::shared_ptr<OsmAnd::ObfMapObjectsProvider> provider_;
    std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources_;
    std::function<void()> on_ready_;
    ImageCache images_;
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

// ---- GL ----------------------------------------------------------------------

GLuint compile(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "vector_map shader: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint program(const char* vs, const char* fs, std::initializer_list<const char*> attributes)
{
    const GLuint v = compile(GL_VERTEX_SHADER, vs);
    const GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f)
        return 0;
    const GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    GLuint index = 0;
    for (const char* name : attributes)
        glBindAttribLocation(p, index++, name);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// u_m maps tile-local coordinates to homogeneous clip space; w carries the
// camera's perspective.
const char* kAreaVs =
    "attribute vec2 a_pos; attribute vec4 a_color;"
    "uniform mat3 u_m; varying lowp vec4 v_color;"
    "void main(){ v_color = a_color; vec3 p = u_m * vec3(a_pos, 1.0);"
    "  gl_Position = vec4(p.xy, 0.0, p.z); }";
const char* kAreaFs =
    "varying lowp vec4 v_color; void main(){ gl_FragColor = v_color; }";
// Lines extrude in screen space: the local normal is pushed through the
// projection to find its on-screen direction, then scaled to pixels.
const char* kLineVs =
    "attribute vec2 a_pos; attribute vec2 a_normal; attribute float a_side;"
    "attribute float a_width; attribute float a_cwidth;"
    "attribute vec4 a_fill; attribute vec4 a_casing;"
    "uniform mat3 u_m; uniform vec2 u_half_vp; uniform float u_eps;"
    "uniform float u_wscale; uniform float u_casing;"
    "varying mediump float v_dist; varying mediump float v_half; varying lowp vec4 v_color;"
    "void main(){"
    "  float w = mix(a_width, a_cwidth, u_casing);"
    "  float hw = w > 0.0 ? max(w * u_wscale, 1.0) * 0.5 + 0.5 : 0.0;"
    "  v_color = mix(a_fill, a_casing, u_casing);"
    "  v_dist = a_side * hw; v_half = hw;"
    "  vec3 p = u_m * vec3(a_pos, 1.0);"
    "  vec3 q = u_m * vec3(a_pos + a_normal * u_eps, 1.0);"
    "  vec2 sp = p.xy / p.z;"
    "  vec2 d = (q.xy / q.z - sp) * u_half_vp;"
    "  float len = length(d);"
    "  vec2 off = len > 0.000001 ? d / len * length(a_normal) * hw : vec2(0.0);"
    "  gl_Position = vec4((sp + off / u_half_vp) * p.z, 0.0, p.z);"
    "}";
const char* kLineFs =
    "precision mediump float;"
    "varying mediump float v_dist; varying mediump float v_half; varying lowp vec4 v_color;"
    "void main(){ float a = clamp(v_half - abs(v_dist), 0.0, 1.0);"
    "  gl_FragColor = vec4(v_color.rgb, v_color.a * a); }";
const char* kLabelVs =
    "attribute vec2 a_pos; attribute vec2 a_uv; attribute float a_alpha;"
    "varying mediump vec2 v_uv; varying lowp float v_alpha;"
    "void main(){ v_uv = a_uv; v_alpha = a_alpha; gl_Position = vec4(a_pos, 0.0, 1.0); }";
const char* kLabelFs =
    "precision mediump float; uniform sampler2D u_tex;"
    "varying mediump vec2 v_uv; varying lowp float v_alpha;"
    "void main(){ gl_FragColor = texture2D(u_tex, v_uv) * v_alpha; }";

// One RGBA texture with a shelf packer. When it fills up it is cleared and
// visible labels are packed again on the following frames.
class LabelAtlas {
public:
    static constexpr int kSize = 2048;
    struct Slot {
        float u0, v0, u1, v1;
    };
    void init()
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        slots_.clear();
        x_ = y_ = row_ = 0;
    }
    void release(bool context_lost)
    {
        if (texture_ && !context_lost)
            glDeleteTextures(1, &texture_);
        texture_ = 0;
        slots_.clear();
    }
    GLuint texture() const { return texture_; }
    std::size_t resets() const { return resets_; }
    std::size_t uploads() const { return uploads_; }
    // False when the atlas had to be cleared; the label waits a frame.
    bool slot(const std::string& key, const LabelImage& image, Slot& out)
    {
        const auto it = slots_.find(key);
        if (it != slots_.end()) {
            out = it->second;
            return true;
        }
        if (image.width + 2 > kSize || image.height + 2 > kSize)
            return false;
        if (x_ + image.width + 2 > kSize) {
            x_ = 0;
            y_ += row_ + 2;
            row_ = 0;
        }
        if (y_ + image.height + 2 > kSize) {
            slots_.clear();
            x_ = y_ = row_ = 0;
            ++resets_;
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x_ + 1, y_ + 1, image.width, image.height,
                        GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        ++uploads_;
        const float k = 1.0f / kSize;
        out = {(x_ + 1) * k, (y_ + 1) * k, (x_ + 1 + image.width) * k,
               (y_ + 1 + image.height) * k};
        slots_[key] = out;
        x_ += image.width + 2;
        row_ = std::max(row_, image.height);
        return true;
    }

private:
    GLuint texture_ = 0;
    int x_ = 0, y_ = 0, row_ = 0;
    std::size_t resets_ = 0, uploads_ = 0;
    std::unordered_map<std::string, Slot> slots_;
};

struct LabelVertex {
    float x, y, u, v, alpha;
};

struct GpuMesh {
    GLuint buffers[4] = {};  // area vbo, area ibo, line vbo, line ibo
    GLsizei area_count = 0;
    GLsizei line_count = 0;
    std::size_t bytes = 0;
    std::uint64_t last_used = 0;
};

GpuMesh upload(const TileMesh& mesh)
{
    GpuMesh gpu;
    glGenBuffers(4, gpu.buffers);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, mesh.area_vertices.size() * sizeof(AreaVertex),
                 mesh.area_vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.buffers[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.area_indices.size() * 4,
                 mesh.area_indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.buffers[2]);
    glBufferData(GL_ARRAY_BUFFER, mesh.line_vertices.size() * sizeof(LineVertex),
                 mesh.line_vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.buffers[3]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.line_indices.size() * 4,
                 mesh.line_indices.data(), GL_STATIC_DRAW);
    gpu.area_count = static_cast<GLsizei>(mesh.area_indices.size());
    gpu.line_count = static_cast<GLsizei>(mesh.line_indices.size());
    gpu.bytes = mesh.area_vertices.size() * sizeof(AreaVertex) +
                mesh.line_vertices.size() * sizeof(LineVertex) +
                (mesh.area_indices.size() + mesh.line_indices.size()) * 4;
    return gpu;
}

// Local -> world: world31 = origin + local * k.
struct Placement {
    double ox, oy, k;
};

Placement tile_placement(const TileKey& key)
{
    const double tile31 = std::ldexp(1.0, 31 - key.z);
    return {key.x * tile31, key.y * tile31, tile31 / kTileExtent};
}

void project(const MapView& v, double x31, double y31, double& sx, double& sy, double& w)
{
    const double dx = x31 - v.origin_x31, dy = y31 - v.origin_y31;
    const double X = v.h[0] * dx + v.h[1] * dy + v.h[2];
    const double Y = v.h[3] * dx + v.h[4] * dy + v.h[5];
    w = v.h[6] * dx + v.h[7] * dy + v.h[8];
    sx = X / w;
    sy = Y / w;
}

// Column-major mat3: local -> homogeneous clip.
void clip_matrix(const MapView& v, const Placement& p, float out[9])
{
    // T: local -> world relative to the view origin.
    const double t[9] = {p.k, 0, p.ox - v.origin_x31, 0, p.k, p.oy - v.origin_y31, 0, 0, 1};
    // C: screen px -> clip.
    const double c[9] = {2.0 / v.width, 0, -1, 0, -2.0 / v.height, 1, 0, 0, 1};
    double ht[9], m[9];
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col) {
            ht[r * 3 + col] = 0;
            for (int i = 0; i < 3; ++i)
                ht[r * 3 + col] += v.h[r * 3 + i] * t[i * 3 + col];
        }
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col) {
            m[r * 3 + col] = 0;
            for (int i = 0; i < 3; ++i)
                m[r * 3 + col] += c[r * 3 + i] * ht[i * 3 + col];
        }
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col)
            out[col * 3 + r] = static_cast<float>(m[r * 3 + col]);
}

double percentile_max(double a, double b) { return a > b ? a : b; }

} // namespace

// ---- VectorMap -----------------------------------------------------------------

struct VectorMap::Impl {
    Impl(std::shared_ptr<const OsmAnd::IObfsCollection> obfs,
         std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources, int threads,
         std::function<void()> on_ready)
        : builder(std::move(obfs), std::move(resources), threads, std::move(on_ready))
    {
    }

    TileBuilder builder;
    bool gl_ready = false;
    GLuint area_prog = 0, line_prog = 0, label_prog = 0;
    GLint area_m = -1, line_m = -1, line_half_vp = -1, line_eps = -1, line_wscale = -1,
          line_casing = -1, label_tex = -1;
    LabelAtlas atlas;
    GLuint label_vbo = 0, label_ibo = 0;
    PFNGLBINDVERTEXARRAYOESPROC bind_vao = nullptr;
    std::map<TileKey, GpuMesh> gpu;
    std::map<TileKey, TileLabels> labels;
    std::size_t gpu_bytes = 0;
    std::uint64_t frame = 0;
    LabelImagePtr location_dot = make_location_dot();
    std::unordered_map<std::string, float> fade, next_fade;
    std::vector<LabelVertex> label_vertices;
    std::vector<std::uint16_t> label_indices;
    // Route: one mesh relative to its first point.
    std::mutex route_mutex;
    bool route_dirty = false;
    std::vector<std::pair<std::int32_t, std::int32_t>> route_points;
    GpuMesh route_mesh;
    Placement route_placement{0, 0, 1};
    bool last_incomplete = false;
    mutable std::mutex stats_mutex;
    VectorMapStats stats;

    void draw(const MapView& view, double dt, bool clear, bool& wants_more);
    void place_labels(const MapView& view, const std::vector<TileKey>& draw, double zoom,
                      double dt, std::size_t& calls);
    void update_route();
};

VectorMap::VectorMap(std::shared_ptr<const OsmAnd::IObfsCollection> obfs,
                     std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources,
                     int threads, std::function<void()> on_tile_ready)
    : impl_(new Impl(std::move(obfs), std::move(resources), threads, std::move(on_tile_ready)))
{
}

VectorMap::~VectorMap() = default;

bool VectorMap::init_gl()
{
    auto& d = *impl_;
    d.area_prog = program(kAreaVs, kAreaFs, {"a_pos", "a_color"});
    d.line_prog = program(kLineVs, kLineFs,
                          {"a_pos", "a_normal", "a_side", "a_width", "a_cwidth", "a_fill", "a_casing"});
    d.label_prog = program(kLabelVs, kLabelFs, {"a_pos", "a_uv", "a_alpha"});
    if (!d.area_prog || !d.line_prog || !d.label_prog)
        return false;
    d.area_m = glGetUniformLocation(d.area_prog, "u_m");
    d.line_m = glGetUniformLocation(d.line_prog, "u_m");
    d.line_half_vp = glGetUniformLocation(d.line_prog, "u_half_vp");
    d.line_eps = glGetUniformLocation(d.line_prog, "u_eps");
    d.line_wscale = glGetUniformLocation(d.line_prog, "u_wscale");
    d.line_casing = glGetUniformLocation(d.line_prog, "u_casing");
    d.label_tex = glGetUniformLocation(d.label_prog, "u_tex");
    d.atlas.init();
    glGenBuffers(1, &d.label_vbo);
    glGenBuffers(1, &d.label_ibo);
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions && std::strstr(extensions, "GL_OES_vertex_array_object"))
        d.bind_vao = reinterpret_cast<PFNGLBINDVERTEXARRAYOESPROC>(
            eglGetProcAddress("glBindVertexArrayOES"));
    {
        std::lock_guard<std::mutex> lock(d.route_mutex);
        d.route_dirty = true;
    }
    d.gl_ready = true;
    return true;
}

void VectorMap::release_gl(bool context_lost)
{
    auto& d = *impl_;
    if (!context_lost) {
        for (auto& entry : d.gpu)
            glDeleteBuffers(4, entry.second.buffers);
        if (d.route_mesh.buffers[0])
            glDeleteBuffers(4, d.route_mesh.buffers);
        if (d.label_vbo) glDeleteBuffers(1, &d.label_vbo);
        if (d.label_ibo) glDeleteBuffers(1, &d.label_ibo);
        if (d.area_prog) glDeleteProgram(d.area_prog);
        if (d.line_prog) glDeleteProgram(d.line_prog);
        if (d.label_prog) glDeleteProgram(d.label_prog);
    }
    d.atlas.release(context_lost);
    for (const auto& entry : d.gpu)
        d.builder.forget(entry.first);
    d.gpu.clear();
    d.labels.clear();
    d.gpu_bytes = 0;
    d.route_mesh = GpuMesh{};
    d.label_vbo = d.label_ibo = 0;
    d.area_prog = d.line_prog = d.label_prog = 0;
    d.gl_ready = false;
}

void VectorMap::set_route(const std::vector<std::pair<std::int32_t, std::int32_t>>& points31)
{
    std::lock_guard<std::mutex> lock(impl_->route_mutex);
    impl_->route_points = points31;
    impl_->route_dirty = true;
}

void VectorMap::Impl::update_route()
{
    std::vector<std::pair<std::int32_t, std::int32_t>> points;
    {
        std::lock_guard<std::mutex> lock(route_mutex);
        if (!route_dirty)
            return;
        route_dirty = false;
        points = route_points;
    }
    if (route_mesh.buffers[0])
        glDeleteBuffers(4, route_mesh.buffers);
    route_mesh = GpuMesh{};
    if (points.size() < 2)
        return;
    // Local units of 16 31-bit units keep float precision across a city.
    route_placement = {static_cast<double>(points[0].first),
                       static_cast<double>(points[0].second), 16.0};
    Ring line;
    line.reserve(points.size());
    for (const auto& p : points)
        line.push_back({static_cast<float>((p.first - route_placement.ox) / 16.0),
                        static_cast<float>((p.second - route_placement.oy) / 16.0)});
    TileTessellator tessellator;
    const LineStyle style{LineLayer::Motorway, {26, 115, 232, 255}, {21, 87, 176, 255},
                          9.0f, 12.0f};
    tessellator.add_line_unclipped(line, style);
    route_mesh = upload(tessellator.finish());
}

double VectorMap::zoom_of(const MapView& view)
{
    const double step = std::ldexp(1.0, 31 - 18);
    double x0, y0, w0, x1, y1, w1, x2, y2, w2;
    project(view, view.origin_x31, view.origin_y31, x0, y0, w0);
    project(view, view.origin_x31 + step, view.origin_y31, x1, y1, w1);
    project(view, view.origin_x31, view.origin_y31 + step, x2, y2, w2);
    const double px = (std::hypot(x1 - x0, y1 - y0) + std::hypot(x2 - x0, y2 - y0)) / 2.0;
    if (!(px > 0.0))
        return 0.0;
    return std::log2(px / step * std::ldexp(1.0, 31) / kVectorTilePx);
}

void VectorMap::set_flat_camera(MapView& view, double x31, double y31, double zoom,
                                double azimuth_degrees, double anchor_x, double anchor_y)
{
    const double s = kVectorTilePx * std::pow(2.0, zoom) / std::ldexp(1.0, 31);
    const double phi = -azimuth_degrees * 3.14159265358979323846 / 180.0;
    const double cs = std::cos(phi), sn = std::sin(phi);
    view.origin_x31 = x31;
    view.origin_y31 = y31;
    const double h[9] = {s * cs, -s * sn, anchor_x, s * sn, s * cs, anchor_y, 0, 0, 1};
    std::copy(h, h + 9, view.h);
    double min_x = 1e300, min_y = 1e300, max_x = -1e300, max_y = -1e300;
    for (const auto& corner : {std::pair<double, double>{0, 0}, {double(view.width), 0},
                               {0, double(view.height)}, {double(view.width), double(view.height)}}) {
        const double sx = (corner.first - anchor_x) / s;
        const double sy = (corner.second - anchor_y) / s;
        const double wx = cs * sx + sn * sy;
        const double wy = -sn * sx + cs * sy;
        min_x = std::min(min_x, x31 + wx);
        max_x = std::max(max_x, x31 + wx);
        min_y = std::min(min_y, y31 + wy);
        max_y = std::max(max_y, y31 + wy);
    }
    view.min_x31 = min_x;
    view.min_y31 = min_y;
    view.max_x31 = max_x;
    view.max_y31 = max_y;
    view.dot_x = static_cast<float>(anchor_x);
    view.dot_y = static_cast<float>(anchor_y);
}

bool VectorMap::set_from_correspondences(MapView& view, const double world31[4][2],
                                         const double screen[4][2])
{
    // Normalised DLT: u = (world - origin) / scale.
    double scale = 1.0;
    for (int i = 0; i < 4; ++i)
        scale = std::max({scale, std::fabs(world31[i][0] - view.origin_x31),
                          std::fabs(world31[i][1] - view.origin_y31)});
    double a[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double u = (world31[i][0] - view.origin_x31) / scale;
        const double v = (world31[i][1] - view.origin_y31) / scale;
        const double X = screen[i][0], Y = screen[i][1];
        const double r0[9] = {u, v, 1, 0, 0, 0, -u * X, -v * X, X};
        const double r1[9] = {0, 0, 0, u, v, 1, -u * Y, -v * Y, Y};
        std::copy(r0, r0 + 9, a[2 * i]);
        std::copy(r1, r1 + 9, a[2 * i + 1]);
    }
    for (int c = 0; c < 8; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 8; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[pivot][c]))
                pivot = r;
        if (std::fabs(a[pivot][c]) < 1e-12)
            return false;
        for (int k = 0; k < 9; ++k)
            std::swap(a[c][k], a[pivot][k]);
        for (int r = 0; r < 8; ++r) {
            if (r == c)
                continue;
            const double f = a[r][c] / a[c][c];
            for (int k = c; k < 9; ++k)
                a[r][k] -= f * a[c][k];
        }
    }
    double hn[9];
    for (int i = 0; i < 8; ++i)
        hn[i] = a[i][8] / a[i][i];
    hn[8] = 1.0;
    // Fold the normalisation back in: columns 0 and 1 divided by scale.
    for (int r = 0; r < 3; ++r) {
        view.h[r * 3 + 0] = hn[r * 3 + 0] / scale;
        view.h[r * 3 + 1] = hn[r * 3 + 1] / scale;
        view.h[r * 3 + 2] = hn[r * 3 + 2];
    }
    return true;
}

bool VectorMap::render(const MapView& view, double dt_seconds, bool clear)
{
    auto& d = *impl_;
    if (!d.gl_ready || view.width <= 0 || view.height <= 0)
        return false;
    bool wants_more = false;
    d.draw(view, dt_seconds, clear, wants_more);
    return wants_more;
}

void VectorMap::Impl::draw(const MapView& view, double dt, bool clear, bool& wants_more)
{
    const double zoom = VectorMap::zoom_of(view);
    const int tz = std::clamp(static_cast<int>(std::floor(zoom)), kMinTileZoom, kMaxTileZoom);

    // Finished tiles.
    for (auto& tile : builder.take()) {
        {
            std::lock_guard<std::mutex> lock(stats_mutex);
            ++stats.tiles_built;
            stats.fetch_ms_total += tile.fetch_ms;
            stats.fetch_ms_max = percentile_max(stats.fetch_ms_max, tile.fetch_ms);
            stats.build_ms_total += tile.build_ms;
            stats.labels_ms_total += tile.labels_ms;
        }
        GpuMesh mesh = upload(tile.mesh);
        mesh.last_used = frame;
        gpu_bytes += mesh.bytes;
        auto existing = gpu.find(tile.key);
        if (existing != gpu.end()) {
            glDeleteBuffers(4, existing->second.buffers);
            gpu_bytes -= existing->second.bytes;
        }
        gpu[tile.key] = mesh;
        labels[tile.key] = std::move(tile.labels);
    }
    update_route();

    // Wanted tiles: the visible area at tile zoom tz, nearest first.
    const double size31 = std::ldexp(1.0, 31 - tz);
    const double max31 = std::ldexp(1.0, 31) - 1.0;
    const int x0 = static_cast<int>(std::clamp(view.min_x31, 0.0, max31) / size31);
    const int x1 = static_cast<int>(std::clamp(view.max_x31, 0.0, max31) / size31);
    const int y0 = static_cast<int>(std::clamp(view.min_y31, 0.0, max31) / size31);
    const int y1 = static_cast<int>(std::clamp(view.max_y31, 0.0, max31) / size31);
    std::vector<TileKey> wanted;
    for (int y = y0; y <= y1 && wanted.size() < 4096; ++y)
        for (int x = x0; x <= x1; ++x)
            wanted.push_back({tz, x, y});
    const auto distance = [&](const TileKey& k) {
        return std::hypot((k.x + 0.5) * size31 - view.origin_x31,
                          (k.y + 0.5) * size31 - view.origin_y31);
    };
    std::sort(wanted.begin(), wanted.end(),
              [&](const TileKey& a, const TileKey& b) { return distance(a) < distance(b); });
    if (wanted.size() > kMaxWantedTiles)
        wanted.resize(kMaxWantedTiles);
    builder.want(wanted);

    // Draw list: wanted tiles, or their nearest ready ancestor / children.
    std::vector<TileKey> draw_list;
    std::size_t missing = 0;
    for (const auto& key : wanted) {
        if (gpu.count(key)) {
            draw_list.push_back(key);
            continue;
        }
        ++missing;
        bool covered = false;
        for (int up = 1; up <= 3 && !covered; ++up) {
            const TileKey parent{key.z - up, key.x >> up, key.y >> up};
            if (gpu.count(parent)) {
                draw_list.push_back(parent);
                covered = true;
            }
        }
        if (!covered)
            for (int cy = 0; cy < 2; ++cy)
                for (int cx = 0; cx < 2; ++cx) {
                    const TileKey child{key.z + 1, key.x * 2 + cx, key.y * 2 + cy};
                    if (gpu.count(child))
                        draw_list.push_back(child);
                }
    }
    std::sort(draw_list.begin(), draw_list.end());
    draw_list.erase(std::unique(draw_list.begin(), draw_list.end()), draw_list.end());
    std::stable_sort(draw_list.begin(), draw_list.end(),
                     [](const TileKey& a, const TileKey& b) { return a.z < b.z; });
    wants_more = missing > 0;
    last_incomplete = missing > 0;

    // GL state after whatever drew before us (OsmAnd, or nothing).
    if (bind_vao)
        bind_vao(0);
    glViewport(0, 0, view.width, view.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    if (clear) {
        const Rgba land = land_colour();
        glClearColor(land.r / 255.0f, land.g / 255.0f, land.b / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    std::size_t calls = 0;
    float m[9];

    glDisable(GL_BLEND);
    glUseProgram(area_prog);
    for (GLuint a = 0; a < 7; ++a)
        glDisableVertexAttribArray(a);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    for (const auto& key : draw_list) {
        auto& mesh = gpu[key];
        mesh.last_used = frame;
        if (!mesh.area_count)
            continue;
        clip_matrix(view, tile_placement(key), m);
        glUniformMatrix3fv(area_m, 1, GL_FALSE, m);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.buffers[0]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.buffers[1]);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(AreaVertex), nullptr);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(AreaVertex),
                              reinterpret_cast<void*>(8));
        glDrawElements(GL_TRIANGLES, mesh.area_count, GL_UNSIGNED_INT, nullptr);
        ++calls;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(line_prog);
    for (GLuint a = 0; a < 7; ++a)
        glEnableVertexAttribArray(a);
    glUniform2f(line_half_vp, view.width / 2.0f, view.height / 2.0f);
    glUniform1f(line_wscale, static_cast<float>(std::pow(2.0, (zoom - 16.0) * (zoom < 16.0 ? 0.5 : 0.75))));
    const auto draw_lines = [&](const GpuMesh& mesh, const Placement& placement) {
        clip_matrix(view, placement, m);
        glUniformMatrix3fv(line_m, 1, GL_FALSE, m);
        // About two pixels along the normal, in local units.
        const double px_per_unit31 = kVectorTilePx * std::pow(2.0, zoom) / std::ldexp(1.0, 31);
        glUniform1f(line_eps, static_cast<float>(
                                  std::max(1e-3, 2.0 / (placement.k * px_per_unit31))));
        glBindBuffer(GL_ARRAY_BUFFER, mesh.buffers[2]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.buffers[3]);
        const GLsizei stride = sizeof(LineVertex);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8));
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(16));
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(20));
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(24));
        glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(28));
        glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(32));
        glDrawElements(GL_TRIANGLES, mesh.line_count, GL_UNSIGNED_INT, nullptr);
        ++calls;
    };
    for (int pass = 0; pass < 2; ++pass) {
        glUniform1f(line_casing, pass == 0 ? 1.0f : 0.0f);
        for (const auto& key : draw_list) {
            const auto& mesh = gpu[key];
            if (mesh.line_count)
                draw_lines(mesh, tile_placement(key));
        }
    }
    // Route above the roads, below the labels.
    if (route_mesh.line_count) {
        for (int pass = 0; pass < 2; ++pass) {
            glUniform1f(line_casing, pass == 0 ? 1.0f : 0.0f);
            draw_lines(route_mesh, route_placement);
        }
    }
    for (GLuint a = 0; a < 7; ++a)
        glDisableVertexAttribArray(a);

    const auto label_started = Clock::now();
    place_labels(view, draw_list, zoom, dt, calls);
    const double label_ms = ms_since(label_started);
    for (const auto& entry : fade)
        if (entry.second < 1.0f)
            wants_more = true;

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);

    // Evict least recently used tiles.
    while (gpu.size() > kMaxGpuTiles) {
        auto oldest = gpu.begin();
        for (auto it = gpu.begin(); it != gpu.end(); ++it)
            if (it->second.last_used < oldest->second.last_used)
                oldest = it;
        if (oldest->second.last_used == frame)
            break;
        glDeleteBuffers(4, oldest->second.buffers);
        gpu_bytes -= oldest->second.bytes;
        labels.erase(oldest->first);
        builder.forget(oldest->first);
        gpu.erase(oldest);
    }
    ++frame;
    std::lock_guard<std::mutex> lock(stats_mutex);
    ++stats.frames;
    if (missing)
        ++stats.incomplete_frames;
    stats.draw_calls += calls;
    stats.label_quads += label_indices.size() / 6;
    stats.label_place_ms_max = percentile_max(stats.label_place_ms_max, label_ms);
    stats.gpu_tiles = gpu.size();
    stats.gpu_bytes = gpu_bytes;
    stats.atlas_uploads = atlas.uploads();
    stats.atlas_resets = atlas.resets();
    stats.tile_zoom = tz;
    stats.zoom = zoom;
}

void VectorMap::Impl::place_labels(const MapView& view, const std::vector<TileKey>& draw_list,
                                   double zoom, double dt, std::size_t& calls)
{
    const int W = view.width, H = view.height;
    const auto to_screen = [&](const TileKey& key, const Point& p, bool& ok) {
        const Placement pl = tile_placement(key);
        double sx, sy, w;
        project(view, pl.ox + p[0] * pl.k, pl.oy + p[1] * pl.k, sx, sy, w);
        ok = w > 0.0;
        return ScreenPoint{static_cast<float>(sx), static_cast<float>(sy)};
    };
    std::vector<OrientedBox> accepted;
    if (view.location_dot || view.dot_x > 0.0f)
        accepted.push_back({view.dot_x, view.dot_y, 14.0f, 14.0f, 0.0f});
    label_vertices.clear();
    label_indices.clear();
    next_fade.clear();
    const auto on_screen = [&](const OrientedBox& box) {
        ScreenPoint c[4];
        navicore_osmand::box_corners(box, c);
        for (const auto& q : c)
            if (q.x < 2 || q.y < 2 || q.x > W - 2 || q.y > H - 2)
                return false;
        return true;
    };
    const auto free_of = [&](const OrientedBox& box) {
        for (const auto& other : accepted)
            if (navicore_osmand::boxes_overlap(box, other, 3.0f))
                return false;
        return true;
    };
    const auto emit_label = [&](const std::string& id, const std::string& key,
                                const LabelImage& image, OrientedBox box) {
        LabelAtlas::Slot slot;
        if (!atlas.slot(key, image, slot))
            return;
        if (box.angle == 0.0f) {  // crisp: snap unrotated quads to pixels
            box.cx = std::floor(box.cx - box.half_width) + box.half_width;
            box.cy = std::floor(box.cy - box.half_height) + box.half_height;
        }
        const auto previous = fade.find(id);
        const float alpha = std::min(
            1.0f, (previous == fade.end() ? 0.0f : previous->second) + static_cast<float>(dt) * 5.0f);
        next_fade[id] = alpha;
        ScreenPoint c[4];
        navicore_osmand::box_corners(box, c);
        const float uv[4][2] = {{slot.u0, slot.v0}, {slot.u1, slot.v0},
                                {slot.u1, slot.v1}, {slot.u0, slot.v1}};
        if (label_vertices.size() + 4 > 65535)
            return;
        const auto base = static_cast<std::uint16_t>(label_vertices.size());
        for (int i = 0; i < 4; ++i)
            label_vertices.push_back({c[i].x * 2.0f / W - 1.0f, 1.0f - c[i].y * 2.0f / H,
                                      uv[i][0], uv[i][1], alpha});
        label_indices.insert(label_indices.end(),
                             {base, static_cast<std::uint16_t>(base + 1),
                              static_cast<std::uint16_t>(base + 2), base,
                              static_cast<std::uint16_t>(base + 2),
                              static_cast<std::uint16_t>(base + 3)});
    };

    // Priority: major streets, places, POIs, other streets; labels shown
    // last frame first within each group.
    struct Candidate {
        int group, shown, rank;
        int kind;  // 0 street, 1 poi, 2 place
        const TileKey* key;
        const void* label;
    };
    std::vector<Candidate> candidates;
    std::unordered_map<std::string, bool> seen_streets;
    const int view_zoom = static_cast<int>(std::floor(zoom));
    for (const auto& key : draw_list) {
        const auto it = labels.find(key);
        if (it == labels.end())
            continue;
        for (const auto& street : it->second.streets)
            if (seen_streets.emplace(street.id, true).second)
                candidates.push_back({street.rank <= 3 ? 0 : 3, fade.count(street.name) ? 0 : 1,
                                      street.rank, 0, &key, &street});
        for (const auto& place : it->second.places) {
            const int rank = place_label_rank(place.place, view_zoom);
            if (rank >= 0)
                candidates.push_back({1, fade.count(place.key) ? 0 : 1, rank, 2, &key, &place});
        }
        for (const auto& poi : it->second.pois)
            if (zoom >= poi.min_zoom)
                candidates.push_back({2, fade.count(poi.id) ? 0 : 1, poi.rank, 1, &key, &poi});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.shown != b.shown) return a.shown < b.shown;
        return a.rank < b.rank;
    });
    std::unordered_map<std::string, std::vector<ScreenPoint>> placed_streets;
    std::size_t streets = 0, pois = 0, places = 0;
    std::vector<ScreenPoint> screen_path;
    std::vector<float> lengths;
    for (const auto& c : candidates) {
        if (c.kind == 0) {
            if (streets >= 16) continue;
            const auto& street = *static_cast<const StreetLabel*>(c.label);
            const auto& image = *street.image;
            lengths.assign(1, 0.0f);
            for (std::size_t i = 1; i < street.path.size(); ++i)
                lengths.push_back(lengths.back() + std::hypot(street.path[i][0] - street.path[i - 1][0],
                                                              street.path[i][1] - street.path[i - 1][1]));
            bool projected = false, path_ok = true;
            for (const float fraction : {0.5f, 0.3f, 0.7f, 0.15f, 0.85f}) {
                const float target = fraction * lengths.back();
                std::size_t seg = 0;
                while (seg + 2 < lengths.size() && lengths[seg + 1] < target)
                    ++seg;
                const float span = lengths[seg + 1] - lengths[seg];
                const float t = span > 0.0f ? (target - lengths[seg]) / span : 0.0f;
                const Point pin{street.path[seg][0] + t * (street.path[seg + 1][0] - street.path[seg][0]),
                                street.path[seg][1] + t * (street.path[seg + 1][1] - street.path[seg][1])};
                bool ok = false;
                const ScreenPoint pin_screen = to_screen(*c.key, pin, ok);
                if (!ok || pin_screen.x < 0 || pin_screen.y < 0 || pin_screen.x > W || pin_screen.y > H)
                    continue;
                bool repeat = false;
                for (const auto& other : placed_streets[street.name])
                    if (std::hypot(other.x - pin_screen.x, other.y - pin_screen.y) < 380.0f)
                        repeat = true;
                if (repeat) break;
                if (!projected) {
                    screen_path.clear();
                    for (const auto& p : street.path) {
                        bool point_ok = false;
                        screen_path.push_back(to_screen(*c.key, p, point_ok));
                        path_ok = path_ok && point_ok;
                    }
                    projected = true;
                }
                if (!path_ok)
                    break;
                OrientedBox box;
                if (!navicore_osmand::place_along_path(screen_path, static_cast<int>(seg), t,
                                                       static_cast<float>(image.width),
                                                       static_cast<float>(image.height), box, 0.5f))
                    continue;
                if (!on_screen(box) || !free_of(box))
                    continue;
                accepted.push_back(box);
                placed_streets[street.name].push_back(pin_screen);
                emit_label(street.name, street.key, image, box);
                ++streets;
                break;
            }
        } else if (c.kind == 1) {
            if (pois >= 18) continue;
            const auto& poi = *static_cast<const PoiLabel*>(c.label);
            bool ok = false;
            const ScreenPoint at = to_screen(*c.key, poi.pos, ok);
            if (!ok) continue;
            const OrientedBox icon{at.x, at.y, poi.icon->width / 2.0f, poi.icon->height / 2.0f, 0.0f};
            if (!on_screen(icon) || !free_of(icon))
                continue;
            const OrientedBox text{at.x + poi.icon->width / 2.0f + 3.0f + poi.text->width / 2.0f, at.y,
                                   poi.text->width / 2.0f, poi.text->height / 2.0f, 0.0f};
            const bool with_text = on_screen(text) && free_of(text);
            accepted.push_back(icon);
            emit_label(poi.id, poi.icon_key, *poi.icon, icon);
            if (with_text) {
                accepted.push_back(text);
                emit_label(poi.id + "|t", poi.text_key, *poi.text, text);
            }
            ++pois;
        } else {
            if (places >= 6) continue;
            const auto& place = *static_cast<const PlaceLabel*>(c.label);
            bool ok = false;
            const ScreenPoint at = to_screen(*c.key, place.pos, ok);
            if (!ok) continue;
            const OrientedBox box{at.x, at.y, place.image->width / 2.0f, place.image->height / 2.0f, 0.0f};
            if (!on_screen(box) || !free_of(box))
                continue;
            accepted.push_back(box);
            emit_label(place.key, place.key, *place.image, box);
            ++places;
        }
    }
    if (view.location_dot) {
        const OrientedBox dot{view.dot_x, view.dot_y, location_dot->width / 2.0f,
                              location_dot->height / 2.0f, 0.0f};
        fade["@location"] = 1.0f;
        emit_label("@location", "@location", *location_dot, dot);
    }
    fade.swap(next_fade);
    if (label_indices.empty())
        return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(label_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.texture());
    glUniform1i(label_tex, 0);
    glBindBuffer(GL_ARRAY_BUFFER, label_vbo);
    glBufferData(GL_ARRAY_BUFFER, label_vertices.size() * sizeof(LabelVertex),
                 label_vertices.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, label_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, label_indices.size() * 2, label_indices.data(),
                 GL_STREAM_DRAW);
    for (GLuint a = 0; a < 3; ++a)
        glEnableVertexAttribArray(a);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(LabelVertex), nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(LabelVertex), reinterpret_cast<void*>(8));
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(LabelVertex), reinterpret_cast<void*>(16));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(label_indices.size()), GL_UNSIGNED_SHORT,
                   nullptr);
    for (GLuint a = 0; a < 3; ++a)
        glDisableVertexAttribArray(a);
    glBindTexture(GL_TEXTURE_2D, 0);
    ++calls;
}

VectorMapStats VectorMap::stats() const
{
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    return impl_->stats;
}

bool VectorMap::last_frame_incomplete() const
{
    return impl_->last_incomplete;
}

} // namespace navicore_vector
