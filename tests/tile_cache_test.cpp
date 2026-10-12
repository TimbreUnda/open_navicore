#include "tile_cache.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace navicore_osmand;

namespace {
std::vector<std::uint8_t> tile(std::uint32_t seed, std::size_t size)
{
    std::vector<std::uint8_t> pixels(size);
    for (std::size_t i = 0; i < size; ++i)
        pixels[i] = static_cast<std::uint8_t>((i / 64 + seed * 7) & 0xff);
    return pixels;
}

std::string temporary_root()
{
    char pattern[] = "/tmp/open-navicore-tilecache-XXXXXX";
    const char* root = ::mkdtemp(pattern);
    assert(root);
    return root;
}

void remove_tree(const std::string& root)
{
    const std::string command = "rm -rf '" + root + "'";
    if (std::system(command.c_str()) != 0)
        std::fprintf(stderr, "cleanup failed: %s\n", root.c_str());
}
} // namespace

int main()
{
    const std::string root = temporary_root();
    const std::size_t size = tile_bytes(TileFormat::Rgb565, 64);
    assert(size == 64u * 64u * 2u);
    assert(tile_bytes(TileFormat::Rgba8888, 256) == 256u * 256u * 4u);
    {
        TilePackStore::Options options;
        options.root = root + "/rw";
        options.writable = true;
        options.tile_size = 64;
        options.open_packs = 2;
        TilePackStore store(options);
        std::vector<std::uint8_t> out;
        assert(!store.get({15, 100, 200}, out));
        // Same pack (16x16 block), different packs, reopen after LRU closes.
        const auto a = tile(1, size);
        const auto b = tile(2, size);
        const auto c = tile(3, size);
        assert(store.put({15, 100, 200}, a.data(), a.size()));
        assert(store.put({15, 101, 200}, b.data(), b.size()));
        assert(store.put({15, 400, 200}, c.data(), c.size()));
        assert(store.put({16, 0, 0}, c.data(), c.size()));
        assert(store.get({15, 100, 200}, out) && out == a);
        assert(store.get({15, 101, 200}, out) && out == b);
        assert(store.get({15, 400, 200}, out) && out == c);
        assert(store.contains({16, 0, 0}) && !store.contains({16, 1, 0}));
        assert(!store.put({15, 1, 1}, a.data(), a.size() - 1)); // wrong size
        const auto stats = store.stats();
        assert(stats.writes == 4 && stats.hits == 3 && stats.corrupt == 0);
        assert(stats.bytes > 0);
    }
    {
        // Data survives a new store instance; a read-only store reads it but
        // never writes, and a format mismatch is rejected as corrupt.
        TilePackStore::Options options;
        options.root = root + "/rw";
        options.tile_size = 64;
        TilePackStore reader(options);
        std::vector<std::uint8_t> out;
        assert(reader.get({15, 100, 200}, out) && out == tile(1, size));
        assert(!reader.put({15, 2, 2}, out.data(), out.size()));
        options.format = TileFormat::Rgba8888;
        TilePackStore wrong(options);
        assert(!wrong.get({15, 100, 200}, out));
        assert(wrong.stats().corrupt == 1);
    }
    {
        // A flipped payload byte fails the CRC and reads as a miss.
        const std::string pack = root + "/rw/z15/6_12.pak";
        const int fd = ::open(pack.c_str(), O_RDWR);
        assert(fd >= 0);
        std::uint8_t byte = 0;
        assert(::pread(fd, &byte, 1, 32 + 4096 + 3) == 1);
        byte ^= 0xff;
        assert(::pwrite(fd, &byte, 1, 32 + 4096 + 3) == 1);
        ::close(fd);
        TilePackStore::Options options;
        options.root = root + "/rw";
        options.tile_size = 64;
        TilePackStore reader(options);
        std::vector<std::uint8_t> out;
        assert(!reader.get({15, 100, 200}, out));
        assert(reader.stats().corrupt == 1);
        // The other tile in the same pack is unaffected.
        assert(reader.get({15, 101, 200}, out) && out == tile(2, size));
    }
    {
        // Budget: least-recently-used packs are deleted, newest kept.
        TilePackStore::Options options;
        options.root = root + "/budget";
        options.writable = true;
        options.tile_size = 64;
        options.budget_bytes = 3 * (32 + 4096) + 3 * 2000;
        TilePackStore store(options);
        for (std::uint32_t block = 0; block < 6; ++block) {
            const auto pixels = tile(block, size);
            assert(store.put({14, block * 16, 0}, pixels.data(), pixels.size()));
        }
        const auto stats = store.stats();
        assert(stats.evicted_packs > 0);
        assert(stats.bytes <= options.budget_bytes);
        std::vector<std::uint8_t> out;
        assert(store.get({14, 5 * 16, 0}, out) && out == tile(5, size));
        assert(!store.get({14, 0, 0}, out));
        // A new instance scans what survived and keeps enforcing the budget.
        TilePackStore again(options);
        const auto pixels = tile(9, size);
        assert(again.put({14, 9 * 16, 0}, pixels.data(), pixels.size()));
        assert(again.stats().bytes <= options.budget_bytes);
    }
    {
        // Free-space reserve: writes stop below it and resume above it,
        // checked on the first write and every 64 writes.
        std::int64_t available = 10ll << 20;
        TilePackStore::Options options;
        options.root = root + "/space";
        options.writable = true;
        options.tile_size = 64;
        options.reserve_bytes = 64ull << 20;
        options.available_bytes = [&available](const std::string&) {
            return available;
        };
        TilePackStore store(options);
        const auto pixels = tile(7, size);
        assert(!store.put({12, 0, 0}, pixels.data(), pixels.size()));
        assert(store.stats().refused_low_space == 1);
        available = 1ll << 30;
        assert(store.put({12, 0, 0}, pixels.data(), pixels.size()));
        assert(store.stats().writes == 1);
        // Unknown free space (-1) does not block writes.
        available = -1;
        TilePackStore unknown(options);
        assert(unknown.put({12, 1, 0}, pixels.data(), pixels.size()));
    }
    {
        // Layered cache: runtime first, shipped base second, namespaced.
        const std::string ns = TileCache::namespace_hash("kyiv.obf|car|day|565");
        assert(ns.size() == 16);
        assert(ns != TileCache::namespace_hash("kyiv.obf|car|night|565"));
        TileCache seed("", root + "/base", ns, TileFormat::Rgb565, 64, 0);
        const auto shipped = tile(42, size);
        assert(seed.put({13, 1, 1}, shipped.data(), shipped.size()));
        TileCache cache(root + "/base", root + "/runtime", ns,
                        TileFormat::Rgb565, 64, 0);
        std::vector<std::uint8_t> out;
        assert(cache.get({13, 1, 1}, out) && out == shipped);
        const auto fresh = tile(43, size);
        assert(cache.put({13, 2, 1}, fresh.data(), fresh.size()));
        assert(cache.get({13, 2, 1}, out) && out == fresh);
        TileCache other(root + "/base", root + "/runtime",
                        TileCache::namespace_hash("other"),
                        TileFormat::Rgb565, 64, 0);
        assert(!other.get({13, 1, 1}, out));
    }
    {
        // Web-Mercator ranges: Kyiv centre at z13 is tile (4790, 2762).
        const auto point = tile_range(13, 50.4501, 30.5234, 50.4501, 30.5234);
        assert(point.min_x == 4790 && point.max_x == 4790);
        assert(point.min_y == 2762 && point.max_y == 2762);
        assert(point.count() == 1);
        const auto box = tile_range(14, 50.35, 30.35, 50.55, 30.75);
        assert(box.min_x < box.max_x && box.min_y < box.max_y);
        assert(tile_range(0, -80, -170, 80, 170).count() == 1);
        std::size_t errors = 0;
        const auto ranges = parse_precache_manifest(
            "# Kyiv\n12 14 50.35 30.35 50.55 30.75 # city\n"
            "\n15 14 0 0 1 1\nbad\n16 16 50.4 30.5 50.41 30.51\n", &errors);
        assert(errors == 2);
        assert(ranges.size() == 4);
        assert(ranges[0].zoom == 12 && ranges[2].zoom == 14 && ranges[3].zoom == 16);
        assert(ranges[2].min_x == box.min_x && ranges[2].max_y == box.max_y);
    }
    remove_tree(root);
    return 0;
}
