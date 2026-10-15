"""When does each machine show a torpedo hit? (issue #12)

Usage: python -I hitlag.py <run dir> [more]
For every torpedo hit on the host (one per projectile), the matching hit on each client (same
target, within 1.5 s) and on the shooter's own machine (src= in the H line). Prints the signed
time differences: client - host, and every peer - shooter (median, 10th and 90th percentile).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A

# ship names of the test pilot (mptest.py): the host's default name, then the clients' -Name
NAMES = ['Alpha', 'Bravo', 'Charlie', 'Delta', 'Echo', 'Foxtrot', 'Golf', 'Hotel', 'India', 'Juliett', 'Kilo', 'Lima', 'Mike', 'November', 'Oscar', 'Papa']
ROLE_NAMES = dict([('host', 'Ace'), ('client', 'Bravo')] + [('client%d' % k, NAMES[k]) for k in range(2, 16)])


def hits_with_src(lg):
    out, last = [], {}
    for t, target, rest in lg[1]:
        kv = dict(x.split('=', 1) for x in rest.split() if '=' in x)
        if kv.get('wtype') != '1':
            continue
        key = (kv.get('wid'), target)
        if key in last and t - last[key] < 1000:
            continue
        last[key] = t
        out.append((t, target, kv.get('src', '?').rstrip('0123456789')))
    return out


def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(q * len(v)))] if v else float('nan')


def main():
    vs_host, vs_shooter, vs_host_shooter = [], {}, []
    for run in sys.argv[1:]:
        logs = {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = A.load(os.path.join(run, f))
        H = {r: hits_with_src(lg) for r, lg in logs.items()}
        used = {r: set() for r in logs}
        for t, tgt, src in H['host']:
            shooter = [r for r, n in ROLE_NAMES.items() if n == src]
            times = {'host': t}
            for r in logs:
                if r == 'host':
                    continue
                best = None
                for j, (tc, tg, sc) in enumerate(H[r]):
                    if j in used[r] or tg != tgt or abs(tc - t) > 1500:
                        continue
                    if best is None or abs(tc - t) < abs(H[r][best][0] - t):
                        best = j
                if best is not None:
                    used[r].add(best)
                    times[r] = H[r][best][0]
                    vs_host.append(times[r] - t)
                    if src == 'Ace':
                        vs_host_shooter.append(times[r] - t)
            if shooter and shooter[0] in times:
                ts = times[shooter[0]]
                for r, tt in times.items():
                    if r == shooter[0]:
                        continue
                    kind = 'host' if r == 'host' else ('target owner' if ROLE_NAMES[r] == tgt else 'third party')
                    vs_shooter.setdefault(kind, []).append(tt - ts)
    print('client - host: n=%d median %+d ms, 10th %+d, 90th %+d' % (
        len(vs_host), pct(vs_host, 0.5), pct(vs_host, 0.1), pct(vs_host, 0.9)))
    if vs_host_shooter:  # none with a dedicated server
        print('client - host, torpedoes the host fired (the host shows its own torpedo): n=%d median %+d ms, 10th %+d, '
              '90th %+d' % (len(vs_host_shooter), pct(vs_host_shooter, 0.5), pct(vs_host_shooter, 0.1),
                            pct(vs_host_shooter, 0.9)))
    for kind in ('host', 'target owner', 'third party'):
        v = vs_shooter.get(kind, [])
        if v:
            print('%-12s - shooter: n=%d median %+d ms, 10th %+d, 90th %+d' % (
                kind, len(v), pct(v, 0.5), pct(v, 0.1), pct(v, 0.9)))


if __name__ == '__main__':
    main()
