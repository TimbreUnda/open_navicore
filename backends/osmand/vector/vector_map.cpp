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
#include <OsmAndCore/ObfDataInterface.h>
#include <OsmAndCore/Utilities.h>
#include <OsmAndCore/Data/BinaryMapObject.h>
#include <OsmAndCore/SkiaUtilities.h>
#include <OsmAndCore/TextRasterizer.h>
#include <OsmAndCore/Data/MapObject.h>
#include <OsmAndCore/Map/IMapObjectsProvider.h>
#include <OsmAndCore/Map/ObfMapObjectsProvider.h>

#include <OsmAndCore/ignore_warnings_on_external_includes.h>
#include <SkImage.h>
#include <SkImageInfo.h>
#include <OsmAndCore/restore_internal_warnings.h>

#include "gpu_upload_context.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <QByteArray>

#include <sys/stat.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
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
using navicore_osmand::LabelIdMap;
using navicore_osmand::OrientedBox;
using navicore_osmand::ScreenPoint;

constexpr int kMinTileZoom = 11;
constexpr int kMaxTileZoom = 16;  // deeper zooms reuse z16 geometry
// Map data is read in blocks of this zoom (at the detail level of the
// tiles inside), then cut into the tiles: one read instead of up to 16.
constexpr int kBlockZoom = 14;
// Overzoom mode: z14 tiles at full z16 detail are the deepest tiles.
constexpr int kOverzoomTileZoom = 14;
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

LabelImagePtr rasterize_text(const QString& text, float size, bool bold, std::uint32_t argb,
                             bool night)
{
    OsmAnd::TextRasterizer::Style style;
    style.setSize(size)
        .setBold(bold)
        .setColor(OsmAnd::ColorARGB(argb))
        .setHaloColor(OsmAnd::ColorARGB(night ? 0xFF17202Bu : 0xFFFFFFFFu))
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

// Label records are plain data (cached on disk with the tile); ids, keys
// and images are attached for the current palette by attach_images(), so
// placing labels each frame compares 64-bit hashes, never strings.
constexpr int kPlaceZooms = 25;  // place ranks precomputed for zooms 0..24
struct StreetLabel {
    std::string id;      // one map object: name + first point
    std::string name;
    int rank = 0;
    Ring path;           // tile-local, not clipped
    std::uint64_t id_hash = 0;    // hash of id: one candidate per object
    std::uint64_t name_hash = 0;  // hash of name: fade state, repeat spacing
    std::uint64_t key = 0;        // atlas key (text + style)
    LabelImagePtr image;
};
struct PoiLabel {
    std::string name, id;
    int rank = 0;
    int min_zoom = 15;
    Point pos;
    std::uint32_t rgb = 0;
    std::string icon;
    std::uint64_t id_hash = 0;    // fade state of the icon (text: ^ kPoiTextSalt)
    std::uint64_t text_key = 0, icon_key = 0;
    LabelImagePtr text, icon_image;
};
struct PlaceLabel {
    std::string name, place;
    Point pos;
    std::uint64_t key = 0;        // atlas key, also its fade id
    std::array<std::int8_t, kPlaceZooms> rank_at{};  // place_label_rank per zoom
    LabelImagePtr image;
    int rank(int zoom) const { return rank_at[std::clamp(zoom, 0, kPlaceZooms - 1)]; }
};
constexpr std::uint64_t kPoiTextSalt = 0x5bd1e9955bd1e995ull;
struct TileLabels {
    std::vector<StreetLabel> streets;
    std::vector<PoiLabel> pois;
    std::vector<PlaceLabel> places;
};

struct BuiltTile {
    bool ok = false;
    bool aborted = false;    // cancelled: no longer wanted
    bool from_disk = false;
    TileKey key;
    std::uint64_t generation = 0;
    TileMesh mesh;
    TileLabels labels;
    double fetch_ms = 0.0;
    double build_ms = 0.0;
    double labels_ms = 0.0;
    double disk_ms = 0.0;
};

// ---- disk cache ----------------------------------------------------------------

// Tile file: "NVT1", then the mesh arrays and the label records, compressed
// with qCompress. Anything malformed is treated as a miss.
constexpr std::uint32_t kTileFileMagic = 0x3154564Eu;  // "NVT1"

class Writer {
public:
    template <typename T> void pod(const T& v)
    {
        const auto* p = reinterpret_cast<const char*>(&v);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    template <typename T> void array(const std::vector<T>& v)
    {
        pod(static_cast<std::uint32_t>(v.size()));
        const auto* p = reinterpret_cast<const char*>(v.data());
        bytes.insert(bytes.end(), p, p + v.size() * sizeof(T));
    }
    void str(const std::string& s)
    {
        pod(static_cast<std::uint32_t>(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
    std::vector<char> bytes;
};

class Reader {
public:
    Reader(const char* data, std::size_t size) : p_(data), end_(data + size) {}
    template <typename T> bool pod(T& v)
    {
        if (static_cast<std::size_t>(end_ - p_) < sizeof(T))
            return false;
        std::memcpy(&v, p_, sizeof(T));
        p_ += sizeof(T);
        return true;
    }
    template <typename T> bool array(std::vector<T>& v)
    {
        std::uint32_t n = 0;
        if (!pod(n) || static_cast<std::size_t>(end_ - p_) / sizeof(T) < n)
            return false;
        v.resize(n);
        std::memcpy(v.data(), p_, n * sizeof(T));
        p_ += n * sizeof(T);
        return true;
    }
    bool str(std::string& s)
    {
        std::uint32_t n = 0;
        if (!pod(n) || static_cast<std::size_t>(end_ - p_) < n)
            return false;
        s.assign(p_, n);
        p_ += n;
        return true;
    }
    bool done() const { return p_ == end_; }

private:
    const char* p_;
    const char* end_;
};

std::vector<char> encode_tile(const TileMesh& mesh, const TileLabels& labels)
{
    Writer w;
    w.pod(kTileFileMagic);
    w.array(mesh.area_vertices);
    w.array(mesh.area_indices);
    w.array(mesh.line_vertices);
    w.array(mesh.line_indices);
    w.pod(static_cast<std::uint32_t>(labels.streets.size()));
    for (const auto& s : labels.streets) {
        w.str(s.id);
        w.str(s.name);
        w.pod(static_cast<std::int32_t>(s.rank));
        w.array(s.path);
    }
    w.pod(static_cast<std::uint32_t>(labels.pois.size()));
    for (const auto& p : labels.pois) {
        w.str(p.id);
        w.str(p.name);
        w.pod(static_cast<std::int32_t>(p.rank));
        w.pod(static_cast<std::int32_t>(p.min_zoom));
        w.pod(p.pos);
        w.pod(p.rgb);
        w.str(p.icon);
    }
    w.pod(static_cast<std::uint32_t>(labels.places.size()));
    for (const auto& p : labels.places) {
        w.str(p.name);
        w.str(p.place);
        w.pod(p.pos);
    }
    return std::move(w.bytes);
}

bool decode_tile(const char* data, std::size_t size, TileMesh& mesh, TileLabels& labels)
{
    Reader r(data, size);
    std::uint32_t magic = 0, n = 0;
    if (!r.pod(magic) || magic != kTileFileMagic || !r.array(mesh.area_vertices) ||
        !r.array(mesh.area_indices) || !r.array(mesh.line_vertices) ||
        !r.array(mesh.line_indices))
        return false;
    // Indices must stay inside their vertex arrays.
    for (const auto i : mesh.area_indices)
        if (i >= mesh.area_vertices.size()) return false;
    for (const auto i : mesh.line_indices)
        if (i >= mesh.line_vertices.size()) return false;
    if (!r.pod(n) || n > 100000) return false;
    labels.streets.resize(n);
    for (auto& s : labels.streets) {
        std::int32_t rank = 0;
        if (!r.str(s.id) || !r.str(s.name) || !r.pod(rank) || !r.array(s.path) ||
            s.path.size() < 2)
            return false;
        s.rank = rank;
    }
    if (!r.pod(n) || n > 100000) return false;
    labels.pois.resize(n);
    for (auto& p : labels.pois) {
        std::int32_t rank = 0, min_zoom = 0;
        if (!r.str(p.id) || !r.str(p.name) || !r.pod(rank) || !r.pod(min_zoom) ||
            !r.pod(p.pos) || !r.pod(p.rgb) || !r.str(p.icon))
            return false;
        p.rank = rank;
        p.min_zoom = min_zoom;
    }
    if (!r.pod(n) || n > 100000) return false;
    labels.places.resize(n);
    for (auto& p : labels.places)
        if (!r.str(p.name) || !r.str(p.place) || !r.pod(p.pos))
            return false;
    return r.done();
}

std::uint64_t fnv1a(const std::string& text)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

bool make_dirs(const std::string& path)
{
    for (std::size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') {
            const std::string part = path.substr(0, i);
            if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST)
                return false;
        }
    return true;
}

class DiskCache {
public:
    void configure(const std::string& dir, const std::string& identity,
                   std::uint64_t min_free)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dir_ = dir;
        identity_ = identity;
        min_free_ = min_free;
        free_checked_ = 0;
    }
    bool enabled() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return !dir_.empty();
    }
    bool load(const TileKey& key, const std::string& variant, TileMesh& mesh,
              TileLabels& labels) const
    {
        const std::string path = file(key, variant);
        if (path.empty())
            return false;
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return false;
        QByteArray packed;
        char buffer[16384];
        std::size_t got = 0;
        while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
            packed.append(buffer, static_cast<int>(got));
        std::fclose(f);
        const QByteArray raw = qUncompress(packed);
        if (raw.isEmpty() || !decode_tile(raw.constData(), static_cast<std::size_t>(raw.size()),
                                          mesh, labels)) {
            std::remove(path.c_str());
            return false;
        }
        return true;
    }
    bool store(const TileKey& key, const std::string& variant, const TileMesh& mesh,
               const TileLabels& labels)
    {
        const std::string path = file(key, variant);
        if (path.empty() || !room())
            return false;
        const auto raw = encode_tile(mesh, labels);
        const QByteArray packed =
            qCompress(reinterpret_cast<const uchar*>(raw.data()), static_cast<int>(raw.size()), 1);
        const std::size_t slash = path.rfind('/');
        if (!make_dirs(path.substr(0, slash)))
            return false;
        const std::string temp = path + ".tmp";
        std::FILE* f = std::fopen(temp.c_str(), "wb");
        if (!f)
            return false;
        const bool written =
            std::fwrite(packed.constData(), 1, static_cast<std::size_t>(packed.size()), f) ==
            static_cast<std::size_t>(packed.size());
        const bool closed = std::fclose(f) == 0;
        if (!written || !closed || std::rename(temp.c_str(), path.c_str()) != 0) {
            std::remove(temp.c_str());
            return false;
        }
        return true;
    }

private:
    std::string file(const TileKey& key, const std::string& variant) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (dir_.empty())
            return std::string();
        char name[96];
        std::snprintf(name, sizeof(name), "/%016llx/%d/%d/%d.vt",
                      static_cast<unsigned long long>(
                          fnv1a(identity_ + "|" + variant + "|nvt1")),
                      key.z, key.x, key.y);
        return dir_ + name;
    }
    // Free space is checked every 32 writes.
    bool room()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_checked_++ % 32 == 0) {
            struct statvfs info {};
            has_room_ = ::statvfs(dir_.c_str(), &info) != 0 ||
                        static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize >= min_free_;
        }
        return has_room_;
    }

    mutable std::mutex mutex_;
    std::string dir_, identity_;
    std::uint64_t min_free_ = 0;
    std::uint64_t free_checked_ = 0;
    bool has_room_ = true;
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
        : obfs_(std::move(obfs)),
          resources_(std::move(resources)), on_ready_(std::move(on_ready)),
          started_(Clock::now())
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
        block_ready_.notify_all();
        for (auto& thread : threads_)
            thread.join();
    }
    // Replaces the wish list (most important first). A fetch for a tile
    // that drops off the list is cancelled.
    void want(const std::vector<TileKey>& keys)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (keys == wanted_)
                return;
            wanted_.assign(keys.begin(), keys.end());  // keeps capacity
            ++wanted_revision_;
        }
        wake_.notify_all();
    }
    // An evicted tile may be wanted again later.
    void forget(const TileKey& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done_.erase(key);
    }
    // Palette switch: everything is built again; tiles of the old
    // generation are dropped.
    void set_night(bool night)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (night == night_)
                return;
            night_ = night;
            ++generation_;
            ++wanted_revision_;
            done_.clear();
            ready_.clear();
            failures_.clear();
        }
        wake_.notify_all();
    }
    // Lock-free read for the GL thread (written under mutex_).
    std::uint64_t generation() const { return generation_.load(); }
    // Appends the finished tiles to `out`. An empty `out` swaps buffers with
    // the ready list, so both keep their capacity.
    void take(std::vector<BuiltTile>& out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ready_.empty())
            return;
        if (out.empty()) {
            out.swap(ready_);
            return;
        }
        for (auto& tile : ready_)
            out.push_back(std::move(tile));
        ready_.clear();
    }
    DiskCache& disk() { return disk_; }
    // Overzoom: the deepest tiles are z14 with z16 detail. Changing it
    // starts over (tiles differ), like a palette switch.
    void set_overzoom(bool on)
    {
        std::map<BlockId, std::shared_ptr<Block>> dropped;  // freed unlocked
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (on == overzoom_)
                return;
            overzoom_ = on;
            ++generation_;
            ++wanted_revision_;
            done_.clear();
            ready_.clear();
            failures_.clear();
            dropped.swap(blocks_);
        }
        wake_.notify_all();
    }
    bool overzoom() const { return overzoom_.load(); }
    std::uint64_t block_reads() const { return block_reads_.load(); }
    std::uint64_t block_hits() const { return block_hits_.load(); }
    std::uint64_t aborted() const { return aborted_.load(); }
    std::uint64_t empty_tiles() const { return empty_tiles_.load(); }
    std::uint64_t disk_writes() const { return disk_writes_.load(); }
    std::uint64_t disk_failures() const { return disk_failures_.load(); }

