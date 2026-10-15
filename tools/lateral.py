"""Lateral and along-track error of remote views by turn rate (issue #11).

Usage: python -I lateral.py <run dir> [more run dirs]
Needs S lines with the forward vector (test pilot since v1.3.0). For every remote view sample:
error vector against the owner's interpolated position, split along the owner's velocity
(+ = ahead) and lateral; the owner's turn rate (change of the velocity direction over 0.4 s)
and drift (angle between nose and velocity). Prints per turn-rate bin and viewer kind
(direct = host views clients / clients view host, relayed = clients view clients).
"""
import bisect
import math
import os
import re
import sys

BINS = [(0.0, 0.1), (0.1, 0.3), (0.3, 0.6), (0.6, 99.0)]


def load(path):
    ships, dead = {}, []
    for line in open(path, errors='replace'):
        p = line.split()
        if len(p) >= 18 and p[0] == 'S':
            ships.setdefault(p[3], []).append((int(p[1]), tuple(map(float, p[7:10])), tuple(map(float, p[10:13])),
                                                int(p[6]), tuple(map(float, p[15:18]))))
        elif p and p[0] == 'D':
            dead.append((int(p[1]), p[2]))
    return ships, dead


def norm(v):
    n = math.sqrt(sum(c * c for c in v))
    return n, tuple(c / n for c in v) if n > 1e-6 else (0.0, 0.0, 0.0)


def angle(a, b):
    d = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))
    return math.acos(d)


def t_first0(track):
    return track[0][0] if track else 0


def interp_ref(ref, ts, t):
    i = bisect.bisect_left(ts, t)
    if i <= 0 or i >= len(ref) or ref[i][0] - ref[i - 1][0] > 500:
        return None
    a = (t - ref[i - 1][0]) / float(ref[i][0] - ref[i - 1][0])
    return tuple(ref[i - 1][1][k] + (ref[i][1][k] - ref[i - 1][1][k]) * a for k in range(3))


def main():
    out, noses, jit = {}, {}, {}
    for run in sys.argv[1:]:
        logs = {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = load(os.path.join(run, f))
        dead = [d for lg in logs.values() for d in lg[1]]
        for name in {n for lg in logs.values() for n in lg[0]}:
            owner = max(logs, key=lambda r: sum(1 for x in logs[r][0].get(name, []) if x[3]))
            ref = [x for x in logs[owner][0][name] if x[3]]
            ts = [x[0] for x in ref]
            dts = [d[0] for d in dead if d[1] == name]
            for viewer, lg in logs.items():
                if viewer == owner or name not in lg[0]:
                    continue
                kind = 'relayed' if owner != 'host' and viewer != 'host' else ('host ship' if owner == 'host' else 'by host')
                # smoothness: second differences of the remote view vs the owner's track at the same times
                tr = [x for x in lg[0][name] if x[0] - t_first0(lg[0][name]) >= 5000]
                for j in range(1, len(tr) - 1):
                    t0, t1, t2 = tr[j - 1][0], tr[j][0], tr[j + 1][0]
                    if t2 - t0 > 300 or any(-1000 < t1 - d < 16000 for d in dts):
                        continue
                    o = [interp_ref(ref, ts, t) for t in (t0, t1, t2)]
                    if None in o:
                        continue
                    av = [tr[j + 1][1][k] - 2 * tr[j][1][k] + tr[j - 1][1][k] for k in range(3)]
                    ao = [o[2][k] - 2 * o[1][k] + o[0][k] for k in range(3)]
                    jit.setdefault(kind, [0.0, 0.0, 0])
                    jit[kind][0] += sum(c * c for c in av)
                    jit[kind][1] += sum((av[k] - ao[k]) ** 2 for k in range(3))
                    jit[kind][2] += 1
                t_first = lg[0][name][0][0]
                for t, pos, vel, loc, fwd in lg[0][name]:
                    if t - t_first < 5000 or any(-1000 < t - d < 16000 for d in dts):
                        continue
                    i = bisect.bisect_left(ts, t)
                    if i <= 2 or i >= len(ref) - 3 or ref[i][0] - ref[i - 1][0] > 500:
                        continue
                    a = (t - ref[i - 1][0]) / float(ref[i][0] - ref[i - 1][0])
                    r = tuple(ref[i - 1][1][k] + (ref[i][1][k] - ref[i - 1][1][k]) * a for k in range(3))
                    sp, u = norm(ref[i][2])
                    if sp < 1.0:
                        continue
                    _, u0 = norm(ref[i - 2][2])
                    _, u1 = norm(ref[i + 2][2])
                    w = angle(u0, u1) / max((ref[i + 2][0] - ref[i - 2][0]) / 1000.0, 0.05)
                    drift = math.degrees(angle(norm(ref[i][4])[1], u))
                    # nose of the remote view against the owner's nose (interpolated)
                    of = tuple(ref[i - 1][4][k] + (ref[i][4][k] - ref[i - 1][4][k]) * a for k in range(3))
                    noses.setdefault(kind, []).append(math.degrees(angle(norm(fwd)[1], norm(of)[1])))
                    e = tuple(pos[k] - r[k] for k in range(3))
                    al = sum(e[k] * u[k] for k in range(3))
                    lat = math.sqrt(max(sum(c * c for c in e) - al * al, 0.0))
                    # signed lateral towards the inside of the curve (direction the velocity turns to)
                    dn = tuple(u1[k] - u0[k] for k in range(3))
                    dd = sum(dn[k] * u[k] for k in range(3))
                    _, inside = norm(tuple(dn[k] - dd * u[k] for k in range(3)))
                    lin = sum(e[k] * inside[k] for k in range(3))
                    for b in BINS:
                        if b[0] <= w < b[1]:
                            out.setdefault((kind, b), []).append((lat, al, math.sqrt(al * al + lat * lat), drift, lin))
    for kind in ('host ship', 'by host', 'relayed'):
        v = sorted(noses.get(kind, []))
        if v:
            print('  nose of the remote view vs owner, %-9s: median %.1f deg, 90th %.1f, mean %.1f' % (
                kind, v[len(v) // 2], v[int(len(v) * 0.9)], sum(v) / len(v)))
    for kind in ('host ship', 'by host', 'relayed'):
        if kind in jit and jit[kind][2]:
            j = jit[kind]
            print('  smoothness %-9s: RMS second difference of the remote view %.4f, of (view - owner) %.4f (n=%d)' % (
                kind, math.sqrt(j[0] / j[2]), math.sqrt(j[1] / j[2]), j[2]))
    print('  %-9s %-11s %7s %8s %8s %8s %8s %8s %6s' % ('views', 'turn rad/s', 'samples', 'lateral', 'lat p95', 'inside', 'along', 'error', 'drift'))
    for kind in ('host ship', 'by host', 'relayed'):
        for b in BINS:
            v = out.get((kind, b), [])
            if not v:
                continue
            lats = sorted(x[0] for x in v)
            print('  %-9s %4.1f-%-6s %7d %8.3f %8.3f %+8.3f %+8.3f %8.3f %5.1f' % (
                kind, b[0], ('%.1f' % b[1]) if b[1] < 50 else 'max', len(v), sum(lats) / len(v),
                lats[int(len(lats) * 0.95)], sum(x[4] for x in v) / len(v), sum(x[1] for x in v) / len(v),
                sum(x[2] for x in v) / len(v), sum(x[3] for x in v) / len(v)))


if __name__ == '__main__':
    main()
