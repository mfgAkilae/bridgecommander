"""Combat sync of a test run: hull values and phaser hits as each instance sees them.

Usage: python -I combat.py <run dir> [<run dir> ...]

- Hull: the host applies all damage. For every ship and every other instance, compare the hull
  value that instance shows with the host's value at the same moment (QPC ms). Reports the mean
  and 95th percentile difference and how long a difference above 5 % of the maximum hull lasts.
- Phaser hits (wtype=0): per target and 0.5 s bin, does a client show a phaser hit when the host
  does, and vice versa.
"""
import bisect
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A


def hull_series(ships, name):
    return [(x[0], x[3]) for x in ships.get(name, [])]


def at(series, t):
    ts = [x[0] for x in series]
    i = bisect.bisect_right(ts, t) - 1
    if i < 0 or t - series[i][0] > 300:
        return None
    return series[i][1]


def combat(run):
    logs = {}
    for f in os.listdir(run):
        m = re.match(r'tlog_(host|client\d*)\.txt$', f)
        if m:
            logs[m.group(1)] = A.load(os.path.join(run, f))
    roles = sorted(logs, key=A.role_key)
    if 'host' not in logs:
        return
    print('=== %s' % os.path.basename(run))
    host = logs['host']
    diffs, long_div = [], []
    for name in host[0]:
        hs = hull_series(host[0], name)
        if not hs:
            continue
        hmax = max(x[1] for x in hs) or 1.0
        for r in roles:
            if r == 'host' or name not in logs[r][0]:
                continue
            run_start = None
            for t, hull in hull_series(logs[r][0], name):
                ref = at(hs, t)
                if ref is None or hull < 0 or ref < 0:
                    continue
                d = abs(hull - ref) / hmax
                diffs.append(d)
                if d > 0.05:
                    if run_start is None:
                        run_start = t
                elif run_start is not None:
                    long_div.append(t - run_start)
                    run_start = None
    diffs.sort()
    if diffs:
        print('  hull vs host: samples %d, mean diff %.2f %% of max, p95 %.2f %%, p99 %.2f %%; '
              'divergences > 5 %%: %d, longest %d ms, median %d ms' % (
                  len(diffs), 100.0 * sum(diffs) / len(diffs), 100.0 * diffs[len(diffs) * 95 // 100],
                  100.0 * diffs[len(diffs) * 99 // 100], len(long_div), max(long_div) if long_div else 0,
                  sorted(long_div)[len(long_div) // 2] if long_div else 0))

    def phaser_bins(hits):
        b = set()
        for t, target, rest in hits:
            if 'wtype=0' in rest:
                b.add((target, t // 500))
        return b
    hb = phaser_bins(host[1])
    for r in roles:
        if r == 'host':
            continue
        ts = [x[0] for tr in logs[r][0].values() for x in tr]
        if not ts:
            continue
        w0, w1 = min(ts) // 500 + 4, max(ts) // 500 - 4
        cb = phaser_bins(logs[r][1])
        hbw = {x for x in hb if w0 <= x[1] <= w1}
        cbw = {x for x in cb if w0 <= x[1] <= w1}
        near = lambda x, s: (x[0], x[1]) in s or (x[0], x[1] - 1) in s or (x[0], x[1] + 1) in s
        both = sum(1 for x in hbw if near(x, cbw))
        conly = sum(1 for x in cbw if not near(x, hbw))
        print('  phaser hit bins %-7s host %4d, also on client %4d (%.0f %%), client only %4d' % (
            r, len(hbw), both, 100.0 * both / max(len(hbw), 1), conly))


if __name__ == '__main__':
    for run in sys.argv[1:]:
        combat(run)