private:
    using TagCache =
        std::unordered_map<TagKey, std::pair<std::string, std::string>, TagKeyHash>;

    // Detail (OBF level and style zoom) for a tile: its own zoom, or z16
    // for the deepest overzoomed tiles.
    static int detail_zoom(const TileKey& key, bool overzoom)
    {
        return overzoom && key.z == kOverzoomTileZoom ? kMaxTileZoom : key.z;
    }

    // Map objects of one block at one detail level, shared by its tiles.
    struct Block {
        bool loading = true;
        bool ok = false;
        QList<std::shared_ptr<const OsmAnd::BinaryMapObject>> objects;
        std::uint64_t used = 0;
    };
    struct BlockId {
        TileKey block;
        int detail;
        bool operator<(const BlockId& o) const
        {
            return detail != o.detail ? detail < o.detail : block < o.block;
        }
    };

    static TileKey block_of(const TileKey& key)
    {
        const int bz = std::min(key.z, kBlockZoom);
        return {bz, key.x >> (key.z - bz), key.y >> (key.z - bz)};
    }

    // Loads (or waits for another worker loading) the block holding `key`.
    // Null when cancelled (aborted=true) or when there is no data.
    std::shared_ptr<Block> obtain_block(const TileKey& key, int detail, std::uint64_t generation,
                                        double& fetch_ms, bool& aborted)
    {
        const BlockId id{block_of(key), detail};
        std::shared_ptr<Block> block;
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto it = blocks_.find(id);
            if (it == blocks_.end()) {
                block = std::make_shared<Block>();
                blocks_[id] = block;
                break;
            }
            auto existing = it->second;
            block_ready_.wait(lock, [&] { return !existing->loading || stopping_; });
            if (stopping_ || generation != generation_.load()) {
                aborted = true;
                return nullptr;
            }
            if (existing->ok) {
                existing->used = ++block_clock_;
                ++block_hits_;
                return existing;
            }
            // That load failed or was cancelled; it removed itself. Retry.
        }

        const auto started = Clock::now();
        const TileKey b = id.block;
        const OsmAnd::AreaI bbox31 = OsmAnd::Utilities::tileBoundingBox31(
            OsmAnd::TileId::fromXY(b.x, b.y), static_cast<OsmAnd::ZoomLevel>(b.z));
        // Cancelled once no wanted tile lies in this block.
        std::uint64_t seen_revision = ~0ull;
        bool seen_answer = true;
        const auto controller = std::make_shared<OsmAnd::FunctorQueryController>(
            [&](const OsmAnd::FunctorQueryController*) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (seen_revision != wanted_revision_) {
                    seen_revision = wanted_revision_;
                    seen_answer = false;
                    if (!stopping_ && generation == generation_)
                        for (const auto& k : wanted_)
                            if (k.z >= b.z && block_of(k) == b) {
                                seen_answer = true;
                                break;
                            }
                }
                return !seen_answer;
            });
        bool loaded = false;
        QList<std::shared_ptr<const OsmAnd::BinaryMapObject>> objects;
        const auto level = static_cast<OsmAnd::ZoomLevel>(detail);
        if (const auto data = obfs_->obtainDataInterface(
                &bbox31, level, level,
                OsmAnd::ObfDataTypesMask().set(OsmAnd::ObfDataType::Map)))
            loaded = data->loadBinaryMapObjects(&objects, nullptr, nullptr, level, &bbox31,
                                                nullptr, nullptr, nullptr, controller);
        aborted = controller->isAborted();
        fetch_ms = ms_since(started);
        ++block_reads_;
        // Evicted blocks are released after unlocking: freeing their map
        // objects must not hold up the GL thread waiting on mutex_.
        std::shared_ptr<Block> evicted[4];
        std::size_t evicted_count = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            block->loading = false;
            // Zero objects is what a read returns before OsmAnd has opened
            // the map: no data, retried, never kept.
            block->ok = loaded && !aborted && !objects.isEmpty();
            block->objects = std::move(objects);
            block->used = ++block_clock_;
            const auto it = blocks_.find(id);
            if (!block->ok && it != blocks_.end() && it->second == block)
                blocks_.erase(it);
            // Keep the most recently used blocks (the rest go on later loads
            // if more than four are over the limit).
            while (blocks_.size() > 6 && evicted_count < 4) {
                auto oldest = blocks_.end();
                for (auto i = blocks_.begin(); i != blocks_.end(); ++i)
                    if (!i->second->loading &&
                        (oldest == blocks_.end() || i->second->used < oldest->second->used))
                        oldest = i;
                if (oldest == blocks_.end())
                    break;
                evicted[evicted_count++] = std::move(oldest->second);
                blocks_.erase(oldest);
            }
        }
        for (std::size_t i = 0; i < evicted_count; ++i)
            evicted[i].reset();
        block_ready_.notify_all();
        return block->ok ? block : nullptr;
    }

    void run()
    {
        (void)navicore_osmand::lower_current_thread_priority(2);
        TagCache cache;
        for (;;) {
            TileKey key{};
            std::uint64_t generation = 0;
            bool night = false, overzoom = false;
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
                generation = generation_;
                night = night_;
                overzoom = overzoom_;
            }
            BuiltTile tile;
            try {
                tile = build(key, generation, night, overzoom, cache);
            } catch (...) {
                tile = BuiltTile{};
                tile.key = key;
            }
            tile.generation = generation;
            if (tile.aborted) {
                ++aborted_;
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation == generation_)
                    done_.erase(key);
                continue;
            }
            if (!tile.ok) {
                // No data: the map may still be opening (retry freely for
                // the first 20 s), or the tile is outside the map (give up
                // after three tries and draw it empty).
                int failures = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    failures = ++failures_[key];
                }
                if (failures <= 3 || Clock::now() - started_ < std::chrono::seconds(20)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (generation == generation_)
                        done_.erase(key);
                    continue;
                }
                ++empty_tiles_;
                tile.ok = true;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation != generation_)
                    continue;
                failures_.erase(key);
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

    BuiltTile build(const TileKey& key, std::uint64_t generation, bool night, bool overzoom,
                    TagCache& cache)
    {
        BuiltTile out;
        out.key = key;
        // Cache variant: palette, and overzoom (its z14 tiles hold z16 detail).
        const std::string variant =
            std::string(night ? "night" : "day") + (overzoom ? "|oz" : "");
        if (disk_.enabled()) {
            const auto disk_started = Clock::now();
            if (disk_.load(key, variant, out.mesh, out.labels)) {
                const auto labels_started = Clock::now();
                attach_images(out.labels, night);
                out.labels_ms = ms_since(labels_started);
                out.disk_ms = ms_since(disk_started) - out.labels_ms;
                out.from_disk = true;
                out.ok = true;
                return out;
            }
            out.mesh = TileMesh{};
            out.labels = TileLabels{};
        }

        const int detail = detail_zoom(key, overzoom);
        bool aborted = false;
        const auto block = obtain_block(key, detail, generation, out.fetch_ms, aborted);
        if (aborted) {
            out.aborted = true;
            return out;
        }
        if (!block)
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
        const double tile_x1 = origin_x + size31, tile_y1 = origin_y + size31;
        for (const auto& object : block->objects) {
            if (!object || object->isCoastline || object->points31.isEmpty())
                continue;
            // The block is larger than the tile: skip objects not touching it.
            const auto& box = object->bbox31;
            if (box.right() > box.left() || box.bottom() > box.top())
                if (box.left() > tile_x1 || box.right() < origin_x || box.top() > tile_y1 ||
                    box.bottom() < origin_y)
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
            const FeatureStyle style = classify(tags, area, detail, night);
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
        out.build_ms = ms_since(build_started) - labels_ms;
        // Written before the images are attached: the file holds records.
        if (disk_.enabled()) {
            if (disk_.store(key, variant, out.mesh, out.labels))
                ++disk_writes_;
            else
                ++disk_failures_;
        }
        const auto images_started = Clock::now();
        attach_images(out.labels, night);
        out.labels_ms = labels_ms + ms_since(images_started);
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
            label.rgb = (static_cast<std::uint32_t>(poi.colour.r) << 16) |
                        (static_cast<std::uint32_t>(poi.colour.g) << 8) | poi.colour.b;
            label.icon = poi.icon;
            label.id = "p|" + name + "@" +
                       std::to_string(static_cast<long long>(origin_x + pos[0] / to_local)) + "," +
                       std::to_string(static_cast<long long>(origin_y + pos[1] / to_local));
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
            label.name = name;
            label.place = *place;
            label.pos = pos;
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
        label.id = name + "@" + std::to_string(object.points31[0].x) + "," +
                   std::to_string(object.points31[0].y);
        label.path.reserve(static_cast<std::size_t>(object.points31.size()));
        for (const auto& p : object.points31)
            label.path.push_back({static_cast<float>((p.x - origin_x) * to_local),
                                  static_cast<float>((p.y - origin_y) * to_local)});
        out.streets.push_back(std::move(label));
    }

    // Rasterized text and icons for the palette; labels without an image
    // are dropped.
    void attach_images(TileLabels& labels, bool night)
    {
        const std::string mode = night ? "n|" : "d|";
        for (auto& label : labels.streets) {
            const std::uint32_t argb = label.rank <= 3 ? (night ? 0xFFD3D8DFu : 0xFF3C4043u)
                                                       : (night ? 0xFFA9B1BCu : 0xFF5F6368u);
            const std::string key = mode + "s|" + std::to_string(argb) + "|" + label.name;
            label.key = fnv1a(key);
            label.id_hash = fnv1a(label.id);
            label.name_hash = fnv1a(label.name);
            label.image = images_.get(key, [&] {
                return rasterize_text(QString::fromStdString(label.name), 17.0f, false, argb,
                                      night);
            });
        }
        labels.streets.erase(std::remove_if(labels.streets.begin(), labels.streets.end(),
                                            [](const StreetLabel& l) { return !l.image; }),
                             labels.streets.end());
        for (auto& label : labels.pois) {
            Rgba colour{static_cast<std::uint8_t>(label.rgb >> 16),
                        static_cast<std::uint8_t>(label.rgb >> 8),
                        static_cast<std::uint8_t>(label.rgb), 255};
            // Category colours are too dark on the night halo: lighten text.
            std::uint32_t text_rgb = label.rgb;
            if (night)
                text_rgb = ((colour.r + (255 - colour.r) * 2 / 5) << 16) |
                           ((colour.g + (255 - colour.g) * 2 / 5) << 8) |
                           (colour.b + (255 - colour.b) * 2 / 5);
            const std::uint32_t argb = 0xFF000000u | text_rgb;
            const std::string text_key = mode + "p|" + std::to_string(argb) + "|" + label.name;
            label.text_key = fnv1a(text_key);
            label.id_hash = fnv1a(label.id);
            label.text = images_.get(text_key, [&] {
                return rasterize_text(QString::fromStdString(label.name), 17.0f, true, argb,
                                      night);
            });
            const std::string icon_key = "i|" + label.icon + "|" + std::to_string(label.rgb);
            label.icon_key = fnv1a(icon_key);
            label.icon_image = images_.get(icon_key, [&] {
                return make_poi_icon(*resources_, label.icon.c_str(), colour);
            });
        }
        labels.pois.erase(std::remove_if(labels.pois.begin(), labels.pois.end(),
                                         [](const PoiLabel& l) { return !l.text || !l.icon_image; }),
                          labels.pois.end());
        for (auto& label : labels.places) {
            const std::uint32_t argb = night ? 0xFF9AA3AEu : 0xFF70757Au;
            const std::string key = mode + "c|" + label.name;
            label.key = fnv1a(key);
            for (int z = 0; z < kPlaceZooms; ++z)
                label.rank_at[static_cast<std::size_t>(z)] =
                    static_cast<std::int8_t>(place_label_rank(label.place, z));
            label.image = images_.get(key, [&] {
                return rasterize_text(QString::fromStdString(label.name), 20.0f, true, argb,
                                      night);
            });
        }
        labels.places.erase(std::remove_if(labels.places.begin(), labels.places.end(),
                                           [](const PlaceLabel& l) { return !l.image; }),
                            labels.places.end());
    }

    std::shared_ptr<const OsmAnd::IObfsCollection> obfs_;
    std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources_;
    std::function<void()> on_ready_;
    const Clock::time_point started_;
    ImageCache images_;
    DiskCache disk_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<TileKey> wanted_;
    std::uint64_t wanted_revision_ = 0;
    std::map<TileKey, bool> done_;
    std::map<TileKey, int> failures_;
    std::vector<BuiltTile> ready_;
    // Written under mutex_; atomic so the GL thread reads them unlocked.
    std::atomic<std::uint64_t> generation_{0};
    bool night_ = false;
    std::atomic<bool> overzoom_{false};
    std::map<BlockId, std::shared_ptr<Block>> blocks_;
    std::condition_variable block_ready_;
    std::uint64_t block_clock_ = 0;
    std::atomic<std::uint64_t> block_reads_{0}, block_hits_{0};
    bool stopping_ = false;
    std::atomic<std::uint64_t> aborted_{0}, empty_tiles_{0}, disk_writes_{0}, disk_failures_{0};
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
    // Forgets every slot; the texture is overwritten as labels return.
    void clear()
    {
        slots_.clear();
        x_ = y_ = row_ = 0;
    }
    GLuint texture() const { return texture_; }
    std::size_t resets() const { return resets_; }
    std::size_t uploads() const { return uploads_; }
    // Uploads allowed this frame; the rest of the new labels wait, so a
    // burst of new tiles does not stall one frame.
    void begin_frame(int upload_budget) { budget_ = upload_budget; }
    // False when the atlas had to be cleared or this frame's uploads are
    // used up; the label waits a frame.
    bool slot(std::uint64_t key, const LabelImage& image, Slot& out)
    {
        if (const Slot* found = slots_.find(key)) {
            out = *found;
            return true;
        }
        if (budget_ <= 0)
            return false;
        --budget_;
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
    int budget_ = 1 << 30;
    int x_ = 0, y_ = 0, row_ = 0;
    std::size_t resets_ = 0, uploads_ = 0;
    // Keyed by atlas key hash; a full atlas holds ~1-2k labels.
    LabelIdMap<Slot> slots_{2048};
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
    std::size_t capacity[4] = {};  // allocated bytes (dynamic meshes only)
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

// Uploads finished tiles on its own thread with a shared GL context: the
// render thread only adopts ready buffers. glFinish() before handing a mesh
// over makes it complete for the render context (GLES2 has no fences). The
// tile's CPU mesh is freed here too, off the render thread.
class TileUploader {
public:
    struct Ready {
        BuiltTile tile;  // mesh emptied; key, labels, timings kept
        GpuMesh gpu;
    };

    bool start(EGLDisplay display, EGLContext shared_with)
    {
        if (!context_.create(display, shared_with))
            return false;
        thread_ = std::thread([this] { run(); });
        return true;
    }

    // Render thread, context current: stops the thread, then frees what it
    // left behind (unless the context is gone).
    void stop(bool context_lost)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable())
            thread_.join();
        if (!context_lost)
            for (auto& ready : ready_)
                glDeleteBuffers(4, ready.gpu.buffers);
        ready_.clear();
        pending_.clear();
        context_.destroy();
    }

    void submit(std::vector<BuiltTile>& tiles)
    {
        if (tiles.empty())
            return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& tile : tiles)
                pending_.push_back(std::move(tile));
        }
        tiles.clear();
        wake_.notify_one();
    }

    void take(std::vector<Ready>& out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& ready : ready_)
            out.push_back(std::move(ready));
        ready_.clear();
    }

    std::uint32_t upload_max_us() { return upload_max_us_.exchange(0); }

