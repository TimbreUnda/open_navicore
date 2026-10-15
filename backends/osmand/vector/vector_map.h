#pragma once

// GPU vector map: OBF map objects -> per-tile GL geometry (worker threads)
// plus street/POI/place labels, drawn with GLES2 under any camera that maps
// the ground plane to the screen by a homography (2D or tilted 3D).
//
// Used by the navigation backend (camera from OsmAnd's renderer).

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace OsmAnd {
class IObfsCollection;
class CoreResourcesEmbeddedBundle;
} // namespace OsmAnd

namespace navicore_vector {

// What the camera shows this frame.
struct MapView {
    int width = 0;   // viewport in pixels (origin top-left)
    int height = 0;
    // Reference point; h maps (x31 - origin_x31, y31 - origin_y31, 1) to
    // homogeneous screen pixels (sx * w, sy * w, w). Row-major.
    double origin_x31 = 0.0;
    double origin_y31 = 0.0;
    double h[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    // Ground area to load, in 31-bit coordinates.
    double min_x31 = 0.0, min_y31 = 0.0, max_x31 = 0.0, max_y31 = 0.0;
    // Location dot (also kept clear of labels).
    bool location_dot = true;
    float dot_x = 0.0f, dot_y = 0.0f;
    // Screen rectangles covered by UI (x, y, width, height); no label is
    // placed under them.
    std::vector<std::array<float, 4>> blocked;
    // Camera zoom transition: the deepest tile zoom to use until it ends
    // (one level instead of every level crossed). Negative: none.
    double tile_zoom_limit = -1.0;
};

struct VectorMapStats {
    std::uint64_t tiles_built = 0;
    double fetch_ms_total = 0.0, fetch_ms_max = 0.0;
    double build_ms_total = 0.0;    // geometry
    double labels_ms_total = 0.0;   // label rasterization per tile
    std::uint64_t frames = 0;
    std::uint64_t incomplete_frames = 0;
    std::uint64_t draw_calls = 0;
    std::uint64_t label_quads = 0;
    double label_place_ms_max = 0.0;
    std::size_t gpu_tiles = 0;
    std::size_t gpu_bytes = 0;
    std::size_t atlas_uploads = 0, atlas_resets = 0;
    std::uint64_t disk_hits = 0, disk_writes = 0, disk_failures = 0;
    std::uint64_t block_reads = 0;  // map reads (one per z14 block and detail)
    std::uint64_t block_hits = 0;   // tiles cut from an already read block
    double disk_ms_total = 0.0;     // reading cached tiles
    std::uint64_t aborted = 0;      // fetches cancelled as no longer wanted
    std::uint64_t empty_tiles = 0;  // no map data after the retries
    std::size_t wanted_tiles = 0, prefetch_tiles = 0;
    int min_tile_zoom = 0;          // detail levels in the last frame
    int tile_zoom = 0;
    double zoom = 0.0;
};

// Pixels across one 2^zoom tile at style zoom `zoom` (density 1.5).
constexpr double kVectorTilePx = 384.0;

// A POI or a place name under the finger (see VectorMap::pick_poi).
struct PoiPick {
    std::string name;
    std::string icon;     // OsmAnd icon name, e.g. "amenity_fuel"; empty for a place
    std::string place;    // place name: its OSM place type ("city", "village")
    double x31 = 0.0, y31 = 0.0;
    float screen_x = 0.0f, screen_y = 0.0f;
};

// Where the last render() spent its time, in microseconds (bench timing).
struct VectorFrameTiming {
    std::uint32_t take_us = 0;    // finished tiles: builder lock + GL uploads
    std::uint32_t uploads = 0;    // tiles uploaded
    std::uint32_t route_us = 0;   // route mesh update
    std::uint32_t want_us = 0;    // tile selection + builder.want()
    std::uint32_t draw_us = 0;    // GL draw calls (tiles, route, arrow)
    std::uint32_t labels_us = 0;  // label placement and label GL
    std::uint32_t evict_us = 0;   // LRU eviction (glDeleteBuffers, builder.forget)
    std::uint32_t upload_max_us = 0;  // slowest single tile upload
};

class VectorMap {
public:
    // on_tile_ready runs on a worker thread when a tile finishes (use it to
    // request a frame). Workers start immediately.
    VectorMap(std::shared_ptr<const OsmAnd::IObfsCollection> obfs,
              std::shared_ptr<const OsmAnd::CoreResourcesEmbeddedBundle> resources,
              int threads, std::function<void()> on_tile_ready);
    ~VectorMap();
    VectorMap(const VectorMap&) = delete;
    VectorMap& operator=(const VectorMap&) = delete;

    // GL objects; call with the context current.
    bool init_gl();
    void release_gl(bool context_lost);

    // Built tiles are kept under dir/<hash of identity>/ (identity names the
    // map file and anything else that changes geometry). Empty dir: off.
    // Writes stop while the file system has less than min_free_bytes free.
    void set_disk_cache(const std::string& dir, const std::string& identity,
                        std::uint64_t min_free_bytes);

    // Route polyline in 31-bit coordinates; empty clears it.
    void set_route(const std::vector<std::pair<std::int32_t, std::int32_t>>& points31);
    // Vehicle position on the route: the part behind it is drawn grey and
    // tiles ahead of it are prefetched. valid=false: no progress.
    void set_route_progress(bool valid, std::int32_t x31, std::int32_t y31);
    // Turn arrow on the route at route point `geometry_index` (the
    // manoeuvre); show=false hides it.
    void set_maneuver(bool show, std::size_t geometry_index);
    // Overzoom: z14 tiles at full z16 detail serve every deeper zoom
    // (fewer, larger tiles; more geometry per draw). Tiles are rebuilt.
    void set_overzoom(bool on);
    // Night palette; tiles are rebuilt (or read from their own cache).
    void set_night(bool night);
    // Tile uploads (render thread): reuse evicted tiles' GL buffers instead
    // of deleting and allocating new ones, and at most `budget` tiles per
    // frame (1..8).
    void set_upload_policy(bool pool, int budget);
    // Upload tiles on a thread with a shared GL context (call before
    // init_gl; falls back to render-thread uploads if the context fails).
    void set_upload_thread(bool on);
    bool upload_thread_active() const;

    // Draws map, route and labels into the current framebuffer; clear fills
    // it with the land colour first. Returns true while it wants more frames
    // (tiles pending or labels fading in).
    bool render(const MapView& view, double dt_seconds, bool clear);

    // Style zoom implied by the view's scale at its origin.
    static double zoom_of(const MapView& view);
    // Builds h for a flat top-down camera: centre at (anchor_x, anchor_y)
    // pixels, azimuth degrees (map heading at the top of the screen).
    static void set_flat_camera(MapView& view, double x31, double y31, double zoom,
                                double azimuth_degrees, double anchor_x, double anchor_y);
    // Solves h from four ground points (31-bit) and their screen pixels.
    static bool set_from_correspondences(MapView& view, const double world31[4][2],
                                         const double screen[4][2]);

    // Maxima (fetch_ms_max, label_place_ms_max) are since the previous call.
    VectorMapStats stats() const;
    VectorFrameTiming frame_timing() const;
    // The POI shown in the last frame nearest to (x, y) within `radius`
    // pixels of its icon, else a place name shown there (city, town...);
    // false when there is neither.
    bool pick_poi(float x, float y, float radius, PoiPick& out) const;
    bool last_frame_incomplete() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace navicore_vector
