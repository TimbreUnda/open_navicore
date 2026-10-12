/*
 * cpu_bench: what the Cortex-A15 cores on the radio do per operation.
 *
 * Built twice by the Makefile: cpu_bench (vfpv3-d16, the backend's current
 * flags) and cpu_bench_neon (-mfpu=neon), to show what NEON would buy for
 * pixel work. Each test prints ms per iteration and bandwidth or rate.
 *
 *   cpu_bench [--tile raw.rgba]   optional 256x256 RGBA tile for zlib tests
 */
#define _POSIX_C_SOURCE 200112L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static volatile uint32_t g_sink;

static void report(const char *name, double ms, double bytes)
{
    if (bytes > 0)
        printf("%-38s %9.3f ms  %8.1f MB/s\n", name, ms, bytes / ms / 1e3);
    else
        printf("%-38s %9.3f ms\n", name, ms);
}

static void bench_memcpy(size_t size, int iterations)
{
    uint8_t *a = malloc(size), *b = malloc(size);
    double t0;
    int i;
    char name[64];
    memset(a, 1, size);
    memset(b, 2, size);
    t0 = now_ms();
    for (i = 0; i < iterations; ++i) { memcpy(b, a, size); g_sink += b[i % size]; }
    snprintf(name, sizeof(name), "memcpy %zu KiB", size >> 10);
    report(name, (now_ms() - t0) / iterations, (double)size);
    t0 = now_ms();
    for (i = 0; i < iterations; ++i) { memset(b, i, size); g_sink += b[0]; }
    snprintf(name, sizeof(name), "memset %zu KiB", size >> 10);
    report(name, (now_ms() - t0) / iterations, (double)size);
    free(a);
    free(b);
}

/* RGBA8888 -> RGB565, the per-tile conversion the 16-bit path needs. */
static void rgba_to_565(const uint32_t *in, uint16_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        const uint32_t p = in[i];
        out[i] = (uint16_t)(((p & 0xf8u) << 8) | ((p & 0xfc00u) >> 5) |
                            ((p & 0xf80000u) >> 19));
    }
}

/* Straight-alpha blend of a canvas row onto another (software UI cost). */
static void blend_rows(const uint32_t *src, uint32_t *dst, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        const uint32_t s = src[i], d = dst[i], a = s >> 24, ia = 255u - a;
        const uint32_t rb = (((s & 0xff00ffu) * a + (d & 0xff00ffu) * ia) >> 8) & 0xff00ffu;
        const uint32_t g = (((s & 0xff00u) * a + (d & 0xff00u) * ia) >> 8) & 0xff00u;
        dst[i] = 0xff000000u | rb | g;
    }
}

static void bench_pixels(void)
{
    const size_t n = 256u * 256u;
    uint32_t *rgba = malloc(n * 4), *dst = malloc(n * 4);
    uint16_t *rgb565 = malloc(n * 2);
    double t0;
    int i;
    for (i = 0; i < (int)n; ++i) { rgba[i] = 0x80000000u | (uint32_t)i * 2654435761u; dst[i] = 0xff202020u; }
    t0 = now_ms();
    for (i = 0; i < 200; ++i) { rgba_to_565(rgba, rgb565, n); g_sink += rgb565[i]; }
    report("RGBA->RGB565 256x256 tile", (now_ms() - t0) / 200, n * 4.0);
    t0 = now_ms();
    for (i = 0; i < 200; ++i) { blend_rows(rgba, dst, n); g_sink += dst[i]; }
    report("alpha blend 256x256 (software)", (now_ms() - t0) / 200, n * 4.0);
    free(rgba); free(dst); free(rgb565);
}

static void bench_zlib(const uint8_t *tile, size_t size)
{
    uLongf bound = compressBound((uLong)size), clen;
    uint8_t *packed = malloc(bound), *out = malloc(size);
    int level, i;
    for (level = 1; level <= 9; level += 4) {
        double t0 = now_ms();
        char name[64];
        for (i = 0; i < 20; ++i) {
            clen = bound;
            compress2(packed, &clen, tile, (uLong)size, level);
        }
        snprintf(name, sizeof(name), "zlib compress level %d (%lu KiB out)",
                 level, (unsigned long)clen >> 10);
        report(name, (now_ms() - t0) / 20, (double)size);
        t0 = now_ms();
        for (i = 0; i < 20; ++i) {
            uLongf out_len = (uLongf)size;
            uncompress(out, &out_len, packed, clen);
        }
        snprintf(name, sizeof(name), "zlib uncompress (level %d stream)", level);
        report(name, (now_ms() - t0) / 20, (double)size);
    }
    free(packed); free(out);
}

static void bench_alloc(void)
{
    static const size_t sizes[] = {64, 4096, 65536, 262144};
    size_t s;
    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        void *blocks[256];
        double t0 = now_ms();
        int round, i;
        char name[64];
        for (round = 0; round < 40; ++round) {
            for (i = 0; i < 256; ++i) { blocks[i] = malloc(sizes[s]); ((char *)blocks[i])[0] = 1; }
            for (i = 0; i < 256; ++i) free(blocks[i]);
        }
        snprintf(name, sizeof(name), "malloc+free %zu B (per pair, us)", sizes[s]);
        printf("%-38s %9.3f us\n", name, (now_ms() - t0) * 1e3 / (40.0 * 256));
    }
}

static void bench_float(void)
{
    volatile float x = 1.0001f;
    float a = 0.0f, b = 1.0f;
    double t0 = now_ms();
    int i;
    for (i = 0; i < 20000000; ++i) { a = a * x + b; b = b * x - a * 0.5f; }
    g_sink += (uint32_t)a;
    printf("%-38s %9.1f MFLOPS\n", "float mul-add chain",
           4 * 20000000 / ((now_ms() - t0) * 1e3));
}

int main(int argc, char **argv)
{
    const size_t tile_size = 256u * 256u * 4u;
    uint8_t *tile = malloc(tile_size);
    int i;
#if defined(__ARM_NEON__)
    puts("build: NEON enabled");
#else
    puts("build: VFP only (no NEON)");
#endif
    /* Synthetic map-like tile: large flat areas with sparse lines. */
    for (i = 0; i < (int)(tile_size / 4); ++i) {
        const int x = i % 256, y = i / 256;
        const uint32_t p = (x % 37 < 3 || y % 53 < 4) ? 0xff606060u :
                           (y < 128 ? 0xffe8e4dcu : 0xffc8e0c0u);
        memcpy(tile + i * 4, &p, 4);
    }
    if (argc > 2 && !strcmp(argv[1], "--tile")) {
        FILE *f = fopen(argv[2], "rb");
        if (f && fread(tile, 1, tile_size, f) == tile_size)
            puts("using provided tile");
        if (f) fclose(f);
    }
    puts("-- memory --");
    bench_memcpy(4u << 10, 20000);
    bench_memcpy(64u << 10, 2000);
    bench_memcpy(1u << 20, 200);
    bench_memcpy(8u << 20, 20);
    puts("-- pixels --");
    bench_pixels();
    puts("-- tile cache zlib (256x256 RGBA) --");
    bench_zlib(tile, tile_size);
    puts("-- allocation --");
    bench_alloc();
    puts("-- math --");
    bench_float();
    free(tile);
    return 0;
}