private:
    void run()
    {
        if (!context_.make_current())
            return;
        for (;;) {
            std::vector<BuiltTile> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
                if (stopping_)
                    break;
                batch.swap(pending_);
            }
            std::vector<Ready> done;
            done.reserve(batch.size());
            for (auto& tile : batch) {
                const auto started = Clock::now();
                Ready ready;
                ready.gpu = upload(tile.mesh);
                const auto us = static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        Clock::now() - started).count());
                std::uint32_t seen = upload_max_us_.load();
                while (us > seen && !upload_max_us_.compare_exchange_weak(seen, us)) {}
                tile.mesh = TileMesh{};
                ready.tile = std::move(tile);
                done.push_back(std::move(ready));
            }
            glFinish();
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& ready : done)
                ready_.push_back(std::move(ready));
        }
        context_.release_current();
    }

    navicore_osmand::GpuUploadContext context_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    std::vector<BuiltTile> pending_;
    std::vector<Ready> ready_;
    std::atomic<std::uint32_t> upload_max_us_{0};
};


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

    // A label shown last frame; kept for fade-out at its world anchor.
    // Images live in the builder's ImageCache, which never evicts.
    struct Shown {
        float alpha = 0.0f;
        std::uint64_t atlas_key = 0;
        const LabelImage* image = nullptr;
        double x31 = 0.0, y31 = 0.0;    // anchor
        float off_x = 0.0f, off_y = 0.0f;  // box centre minus projected anchor
        float half_w = 0.0f, half_h = 0.0f, angle = 0.0f;
    };
    // Priority: major streets, places, POIs, other streets; labels shown
    // last frame first within each group; then rank, then input order.
    struct Candidate {
        int group, shown, rank;
        int kind;  // 0 street, 1 poi, 2 place
        std::uint32_t order;
        const TileKey* key;
        const void* label;
    };
    struct TileOrder {
        double distance;
        std::uint32_t order;
        TileKey key;
    };
    struct SelectNode {
        int z, x, y;
    };
    // Per-frame working sets. Cleared, never shrunk: after the first frames
    // a steady frame allocates nothing.
    struct FrameScratch {
        std::vector<TileKey> wanted, request, draw_list;
        std::vector<TileOrder> ordered;
        std::vector<SelectNode> stack;
        std::vector<BuiltTile> arrivals;   // finished tiles not yet uploaded
        std::vector<OrientedBox> accepted;
        std::vector<Candidate> candidates;
        LabelIdMap<char> seen_streets{4096};
        std::vector<std::pair<std::uint64_t, ScreenPoint>> placed_streets;
        std::vector<ScreenPoint> screen_path;
        std::vector<float> lengths, cumulative;
        void reserve()
        {
            wanted.reserve(4096);
            request.reserve(kMaxWantedTiles + 32);
            draw_list.reserve(kMaxWantedTiles * 4);
            ordered.reserve(4096);
            stack.reserve(256);
            arrivals.reserve(64);
            accepted.reserve(256);
            candidates.reserve(4096);
            placed_streets.reserve(32);
            screen_path.reserve(256);
            lengths.reserve(256);
            cumulative.reserve(256);
        }
    };

    TileBuilder builder;
    bool gl_ready = false;
    GLuint area_prog = 0, line_prog = 0, label_prog = 0;
    GLint area_m = -1, line_m = -1, line_half_vp = -1, line_eps = -1, line_wscale = -1,
          line_casing = -1, label_tex = -1;
    LabelAtlas atlas;
    GLuint label_vbo = 0, label_ibo = 0;
    PFNGLBINDVERTEXARRAYOESPROC bind_vao = nullptr;
    std::map<TileKey, GpuMesh> gpu;
    // Tile uploads on their own thread (shared context), when enabled.
    std::unique_ptr<TileUploader> uploader;
    bool upload_thread_requested = false;
    std::vector<TileUploader::Ready> uploaded;  // scratch
    // Buffers of evicted tiles kept for reuse (upload pool), and the policy.
    std::vector<GpuMesh> pool;
    bool use_pool = false;
    int upload_budget = 4;
    static constexpr std::size_t kPoolMeshes = 24;
    void retire(GpuMesh& mesh)
    {
        if (use_pool && pool.size() < kPoolMeshes && mesh.buffers[0]) {
            mesh.last_used = 0;
            pool.push_back(mesh);
        } else {
            glDeleteBuffers(4, mesh.buffers);
        }
    }
    std::map<TileKey, TileLabels> labels;
    std::size_t gpu_bytes = 0;
    std::uint64_t frame = 0;
    // Previous frame's camera: labels snap to pixels only while it is still.
    double last_origin[2] = {0.0, 0.0};
    double last_h[9] = {};
    LabelImagePtr location_dot = make_location_dot();
    const std::uint64_t location_key = fnv1a("@location");
    // Fade state by label id: this frame's table is filled, then swapped.
    LabelIdMap<Shown> fade{1024}, next_fade{1024};
    FrameScratch scratch;
    std::vector<LabelVertex> label_vertices;
    std::vector<std::uint16_t> label_indices;
    // POIs placed in the last frame, for pick_poi(). Reused every frame;
    // the label records stay valid until the next draw() takes new tiles.
    // Place names (city, town...) are recorded too: label is null, place set.
    struct PlacedPoi {
        const PoiLabel* label;
        const TileKey* key;
        float x, y, half;  // icon centre and half size, pixels
        const PlaceLabel* place = nullptr;
        float half_width = 0.0f, half_height = 0.0f;  // place name box
    };
    std::vector<PlacedPoi> placed_pois;

    // Palette: requested from any thread, applied on the GL thread.
    std::atomic<bool> night_requested{false};
    bool night = false;
    std::atomic<bool> overzoom_requested{false};
    bool overzoom = false;

    // Route state (set from the engine thread, consumed in draw()).
    std::mutex route_mutex;
    bool route_dirty = false;
    std::vector<std::pair<std::int32_t, std::int32_t>> route_points;
    bool progress_valid = false;
    double progress_x = 0.0, progress_y = 0.0;
    bool maneuver_show = false;
    std::size_t maneuver_index = 0;

    // GL-thread copies.
    std::vector<std::pair<double, double>> route;  // 31-bit
    std::vector<double> route_length;              // cumulative, 31-bit units
    Placement route_placement{0, 0, 16.0};
    GpuMesh route_static;   // driven part (grey) + part ahead (blue)
    // The segment the vehicle is on; rewritten most frames, so two buffer
    // sets alternate (the GPU may still read last frame's).
    GpuMesh route_joint[2];
    int joint_current = 0;
    GpuMesh arrow_lines, arrow_head;
    // Reused for the route meshes above.
    TileTessellator route_tess;
    TileMesh route_mesh;
    Ring route_line;
    std::size_t route_segment = static_cast<std::size_t>(-1);  // static split
    bool have_progress = false;
    double pos_x = 0.0, pos_y = 0.0;
    double joint_x = -1.0, joint_y = -1.0;
    std::size_t arrow_index = static_cast<std::size_t>(-1);
    double arrow_zoom = -100.0;
    // Tiles along the route ahead, recomputed when the segment or the
    // tile zoom changes.
    std::vector<TileKey> prefetch;
    std::size_t prefetch_segment = static_cast<std::size_t>(-1);
    int prefetch_zoom = -1;

    bool last_incomplete = false;
    mutable std::mutex stats_mutex;
    VectorMapStats stats;
    VectorFrameTiming timing;  // render thread only

    void draw(const MapView& view, double dt, bool clear, bool& wants_more);
    void place_labels(const MapView& view, const std::vector<TileKey>& draw, double zoom,
                      double dt, std::size_t& calls, bool& fading);
    void apply_night();
    void take_tiles(std::uint64_t generation);
    void update_route(double zoom);
    void select_tiles(const MapView& view, int max_z, std::vector<TileKey>& out);
    void update_prefetch(int tz);
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
    if (d.upload_thread_requested) {
        d.uploader.reset(new TileUploader());
        if (!d.uploader->start(eglGetCurrentDisplay(), eglGetCurrentContext()))
            d.uploader.reset();
    }
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
    // Per-frame buffers sized once (they only grow past these).
    d.scratch.reserve();
    d.label_vertices.reserve(4096);
    d.label_indices.reserve(6144);
    d.placed_pois.reserve(64);
    d.prefetch.reserve(32);
    d.gl_ready = true;
    return true;
}

