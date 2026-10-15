"""Compare telemetry logs of a multiplayer test run (2 or more instances).

Usage: python -I analyze.py <run dir> [<run dir> ...]

For each ship, the owner's log (local=1) is the reference trajectory. For every
other instance the remote view is compared with the owner's position at the same
wall-clock time (QPC ms, shared by all processes on one machine).
Also lists weapon hits and deaths as seen by each instance, and checks whether all
instances saw the same torpedo hits as the host (the host decides damage).
"""
import bisect
import math
import os
import re
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


def role_key(r):
    if r == 'host':
        return 0
    return int(r[6:] or 1)


def view_errors(ref, track, dead_ts):
    """Errors of a remote view (track) against the owner's trajectory (ref)."""
    errs, ahead, jumps = [], [], 0
    back, steps = 0, 0  # rubber-banding: 100 ms steps against the view's own direction of flight
    prev = None
    ref_ts = [x[0] for x in ref]
    # skip the first 5 s after the ship appears for this viewer (join: proxies settle)
    t_first = track[0][0] if track else 0
    for t, pos, vel, hull, local in track:
        if t - t_first < 5000:
            continue
        # skip 16 s after each death of this ship (respawn moves it legitimately)
        if any(0 <= t - dt0 < 16000 for dt0 in dead_ts):
            prev = None
            continue
        r = interp(ref, t)
        if r is not None:
            errs.append(dist(pos, r))
            i = bisect.bisect_left(ref_ts, t)
            v = ref[min(i, len(ref) - 1)][2]
            sp = math.sqrt(sum(c * c for c in v))
            if sp > 1.0:
                # signed error along the owner's velocity: + = remote view is ahead
                ahead.append(sum((pos[k] - r[k]) * v[k] / sp for k in range(3)))
        if prev is not None:
            dt = (t - prev[0]) / 1000.0
            speed = max(math.sqrt(sum(c * c for c in vel)), 1.0)
            if dt > 0 and dist(pos, prev[1]) > 3 * speed * dt + 2.0:
                jumps += 1
            pv = prev[2]
            psp = math.sqrt(sum(c * c for c in pv))
            if 0 < dt <= 0.2 and psp > 1.0:
                steps += 1
                if sum((pos[k] - prev[1][k]) * pv[k] / psp for k in range(3)) < -0.05:
                    back += 1
        prev = (t, pos, vel)
    return errs, ahead, jumps, back, steps


def torp_hits(hits):
    """Torpedo/pulse projectile hits, one per projectile: the stock code reports a hit that
    bleeds through the shield as two events (shield, then hull) with the same weapon
    instance id, the hull part up to a few hundred ms later; keep the first and its shield/hull flag."""
    out, last = [], {}
    for t, target, rest in hits:
        kv = dict(x.split('=', 1) for x in rest.split() if '=' in x)
        if kv.get('wtype') != '1':
            continue
        key = (kv.get('wid'), target)
        if key in last and t - last[key] < 1000:  # the host reports one hit per torpedo within 1 s
            continue
        last[key] = t
        out.append((t, target, kv.get('hull')))
    return out


def match_hits(h, c):
    """Greedy match of host hits h with client hits c (same target, within 1.5 s)."""
    used, pairs = set(), {}
    for i, (t, target, hull) in enumerate(h):
        best = None
        for j, (tc, tgc, hc) in enumerate(c):
            if j in used or tgc != target or abs(tc - t) > 1500:
                continue
            if best is None or abs(tc - t) < abs(c[best][0] - t):
                best = j
        if best is not None:
            used.add(best)
            pairs[i] = best
    return pairs, [j for j in range(len(c)) if j not in used]


