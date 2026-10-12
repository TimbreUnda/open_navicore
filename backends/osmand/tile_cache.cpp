#include "tile_cache.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace navicore_osmand {
namespace {
constexpr std::uint32_t pack_magic = 0x5054434eu; // "NCTP" little-endian
constexpr std::uint32_t pack_version = 1u;
constexpr std::uint32_t block_shift = 4u;         // 16 x 16 tiles per pack
constexpr std::uint32_t block_tiles = 1u << block_shift;
constexpr std::uint32_t header_bytes = 32u;
constexpr std::uint32_t index_entry_bytes = 16u;
constexpr std::uint32_t index_bytes = 256u * index_entry_bytes;
constexpr std::uint32_t data_offset = header_bytes + index_bytes;

struct Header {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t zoom;
    std::uint32_t block_x;
    std::uint32_t block_y;
    std::uint32_t format;
    std::uint32_t tile_size;
    std::uint32_t reserved;
};
static_assert(sizeof(Header) == header_bytes, "pack header layout");

bool read_all(int fd, void* buffer, std::size_t size, off_t offset)
{
    auto* bytes = static_cast<std::uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t got = ::pread(fd, bytes, size, offset);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return false;
        bytes += got;
        size -= static_cast<std::size_t>(got);
        offset += got;
    }
    return true;
}

bool write_all(int fd, const void* buffer, std::size_t size, off_t offset)
{
    const auto* bytes = static_cast<const std::uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t put = ::pwrite(fd, bytes, size, offset);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) return false;
        bytes += put;
        size -= static_cast<std::size_t>(put);
        offset += put;
    }
    return true;
}

bool make_directories(const std::string& path)
{
    std::string current;
    for (std::size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if ((path[i] == '/' && i > 0) || i + 1 == path.size()) {
            if (::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
                return false;
        }
    }
    return true;
}

std::uint32_t tile_slot(const TileKey& key)
{
    return ((key.y & (block_tiles - 1u)) << block_shift) |
           (key.x & (block_tiles - 1u));
}
} // namespace

std::size_t tile_bytes(TileFormat format, std::uint32_t tile_size) noexcept
{
    const std::size_t pixels = static_cast<std::size_t>(tile_size) * tile_size;
    return pixels * (format == TileFormat::Rgb565 ? 2u : 4u);
}

TilePackStore::TilePackStore(Options options) : options_(std::move(options))
{
    if (options_.open_packs == 0)
        options_.open_packs = 1;
    if (!options_.available_bytes) {
        options_.available_bytes = [](const std::string& root) -> std::int64_t {
            // The root may not exist yet; walk up to an existing directory.
            std::string path = root;
            struct statvfs info {};
            while (!path.empty()) {
                if (::statvfs(path.c_str(), &info) == 0)
                    return static_cast<std::int64_t>(info.f_bavail) *
                           static_cast<std::int64_t>(info.f_frsize);
                const auto slash = path.rfind('/');
                if (slash == std::string::npos || slash == 0)
                    break;
                path.resize(slash);
            }
            return -1;
        };
    }
}

bool TilePackStore::space_allows_write()
{
    // Re-check every 64 writes: cheap, and a 128 KiB tile cannot overrun a
    // 256 MiB reserve in that interval.
    if (writes_since_space_check_ == 0) {
        const std::int64_t available = options_.available_bytes(options_.root);
        low_space_ = available >= 0 &&
                     static_cast<std::uint64_t>(available) < options_.reserve_bytes;
    }
    writes_since_space_check_ = (writes_since_space_check_ + 1) % 64u;
    if (low_space_) {
        ++stats_.refused_low_space;
        writes_since_space_check_ = 0; // re-check next time
        return false;
    }
    return true;
}

TilePackStore::~TilePackStore()
{
    for (auto& entry : packs_)
        if (entry.second->fd >= 0)
            ::close(entry.second->fd);
}

std::string TilePackStore::pack_path(const TileKey& key) const
{
    char name[96];
    std::snprintf(name, sizeof(name), "/z%u/%u_%u.pak", key.zoom,
                  key.x >> block_shift, key.y >> block_shift);
    return options_.root + name;
}

