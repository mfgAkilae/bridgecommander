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
    A.trim(logs)
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


SUBS = ['impulse', 'phaser', 'torpedo', 'pulse', 'power', 'sensor']


def load_p(path):
    """P lines: t name sh0..sh5 impulse phaser torpedo pulse power sensor (once a second)."""
    out = {}
    for line in open(path, errors='replace'):
        p = line.split()
        if len(p) == 15 and p[0] == 'P':
            out.setdefault(p[2], []).append((int(p[1]), [float(x) for x in p[3:]]))
    return out


def subsystems(run):
    logs = {}
    for f in os.listdir(run):
        m = re.match(r'tlog_(host|client\d*)\.txt$', f)
        if m:
            logs[m.group(1)] = load_p(os.path.join(run, f))
    if 'host' not in logs or not logs['host']:
        return
    host = logs['host']
    shield_d, sub_d = [], {s: [] for s in SUBS}
    for name, hs in host.items():
        ts = [x[0] for x in hs]
        smax = [max(x[1][i] for x in hs) or 1.0 for i in range(6)]
        for r, lg in logs.items():
            if r == 'host' or name not in lg:
                continue
            for t, v in lg[name]:
                i = bisect.bisect_left(ts, t)
                cand = [j for j in (i - 1, i) if 0 <= j < len(hs) and abs(hs[j][0] - t) < 600]
                if not cand:
                    continue
                ref = hs[min(cand, key=lambda j: abs(hs[j][0] - t))][1]
                for k in range(6):
                    if v[k] >= 0 and ref[k] >= 0:
                        shield_d.append(abs(v[k] - ref[k]) / smax[k])
                for k, s in enumerate(SUBS):
                    a, b = v[6 + k], ref[6 + k]
                    if a >= 0 and b >= 0:
                        sub_d[s].append(abs(a - b))
    if shield_d:
        shield_d.sort()
        print('  shields vs host (per facing, fraction of max): mean %.3f p95 %.3f p99 %.3f (n=%d)' % (
            sum(shield_d) / len(shield_d), shield_d[len(shield_d) * 95 // 100], shield_d[len(shield_d) * 99 // 100],
            len(shield_d)))
    parts = []
    for s in SUBS:
        d = sorted(sub_d[s])
        if d:
            parts.append('%s %.3f/%.3f' % (s, sum(d) / len(d), d[len(d) * 99 // 100]))
    if parts:
        print('  subsystem condition vs host (mean/p99 of |diff|): ' + ', '.join(parts))


if __name__ == '__main__':
    for run in sys.argv[1:]:
        combat(run)
        subsystems(run)