void VectorMap::release_gl(bool context_lost)
{
    auto& d = *impl_;
    if (d.uploader) {
        d.uploader->stop(context_lost);
        d.uploader.reset();
    }
    if (!context_lost) {
        for (auto& entry : d.gpu)
            glDeleteBuffers(4, entry.second.buffers);
        for (auto& mesh : d.pool)
            glDeleteBuffers(4, mesh.buffers);
        for (GpuMesh* mesh : {&d.route_static, &d.route_joint[0], &d.route_joint[1],
                              &d.arrow_lines, &d.arrow_head})
            if (mesh->buffers[0])
                glDeleteBuffers(4, mesh->buffers);
        if (d.label_vbo) glDeleteBuffers(1, &d.label_vbo);
        if (d.label_ibo) glDeleteBuffers(1, &d.label_ibo);
        if (d.area_prog) glDeleteProgram(d.area_prog);
        if (d.line_prog) glDeleteProgram(d.line_prog);
        if (d.label_prog) glDeleteProgram(d.label_prog);
    }
    d.pool.clear();
    d.atlas.release(context_lost);
    for (const auto& entry : d.gpu)
        d.builder.forget(entry.first);
    d.gpu.clear();
    d.labels.clear();
    d.fade.clear();
    d.gpu_bytes = 0;
    d.route_static = d.route_joint[0] = d.route_joint[1] = d.arrow_lines = d.arrow_head =
        GpuMesh{};
    d.route_segment = d.arrow_index = static_cast<std::size_t>(-1);
    d.joint_x = d.joint_y = -1.0;
    d.label_vbo = d.label_ibo = 0;
    d.area_prog = d.line_prog = d.label_prog = 0;
    d.gl_ready = false;
}

