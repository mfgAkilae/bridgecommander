"""Torpedo copies against the shooter's own torpedo (Q lines of the test pilot).

Usage: python -I torptrack.py <run dir> [more]
For every torpedo on its shooter's machine, the copy on each other peer is the track of the
same shooter that stays closest. Prints the along-track offset of the copies (negative =
behind the shooter's torpedo) and the distance, for the host and for the clients.
"""
import math
import os
import re
import sys

ROLE_NAMES = {'host': 'Ace', 'client': 'Bravo', 'client2': 'Charlie', 'client3': 'Delta', 'client4': 'Echo',
              'client5': 'Foxtrot'}


def load(path):
    tr = {}
    for line in open(path, errors='replace'):
        p = line.split()
        if len(p) == 7 and p[0] == 'Q':
            tr.setdefault((p[3].rstrip('0123456789'), p[2]), []).append((int(p[1]), float(p[4]), float(p[5]), float(p[6])))
    return tr


def at(track, t):
    for a, b in zip(track, track[1:]):
        if a[0] <= t <= b[0] and b[0] - a[0] <= 300:
            f = (t - a[0]) / float(b[0] - a[0]) if b[0] > a[0] else 0.0
            return tuple(a[k] + (b[k] - a[k]) * f for k in (1, 2, 3))
    return None


def main():
    res = {}
    for run in sys.argv[1:]:
        logs = {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = load(os.path.join(run, f))
        for role, name in ROLE_NAMES.items():
            if role not in logs:
                continue
            for (sh, tid), tr in logs[role].items():
                if sh != name or len(tr) < 4:
                    continue
                for r, lg in logs.items():
                    if r == role:
                        continue
                    best = None
                    for (sh2, tid2), tr2 in lg.items():
                        if sh2 != name or len(tr2) < 3:
                            continue
                        ds = []
                        for s in tr[1:-1]:
                            p = at(tr2, s[0])
                            if p:
                                ds.append(math.sqrt(sum((p[k] - s[k + 1]) ** 2 for k in range(3))))
                        if len(ds) >= 2 and (best is None or sum(ds) / len(ds) < best[0]):
                            best = (sum(ds) / len(ds), tr2)
                    if best is None or best[0] > 30:
                        continue
                    tr2 = best[1]
                    for a, b in zip(tr[1:-1], tr[2:]):
                        p = at(tr2, a[0])
                        if not p:
                            continue
                        v = [b[k] - a[k] for k in (1, 2, 3)]
                        n = math.sqrt(sum(c * c for c in v))
                        if n < 1e-3:
                            continue
                        d = [p[k] - a[k + 1] for k in range(3)]
                        along = sum(d[k] * v[k] for k in range(3)) / n
                        speed = n / ((b[0] - a[0]) / 1000.0)
                        kind = 'host' if r == 'host' else 'client'
                        res.setdefault(kind, []).append((along, math.sqrt(sum(c * c for c in d)), along / speed * 1000.0))
    for kind, v in sorted(res.items()):
        al = sorted(x[0] for x in v)
        ms = sorted(x[2] for x in v)
        di = sorted(x[1] for x in v)
        n = len(v)
        print('%-6s copies: samples %d, along-track median %+.2f units (%+.0f ms of flight), 10th %+.0f ms, 90th %+.0f ms; '
              'distance median %.2f, 90th %.2f' % (kind, n, al[n // 2], ms[n // 2], ms[n // 10], ms[n * 9 // 10],
                                                    di[n // 2], di[n * 9 // 10]))


if __name__ == '__main__':
    main()
