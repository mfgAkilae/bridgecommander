"""Find knockbacks (big owner velocity steps) and the viewers' error after them.

Usage: python -I knock.py <run dir> [min step=8] [--summary]
For each owner sample whose velocity changes by more than <min step> in one sample
(a hit's impulse; respawns excluded): time, step size, hull drop, and per viewer the
peak error and the time the error stays above 5 units in the 15 s after.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('--')]
    summary = '--summary' in sys.argv
    run = args[0]
    thr = float(args[1]) if len(args) > 1 else 8.0
    peaks, aboves = [], []
    logs = {}
    for f in os.listdir(run):
        m = re.match(r'tlog_(host|client\d*)\.txt$', f)
        if m:
            logs[m.group(1)] = A.load(os.path.join(run, f))
    for name in sorted({n for lg in logs.values() for n in lg[0]}):
        owner = max(logs, key=lambda r: sum(1 for x in logs[r][0].get(name, []) if x[4]))
        ref = [x for x in logs[owner][0][name] if x[4]]
        dead = [d[0] for lg in logs.values() for d in lg[2] if d[1] == name]
        last = -1e9
        for a, b in zip(ref, ref[1:]):
            dv = A.dist(a[2], b[2])
            if dv < thr or b[0] - a[0] > 500 or b[0] - last < 15000 or A.dist(a[1], b[1]) > 50:
                continue
            last = b[0]
            if any(-1000 <= d - b[0] <= 15000 or 0 <= b[0] - d < 16000 for d in dead):
                continue  # the ship's own death or respawn
            out = []
            for viewer, lg in sorted(logs.items()):
                if viewer == owner or name not in lg[0]:
                    continue
                peak, above = 0.0, 0
                prev = None
                for t, pos, vel, hull, loc in lg[0][name]:
                    if not (b[0] <= t <= b[0] + 15000):
                        continue
                    r = A.interp(ref, t)
                    if r is None:
                        continue
                    e = A.dist(pos, r)
                    peak = max(peak, e)
                    if e > 5 and prev is not None:
                        above += t - prev
                    prev = t
                out.append('%s %.1f/%.1fs' % (viewer, peak, above / 1000.0))
                peaks.append(peak)
                aboves.append(above / 1000.0)
            if not summary:
                print('%-9s %s t=%d step %.1f hull %.0f -> %.0f | %s' % (
                name, owner, b[0], dv, a[3], b[3], '  '.join(out)))
    if summary and peaks:
        peaks.sort()
        n = len(peaks)
        print('%s: %d (event, viewer) pairs, peak error median %.1f, 90th %.1f, max %.1f; '
              'time above 5 units mean %.1f s' % (run, n, peaks[n // 2], peaks[int(n * 0.9)], peaks[-1],
                                                  sum(aboves) / n))


if __name__ == '__main__':
    main()
