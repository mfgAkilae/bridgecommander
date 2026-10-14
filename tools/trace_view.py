"""Correlate the state-update trace of one client with its telemetry for one remote ship.

Usage: python -I trace_view.py <run dir> [viewer=client] [ship=Ace] [seconds=60]
Needs a run with TraceStateSec > 0 (mptest --test-set TraceStateSec=70). Per 2 s: backward steps
(telemetry), state updates received (all / with forward vector / with speed), lead, received speed,
nose.fwd (proxy nose vs. received forward), along-track and lateral error, target distance.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A

run = sys.argv[1]
viewer = sys.argv[2] if len(sys.argv) > 2 else 'client'
ship = sys.argv[3] if len(sys.argv) > 3 else 'Ace'
secs = int(sys.argv[4]) if len(sys.argv) > 4 else 60

# objid of the ship on the viewer
objid = None
for line in open(os.path.join(run, 'tlog_%s.txt' % viewer), errors='replace'):
    p = line.split()
    if p and p[0] == 'S' and len(p) >= 15 and p[3] == ship and p[6] == '0':
        objid = p[5]
        break
track = [x for x in A.load(os.path.join(run, 'tlog_%s.txt' % viewer))[0][ship] if not x[4]]
t0 = track[0][0]

U, C = [], []
for line in open(os.path.join(run, 'trace_%s.txt' % viewer), errors='replace'):
    p = line.split()
    if len(p) < 3 or p[2] != 'obj=' + str(objid):
        continue
    t = int(p[0])
    kv = {}
    toks = p[1:]
    if toks[0] == 'U':
        mask = int(toks[3].split('=')[1])
        i = toks.index('fwd')
        fwd = [float(x) for x in toks[i + 1:i + 4]]
        i = toks.index('nose')
        nose = [float(x) for x in toks[i + 1:i + 4]]
        i = toks.index('spd')
        spd = float(toks[i + 1])
        lead = int(toks[toks.index('lead') + 1])
        i = toks.index('tgt')
        tgt = [float(x) for x in toks[i + 1:i + 4]]
        i = toks.index('pos')
        pos = [float(x) for x in toks[i + 1:i + 4]]
        U.append((t, mask, fwd, nose, spd, lead, A.dist(tgt, pos)))
    elif toks[0] == 'C':
        d = dict(x.split('=') for x in toks[2:] if '=' in x)
        C.append((t, float(d['along']), float(d['lat']), float(d['back']), float(d['nose.fwd'])))

print('ship %s objid %s on %s: %d updates, %d corrections traced; telemetry starts %d' % (
    ship, objid, viewer, len(U), len(C), t0))
if U:
    print('trace starts %+d ms after first telemetry sample' % (U[0][0] - t0))

# backward steps per bucket from telemetry
back = {}
prev = None
for t, pos, vel, h, loc in track:
    if prev:
        dt = (t - prev[0]) / 1000.0
        pv = prev[2]
        psp = math.sqrt(sum(c * c for c in pv))
        if 0 < dt <= 0.2 and psp > 1.0:
            k = (t - t0) // 2000
            n, b = back.get(k, (0, 0))
            back[k] = (n + 1, b + (sum((pos[i] - prev[1][i]) * pv[i] / psp for i in range(3)) < -0.05))
    prev = (t, pos, vel)

print('%5s %6s %5s %5s %5s %6s %6s %6s %6s %6s %6s' % ('t[s]', 'back', 'upd', 'fwdU', 'spdU', 'lead', 'spd', 'n.f', 'along', 'lat', 'err'))
for k in range(0, secs // 2):
    a, b = t0 + k * 2000, t0 + (k + 1) * 2000
    u = [x for x in U if a <= x[0] < b]
    c = [x for x in C if a <= x[0] < b]
    nb = back.get(k, (0, 0))
    mean = lambda v: sum(v) / len(v) if v else float('nan')
    print('%5d %3d/%-3d %4d %5d %5d %6.0f %6.2f %6.2f %6.2f %6.2f %6.2f' % (
        k * 2, nb[1], nb[0], len(u), sum(1 for x in u if x[1] & 2), sum(1 for x in u if x[1] & 8),
        mean([x[5] for x in u]), mean([x[4] for x in u]), mean([x[4] for x in c]),
        mean([x[1] for x in c]), mean([x[2] for x in c]), mean([x[6] for x in u])))
