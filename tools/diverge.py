"""Find divergence events (remote view far from the owner) and show what the viewer's
trace says about that proxy around them.

Usage: python -I diverge.py <run dir> [threshold=5]
Needs a run with TraceStateSec. For each (ship, viewer) with error > threshold: start time,
peak error, the hull drop / velocity change of the owner just before, and per second the
number of state updates (U) and corrections (C) the viewer logged for that proxy, plus the
mean along/lateral error and target distance from the C/U lines.
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A


def main():
    run = sys.argv[1]
    thr = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
    logs = {}
    for f in os.listdir(run):
        m = re.match(r'tlog_(host|client\d*)\.txt$', f)
        if m:
            logs[m.group(1)] = A.load(os.path.join(run, f))
    owners = {}
    for r, lg in logs.items():
        for name, tr in lg[0].items():
            n = sum(1 for x in tr if x[4])
            if n > owners.get(name, ('', 0))[1]:
                owners[name] = (r, n)
    for name, (owner, _) in sorted(owners.items()):
        ref = [x for x in logs[owner][0][name] if x[4]]
        dead = [d[0] for lg in logs.values() for d in lg[2] if d[1] == name]
        for viewer, lg in sorted(logs.items()):
            if viewer == owner or name not in lg[0]:
                continue
            ev = None
            t_first = lg[0][name][0][0]
            for t, pos, vel, hull, loc in lg[0][name]:
                if t - t_first < 5000 or any(0 <= t - d < 16000 for d in dead):
                    continue  # join settling, respawn
                r = A.interp(ref, t)
                if r is None:
                    continue
                e = A.dist(pos, r)
                if e > thr and ev is None:
                    ev = [t, e]
                elif ev is not None:
                    ev[1] = max(ev[1], e)
                    if e < thr / 2:
                        report(run, name, owner, viewer, ref, lg, ev, t)
                        ev = None
            if ev is not None:
                report(run, name, owner, viewer, ref, lg, ev, None)


def report(run, name, owner, viewer, ref, lg, ev, tend):
    t0 = ev[0]
    before = [x for x in ref if t0 - 3000 <= x[0] <= t0]
    dv, dh = 0.0, 0.0
    for a, b in zip(before, before[1:]):
        dv = max(dv, A.dist(a[2], b[2]))
        dh = max(dh, a[3] - b[3])
    print('%s seen by %s: error > threshold from %d ms (%s), peak %.1f; owner in the 3 s before: '
          'max velocity step %.1f per sample, hull drop %.0f' % (
              name, viewer, t0, 'until %d' % tend if tend else 'to the end', ev[1], dv, dh))
    obj = None
    for t, pos, vel, hull, loc in lg[0][name]:
        pass
    tl = os.path.join(run, 'tlog_%s.txt' % viewer)
    for line in open(tl, errors='replace'):
        p = line.split()
        if p and p[0] == 'S' and len(p) >= 15 and p[3] == name and abs(int(p[1]) - t0) < 2000:
            obj = p[5]
            break
    tr = os.path.join(run, 'trace_%s.txt' % viewer)
    if not obj or not os.path.exists(tr):
        return
    per = {}
    for line in open(tr, errors='replace'):
        p = line.split()
        if len(p) < 3 or p[2] != 'obj=' + obj:
            continue
        t = int(p[0])
        if not (t0 - 3000 <= t <= t0 + 8000):
            continue
        k = (t - t0) // 1000
        d = per.setdefault(k, {'U': 0, 'C': 0, 'along': [], 'lat': []})
        d[p[1]] = d.get(p[1], 0) + 1
        if p[1] == 'C':
            kv = dict(x.split('=') for x in p[3:] if '=' in x)
            d['along'].append(float(kv['along']))
            d['lat'].append(float(kv['lat']))
    for k in sorted(per):
        d = per[k]
        mean = lambda v: sum(v) / len(v) if v else float('nan')
        print('    %+3d s: updates %3d corrections %4d  along %+7.2f  lateral %6.2f' % (
            k, d['U'], d['C'], mean(d['along']), mean(d['lat'])))


if __name__ == '__main__':
    main()