void VectorMap::set_disk_cache(const std::string& dir, const std::string& identity,
                               std::uint64_t min_free_bytes)
{
    impl_->builder.disk().configure(dir, identity, min_free_bytes);
}

void VectorMap::set_route(const std::vector<std::pair<std::int32_t, std::int32_t>>& points31)
{
    std::lock_guard<std::mutex> lock(impl_->route_mutex);
    impl_->route_points = points31;
    impl_->route_dirty = true;
    impl_->progress_valid = false;
    impl_->maneuver_show = false;
}

void VectorMap::set_route_progress(bool valid, std::int32_t x31, std::int32_t y31)
{
    std::lock_guard<std::mutex> lock(impl_->route_mutex);
    impl_->progress_valid = valid;
    impl_->progress_x = x31;
    impl_->progress_y = y31;
}

void VectorMap::set_maneuver(bool show, std::size_t geometry_index)
{
    std::lock_guard<std::mutex> lock(impl_->route_mutex);
    impl_->maneuver_show = show;
    impl_->maneuver_index = geometry_index;
}

void VectorMap::set_upload_thread(bool on)
{
    impl_->upload_thread_requested = on;
}

bool VectorMap::upload_thread_active() const
{
    return impl_->uploader != nullptr;
}

void VectorMap::set_upload_policy(bool pool, int budget)
{
    impl_->use_pool = pool;
    impl_->upload_budget = std::clamp(budget, 1, 8);
}

void VectorMap::set_overzoom(bool on)
{
    impl_->overzoom_requested = on;
    impl_->builder.set_overzoom(on);
}

void VectorMap::set_night(bool night)
{
    impl_->night_requested = night;
    impl_->builder.set_night(night);
}

void VectorMap::Impl::apply_night()
{
    const bool wanted = night_requested.load();
    const bool wanted_overzoom = overzoom_requested.load();
    if (wanted == night && wanted_overzoom == overzoom)
        return;
    night = wanted;
    overzoom = wanted_overzoom;
    // Every tile and label image was made for the old palette.
    for (auto& entry : gpu)
        retire(entry.second);
    gpu.clear();
    labels.clear();
    fade.clear();
    gpu_bytes = 0;
    atlas.clear();
}

namespace {

// Re-uploads a small dynamic mesh into its existing buffers: storage is
// (re)allocated with headroom only when the data outgrows it, otherwise the
// data is written with glBufferSubData.
void upload_into(GpuMesh& gpu, const TileMesh& mesh)
{
    if (!gpu.buffers[0])
        glGenBuffers(4, gpu.buffers);
    const struct {
        GLenum target;
        const void* data;
        std::size_t bytes;
    } parts[4] = {
        {GL_ARRAY_BUFFER, mesh.area_vertices.data(), mesh.area_vertices.size() * sizeof(AreaVertex)},
        {GL_ELEMENT_ARRAY_BUFFER, mesh.area_indices.data(), mesh.area_indices.size() * 4},
        {GL_ARRAY_BUFFER, mesh.line_vertices.data(), mesh.line_vertices.size() * sizeof(LineVertex)},
        {GL_ELEMENT_ARRAY_BUFFER, mesh.line_indices.data(), mesh.line_indices.size() * 4}};
    for (int i = 0; i < 4; ++i) {
        if (!parts[i].bytes)
            continue;
        glBindBuffer(parts[i].target, gpu.buffers[i]);
        if (parts[i].bytes > gpu.capacity[i]) {
            gpu.capacity[i] = std::max<std::size_t>(parts[i].bytes + parts[i].bytes / 2, 4096);
            glBufferData(parts[i].target, static_cast<GLsizeiptr>(gpu.capacity[i]), nullptr,
                         GL_DYNAMIC_DRAW);
        }
        glBufferSubData(parts[i].target, 0, static_cast<GLsizeiptr>(parts[i].bytes),
                        parts[i].data);
    }
    gpu.area_count = static_cast<GLsizei>(mesh.area_indices.size());
    gpu.line_count = static_cast<GLsizei>(mesh.line_indices.size());
}

// Route colours: Google blue ahead, grey behind the vehicle.
const LineStyle kRouteAhead{LineLayer::Motorway, {26, 115, 232, 255}, {21, 87, 176, 255},
                            9.0f, 12.0f};
const LineStyle kRouteDriven{LineLayer::Rail, {154, 160, 166, 255}, {128, 134, 139, 255},
                             9.0f, 12.0f};
const LineStyle kArrowLine{LineLayer::Motorway, {255, 255, 255, 255}, {23, 78, 166, 255},
                           5.0f, 8.0f};

// 31-bit units per metre at a 31-bit y (Web Mercator).
double units_per_metre(double y31)
{
    const double pi = 3.14159265358979323846;
    const double lat = std::atan(std::sinh(pi * (1.0 - 2.0 * y31 / std::ldexp(1.0, 31))));
    return std::ldexp(1.0, 31) / (40075016.686 * std::max(0.01, std::cos(lat)));
}

} // namespace

// Splits the route at the vehicle: everything before its segment is grey,
// everything after blue (both rebuilt only when the segment changes), and
// the segment itself is a small per-position mesh. The turn arrow is a
// white line with a head along the route through the manoeuvre point.
void VectorMap::Impl::update_route(double zoom)
{
    bool dirty = false, valid = false, show_arrow = false;
    double px = 0.0, py = 0.0;
    std::size_t arrow_at = 0;
    {
        std::lock_guard<std::mutex> lock(route_mutex);
        if (route_dirty) {
            route.clear();
            for (const auto& p : route_points)
                route.emplace_back(p.first, p.second);
            route_dirty = false;
            dirty = true;
        }
        valid = progress_valid;
        px = progress_x;
        py = progress_y;
        show_arrow = maneuver_show;
        arrow_at = maneuver_index;
    }
    if (dirty) {
        route_length.assign(route.size(), 0.0);
        for (std::size_t i = 1; i < route.size(); ++i)
            route_length[i] = route_length[i - 1] +
                              std::hypot(route[i].first - route[i - 1].first,
                                         route[i].second - route[i - 1].second);
        if (!route.empty())
            route_placement = {route[0].first, route[0].second, 16.0};
        route_segment = static_cast<std::size_t>(-1);
        arrow_index = static_cast<std::size_t>(-1);
        joint_x = joint_y = -1.0;
        prefetch.clear();
        prefetch_segment = static_cast<std::size_t>(-1);
        for (GpuMesh* mesh : {&route_static, &route_joint[0], &route_joint[1], &arrow_lines,
                              &arrow_head})
            mesh->line_count = mesh->area_count = 0;
    }
    if (route.size() < 2) {
        have_progress = false;
        return;
    }
    const auto local = [&](double x, double y) {
        return Point{static_cast<float>((x - route_placement.ox) / route_placement.k),
                     static_cast<float>((y - route_placement.oy) / route_placement.k)};
    };

    // Segment under the vehicle: near the previous one first, then anywhere.
    std::size_t segment = route_segment == static_cast<std::size_t>(-1) ? 0 : route_segment;
    have_progress = valid;
    if (valid) {
        const auto nearest = [&](std::size_t from, std::size_t to, double& best) {
            std::size_t found = from;
            best = 1e300;
            for (std::size_t i = from; i + 1 < route.size() && i < to; ++i) {
                const double ax = route[i].first, ay = route[i].second;
                const double bx = route[i + 1].first - ax, by = route[i + 1].second - ay;
                const double len2 = bx * bx + by * by;
                const double t = len2 > 0 ? std::clamp(((px - ax) * bx + (py - ay) * by) / len2,
                                                       0.0, 1.0)
                                          : 0.0;
                const double d = std::hypot(ax + t * bx - px, ay + t * by - py);
                if (d < best) {
                    best = d;
                    found = i;
                }
            }
            return found;
        };
        double best = 0.0;
        const std::size_t from = segment > 3 ? segment - 3 : 0;
        segment = nearest(from, segment + 80, best);
        if (best > 50.0 * units_per_metre(py))
            segment = nearest(0, route.size(), best);
        pos_x = px;
        pos_y = py;
    } else {
        segment = 0;
    }

    TileTessellator& t = route_tess;
    Ring& line = route_line;
    if (segment != route_segment || dirty) {
        route_segment = segment;
        line.clear();
        if (valid) {
            for (std::size_t i = 0; i <= segment; ++i)
                line.push_back(local(route[i].first, route[i].second));
            if (line.size() >= 2)
                t.add_line_unclipped(line, kRouteDriven);
            line.clear();
            for (std::size_t i = segment + 1; i < route.size(); ++i)
                line.push_back(local(route[i].first, route[i].second));
        } else {
            for (const auto& p : route)
                line.push_back(local(p.first, p.second));
        }
        if (line.size() >= 2)
            t.add_line_unclipped(line, kRouteAhead);
        t.finish_into(route_mesh);
        upload_into(route_static, route_mesh);
        joint_x = joint_y = -1.0;
    }
    if (valid && (pos_x != joint_x || pos_y != joint_y)) {
        joint_x = pos_x;
        joint_y = pos_y;
        const Point here = local(pos_x, pos_y);
        line.clear();
        line.push_back(local(route[segment].first, route[segment].second));
        line.push_back(here);
        if (!(line[0] == line[1]))
            t.add_line_unclipped(line, kRouteDriven);
        // Overlaps the next segment so the blue join has no notch.
        line.clear();
        line.push_back(here);
        for (std::size_t i = segment + 1; i < route.size() && i <= segment + 2; ++i)
            line.push_back(local(route[i].first, route[i].second));
        if (line.size() >= 2 && !(line[0] == line[1]))
            t.add_line_unclipped(line, kRouteAhead);
        t.finish_into(route_mesh);
        joint_current ^= 1;
        upload_into(route_joint[joint_current], route_mesh);
    } else if (!valid && route_joint[joint_current].line_count) {
        route_joint[joint_current].line_count = 0;
    }

    // Turn arrow: 35 m before to 22 m after the manoeuvre, head sized in
    // screen pixels (rebuilt when the zoom moves noticeably).
    if (!show_arrow || arrow_at == 0 || arrow_at + 1 >= route.size()) {
        arrow_lines.line_count = arrow_head.area_count = 0;
        arrow_index = static_cast<std::size_t>(-1);
        return;
    }
    if (arrow_at == arrow_index && std::fabs(zoom - arrow_zoom) < 0.25)
        return;
    arrow_index = arrow_at;
    arrow_zoom = zoom;
    // At least ~60 / 40 px either side so the arrow reads when zoomed out.
    const double upm = units_per_metre(route[arrow_at].second);
    const double units_per_px = std::ldexp(1.0, 31) / (kVectorTilePx * std::pow(2.0, zoom));
    const double start = route_length[arrow_at] - std::max(35.0 * upm, 60.0 * units_per_px);
    const double end = route_length[arrow_at] + std::max(22.0 * upm, 40.0 * units_per_px);
    const auto at = [&](double s) {
        s = std::clamp(s, 0.0, route_length.back());
        std::size_t i = static_cast<std::size_t>(
            std::upper_bound(route_length.begin(), route_length.end(), s) - route_length.begin());
        i = std::clamp<std::size_t>(i, 1, route.size() - 1);
        const double span = route_length[i] - route_length[i - 1];
        const double f = span > 0 ? (s - route_length[i - 1]) / span : 0.0;
        return std::pair<double, double>{
            route[i - 1].first + f * (route[i].first - route[i - 1].first),
            route[i - 1].second + f * (route[i].second - route[i - 1].second)};
    };
    Ring& shaft = line;
    shaft.clear();
    const auto first = at(start);
    shaft.push_back(local(first.first, first.second));
    for (std::size_t i = 0; i < route.size(); ++i)
        if (route_length[i] > start && route_length[i] < end)
            shaft.push_back(local(route[i].first, route[i].second));
    const auto tip_base = at(end);
    shaft.push_back(local(tip_base.first, tip_base.second));
    t.add_line_unclipped(shaft, kArrowLine);
    t.finish_into(route_mesh);
    upload_into(arrow_lines, route_mesh);

    // Head: a triangle past the end of the shaft, along its last direction.
    const auto before = at(end - 4.0 * upm);
    double dx = tip_base.first - before.first, dy = tip_base.second - before.second;
    const double len = std::hypot(dx, dy);
    if (len <= 0.0) {
        arrow_head.area_count = 0;
        return;
    }
    dx /= len;
    dy /= len;
    const double px_per_unit = kVectorTilePx * std::pow(2.0, zoom) / std::ldexp(1.0, 31);
    TileMesh& head = route_mesh;
    head.area_vertices.clear();
    head.area_indices.clear();
    head.line_vertices.clear();
    head.line_indices.clear();
    const auto triangle = [&](double back_px, double length_px, double half_px, Rgba colour) {
        const double b = back_px / px_per_unit, l = length_px / px_per_unit,
                     h = half_px / px_per_unit;
        const double bx = tip_base.first - dx * b, by = tip_base.second - dy * b;
        const double pts[3][2] = {{bx - dy * h, by + dx * h},
                                  {bx + dy * h, by - dx * h},
                                  {bx + dx * l, by + dy * l}};
        const auto base = static_cast<std::uint32_t>(head.area_vertices.size());
        for (const auto& p : pts) {
            const Point q = local(p[0], p[1]);
            head.area_vertices.push_back({q[0], q[1], {colour.r, colour.g, colour.b, colour.a}});
        }
        head.area_indices.insert(head.area_indices.end(), {base, base + 1, base + 2});
    };
    triangle(2.0, 18.0, 12.0, kArrowLine.casing);
    triangle(0.0, 14.0, 9.0, kArrowLine.fill);
    upload_into(arrow_head, head);
}

