# Screen presentation bench tools

Standalone QNX 6.6 tools for the ~27 ms `eglSwapBuffers` cost seen on the VP4R
radio (see `docs/performance/rendering-architecture.md`). They do not touch
`nv_navigation`, so testing never spends System Manager relaunches.

| Tool | Purpose |
|---|---|
| `screen_bench` | One test window with explicit class, pipeline, overlay usage, format, transparency, size, position, z-order, buffer count and swap interval. Reports what Screen applied and 2 ms histograms of swap/post time and frame interval. `--mode gl` mimics `NAV_main`; `--mode sw` posts CPU-filled native buffers with no GL, measuring Screen alone; `--dirty small` tests whether cost follows the damaged area; `--finish` splits GPU time from compositor time. |
| `screen_survey` | Read-only display and window report (application or `--display-manager` context; never a window-manager context, which the HMI owns). |
| `survey_debugfs.sh` | Read-only dump of `/dev/screen/*/win-*` and `dpy-*` text nodes: every window Screen composites, from any process. |
| `run_sweep.sh` | Bounded sweep answering, in order: GPU vs compositor vs vsync; composition without GL; area and damage scaling; transparency/format/buffers; z-order above the HMI; overlay usage on pipelines 1-3. |

Build on the host and copy to the radio's RAM-backed `/tmp`:

```sh
make -C tools/qnx/screen_bench QNX_SDK=/mnt/d/qnx660-sdk \
    QNX_HOST_LIBS=/mnt/d/tools/qnx-host-libs
scp -p dist/screen_bench_tools/* tools/qnx/screen_bench/*.sh root@192.168.2.8:/tmp/
```

On the radio:

```sh
sh /tmp/survey_debugfs.sh > /tmp/screen_survey.txt
/tmp/screen_survey; /tmp/screen_survey --display-manager
sh /tmp/run_sweep.sh 8 /tmp/screen_bench_sweep.txt   # ~2.5 minutes
```

Defaults place a 1024x574 window at (0,72), z-order 999: under the HMI like
`NAV_main`. Runs are capped at 120 s and stop on SIGINT/SIGTERM. A test window
on pipelines 1-3 or above the HMI (`--zorder 2000`) is briefly visible.

## Capability benchmarks

| Tool | Purpose |
|---|---|
| `gpu_bench` | Off-screen GLES2 micro-benchmarks on a 1024x574 FBO: clear and fill, blending, overdraw, shader ALU (mediump/highp), texture formats and filtering, texture upload, draw-call count, vertex streaming, readback. Uses the cycle counter and reports the fixed GPU round trip separately from each test's marginal cost. |
| `cpu_bench`, `cpu_bench_neon` | memcpy/memset by working-set size, RGBA->RGB565 conversion, software alpha blend, zlib levels 1/5/9, malloc/free by size, float throughput. Same code built with the backend's VFP flags and with NEON. |

EGL needs the graphics environment `nv_navigation` uses: the tools set
`GRAPHICS_ROOT` themselves, and `LD_LIBRARY_PATH` must include
`/proc/boot/usr/lib/graphics/jacinto6:/usr/lib/graphics/jacinto6`.

## First results (radio, 2026-10-12, our navigation build running)

Screen presentation (`screen_bench`, 1024x574 under the HMI):
- bare GL window (glClear + swap, no map): **28.7 fps, swap 34.7 ms, glFinish
  0.11 ms**, so the presentation cost reproduces without OsmAnd at all;
- CPU-posted native buffers never block (0.03-0.06 ms per post, even with a
  flush-and-wait): Screen drops superseded native posts instead of composing
  each one; only the EGL path is throttled;
- Screen exposes one display, id 4 "OTHER4" 1024x768 (graphics.conf
  "display 2" is the WFD port number).

GPU (`gpu_bench`, PowerVR **SGX 544MP**, OpenGL ES 2.0 build 1.9@2291151):
- a forced CPU<->GPU sync round trip costs **~13 ms** with nothing to draw:
  never call glFinish/glReadPixels in the frame path;
- opaque full-screen passes are ~free (tile-based hidden-surface removal);
  **blending costs ~0.29 ms per full-screen pass** (~2 Gpix/s);
- shader ALU is cheap at this size (32 mad: 0.02 ms mediump, 0.035 ms highp);
- **draw calls cost ~0.1 ms each** (1000 draws: 98 ms vs 0.13 ms batched):
  batching is the most important GPU rule here;
- per-frame vertex re-upload (1000 quads): ~1 ms via glBufferData/SubData,
  0.18 ms with client-side arrays;
- texture upload: 256x256 RGBA ~0.09 ms, RGB565 ~0.03-0.05 ms; full 1024x574
  RGBA ~1.75 ms; full-frame glReadPixels 15 ms.

CPU (`cpu_bench` vs `cpu_bench_neon`, Cortex-A15):
- memcpy 7.4 GB/s in L1, 3.7 GB/s at 1 MiB, **1.5 GB/s at 8 MiB** (DRAM);
- **NEON doubles pixel loops**: RGBA->RGB565 0.295 -> 0.130 ms per 256x256
  tile, software blend 0.615 -> 0.320 ms; memcpy and zlib unchanged;
- zlib on a synthetic tile: level 1 compress 3.2 ms, level 9 8.4 ms,
  decompress ~0.7-0.8 ms;
- **malloc+free of 64-256 KiB blocks costs 116-280 us** (64 B: 0.5 us): pool
  tile-sized buffers instead of allocating per tile;
- float mul-add chain ~240 MFLOPS.

Caveats: taken with navigation rendering concurrently; marginal GPU costs
rely on tile-based rendering (repeated opaque passes are culled), so compare
tests with each other rather than reading them as peak rates.
