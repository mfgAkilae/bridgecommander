"""Compare telemetry logs of a multiplayer test run.

Usage: python -I analyze.py <run dir> [<run dir> ...]

For each ship, the owner's log (local=1) is the reference trajectory. For every
other instance the remote view is compared with the owner's position at the same
wall-clock time (QPC ms, shared by all processes on one machine).
Also lists weapon hits and deaths as seen by each instance.
"""
import bisect
import math
import os
import sys


def load(path):
    ships = {}
    hits, deaths, fired, states = [], [], [], []
    for line in open(path, errors='replace'):
        p = line.split()
        if not p:
            continue
        if p[0] == 'S' and len(p) >= 15:
            t = int(p[1])
            name, local = p[3], int(p[6])
            pos = tuple(float(x) for x in p[7:10])
            vel = tuple(float(x) for x in p[10:13])
            hull = float(p[13])
            ships.setdefault(name, []).append((t, pos, vel, hull, local))
        elif p[0] == 'H':
            hits.append((int(p[1]), p[2], ' '.join(p[3:])))
        elif p[0] == 'D':
            deaths.append((int(p[1]), p[2], ' '.join(p[3:])))
        elif p[0] == 'F':
            fired.append((int(p[1]), p[2]))
        elif p[0] == 'X':
            states.append(line.strip())
    return ships, hits, deaths, fired, states


def interp(track, t):
    ts = [x[0] for x in track]
    i = bisect.bisect_left(ts, t)
    if i <= 0 or i >= len(track):
        return None
    t0, p0 = track[i - 1][0], track[i - 1][1]
    t1, p1 = track[i][0], track[i][1]
    if t1 - t0 > 500:
        return None
    a = (t - t0) / float(t1 - t0)
    return tuple(p0[k] + (p1[k] - p0[k]) * a for k in range(3))


def dist(a, b):
    return math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3)))


def pct(v, q):
    if not v:
        return float('nan')
    v = sorted(v)
    return v[min(len(v) - 1, int(q * len(v)))]


def analyze(run):
    logs = {}
    for role in ('host', 'client'):
        p = os.path.join(run, 'tlog_%s.txt' % role)
        if os.path.exists(p):
            logs[role] = load(p)
    print('=== %s (%s)' % (os.path.basename(run), ', '.join(logs)))
    # owner = the instance that logs the ship as local most often (names can be
    # briefly attached to the wrong object around a respawn)
    owners, nlocal = {}, {}
    for role, (ships, *_rest) in logs.items():
        for name, tr in ships.items():
            n = sum(1 for x in tr if x[4])
            if n > nlocal.get(name, 0):
                owners[name], nlocal[name] = role, n
    for name, owner in sorted(owners.items()):
        ref = [x for x in logs[owner][0][name] if x[4]]
        # skip 16 s after each death of this ship (respawn moves it legitimately)
        dead_ts = [d[0] for lg in logs.values() for d in lg[2] if d[1] == name]
        # own ship: any teleport in the owner's own view?
        own_jumps = 0
        for a, b in zip(ref, ref[1:]):
            if any(0 <= b[0] - dt0 < 16000 for dt0 in dead_ts):
                continue
            dt = (b[0] - a[0]) / 1000.0
            speed = max(math.sqrt(sum(c * c for c in a[2])), 1.0)
            if dt > 0 and dist(a[1], b[1]) > 3 * speed * dt + 2.0:
                own_jumps += 1
        if own_jumps:
            print('  %-8s OWN-VIEW JUMPS on %s: %d' % (name, owner, own_jumps))
        for role, (ships, *_r) in logs.items():
            if role == owner or name not in ships:
                continue
            errs, jumps = [], 0
            ahead = []  # signed error along the owner's velocity: + = remote view is ahead
            prev = None
            for t, pos, vel, hull, local in ships[name]:
                if any(0 <= t - dt0 < 16000 for dt0 in dead_ts):
                    prev = None
                    continue
                r = interp(ref, t)
                if r is not None:
                    errs.append(dist(pos, r))
                    i = bisect.bisect_left([x[0] for x in ref], t)
                    v = ref[min(i, len(ref) - 1)][2]
                    sp = math.sqrt(sum(c * c for c in v))
                    if sp > 1.0:
                        ahead.append(sum((pos[k] - r[k]) * v[k] / sp for k in range(3)))
                if prev is not None:
                    dt = (t - prev[0]) / 1000.0
                    speed = max(math.sqrt(sum(c * c for c in vel)), 1.0)
                    if dt > 0 and dist(pos, prev[1]) > 3 * speed * dt + 2.0:
                        jumps += 1
                prev = (t, pos)
            sp = [math.sqrt(sum(c * c for c in x[2])) for x in ref]
            print('  %-8s owner=%-6s viewed by %-6s samples=%5d err mean=%7.2f p50=%7.2f p95=%7.2f max=%8.2f '
                  'jumps=%d ahead=%+.2f (owner mean speed %.1f)' % (
                      name, owner, role, len(errs), sum(errs) / max(len(errs), 1), pct(errs, 0.5),
                      pct(errs, 0.95), max(errs) if errs else float('nan'), jumps,
                      sum(ahead) / max(len(ahead), 1), sum(sp) / max(len(sp), 1)))
    for role, (ships, hits, deaths, fired, states) in logs.items():
        tor = sum(1 for h in hits if 'wtype=1' in h[2])
        print('  %-6s hits=%d (torpedo %d) deaths=%d torps_fired=%d' % (role, len(hits), tor, len(deaths), len(fired)))
        for d in deaths:
            print('     death %d %s %s' % d)
        errs = [s for s in states if 'ERROR' in s]
        for e in errs[:5]:
            print('     ', e)
    # torpedo hit consistency: match hits on the same target within a time window
    if len(logs) == 2:
        def torp_hits(hits):
            out = []
            for t, target, rest in hits:
                kv = dict(x.split('=', 1) for x in rest.split() if '=' in x)
                if kv.get('wtype') == '1':
                    out.append((t, target, kv.get('hull')))
            return out
        h = torp_hits(logs['host'][1])
        c = torp_hits(logs['client'][1])
        used = set()
        both, dts, hull_mismatch, only_h = 0, [], 0, []
        for t, target, hull in h:
            best = None
            for j, (tc, tgc, hc) in enumerate(c):
                if j in used or tgc != target or abs(tc - t) > 1500:
                    continue
                if best is None or abs(tc - t) < abs(c[best][0] - t):
                    best = j
            if best is None:
                only_h.append((t, target))
            else:
                used.add(best)
                both += 1
                dts.append(abs(c[best][0] - t))
                hull_mismatch += c[best][2] != hull
        only_c = [(c[j][0], c[j][1]) for j in range(len(c)) if j not in used]
        print('  torpedo hits: both=%d host-only=%d client-only=%d  (shield/hull differs on %d, '
              'mean |dt| %.0f ms, max %.0f ms)' % (both, len(only_h), len(only_c), hull_mismatch,
                                                  sum(dts) / max(len(dts), 1), max(dts) if dts else 0))


def main():
    for run in sys.argv[1:]:
        analyze(run)


if __name__ == '__main__':
    main()