// Tiles ahead on the route (up to ~3 km or 24 tiles) at the current tile
// zoom, so they are ready before the camera gets there.
void VectorMap::Impl::update_prefetch(int tz)
{
    if (!have_progress || route.size() < 2) {
        prefetch.clear();
        return;
    }
    if (route_segment == prefetch_segment && tz == prefetch_zoom)
        return;
    prefetch_segment = route_segment;
    prefetch_zoom = tz;
    prefetch.clear();
    const double size31 = std::ldexp(1.0, 31 - tz);
    const double limit = route_length[route_segment] + 3000.0 * units_per_metre(pos_y);
    const auto add = [&](double x, double y) {
        const TileKey key{tz, static_cast<int>(x / size31), static_cast<int>(y / size31)};
        if (std::find(prefetch.begin(), prefetch.end(), key) == prefetch.end())
            prefetch.push_back(key);  // at most 24: a scan beats a set
    };
    for (std::size_t i = route_segment; i + 1 < route.size() && prefetch.size() < 24; ++i) {
        if (route_length[i] > limit)
            break;
        // Sample each segment at half-tile steps so no crossed tile is missed.
        const double len = route_length[i + 1] - route_length[i];
        const int steps = std::max(1, static_cast<int>(std::ceil(len / (size31 * 0.5))));
        for (int s = 0; s <= steps && prefetch.size() < 24; ++s) {
            const double f = static_cast<double>(s) / steps;
            add(route[i].first + f * (route[i + 1].first - route[i].first),
                route[i].second + f * (route[i + 1].second - route[i].second));
        }
    }
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

// Quadtree from the zoom-11 tiles under the view: a tile is split while its
// projection is clearly larger than kVectorTilePx, so a tilted camera gets
// detailed tiles near the car and coarse ones towards the horizon. Tiles
// entirely off screen or behind the camera are dropped.
void VectorMap::Impl::select_tiles(const MapView& view, int max_z,
                                   std::vector<TileKey>& out)
{
    const double max31 = std::ldexp(1.0, 31) - 1.0;
    const double margin = 24.0;
    // Depth-first with an explicit, reused stack (same order as recursion).
    std::vector<SelectNode>& stack = scratch.stack;
    const auto visit = [&](int z, int x, int y) {
        if (out.size() >= 4096)
            return;
        const double size31 = std::ldexp(1.0, 31 - z);
        const double x0 = x * size31, y0 = y * size31;
        if (x0 > view.max_x31 || y0 > view.max_y31 || x0 + size31 < view.min_x31 ||
            y0 + size31 < view.min_y31)
            return;
        double sx[4], sy[4];
        int behind = 0;
        const double corners[4][2] = {{x0, y0}, {x0 + size31, y0}, {x0 + size31, y0 + size31},
                                      {x0, y0 + size31}};
        for (int i = 0; i < 4; ++i) {
            double w = 0.0;
            project(view, corners[i][0], corners[i][1], sx[i], sy[i], w);
            if (!(w > 0.0))
                ++behind;
        }
        if (behind == 4)
            return;
        if (behind == 0) {
            const double lx = std::min({sx[0], sx[1], sx[2], sx[3]});
            const double hx = std::max({sx[0], sx[1], sx[2], sx[3]});
            const double ly = std::min({sy[0], sy[1], sy[2], sy[3]});
            const double hy = std::max({sy[0], sy[1], sy[2], sy[3]});
            if (hx < -margin || hy < -margin || lx > view.width + margin ||
                ly > view.height + margin)
                return;
            double edge = 0.0;
            for (int i = 0; i < 4; ++i)
                edge = std::max(edge, std::hypot(sx[(i + 1) % 4] - sx[i], sy[(i + 1) % 4] - sy[i]));
            if (z >= max_z || edge <= kVectorTilePx * 1.35) {
                out.push_back({z, x, y});
                return;
            }
        } else if (z >= max_z) {
            out.push_back({z, x, y});
            return;
        }
        // Pushed in reverse so they pop in (cy, cx) order.
        for (int cy = 1; cy >= 0; --cy)
            for (int cx = 1; cx >= 0; --cx)
                stack.push_back({z + 1, x * 2 + cx, y * 2 + cy});
    };
    const int root = std::min(kMinTileZoom, max_z);
    const double size31 = std::ldexp(1.0, 31 - root);
    const int x0 = static_cast<int>(std::clamp(view.min_x31, 0.0, max31) / size31);
    const int x1 = static_cast<int>(std::clamp(view.max_x31, 0.0, max31) / size31);
    const int y0 = static_cast<int>(std::clamp(view.min_y31, 0.0, max31) / size31);
    const int y1 = static_cast<int>(std::clamp(view.max_y31, 0.0, max31) / size31);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            stack.clear();
            stack.push_back({root, x, y});
            while (!stack.empty()) {
                const SelectNode node = stack.back();
                stack.pop_back();
                visit(node.z, node.x, node.y);
            }
        }
}

