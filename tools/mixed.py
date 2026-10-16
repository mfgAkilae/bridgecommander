"""Mixed games: sync quality by the bcmpfix version of each instance (mptest.py --variant).

Usage: python -I mixed.py <run dir> [more run dirs]
Reads variants.json of each run (role -> 'off' = original game, 'current' = the game folder's
build, or a release vX.Y.Z) and reports, while every instance is in the game:
  positions   mean / 95th percentile error of a ship as a viewer sees it, by (owner, viewer) version
  torpedoes   the host's torpedo hits that each client also showed, by client version
  phasers     bank firings and beams of a shooter that each other peer also showed, by
              (shooter, peer) version, and client beams the host showed (the host applies damage)
Several runs are added up.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A  # noqa: E402
import banks as B  # noqa: E402
import phasers as P  # noqa: E402


def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * q))] if v else float('nan')


def roles_of(run):
    return json.load(open(os.path.join(run, 'variants.json')))


def main():
    pos, torp, bank, beam, dmg = {}, {}, {}, {}, {}
    for run in sys.argv[1:]:
        var = roles_of(run)
        w0, w1 = A.common_window(run)
        logs, raw_b, raw_p = {}, {}, {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                path = os.path.join(run, f)
                logs[m.group(1)] = A.load(path)
                raw_b[m.group(1)] = B.intervals(path)
                raw_p[m.group(1)] = P.beams(path)
        A.trim(logs)
        # positions: owner = the instance that logs the ship as its own most often
        owners, nlocal = {}, {}
        for role, lg in logs.items():
            for name, tr in lg[0].items():
                n = sum(1 for x in tr if x[4])
                if n > nlocal.get(name, 0):
                    owners[name], nlocal[name] = role, n
        for name, owner in owners.items():
            ref = [x for x in logs[owner][0][name] if x[4]]
            dead = [d[0] for r in logs for d in logs[r][2] if d[1] == name]
            for viewer, lg in logs.items():
                if viewer == owner or name not in lg[0]:
                    continue
                track = [x for x in lg[0][name] if w0 <= x[0] <= w1]
                if not track:
                    continue
                errs = A.view_errors(ref, track, dead)[0]
                pos.setdefault((var[owner], var[viewer]), []).extend(errs)
        # torpedoes: the host's hits in the window, matched per client
        hh = [h for h in A.torp_hits(logs['host'][1]) if w0 <= h[0] <= w1] if 'host' in logs else []
        for role, lg in logs.items():
            if role == 'host' or not hh:
                continue
            ch = [h for h in A.torp_hits(lg[1]) if w0 - 1500 <= h[0] <= w1 + 1500]
            pairs, _ = A.match_hits(hh, ch)
            same = sum(1 for i, j in pairs.items() if hh[i][2] == ch[j][2])
            s = torp.setdefault(var[role], [0, 0, 0])
            s[0] += len(hh)
            s[1] += len(pairs)
            s[2] += same
        # phasers
        for shooter, name in B.ROLE_NAMES.items():
            if shooter not in logs:
                continue
            own_b = {k: v for k, v in raw_b[shooter].items() if k[0] == name}
            own_p = {k: v for k, v in raw_p[shooter].items() if k[0] == name}
            for peer in logs:
                if peer == shooter:
                    continue
                key = (var[shooter], var[peer])
                sb = bank.setdefault(key, [0, 0])
                for k, lst in own_b.items():
                    for t0, t1, tgt in lst:
                        if w0 <= t0 <= w1:
                            sb[0] += 1
                            sb[1] += bool([x for x in raw_b[peer].get(k, []) if -300 <= x[0] - t0 <= 900])
                sp = beam.setdefault(key, [0, 0])
                for k, bl in own_p.items():
                    for b in bl:
                        if w0 <= b[0] <= w1:
                            sp[0] += 1
                            shown = P.overlaps(b, raw_p[peer].get(k, []))
                            sp[1] += shown
                            if peer == 'host':
                                sd = dmg.setdefault(var[shooter], [0, 0])
                                sd[0] += 1
                                sd[1] += shown
    print('positions (owner -> viewer): samples, mean / 95th percentile in game units')
    for k in sorted(pos):
        e = pos[k]
        print('  %-8s -> %-8s %7d  %.2f / %.2f' % (k[0], k[1], len(e), sum(e) / max(len(e), 1), pct(e, 0.95)))
    print('torpedo hits on the host that a client showed (by client version): shown, same shield/hull')
    for k in sorted(torp):
        n, m, s = torp[k]
        print('  %-8s %d of %d (%.1f %%), shield/hull same in %.1f %%' % (k, m, n, 100.0 * m / max(n, 1),
                                                                        100.0 * s / max(m, 1)))
    print('phasers (shooter -> peer): bank firings also on the peer, beams also shown by the peer')
    for k in sorted(set(bank) | set(beam)):
        nb, mb = bank.get(k, [0, 0])
        np_, mp = beam.get(k, [0, 0])
        print('  %-8s -> %-8s banks %d of %d (%.1f %%), beams %d of %d (%.1f %%)' % (
            k[0], k[1], mb, nb, 100.0 * mb / max(nb, 1), mp, np_, 100.0 * mp / max(np_, 1)))
    print('client beams the host showed (the host applies the damage), by shooter version')
    for k in sorted(dmg):
        n, m = dmg[k]
        print('  %-8s %d of %d (%.1f %%)' % (k, m, n, 100.0 * m / max(n, 1)))


if __name__ == '__main__':
    main()