void TilePackStore::close_pack(const std::string& path)
{
    const auto found = packs_.find(path);
    if (found == packs_.end())
        return;
    if (found->second->fd >= 0)
        ::close(found->second->fd);
    lru_.erase(found->second->lru);
    packs_.erase(found);
}

TilePackStore::Pack* TilePackStore::open_pack(const TileKey& key, bool create)
{
    const std::string path = pack_path(key);
    const auto found = packs_.find(path);
    if (found != packs_.end()) {
        Pack* pack = found->second.get();
        if (!create || pack->writable) {
            lru_.splice(lru_.begin(), lru_, pack->lru);
            if (options_.writable)
                disk_used_[path] = ++use_clock_;
            return pack;
        }
        close_pack(path); // reopen read-write below
    }
    const bool writable = create && options_.writable;
    int fd = ::open(path.c_str(), writable ? O_RDWR : O_RDONLY);
    bool fresh = false;
    if (fd < 0 && writable && errno == ENOENT) {
        const std::string directory = path.substr(0, path.rfind('/'));
        if (!make_directories(directory))
            return nullptr;
        fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
        fresh = fd >= 0;
    }
    if (fd < 0)
        return nullptr;

    auto pack = std::make_unique<Pack>();
    pack->fd = fd;
    pack->writable = writable;
    const std::uint32_t expected_format =
        static_cast<std::uint32_t>(options_.format);
    if (fresh) {
        const Header header{pack_magic, pack_version, key.zoom,
                            key.x >> block_shift, key.y >> block_shift,
                            expected_format, options_.tile_size, 0u};
        std::vector<std::uint8_t> empty(index_bytes, 0u);
        if (!write_all(fd, &header, sizeof(header), 0) ||
            !write_all(fd, empty.data(), empty.size(), header_bytes)) {
            ::close(fd);
            ::unlink(path.c_str());
            return nullptr;
        }
        pack->file_size = data_offset;
        if (options_.writable) {
            disk_sizes_[path] = data_offset;
            stats_.bytes += data_offset;
        }
    } else {
        Header header{};
        struct stat info {};
        if (!read_all(fd, &header, sizeof(header), 0) ||
            header.magic != pack_magic || header.version != pack_version ||
            header.zoom != key.zoom || header.format != expected_format ||
            header.tile_size != options_.tile_size ||
            !read_all(fd, pack->index, index_bytes, header_bytes) ||
            ::fstat(fd, &info) != 0) {
            ::close(fd);
            ++stats_.corrupt;
            return nullptr;
        }
        pack->file_size = static_cast<std::uint64_t>(info.st_size);
    }
    while (packs_.size() >= options_.open_packs && !lru_.empty())
        close_pack(lru_.back());
    lru_.push_front(path);
    pack->lru = lru_.begin();
    Pack* raw = pack.get();
    packs_[path] = std::move(pack);
    if (options_.writable)
        disk_used_[path] = ++use_clock_;
    return raw;
}

bool TilePackStore::get(const TileKey& key, std::vector<std::uint8_t>& pixels)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Pack* pack = open_pack(key, false);
    if (!pack) {
        ++stats_.misses;
        return false;
    }
    IndexEntry& entry = pack->index[tile_slot(key)];
    const std::size_t expected = tile_bytes(options_.format, options_.tile_size);
    if (entry.offset == 0 || entry.size == 0) {
        ++stats_.misses;
        return false;
    }
    if (entry.raw_size != expected ||
        static_cast<std::uint64_t>(entry.offset) + entry.size > pack->file_size) {
        ++stats_.corrupt;
        ++stats_.misses;
        return false;
    }
    compressed_.resize(entry.size);
    if (!read_all(pack->fd, compressed_.data(), entry.size, entry.offset) ||
        static_cast<std::uint32_t>(crc32(0u, compressed_.data(), entry.size)) !=
            entry.crc) {
        ++stats_.corrupt;
        ++stats_.misses;
        return false;
    }
    pixels.resize(expected);
    uLongf length = static_cast<uLongf>(expected);
    if (uncompress(pixels.data(), &length, compressed_.data(), entry.size) != Z_OK ||
        length != expected) {
        ++stats_.corrupt;
        ++stats_.misses;
        return false;
    }
    ++stats_.hits;
    return true;
}