void VectorMap::Impl::draw(const MapView& view, double dt, bool clear, bool& wants_more)
{
    apply_night();
    const double zoom = VectorMap::zoom_of(view);
    const int max_tile_zoom = builder.overzoom() ? kOverzoomTileZoom : kMaxTileZoom;
    const double tile_zoom = view.tile_zoom_limit >= 0.0
        ? std::min(zoom, view.tile_zoom_limit) : zoom;
    const int tz = std::clamp(static_cast<int>(std::floor(tile_zoom)), kMinTileZoom,
                              max_tile_zoom);
    const std::uint64_t generation = builder.generation();
    const auto us_since = [](Clock::time_point started) {
        return static_cast<std::uint32_t>(std::chrono::duration_cast<
            std::chrono::microseconds>(Clock::now() - started).count());
    };
    timing = VectorFrameTiming{};
    auto phase = Clock::now();
    take_tiles(generation);
    timing.take_us = us_since(phase);
    phase = Clock::now();
    update_route(zoom);
    timing.route_us = us_since(phase);
    phase = Clock::now();

    // Visible tiles at their detail level, nearest to the vehicle first,
    // then the prefetch along the route.
    std::vector<TileKey>& wanted = scratch.wanted;
    wanted.clear();
    select_tiles(view, tz, wanted);
    {
        double ax = view.width / 2.0, ay = view.height * 0.75;
        if (view.dot_x > 0.0f || view.dot_y > 0.0f) {
            ax = view.dot_x;
            ay = view.dot_y;
        }
        std::vector<TileOrder>& ordered = scratch.ordered;
        ordered.clear();
        for (const auto& key : wanted) {
            const double size31 = std::ldexp(1.0, 31 - key.z);
            double sx, sy, w;
            project(view, (key.x + 0.5) * size31, (key.y + 0.5) * size31, sx, sy, w);
            const double d = w > 0.0 ? std::hypot(sx - ax, sy - ay) : 1e9;
            ordered.push_back({d, static_cast<std::uint32_t>(ordered.size()), key});
        }
        // Stable by the index tie-break; std::sort needs no buffer.
        std::sort(ordered.begin(), ordered.end(), [](const TileOrder& a, const TileOrder& b) {
            return a.distance != b.distance ? a.distance < b.distance : a.order < b.order;
        });
        wanted.clear();
        for (const auto& entry : ordered)
            wanted.push_back(entry.key);
    }
    if (wanted.size() > kMaxWantedTiles)
        wanted.resize(kMaxWantedTiles);
    update_prefetch(tz);
    std::vector<TileKey>& request = scratch.request;
    request.assign(wanted.begin(), wanted.end());
    std::size_t prefetching = 0;
    for (const auto& key : prefetch)
        if (!gpu.count(key) && std::find(request.begin(), request.end(), key) == request.end()) {
            request.push_back(key);
            ++prefetching;
        }
    builder.want(request);
    timing.want_us = us_since(phase);
    phase = Clock::now();
    // Prefetched tiles count as recently used so they survive until seen.
    for (const auto& key : prefetch) {
        auto it = gpu.find(key);
        if (it != gpu.end())
            it->second.last_used = frame;
    }

    // Draw list: wanted tiles, or their nearest ready ancestor / children.
    std::vector<TileKey>& draw_list = scratch.draw_list;
    draw_list.clear();
    std::size_t missing = 0;
    int min_z = max_tile_zoom;
    for (const auto& key : wanted) {
        min_z = std::min(min_z, key.z);
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
    // TileKey orders by zoom first: coarse tiles are drawn before finer ones.
    std::sort(draw_list.begin(), draw_list.end());
    draw_list.erase(std::unique(draw_list.begin(), draw_list.end()), draw_list.end());
    wants_more = missing > 0 || !scratch.arrivals.empty();
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
        const Rgba land = land_colour(night);
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
    const auto draw_areas = [&](const GpuMesh& mesh, const Placement& placement) {
        clip_matrix(view, placement, m);
        glUniformMatrix3fv(area_m, 1, GL_FALSE, m);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.buffers[0]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.buffers[1]);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(AreaVertex), nullptr);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(AreaVertex),
                              reinterpret_cast<void*>(8));
        glDrawElements(GL_TRIANGLES, mesh.area_count, GL_UNSIGNED_INT, nullptr);
        ++calls;
    };
    for (const auto& key : draw_list) {
        auto& mesh = gpu[key];
        mesh.last_used = frame;
        if (mesh.area_count)
            draw_areas(mesh, tile_placement(key));
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
    // Route above the roads, then the turn arrow, both below the labels.
    for (const GpuMesh* group : {&route_static, &arrow_lines}) {
        for (int pass = 0; pass < 2; ++pass) {
            glUniform1f(line_casing, pass == 0 ? 1.0f : 0.0f);
            if (group == &route_static) {
                if (route_static.line_count)
                    draw_lines(route_static, route_placement);
                if (route_joint[joint_current].line_count)
                    draw_lines(route_joint[joint_current], route_placement);
            } else if (arrow_lines.line_count) {
                draw_lines(arrow_lines, route_placement);
            }
        }
    }
    for (GLuint a = 0; a < 7; ++a)
        glDisableVertexAttribArray(a);
    if (arrow_head.area_count) {
        glUseProgram(area_prog);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        draw_areas(arrow_head, route_placement);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    }

    timing.draw_us = us_since(phase);
    const auto label_started = Clock::now();
    bool fading = false;
    place_labels(view, draw_list, zoom, dt, calls, fading);
    const double label_ms = ms_since(label_started);
    timing.labels_us = us_since(label_started);
    phase = Clock::now();
    if (fading)
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
        retire(oldest->second);
        gpu_bytes -= oldest->second.bytes;
        labels.erase(oldest->first);
        builder.forget(oldest->first);
        gpu.erase(oldest);
    }
    timing.evict_us = us_since(phase);
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
    stats.wanted_tiles = wanted.size();
    stats.prefetch_tiles = prefetching;
    stats.min_tile_zoom = wanted.empty() ? tz : min_z;
    stats.tile_zoom = tz;
    stats.zoom = zoom;
}

// Uploads finished tiles: at most four (or about 1 MB) per frame so a burst
// of arrivals is spread over frames; the rest stay queued.
void VectorMap::Impl::take_tiles(std::uint64_t generation)
{
    std::vector<BuiltTile>& arrivals = scratch.arrivals;
    builder.take(arrivals);
    if (uploader) {
        // Current-generation tiles go to the upload thread; ready ones are
        // adopted here without GL uploads.
        arrivals.erase(std::remove_if(arrivals.begin(), arrivals.end(),
                                      [&](const BuiltTile& t) { return t.generation != generation; }),
                       arrivals.end());
        uploader->submit(arrivals);
        uploaded.clear();
        uploader->take(uploaded);
        timing.upload_max_us = uploader->upload_max_us();
        for (auto& ready : uploaded) {
            auto& tile = ready.tile;
            if (tile.generation != generation) {
                glDeleteBuffers(4, ready.gpu.buffers);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                if (tile.from_disk) {
                    ++stats.disk_hits;
                    stats.disk_ms_total += tile.disk_ms;
                } else {
                    ++stats.tiles_built;
                    stats.fetch_ms_total += tile.fetch_ms;
                    stats.fetch_ms_max = percentile_max(stats.fetch_ms_max, tile.fetch_ms);
                    stats.build_ms_total += tile.build_ms;
                }
                stats.labels_ms_total += tile.labels_ms;
            }
            GpuMesh mesh = ready.gpu;
            mesh.last_used = frame;
            gpu_bytes += mesh.bytes;
            ++timing.uploads;
            auto existing = gpu.find(tile.key);
            if (existing != gpu.end()) {
                glDeleteBuffers(4, existing->second.buffers);
                gpu_bytes -= existing->second.bytes;
            }
            gpu[tile.key] = mesh;
            labels[tile.key] = std::move(tile.labels);
        }
        return;
    }
    std::size_t used = 0, uploaded = 0, bytes = 0;
    for (; used < arrivals.size(); ++used) {
        auto& tile = arrivals[used];
        if (tile.generation != generation)
            continue;
        if (uploaded >= static_cast<std::size_t>(upload_budget) ||
            (uploaded > 0 && bytes >= (1u << 20)))
            break;
        {
            std::lock_guard<std::mutex> lock(stats_mutex);
            if (tile.from_disk) {
                ++stats.disk_hits;
                stats.disk_ms_total += tile.disk_ms;
            } else {
                ++stats.tiles_built;
                stats.fetch_ms_total += tile.fetch_ms;
                stats.fetch_ms_max = percentile_max(stats.fetch_ms_max, tile.fetch_ms);
                stats.build_ms_total += tile.build_ms;
            }
            stats.labels_ms_total += tile.labels_ms;
        }
        const auto upload_started = Clock::now();
        GpuMesh mesh;
        if (use_pool && !pool.empty()) {
            mesh = pool.back();
            pool.pop_back();
            upload_into(mesh, tile.mesh);
            mesh.bytes = tile.mesh.area_vertices.size() * sizeof(AreaVertex) +
                         tile.mesh.line_vertices.size() * sizeof(LineVertex) +
                         (tile.mesh.area_indices.size() + tile.mesh.line_indices.size()) * 4;
        } else {
            mesh = upload(tile.mesh);
        }
        timing.upload_max_us = std::max(timing.upload_max_us, static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - upload_started).count()));
        ++timing.uploads;
        mesh.last_used = frame;
        gpu_bytes += mesh.bytes;
        bytes += mesh.bytes;
        ++uploaded;
        auto existing = gpu.find(tile.key);
        if (existing != gpu.end()) {
            retire(existing->second);
            gpu_bytes -= existing->second.bytes;
        }
        gpu[tile.key] = mesh;
        labels[tile.key] = std::move(tile.labels);
    }
    if (used == arrivals.size())
        arrivals.clear();  // keeps capacity for the next swap
    else
        arrivals.erase(arrivals.begin(), arrivals.begin() + static_cast<std::ptrdiff_t>(used));
}

