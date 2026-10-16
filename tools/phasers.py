"""Phaser hits per shooter, target and peer (6-player runs, test pilot with src= in H lines).

Usage: python -I phasers.py [--shooters ai|players] <run dir> [more run dirs]
A "beam" is a series of phaser hit events of one shooter on one target, at most 400 ms apart,
as one instance logs it. For every beam the shooter's own machine shows: did each other peer show
a beam of the same shooter on the same target overlapping it (+-700 ms)? Also the reverse: beams
that other peers show and the shooter does not. Lists the shooter-only beams by length.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402

# ship names of the test pilot (mptest.py): the host's default name, then the clients' -Name
NAMES = ['Alpha', 'Bravo', 'Charlie', 'Delta', 'Echo', 'Foxtrot', 'Golf', 'Hotel', 'India', 'Juliett', 'Kilo', 'Lima', 'Mike', 'November', 'Oscar', 'Papa']
ROLE_NAMES = dict([('host', 'Ace'), ('client', 'Bravo')] + [('client%d' % k, NAMES[k]) for k in range(2, 16)])


def base_name(n):
    """a ship's name without the respawn suffix (Bravo1 -> Bravo); AI ships keep theirs (AI3)"""
    return n if n.startswith('AI') else n.rstrip('0123456789')


def owns(role, name):
    """the instance that flies the ship: the player's own; the host flies every other ship (AI
    ships, the starbase of Mission 5)"""
    return name == ROLE_NAMES.get(role) or (role == 'host' and name not in ROLE_NAMES.values())


def beams(path):
    ev = {}
    for line in open(path, errors='replace'):
        p = line.split()
        if len(p) >= 9 and p[0] == 'H' and 'wtype=0' in line:
            kv = dict(x.split('=', 1) for x in p[3:] if '=' in x)
            src = kv.get('src', '?')
            if src == '?':
                continue
            ev.setdefault((base_name(src), base_name(p[2])), []).append(int(p[1]))
    out = {}
    for k, ts in ev.items():
        ts.sort()
        b = []
        for t in ts:
            if b and t - b[-1][1] <= 400:
                b[-1][1] = t
                b[-1][2] += 1
            else:
                b.append([t, t, 1])
        out[k] = b
    return out


def overlaps(b, lst, slack=700):
    return any(x[0] - slack <= b[1] and b[0] - slack <= x[1] for x in lst)


def main():
    tot = {'shooter': 0, 'seen': 0, 'missing': 0, 'others_only': 0, 'by_host': 0, 'host_missing': 0,
           'host_beams': 0, 'host_beams_missing': 0, 'others_only_host': 0}
    lens_missing, lens_seen, lens_others = [], [], []
    args = sys.argv[1:]
    shooters = 'all'
    if '--shooters' in args:
        shooters = args[args.index('--shooters') + 1]
        args.remove(shooters)

    def kind(src):
        return shooters == 'all' or (shooters == 'ai') == (src not in ROLE_NAMES.values())
    for run in [a for a in args if not a.startswith('--')]:
        w0, w1 = analyze.common_window(run)
        logs = {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = beams(os.path.join(run, f))
        for shooter_role, name in ROLE_NAMES.items():
            if shooter_role not in logs:
                continue
            own = {k: v for k, v in logs[shooter_role].items() if owns(shooter_role, k[0]) and kind(k[0])}
            for (src, tgt), bl in own.items():
                for b in bl:
                    if not w0 <= b[0] <= w1:
                        continue
                    for r, lg in logs.items():
                        if r == shooter_role:
                            continue
                        tot['shooter'] += 1
                        hit = overlaps(b, lg.get((src, tgt), []))
                        if r == 'host':
                            tot['by_host'] += 1
                            tot['host_missing'] += not hit
                        if shooter_role == 'host':
                            tot['host_beams'] += 1
                            tot['host_beams_missing'] += not hit
                        if hit:
                            tot['seen'] += 1
                            lens_seen.append((b[1] - b[0], b[2]))
                        else:
                            tot['missing'] += 1
                            lens_missing.append((b[1] - b[0], b[2]))
            for r, lg in logs.items():
                if r == shooter_role:
                    continue
                for (src, tgt), bl in lg.items():
                    if not owns(shooter_role, src) or not kind(src):
                        continue
                    for b in bl:
                        if not w0 <= b[0] <= w1:
                            continue
                        if not overlaps(b, own.get((src, tgt), [])):
                            tot['others_only'] += 1
                            tot['others_only_host'] += r == 'host'
                            lens_others.append((b[1] - b[0], b[2]))
    n = max(tot['shooter'], 1)
    print('beams of the shooter x other peers: %d; also shown %d (%.1f %%), missing %d (%.1f %%); '
          'beams other peers show and the shooter does not: %d' % (
              tot['shooter'], tot['seen'], 100.0 * tot['seen'] / n, tot['missing'], 100.0 * tot['missing'] / n,
              tot['others_only']))

    print('  client shooters\' beams the host does not show (no damage): %d of %d (%.1f %%); host\'s beams a '
          'client does not show: %d of %d (%.1f %%)' % (
              tot['host_missing'], tot['by_host'], 100.0 * tot['host_missing'] / max(tot['by_host'], 1),
              tot['host_beams_missing'], tot['host_beams'], 100.0 * tot['host_beams_missing'] / max(tot['host_beams'], 1)))

    def hist(v):
        h = {}
        for d, k in v:
            key = '1 event' if k == 1 else ('<=1 s' if d <= 1000 else '>1 s')
            h[key] = h.get(key, 0) + 1
        return h
    print('  missing beams by length:', hist(lens_missing), ' shown beams:', hist(lens_seen))
    print('  beams other peers show and the shooter does not: %d on the host (damage); by length:' % tot['others_only_host'],
          hist(lens_others))


if __name__ == '__main__':
    main()