def analyze(run):
    logs = {}
    for f in os.listdir(run):
        m = re.match(r'tlog_(host|client\d*)\.txt$', f)
        if m:
            logs[m.group(1)] = load(os.path.join(run, f))
    roles = sorted(logs, key=role_key)
    print('=== %s (%s)' % (os.path.basename(run), ', '.join(roles)))
    # owner = the instance that logs the ship as local most often (names can be
    # briefly attached to the wrong object around a respawn)
    owners, nlocal = {}, {}
    for role in roles:
        for name, tr in logs[role][0].items():
            n = sum(1 for x in tr if x[4])
            if n > nlocal.get(name, 0):
                owners[name], nlocal[name] = role, n
    pairs = {}  # (name, viewer) -> (errs, ahead, jumps)
    for name, owner in sorted(owners.items(), key=lambda x: role_key(x[1])):
        ref = [x for x in logs[owner][0][name] if x[4]]
        dead_ts = [d[0] for r in roles for d in logs[r][2] if d[1] == name]
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
        for role in roles:
            if role == owner or name not in logs[role][0]:
                continue
            pairs[(name, role)] = view_errors(ref, logs[role][0][name], dead_ts)
            if len(roles) == 2:
                errs, ahead, jumps, back, steps = pairs[(name, role)]
                sp = [math.sqrt(sum(c * c for c in x[2])) for x in ref]
                print('  %-8s owner=%-6s viewed by %-6s samples=%5d err mean=%7.2f p50=%7.2f p95=%7.2f max=%8.2f '
                      'jumps=%d ahead=%+.2f (owner mean speed %.1f) back=%.1f%%' % (
                          name, owner, role, len(errs), sum(errs) / max(len(errs), 1), pct(errs, 0.5),
                          pct(errs, 0.95), max(errs) if errs else float('nan'), jumps,
                          sum(ahead) / max(len(ahead), 1), sum(sp) / max(len(sp), 1),
                          100.0 * back / max(steps, 1)))
    if len(roles) > 2:
        # matrix of mean errors: rows = ships (owner), columns = viewing instance
        print('  mean error by ship (row, owner) and viewer (column):')
        print('    %-16s' % '' + ''.join('%9s' % r for r in roles) + '   jumps')
        for name, owner in sorted(owners.items(), key=lambda x: role_key(x[1])):
            cells, jsum = [], 0
            for role in roles:
                if (name, role) in pairs:
                    errs, ahead, jumps, back, steps = pairs[(name, role)]
                    cells.append('%9.2f' % (sum(errs) / max(len(errs), 1)))
                    jsum += jumps
                else:
                    cells.append('%9s' % '-')
            print('    %-16s' % ('%s (%s)' % (name, owner)) + ''.join(cells) + '   %d' % jsum)
        cats = {}
        for (name, viewer), v in pairs.items():
            owner = owners[name]
            cat = 'host views client' if viewer == 'host' else ('client views host' if owner == 'host'
                                                              else 'client views client')
            cats.setdefault(cat, []).append(v)
        for cat in ('client views host', 'host views client', 'client views client'):
            if cat not in cats:
                continue
            errs = [e for v in cats[cat] for e in v[0]]
            ahead = [a for v in cats[cat] for a in v[1]]
            jumps = sum(v[2] for v in cats[cat])
            back = sum(v[3] for v in cats[cat])
            steps = sum(v[4] for v in cats[cat])
            print('  %-20s pairs=%2d samples=%6d err mean=%6.2f p50=%6.2f p95=%6.2f max=%7.2f jumps=%d ahead=%+.2f '
                  'back=%.1f%%' % (
                      cat, len(cats[cat]), len(errs), sum(errs) / max(len(errs), 1), pct(errs, 0.5), pct(errs, 0.95),
                      max(errs) if errs else float('nan'), jumps, sum(ahead) / max(len(ahead), 1),
                      100.0 * back / max(steps, 1)))
    for role in roles:
        ships, hits, deaths, fired, states = logs[role]
        tor = sum(1 for h in hits if 'wtype=1' in h[2])
        print('  %-7s hits=%d (torpedo %d) deaths=%d torps_fired=%d' % (role, len(hits), tor, len(deaths), len(fired)))
        if len(roles) == 2:
            for d in deaths:
                print('     death %d %s %s' % d)
        errs = [s for s in states if 'ERROR' in s]
        for e in errs[:5]:
            print('     ', e)
    if 'host' not in logs or len(roles) < 2:
        return
    h = torp_hits(logs['host'][1])
    clients = [r for r in roles if r != 'host']
    if len(roles) == 2:
        c = torp_hits(logs[clients[0]][1])
        m, only_c = match_hits(h, c)
        dts = [abs(c[j][0] - h[i][0]) for i, j in m.items()]
        hull_mismatch = sum(1 for i, j in m.items() if c[j][2] != h[i][2])
        print('  torpedo hits: both=%d host-only=%d client-only=%d  (shield/hull differs on %d, '
              'mean |dt| %.0f ms, max %.0f ms)' % (len(m), len(h) - len(m), len(only_c), hull_mismatch,
                                                  sum(dts) / max(len(dts), 1), max(dts) if dts else 0))
        return
    # clients join and quit at different times: count only host hits inside each
    # client's in-game window (first to last telemetry sample, 2 s margin)
    def window(role):
        ts = [x[0] for tr in logs[role][0].values() for x in tr]
        return (min(ts) + 2000, max(ts) - 2000) if ts else (0, -1)
    seen = [0] * len(h)
    cover = [0] * len(h)
    dts, hull_mismatch, parts = [], 0, []
    for r in clients:
        w0, w1 = window(r)
        hi = [i for i in range(len(h)) if w0 <= h[i][0] <= w1]
        hw = [h[i] for i in hi]
        c = [x for x in torp_hits(logs[r][1]) if w0 - 1500 <= x[0] <= w1 + 1500]
        m, only_c = match_hits(hw, c)
        for i in hi:
            cover[i] += 1
        for i, j in m.items():
            seen[hi[i]] += 1
            dts.append(abs(c[j][0] - hw[i][0]))
            hull_mismatch += c[j][2] != hw[i][2]
        parts.append('%s %d/%d +%d' % (r, len(m), len(hw), len(only_c)))
    full = [i for i in range(len(h)) if cover[i] == len(clients)]
    print('  torpedo hits on host while all %d clients were in game=%d: seen by all=%d, by none=%d  '
          '(shield/hull differs on %d of all matches, mean |dt| %.0f ms, max %.0f ms)' % (
              len(clients), len(full), sum(1 for i in full if seen[i] == len(clients)),
              sum(1 for i in full if seen[i] == 0), hull_mismatch, sum(dts) / max(len(dts), 1),
              max(dts) if dts else 0))
    print('  per client (matched/host hits in its window +client-only): ' + ', '.join(parts))


def main():
    for run in sys.argv[1:]:
        analyze(run)


if __name__ == '__main__':
    main()
