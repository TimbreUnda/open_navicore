#!/usr/bin/env python3
"""Frame-rate report for a bench run of the OsmAnd backend.

  perf_report.py BACKEND_TRACE [FRAMES_LOG] [--from TEXT] [--to TEXT]

BACKEND_TRACE is /tmp/open-navicore-osmand.log and FRAMES_LOG
/tmp/open-navicore-frames.log, both written with the
`enable-performance-metrics` marker. --from/--to select the part of the run
between two trace lines (default: the simulated drive, `simulation=route_started`
to `simulation=arrived`); the frame log is cut to the same wall-clock span.

From the frame log (one line per frame) it reports the exact average fps,
1% and 0.1% lows (fps over the slowest 1% / 0.1% of present intervals),
hitches (intervals over 50 and 100 ms) and, for each hitch, whether the
frame's own work (update, prepare, map, UI) or the time outside it (swap,
pacing, scheduling) took the time. Without a frame log it falls back to the
per-second `perf` lines, whose interval bins are coarse.
"""
import re
import statistics
import sys

STAGES = ('update_us', 'prepare_us', 'map_us', 'ui_render_us', 'ui_composite_us')


def kv(line):
    return {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}


def trace_span(path, start, end):
    """perf rows between the markers, and the span in the trace's t_ms clock
    (approximated by counting perf windows from the first perf line)."""
    rows, active, done = [], start is None, False
    for line in open(path, errors='replace'):
        if start and start in line:
            active = True
            rows = []
        elif end and end in line and active:
            done = True
            break
        elif active and line.startswith('perf '):
            rows.append(kv(line))
    return rows, done


def lows(intervals_ms, fraction):
    worst = sorted(intervals_ms, reverse=True)
    n = max(1, int(len(worst) * fraction))
    return 1000.0 / statistics.mean(worst[:n]), worst[n - 1]


def frames_report(path, seconds):
    frames = []
    for line in open(path, errors='replace'):
        if line.startswith('#'):
            continue
        parts = line.split()
        if len(parts) != 9:
            continue
        frames.append((parts[0], *map(int, parts[1:])))
    presented = [f for f in frames if f[0] == 'P' and f[2] > 0]
    if seconds:
        # The drive is the last `seconds` of presented frames before the
        # backend went idle (intervals over 500 ms mark idle on-demand redraws).
        busy = [f for f in presented if f[2] < 500000]
        end_t = busy[-1][1] if busy else 0
        presented = [f for f in busy if f[1] >= end_t - seconds * 1000]
    if not presented:
        sys.exit('no presented frames in the frame log')
    iv = [f[2] / 1000.0 for f in presented]
    span = (presented[-1][1] - presented[0][1]) / 1000.0
    print(f'frames={len(iv)} span={span:.0f}s fps={len(iv) / span:.2f}')
    for frac, name in ((0.01, '1% low'), (0.001, '0.1% low')):
        fps, edge = lows(iv, frac)
        print(f'{name}: {fps:.1f} fps (slowest {name.split()[0]} of frames take '
              f'{edge:.1f} ms or more)')
    for q in (50, 90, 99, 99.9):
        print(f'interval p{q}: {sorted(iv)[min(len(iv) - 1, int(len(iv) * q / 100))]:.1f} ms')
    for limit in (40, 50, 100):
        n = sum(1 for x in iv if x > limit)
        print(f'intervals > {limit} ms: {n} ({n * 60 / span:.1f}/min)')
    hitches = [f for f in presented if f[2] > 50000]
    work = outside = 0
    causes = {}
    for f in hitches:
        stage = dict(zip(STAGES, f[4:9]))
        frame_work = f[3]
        if frame_work * 2 >= f[2] - 33333:
            work += 1
            top = max(stage, key=stage.get)
            causes[top] = causes.get(top, 0) + 1
        else:
            outside += 1
    print(f'hitches >50 ms: {len(hitches)}; frame work dominates {work} '
          f'({", ".join(f"{k} {v}" for k, v in sorted(causes.items(), key=lambda x: -x[1]))}), '
          f'outside the frame (swap/pacing/scheduling) {outside}')
    print('worst 10: interval_ms total_ms ' + ' '.join(s[:-3] for s in STAGES))
    for f in sorted(presented, key=lambda f: -f[2])[:10]:
        print('  ' + ' '.join(f'{v / 1000:.1f}' for v in (f[2], f[3], *f[4:9])))
    for name, column in (('total', 3), *((s, 4 + i) for i, s in enumerate(STAGES))):
        v = sorted(f[column] / 1000 for f in presented)
        print(f'{name}: p50={v[len(v) // 2]:.1f} p99={v[int(len(v) * 0.99)]:.1f} '
              f'max={v[-1]:.1f} ms')


def perf_report(rows):
    names = ['le33', 'le50', 'le67', 'le100', 'le200', 'gt200']
    ms = sum(r['window_ms'] for r in rows)
    frames = sum(r['presented'] for r in rows)
    bins = [sum(r['interval_' + n] for r in rows) for n in names]
    print(f'perf lines: windows={len(rows)} time={ms / 1000:.0f}s fps={frames * 1000 / ms:.2f}')
    print('interval bins: ' + ' '.join(f'{n}={b}' for n, b in zip(names, bins)))
    print(f'>50 ms: {sum(bins[2:])}, >100 ms: {sum(bins[4:])}, '
          f'worst {max(r["interval_max_us"] for r in rows) / 1000:.0f} ms')


def main():
    args = sys.argv[1:]
    opt = lambda name, default: args[args.index(name) + 1] if name in args else default
    start = opt('--from', 'simulation=route_started')
    end = opt('--to', 'simulation=arrived')
    positional = [a for i, a in enumerate(args)
                  if not a.startswith('--') and (i == 0 or not args[i - 1].startswith('--'))]
    rows, complete = trace_span(positional[0], start, end)
    if not rows:
        sys.exit(f'no perf lines between {start!r} and {end!r}')
    if not complete:
        print(f'note: {end!r} not reached; run still in progress')
    perf_report(rows)
    if len(positional) > 1:
        frames_report(positional[1], sum(r['window_ms'] for r in rows) / 1000.0)


if __name__ == '__main__':
    main()
