# Frame log and zoom-sweep measurements

With the marker `/datastore/navicore-backend/enable-performance-metrics` the
backend writes one line per frame to `/tmp/open-navicore-frames.log` (columns in
its header: interval, total, update, prepare, map, UI, vector stages, label
placement counters, ...) and a one-second summary to
`/tmp/open-navicore-osmand.log`.

## Tools (`scripts/perf/`)

| Tool | Use |
| --- | --- |
| `zoom_sweep.sh OUT` | taps zoom out 9x then in 9x on the bench radio (5 s per level), records the frame-log time at each tap, pulls the logs. Sends touches: bench radio only, announce it. |
| `frame_stats.py FRAMES [MARKS [A B]]` | 1% low per window (`1e6 / p99` of the presented interval), share of frames over 40 ms, worst frame; with `A B` the stage columns of slow frames. |
| `bmp2png.py in.bmp out.png` | converts `vp4r-screenshot -c dm` captures (red and blue are swapped in the capture). |

## Reading the numbers

- The display is 60 Hz and the app presents at 30 fps, so intervals are about
  33 ms; a single missed slot is 50 ms (20 fps). A 1% low of 20 fps therefore
  means "about 1 frame in 100 slipped one slot", not a run of slow frames.
- Slow frames with small render time (`total_us` a few ms) are waiting for
  presentation (Screen composition), not for our CPU work.
- Compare like with like: same start (a recent destination in Kyiv), tile
  cache state, and nothing else running on the radio (ssh sessions, `du`).

## Results (bench radio, Kyiv, zoom 13.6 -> 4.6 -> 13.6)

| Date | Image | Whole sweep 1% low | Zoom 11-13.6 1% low | Worst frame |
| --- | --- | --- | --- | --- |
| 2026-10-16 | main 53f2047 | 23.8 fps | 20.4 fps | 90 ms |
| 2026-10-16 | `integration` image 007adb97 (label reuse, low-zoom style; its content is main f2bd080) | 22.7 fps | 20.4 fps | 62 ms |

Label placement cost at zoom 11-13 fell from about 20 ms to about 1 ms per frame
(87% of frames reuse the previous placement); the remaining slow frames are
presentation slips.
