"""Where a ship appears: when a ship (object ID) first shows up on another peer, how far is it from
where its owner has it at that moment, and 0.25 s, 0.5 s and 1 s later? (The first sample may come
before the peer got the ship's first state update; the later ones show how fast it gets there.)

Usage: python -I spawn.py [-v] <run dir> [more]
Counts every ship that appears on a peer within 5 s after its owner created it (respawns, AI
ships, and the ships a joining player gets). -v lists the ones more than 5 units off.
"""
import bisect
import math
import os
import re
import sys


def load(path):
    """object ID -> [(t, local, pos, name)] from the S lines"""
    tr = {}
    for line in open(path, errors='replace'):
        if not line.startswith('S '):
            continue
        p = line.split()
        if len(p) < 15:
            continue
        tr.setdefault(p[5], []).append((int(p[1]), p[6] == '1', tuple(float(x) for x in p[7:10]), p[3]))
    return tr


def at(own, t):
    ts = [x[0] for x in own]
    i = bisect.bisect_left(ts, t)
    if i == 0 or i >= len(own):
        return None
    a, b = own[i - 1], own[i]
    f = (t - a[0]) / max(b[0] - a[0], 1)
    return tuple(a[2][k] + f * (b[2][k] - a[2][k]) for k in range(3))


def dist(a, b):
    return math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3)))


def q(lst, f):
    return lst[min(int(len(lst) * f), len(lst) - 1)] if lst else float('nan')


def main():
    verbose = '-v' in sys.argv
    for run in [a for a in sys.argv[1:] if not a.startswith('-')]:
        logs = {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = load(os.path.join(run, f))
        owners = {}
        for role, tr in logs.items():
            for oid, lst in tr.items():
                if any(x[1] for x in lst):
                    owners[oid] = role
        first, after025, after05, after1 = [], [], [], []
        for oid, owner in owners.items():
            own = [x for x in logs[owner][oid] if x[1]]
            for role, tr in logs.items():
                if role == owner or oid not in tr:
                    continue
                view = tr[oid]
                if not 0 <= view[0][0] - own[0][0] <= 5000:
                    continue  # the ship existed before this peer's log started
                e0 = at(own, view[0][0])
                if e0 is None:
                    continue
                first.append(dist(view[0][2], e0))
                if verbose and first[-1] > 5:
                    print('   %-9s owner %-8s viewer %-8s t=%d off by %.1f' % (view[0][3], owner, role, view[0][0],
                                                                             first[-1]))
                for lst, dt in ((after025, 250), (after05, 500), (after1, 1000)):
                    v = [x for x in view if x[0] >= view[0][0] + dt]
                    if v and at(own, v[0][0]):
                        lst.append(dist(v[0][2], at(own, v[0][0])))
        n = len(first)
        big = (sum(e > 5 for e in first), sum(e > 20 for e in first))
        for lst in (first, after025, after05, after1):
            lst.sort()
        print('%s: ships appearing on a peer %d; off by more than 5 units %d (%.1f %%), more than 20 units %d; '
              'first sample median %.1f, max %.1f; after 0.25 s 90th %.1f, max %.1f; after 0.5 s 90th %.1f, max %.1f; '
              'after 1 s 90th %.2f' % (
                  os.path.basename(run), n, big[0], 100.0 * big[0] / max(n, 1), big[1], q(first, .5), q(first, 1),
                  q(after025, .9), q(after025, 1), q(after05, .9), q(after05, 1), q(after1, .9)))


if __name__ == '__main__':
    main()