bool TilePackStore::contains(const TileKey& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Pack* pack = open_pack(key, false);
    return pack && pack->index[tile_slot(key)].offset != 0;
}

void TilePackStore::scan_existing()
{
    scanned_ = true;
    DIR* root = ::opendir(options_.root.c_str());
    if (!root)
        return;
    std::uint64_t newest = 0;
    while (dirent* zoom = ::readdir(root)) {
        if (zoom->d_name[0] != 'z')
            continue;
        const std::string directory = options_.root + "/" + zoom->d_name;
        DIR* packs = ::opendir(directory.c_str());
        if (!packs)
            continue;
        while (dirent* file = ::readdir(packs)) {
            const std::string name = file->d_name;
            if (name.size() < 5 || name.compare(name.size() - 4, 4, ".pak") != 0)
                continue;
            const std::string path = directory + "/" + name;
            struct stat info {};
            if (::stat(path.c_str(), &info) != 0)
                continue;
            if (disk_sizes_.count(path))
                continue;
            disk_sizes_[path] = static_cast<std::uint64_t>(info.st_size);
            // Older packs from previous runs rank below anything touched now.
            const auto modified = static_cast<std::uint64_t>(info.st_mtime);
            disk_used_[path] = modified;
            newest = std::max(newest, modified);
            stats_.bytes += static_cast<std::uint64_t>(info.st_size);
        }
        ::closedir(packs);
    }
    ::closedir(root);
    use_clock_ = std::max(use_clock_, newest);
}

void TilePackStore::enforce_budget(const std::string& keep)
{
    if (options_.budget_bytes == 0)
        return;
    while (stats_.bytes > options_.budget_bytes && disk_sizes_.size() > 1) {
        std::string oldest;
        std::uint64_t oldest_use = ~0ull;
        for (const auto& used : disk_used_) {
            if (used.first != keep && used.second < oldest_use) {
                oldest = used.first;
                oldest_use = used.second;
            }
        }
        if (oldest.empty())
            return;
        close_pack(oldest);
        ::unlink(oldest.c_str());
        stats_.bytes -= std::min(stats_.bytes, disk_sizes_[oldest]);
        disk_sizes_.erase(oldest);
        disk_used_.erase(oldest);
        ++stats_.evicted_packs;
    }
}

bool TilePackStore::put(const TileKey& key, const std::uint8_t* pixels,
                        std::size_t size)
{
    if (!options_.writable || !pixels ||
        size != tile_bytes(options_.format, options_.tile_size))
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanned_)
        scan_existing();
    if (!space_allows_write())
        return false;
    Pack* pack = open_pack(key, true);
    if (!pack || !pack->writable)
        return false;
    uLongf bound = compressBound(static_cast<uLong>(size));
    compressed_.resize(bound);
    if (compress2(compressed_.data(), &bound, pixels, static_cast<uLong>(size),
                  options_.compression_level) != Z_OK)
        return false;
    const std::uint64_t offset = pack->file_size;
    if (offset + bound > 0xffffffffull)
        return false;
    IndexEntry entry;
    entry.offset = static_cast<std::uint32_t>(offset);
    entry.size = static_cast<std::uint32_t>(bound);
    entry.crc = static_cast<std::uint32_t>(crc32(0u, compressed_.data(),
                                                 static_cast<uInt>(bound)));
    entry.raw_size = static_cast<std::uint32_t>(size);
    const std::uint32_t slot = tile_slot(key);
    // Payload first, index second: a crash leaves a stale or absent entry.
    if (!write_all(pack->fd, compressed_.data(), bound,
                   static_cast<off_t>(offset)) ||
        !write_all(pack->fd, &entry, sizeof(entry),
                   static_cast<off_t>(header_bytes + slot * index_entry_bytes)))
        return false;
    pack->index[slot] = entry;
    pack->file_size = offset + bound;
    const std::string path = pack_path(key);
    stats_.bytes += bound;
    disk_sizes_[path] = pack->file_size;
    ++stats_.writes;
    enforce_budget(path);
    return true;
}