void VectorMap::Impl::place_labels(const MapView& view, const std::vector<TileKey>& draw_list,
                                   double zoom, double dt, std::size_t& calls, bool& fading)
{
    const int W = view.width, H = view.height;
    // The map is drawn at sub-pixel positions; a label snapped to whole
    // pixels while the camera moves steps against it (jitter). Snap only
    // when the camera is still; linear filtering keeps moving labels smooth.
    bool camera_still = view.origin_x31 == last_origin[0] &&
                        view.origin_y31 == last_origin[1];
    for (int i = 0; i < 9 && camera_still; ++i)
        camera_still = view.h[i] == last_h[i];
    last_origin[0] = view.origin_x31;
    last_origin[1] = view.origin_y31;
    std::copy(std::begin(view.h), std::end(view.h), std::begin(last_h));
    if (!camera_still)
        fading = true;  // one more frame once it stops, to snap
    const auto to_world = [](const TileKey& key, const Point& p) {
        const Placement pl = tile_placement(key);
        return std::pair<double, double>{pl.ox + p[0] * pl.k, pl.oy + p[1] * pl.k};
    };
    const auto to_screen = [&](const TileKey& key, const Point& p, bool& ok) {
        const auto world = to_world(key, p);
        double sx, sy, w;
        project(view, world.first, world.second, sx, sy, w);
        ok = w > 0.0;
        return ScreenPoint{static_cast<float>(sx), static_cast<float>(sy)};
    };
    std::vector<OrientedBox>& accepted = scratch.accepted;
    accepted.clear();
    if (view.location_dot || view.dot_x > 0.0f)
        accepted.push_back({view.dot_x, view.dot_y, 14.0f, 14.0f, 0.0f});
    // UI panels: nothing is placed underneath.
    for (const auto& r : view.blocked)
        accepted.push_back({r[0] + r[2] / 2.0f, r[1] + r[3] / 2.0f, r[2] / 2.0f, r[3] / 2.0f, 0.0f});
    label_vertices.clear();
    label_indices.clear();
    next_fade.clear();
    placed_pois.clear();
    atlas.begin_frame(6);
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
    const auto quad = [&](std::uint64_t atlas_key, const LabelImage& image, OrientedBox box,
                          float alpha) {
        LabelAtlas::Slot slot;
        if (!atlas.slot(atlas_key, image, slot))
            return;
        if (camera_still && box.angle == 0.0f) {  // crisp: snap to pixels
            box.cx = std::round(box.cx - box.half_width) + box.half_width;
            box.cy = std::round(box.cy - box.half_height) + box.half_height;
        }
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
    // Fades in from the alpha it had last frame; remembers the world anchor
    // so the label can fade out where it was if it is not placed again.
    const auto emit_label = [&](std::uint64_t id, std::uint64_t atlas_key,
                                const LabelImage& image, const OrientedBox& box,
                                std::pair<double, double> anchor, ScreenPoint anchor_screen) {
        const Shown* previous = fade.find(id);
        const float alpha = std::min(
            1.0f, (previous ? previous->alpha : 0.0f) + static_cast<float>(dt) * 5.0f);
        Shown& shown = next_fade[id];
        shown.alpha = alpha;
        shown.atlas_key = atlas_key;
        shown.image = &image;
        shown.x31 = anchor.first;
        shown.y31 = anchor.second;
        shown.off_x = box.cx - anchor_screen.x;
        shown.off_y = box.cy - anchor_screen.y;
        shown.half_w = box.half_width;
        shown.half_h = box.half_height;
        shown.angle = box.angle;
        if (alpha < 1.0f)
            fading = true;
        quad(atlas_key, image, box, alpha);
    };

    std::vector<Candidate>& candidates = scratch.candidates;
    candidates.clear();
    LabelIdMap<char>& seen_streets = scratch.seen_streets;
    seen_streets.clear();
    const int view_zoom = static_cast<int>(std::floor(zoom));
    const auto add = [&](int group, std::uint64_t fade_id, int rank, int kind, const TileKey& key,
                         const void* label) {
        candidates.push_back({group, fade.contains(fade_id) ? 0 : 1, rank, kind,
                              static_cast<std::uint32_t>(candidates.size()), &key, label});
    };
    for (const auto& key : draw_list) {
        const auto it = labels.find(key);
        if (it == labels.end())
            continue;
        for (const auto& street : it->second.streets)
            if (seen_streets.emplace(street.id_hash).second)
                add(street.rank <= 3 ? 0 : 3, street.name_hash, street.rank, 0, key, &street);
        for (const auto& place : it->second.places) {
            const int rank = place.rank(view_zoom);
            if (rank >= 0)
                add(1, place.key, rank, 2, key, &place);
        }
        for (const auto& poi : it->second.pois)
            if (zoom >= poi.min_zoom)
                add(2, poi.id_hash, poi.rank, 1, key, &poi);
    }
    // The order tie-break keeps equal candidates in input order (as a stable
    // sort would) without std::stable_sort's buffer.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.shown != b.shown) return a.shown < b.shown;
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.order < b.order;
    });
    // Pins of the street names placed this frame (at most 16: scanned).
    auto& placed_streets = scratch.placed_streets;
    placed_streets.clear();
    std::size_t streets = 0, pois = 0, places = 0;
    std::vector<ScreenPoint>& screen_path = scratch.screen_path;
    std::vector<float>& lengths = scratch.lengths;
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
                for (const auto& other : placed_streets)
                    if (other.first == street.name_hash &&
                        std::hypot(other.second.x - pin_screen.x, other.second.y - pin_screen.y) < 380.0f)
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
                                                       static_cast<float>(image.height), box, 0.5f,
                                                       scratch.cumulative))
                    continue;
                if (!on_screen(box) || !free_of(box))
                    continue;
                accepted.push_back(box);
                placed_streets.emplace_back(street.name_hash, pin_screen);
                emit_label(street.name_hash, street.key, image, box, to_world(*c.key, pin),
                           pin_screen);
                ++streets;
                break;
            }
        } else if (c.kind == 1) {
            if (pois >= 36) continue;
            const auto& poi = *static_cast<const PoiLabel*>(c.label);
            bool ok = false;
            const ScreenPoint at = to_screen(*c.key, poi.pos, ok);
            if (!ok) continue;
            const auto& icon_image = *poi.icon_image;
            const OrientedBox icon{at.x, at.y, icon_image.width / 2.0f, icon_image.height / 2.0f, 0.0f};
            if (!on_screen(icon) || !free_of(icon))
                continue;
            const OrientedBox text{at.x + icon_image.width / 2.0f + 3.0f + poi.text->width / 2.0f, at.y,
                                   poi.text->width / 2.0f, poi.text->height / 2.0f, 0.0f};
            const bool with_text = on_screen(text) && free_of(text);
            accepted.push_back(icon);
            const auto anchor = to_world(*c.key, poi.pos);
            emit_label(poi.id_hash, poi.icon_key, icon_image, icon, anchor, at);
            placed_pois.push_back({&poi, c.key, at.x, at.y, icon_image.width / 2.0f});
            if (with_text) {
                accepted.push_back(text);
                emit_label(poi.id_hash ^ kPoiTextSalt, poi.text_key, *poi.text, text, anchor, at);
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
            emit_label(place.key, place.key, *place.image, box, to_world(*c.key, place.pos), at);
            placed_pois.push_back({nullptr, c.key, at.x, at.y, 0.0f, &place, box.half_width,
                                   box.half_height});
            ++places;
        }
    }
    // Labels not placed this frame fade out where they were, unless that
    // spot is now taken or off screen.
    for (std::size_t i = 0; i < fade.size(); ++i) {
        const std::uint64_t id = fade.key_at(i);
        if (next_fade.contains(id))
            continue;
        Shown shown = fade.value_at(i);
        shown.alpha -= static_cast<float>(dt) * 5.0f;
        if (shown.alpha <= 0.0f || !shown.image)
            continue;
        double sx, sy, w;
        project(view, shown.x31, shown.y31, sx, sy, w);
        if (!(w > 0.0))
            continue;
        const OrientedBox box{static_cast<float>(sx) + shown.off_x, static_cast<float>(sy) + shown.off_y,
                              shown.half_w, shown.half_h, shown.angle};
        if (!on_screen(box) || !free_of(box))
            continue;
        accepted.push_back(box);
        quad(shown.atlas_key, *shown.image, box, shown.alpha);
        fading = true;
        next_fade[id] = shown;
    }
    if (view.location_dot) {
        const OrientedBox dot{view.dot_x, view.dot_y, location_dot->width / 2.0f,
                              location_dot->height / 2.0f, 0.0f};
        quad(location_key, *location_dot, dot, 1.0f);
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

bool VectorMap::pick_poi(float x, float y, float radius, PoiPick& out) const
{
    const auto& d = *impl_;
    const Impl::PlacedPoi* best = nullptr;
    float best_distance = 0.0f;
    // POI icons first; a place name only when no POI is under the finger.
    for (const bool places : {false, true}) {
        for (const auto& placed : d.placed_pois) {
            if ((placed.place != nullptr) != places)
                continue;
            const float distance = std::hypot(placed.x - x, placed.y - y);
            const bool hit = places
                ? std::fabs(placed.x - x) <= placed.half_width + radius / 2.0f &&
                      std::fabs(placed.y - y) <= placed.half_height + radius / 2.0f
                : distance <= placed.half + radius;
            if (hit && (!best || distance < best_distance)) {
                best = &placed;
                best_distance = distance;
            }
        }
        if (best)
            break;
    }
    if (!best)
        return false;
    const Placement pl = tile_placement(*best->key);
    const Point& pos = best->place ? best->place->pos : best->label->pos;
    out.name = best->place ? best->place->name : best->label->name;
    out.icon = best->place ? std::string() : best->label->icon;
    out.place = best->place ? best->place->place : std::string();
    out.x31 = pl.ox + pos[0] * pl.k;
    out.y31 = pl.oy + pos[1] * pl.k;
    out.screen_x = best->x;
    out.screen_y = best->y;
    return true;
}

VectorFrameTiming VectorMap::frame_timing() const
{
    return impl_->timing;
}

VectorMapStats VectorMap::stats() const
{
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    VectorMapStats out = impl_->stats;
    // Maxima cover the time since the previous call.
    impl_->stats.fetch_ms_max = 0.0;
    impl_->stats.label_place_ms_max = 0.0;
    out.aborted = impl_->builder.aborted();
    out.empty_tiles = impl_->builder.empty_tiles();
    out.disk_writes = impl_->builder.disk_writes();
    out.disk_failures = impl_->builder.disk_failures();
    out.block_reads = impl_->builder.block_reads();
    out.block_hits = impl_->builder.block_hits();
    return out;
}

bool VectorMap::last_frame_incomplete() const
{
    return impl_->last_incomplete;
}

} // namespace navicore_vector
