#!/usr/bin/env python3
"""Frame-rate report for a bench run of the OsmAnd backend.

  perf_report.py RUN_DIR [--from TEXT] [--to TEXT]

RUN_DIR holds the unit's /tmp/open-navicore-osmand.log (backend trace),
/tmp/open-navicore-frames.log (one line per frame) and
/tmp/open-navicore-present.log (provider presents), written with the
`enable-performance-metrics` marker. The report covers the simulated drive
(`simulation=route_started` .. `simulation=arrived` unless --from/--to).

Two views:
- presents (provider): true swap-to-swap intervals in 2 ms bins, so the
  average fps, the 1% / 0.1% lows and the hitch counts are what the display
  received;
- frames (backend): what each frame spent its time on. A present is late when
  a frame's work does not fit its 33 ms slot, so the slow frames are listed
  with the stage that dominated them.
"""
import collections
import os
import re
import sys


def kv(line):
    return {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}


def drive_window(trace, start, end):
    """Backend-clock milliseconds of the start/end markers, from the frame
    log timestamps nearest to them (the trace has no clock)."""
    lines = open(trace, errors='replace').read().splitlines()
    try:
        a = next(i for i, l in enumerate(lines) if start in l)
    except StopIteration:
        sys.exit(f'{start!r} not in the trace')
    b = next((i for i, l in enumerate(lines) if end in l and i > a), None)
    perf = [i for i, l in enumerate(lines) if l.startswith('perf ')]
    return (sum(1 for i in perf if i < a), sum(1 for i in perf if b is None or i < b),
            b is not None)


def present_report(path, seconds_hint):
    hist = collections.Counter()
    frames = window_ms = 0
    for line in open(path, errors='replace'):
        if not line.startswith('presenthist'):
            continue
        w = kv(line)
        # Drive windows only: continuous rendering (>= 25 fps).
        if w['frames'] < w['window_ms'] / 1000 * 25:
            continue
        frames += w['frames']
        window_ms += w['window_ms']
        for k, v in re.findall(r'(\d+):(\d+)', line.split('interval=')[1]):
            hist[int(k)] += int(v)
    n = sum(hist.values())
    if not n:
        print('presents: no drive windows')
        return
    print(f'presents: {window_ms / 1000:.0f} s, {frames} frames, '
          f'{frames * 1000 / window_ms:.2f} fps')
    for frac, name in ((0.01, '1% low'), (0.001, '0.1% low')):
        acc, need = 0, n * frac
        for k in sorted(hist, reverse=True):
            acc += hist[k]
            if acc >= need:
                worst = [kk + 1 for kk in sorted(hist, reverse=True) for _ in range(hist[kk])][:max(1, int(need))]
                print(f'  {name}: {1000 * len(worst) / sum(worst):.1f} fps '
                      f'(slowest {name.split()[0]} of presents >= {k} ms)')
                break
    for limit in (40, 50, 62):
        c = sum(v for k, v in hist.items() if k >= limit)
        print(f'  presents >= {limit} ms: {c} ({c * 60000 / window_ms:.1f}/min)')


def frame_report(path):
    names, frames = None, []
    for line in open(path, errors='replace'):
        if line.startswith('#'):
            names = line[1:].split()
            continue
        parts = line.split()
        if names and len(parts) == len(names) and parts[0] == 'P':
            frames.append(dict(zip(names[1:], map(int, parts[1:]))))
    # The drive: continuous frames (the idle tail redraws at ~1 fps).
    busy = [f for f in frames if 0 < f['interval_us'] < 500000]
    if not busy:
        print('frames: none')
        return
    stages = [n for n in names[4:] if (n.endswith('_us') or n == 'vec_uploads') and n != 'cpu_us']
    parts = ('tracker_us', 'route_sync_us', 'guidance_ui_us') + tuple(n for n in names if n.startswith('vec_'))
    print(f'frames: {len(busy)}; work p50 '
          f'{sorted(f["total_us"] for f in busy)[len(busy) // 2] / 1000:.1f} ms')
    for limit in (25, 33, 50):
        print(f'  work >= {limit} ms: {sum(1 for f in busy if f["total_us"] >= limit * 1000)}')
    slow = [f for f in busy if f['total_us'] >= 25000]
    top = collections.Counter()
    for f in slow:
        main = [s for s in stages if s not in parts]
        measured = sum(f[s] for s in main)
        f['unmeasured_us'] = max(0, f['total_us'] - measured)
        top[max(main + ['unmeasured_us'], key=lambda s: f.get(s, 0))] += 1
    print('  dominant stage in work >= 25 ms: ' +
          ', '.join(f'{k[:-3]} {v}' for k, v in top.most_common()))
    if 'cpu_us' in names:
        starved = [f for f in slow if f['cpu_us'] * 2 < f['total_us']]
        print(f'  slow frames with CPU time < half their wall time (preempted or '
              f'blocked): {len(starved)} of {len(slow)}; median CPU/wall '
              f'{sorted(f["cpu_us"] / f["total_us"] for f in slow)[len(slow) // 2]:.2f}'
              if slow else '  no slow frames')
    cols = ['total_us', 'cpu_us'] + stages + ['unmeasured_us']
    print('  slowest 12 (ms): ' + ' '.join(c[:-3] for c in cols))
    for f in sorted(slow, key=lambda f: -f['total_us'])[:12]:
        print('    ' + ' '.join(f'{f.get(c, 0) / 1000:.1f}' for c in cols))
    print('  per stage p50 / p99 / max (ms):')
    for s in stages:
        v = sorted(f[s] for f in busy)
        print(f'    {s[:-3]:<14} {v[len(v) // 2] / 1000:5.1f} {v[int(len(v) * 0.99)] / 1000:6.1f} '
              f'{v[-1] / 1000:6.1f}')


def main():
    args = sys.argv[1:]
    run = args[0]
    trace = os.path.join(run, 'open-navicore-osmand.log')
    if os.path.exists(trace):
        start = args[args.index('--from') + 1] if '--from' in args else 'simulation=route_started'
        end = args[args.index('--to') + 1] if '--to' in args else 'simulation=arrived'
        _, _, done = drive_window(trace, start, end)
        if not done:
            print(f'note: {end!r} not reached')
    present = os.path.join(run, 'open-navicore-present.log')
    if os.path.exists(present):
        present_report(present, None)
    frames = os.path.join(run, 'open-navicore-frames.log')
    if os.path.exists(frames):
        frame_report(frames)


if __name__ == '__main__':
    main()