TilePackStore::Stats TilePackStore::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

TileRange tile_range(std::uint32_t zoom, double min_latitude,
                     double min_longitude, double max_latitude,
                     double max_longitude) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    const double tiles = static_cast<double>(1u << std::min(zoom, 30u));
    const auto tile_x = [&](double longitude) {
        const double x = (longitude + 180.0) / 360.0 * tiles;
        return static_cast<std::uint32_t>(
            std::clamp(x, 0.0, tiles - 1.0));
    };
    const auto tile_y = [&](double latitude) {
        latitude = std::clamp(latitude, -85.0511, 85.0511) * pi / 180.0;
        const double y = (1.0 - std::log(std::tan(latitude) +
                                         1.0 / std::cos(latitude)) / pi) /
                         2.0 * tiles;
        return static_cast<std::uint32_t>(std::clamp(y, 0.0, tiles - 1.0));
    };
    TileRange range;
    range.zoom = zoom;
    range.min_x = tile_x(std::min(min_longitude, max_longitude));
    range.max_x = tile_x(std::max(min_longitude, max_longitude));
    // Tile y grows southward.
    range.min_y = tile_y(std::max(min_latitude, max_latitude));
    range.max_y = tile_y(std::min(min_latitude, max_latitude));
    return range;
}

std::vector<TileRange> parse_precache_manifest(const std::string& text,
                                               std::size_t* errors)
{
    std::vector<TileRange> ranges;
    std::size_t bad = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        if (line.find_first_not_of(" \t\r") == std::string::npos)
            continue;
        unsigned min_zoom = 0;
        unsigned max_zoom = 0;
        double box[4] = {};
        char extra = 0;
        if (std::sscanf(line.c_str(), "%u %u %lf %lf %lf %lf %c", &min_zoom,
                        &max_zoom, &box[0], &box[1], &box[2], &box[3],
                        &extra) != 6 ||
            min_zoom > max_zoom || max_zoom > 21u ||
            box[0] < -90.0 || box[2] > 90.0 || box[0] > box[2] ||
            box[1] < -180.0 || box[3] > 180.0 || box[1] > box[3]) {
            ++bad;
            continue;
        }
        for (unsigned zoom = min_zoom; zoom <= max_zoom; ++zoom)
            ranges.push_back(tile_range(zoom, box[0], box[1], box[2], box[3]));
    }
    if (errors) *errors = bad;
    return ranges;
}

std::string TileCache::namespace_hash(const std::string& description)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : description) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx",
                  static_cast<unsigned long long>(hash));
    return text;
}

TileCache::TileCache(const std::string& base_root,
                     const std::string& runtime_root,
                     const std::string& namespace_key, TileFormat format,
                     std::uint32_t tile_size, std::uint64_t runtime_budget_bytes)
    : format_(format), tile_size_(tile_size)
{
    if (!base_root.empty()) {
        TilePackStore::Options options;
        options.root = base_root + "/" + namespace_key;
        options.format = format;
        options.tile_size = tile_size;
        base_ = std::make_unique<TilePackStore>(options);
    }
    if (!runtime_root.empty()) {
        TilePackStore::Options options;
        options.root = runtime_root + "/" + namespace_key;
        options.writable = true;
        options.budget_bytes = runtime_budget_bytes;
        options.format = format;
        options.tile_size = tile_size;
        runtime_ = std::make_unique<TilePackStore>(options);
    }
}

bool TileCache::get(const TileKey& key, std::vector<std::uint8_t>& pixels)
{
    return (runtime_ && runtime_->get(key, pixels)) ||
           (base_ && base_->get(key, pixels));
}

bool TileCache::put(const TileKey& key, const std::uint8_t* pixels,
                    std::size_t size)
{
    return runtime_ && runtime_->put(key, pixels, size);
}

TilePackStore::Stats TileCache::runtime_stats() const
{
    return runtime_ ? runtime_->stats() : TilePackStore::Stats{};
}

} // namespace navicore_osmand
