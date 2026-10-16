#!/usr/bin/env python3
"""1% lows from /tmp/open-navicore-frames.log (one line per frame).

  frame_stats.py FRAMES.log                      whole run
  frame_stats.py FRAMES.log MARKS.txt            per window of a zoom sweep
  frame_stats.py FRAMES.log MARKS.txt A B        stage columns of slow frames in (A, B]

MARKS.txt is written by zoom_sweep.sh: "name t_ms" per line (the frame-log time
at each tap). A window k is (mark k-1, mark k]. Intervals of one second or more
count as pauses, not frames. "1% low" is 1e6 / p99 of the presented-to-presented
interval; "worst 1% avg" is the mean rate of the slowest 1% of frames.
"""
import sys


def load(path):
    header, rows = None, []
    for line in open(path, errors='replace'):
        if line.startswith('#'):
            header = line[1:].split()
            continue
        parts = line.split()
        if header and len(parts) >= 3:
            row = {}
            for name, value in zip(header, parts):
                try:
                    row[name] = int(value)
                except ValueError:
                    pass
            rows.append(row)
    return header, rows


def stats(rows, t0=0, t1=1 << 60):
    intervals = sorted(r['interval_us'] for r in rows
                       if t0 < r.get('t_ms', 0) <= t1 and 0 < r.get('interval_us', 0) < 1_000_000)
    n = len(intervals)
    if n < 20:
        return None
    p99 = intervals[min(n - 1, int(0.99 * n))]
    worst = intervals[-max(1, n // 100):]
    return {
        'n': n, 'p50_ms': intervals[n // 2] / 1e3, 'p99_ms': p99 / 1e3, 'low1': 1e6 / p99,
        'worst1': 1e6 / (sum(worst) / len(worst)),
        'over40': 100.0 * sum(1 for x in intervals if x > 40000) / n, 'max_ms': intervals[-1] / 1e3,
    }


def fmt(s):
    if not s:
        return 'few frames'
    return ('n=%4d p50=%4.1f p99=%5.1f ms  1%%low=%5.1f  worst1%%=%5.1f fps  >40ms=%4.1f%%  max=%5.1f ms'
            % (s['n'], s['p50_ms'], s['p99_ms'], s['low1'], s['worst1'], s['over40'], s['max_ms']))


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    header, rows = load(argv[0])
    if len(argv) == 1:
        print(fmt(stats(rows)))
        return 0
    marks = {}
    order = []
    for line in open(argv[1]):
        name, t = line.split()
        marks[name] = int(t)
        order.append(name)
    if len(argv) == 4:
        a, b = argv[2], argv[3]
        window = [r for r in rows if marks[a] < r.get('t_ms', 0) <= marks[b]]
        slow = [r for r in window if r.get('interval_us', 0) > 40000]
        fast = [r for r in window if r.get('interval_us', 0) <= 40000]
        print('frames', len(window), 'slow', len(slow))
        print('%-26s %10s %10s' % ('column', 'normal_avg', 'slow_avg'))
        for col in header:
            if col in ('outcome', 't_ms'):
                continue
            fa = sum(r.get(col, 0) for r in fast) / max(1, len(fast))
            sa = sum(r.get(col, 0) for r in slow) / max(1, len(slow))
            if max(fa, sa) > 300 or col.endswith(('uploads', 'reused')):
                print('%-26s %10.1f %10.1f' % (col, fa, sa))
        return 0
    for prev, name in zip(order, order[1:]):
        print('%-12s %s' % (name, fmt(stats(rows, marks[prev], marks[name]))))
    print('%-12s %s' % ('whole', fmt(stats(rows, marks[order[0]], marks[order[-1]]))))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
