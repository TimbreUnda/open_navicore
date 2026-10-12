#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace navicore_osmand {

enum class TileFormat : std::uint32_t { Rgb565 = 1, Rgba8888 = 2 };

std::size_t tile_bytes(TileFormat format, std::uint32_t tile_size) noexcept;

struct TileKey {
    std::uint32_t zoom = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

// Persistent rasterized-tile store in pack files ("pak" style): each file
// holds a 16x16 block of tiles of one zoom, so a 32 GB cache is ~10k files
// rather than millions. Layout of <root>/z<zoom>/<x/16>_<y/16>.pak:
//
//   header  32 bytes  magic "NCTP", version, zoom, block x, block y, format,
//                     tile size, reserved
//   index   256 x 16  per tile: payload offset, compressed size, CRC32 of the
//                     compressed payload, raw size (0 offset = absent)
//   payload           zlib streams appended in write order
//
// A tile is written by appending its payload and then updating its index
// entry, so a crash can leave at most an entry that fails its CRC or bounds
// check, which reads as a miss. The cache is regenerable; nothing is fsynced
// per tile. A writable store keeps total size under `budget_bytes` by
// deleting the least-recently-used pack.
class TilePackStore {
public:
    struct Options {
        std::string root;
        bool writable = false;
        std::uint64_t budget_bytes = 0;     // 0 = unlimited
        TileFormat format = TileFormat::Rgb565;
        std::uint32_t tile_size = 256;
        int compression_level = 1;          // 1 fast runtime, 9 for packing
        std::size_t open_packs = 16;
        // Writes stop while the filesystem has less than this available.
        // Seen live: an unbounded cache filled a 1 GB /datastore and broke
        // the next backend install.
        std::uint64_t reserve_bytes = 256ull << 20;
        // Returns available bytes on the store's filesystem, or -1 if
        // unknown. Defaults to statvfs(root); injectable for tests.
        std::function<std::int64_t(const std::string&)> available_bytes;
    };

    struct Stats {
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::uint64_t writes = 0;
        std::uint64_t corrupt = 0;
        std::uint64_t evicted_packs = 0;
        std::uint64_t bytes = 0;            // tracked size of writable store
        std::uint64_t refused_low_space = 0;
    };

    explicit TilePackStore(Options options);
    ~TilePackStore();
    TilePackStore(const TilePackStore&) = delete;
    TilePackStore& operator=(const TilePackStore&) = delete;

    bool get(const TileKey& key, std::vector<std::uint8_t>& pixels);
    bool contains(const TileKey& key);
    bool put(const TileKey& key, const std::uint8_t* pixels, std::size_t size);
    Stats stats() const;
    const Options& options() const noexcept { return options_; }

private:
    struct IndexEntry {
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t crc = 0;
        std::uint32_t raw_size = 0;
    };
    struct Pack {
        int fd = -1;
        bool writable = false;
        std::uint64_t file_size = 0;
        IndexEntry index[256];
        std::list<std::string>::iterator lru;
    };

    std::string pack_path(const TileKey& key) const;
    Pack* open_pack(const TileKey& key, bool create);
    void close_pack(const std::string& path);
    void scan_existing();
    void enforce_budget(const std::string& keep);
    bool space_allows_write();

    Options options_;
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Pack>> packs_;
    std::list<std::string> lru_;            // front = most recently used
    // Writable store: every pack on disk with its size and recency.
    std::map<std::string, std::uint64_t> disk_sizes_;
    std::map<std::string, std::uint64_t> disk_used_;
    std::uint64_t use_clock_ = 0;
    bool scanned_ = false;
    std::uint64_t writes_since_space_check_ = 0;
    bool low_space_ = false;
    Stats stats_;
    std::vector<std::uint8_t> compressed_;
};

// Inclusive Web-Mercator tile range covering a lat/lon box at one zoom.
struct TileRange {
    std::uint32_t zoom = 0;
    std::uint32_t min_x = 0;
    std::uint32_t min_y = 0;
    std::uint32_t max_x = 0;
    std::uint32_t max_y = 0;
    std::uint64_t count() const noexcept
    {
        return static_cast<std::uint64_t>(max_x - min_x + 1) *
               (max_y - min_y + 1);
    }
};

TileRange tile_range(std::uint32_t zoom, double min_latitude,
                     double min_longitude, double max_latitude,
                     double max_longitude) noexcept;

// Pre-cache manifest: one region per line,
//   <min_zoom> <max_zoom> <min_lat> <min_lon> <max_lat> <max_lon>
// '#' starts a comment. Invalid lines are reported through `errors`.
std::vector<TileRange> parse_precache_manifest(const std::string& text,
                                               std::size_t* errors = nullptr);

// Read-through pair: a shipped read-only base (pre-cached region) under a
// writable runtime store. Both are namespaced by a key that changes whenever
// map data, style settings or tile format change.
class TileCache {
public:
    TileCache(const std::string& base_root, const std::string& runtime_root,
              const std::string& namespace_key, TileFormat format,
              std::uint32_t tile_size, std::uint64_t runtime_budget_bytes);

    bool get(const TileKey& key, std::vector<std::uint8_t>& pixels);
    bool put(const TileKey& key, const std::uint8_t* pixels, std::size_t size);
    TileFormat format() const noexcept { return format_; }
    std::uint32_t tile_size() const noexcept { return tile_size_; }
    TilePackStore::Stats runtime_stats() const;

    // Stable 16-hex-digit FNV-1a hash used as a directory name.
    static std::string namespace_hash(const std::string& description);

private:
    TileFormat format_;
    std::uint32_t tile_size_;
    std::unique_ptr<TilePackStore> base_;
    std::unique_ptr<TilePackStore> runtime_;
};

} // namespace navicore_osmand
