"""Phaser bank firing intervals: the shooter's own machine vs every other peer (B lines).

Usage: python -I banks.py <run dir> [more]
For each firing interval of a bank on its owner's machine: did each other peer fire the same
bank in an overlapping interval (start within -300..+900 ms)? Start delay and duration difference
of the matched intervals; intervals only the shooter had, and intervals only a peer had.
"""
import os
import re
import sys

ROLE_NAMES = {'host': 'Ace', 'client': 'Bravo', 'client2': 'Charlie', 'client3': 'Delta', 'client4': 'Echo',
              'client5': 'Foxtrot'}


def intervals(path):
    on, out = {}, {}
    for line in open(path, errors='replace'):
        p = line.split()
        if len(p) >= 6 and p[0] == 'B':
            t, ship, bank, st = int(p[1]), p[2].rstrip('0123456789'), int(p[3]), int(p[4])
            k = (ship, bank)
            if st and k not in on:
                on[k] = (t, p[5])
            elif not st and k in on:
                t0, tgt = on.pop(k)
                out.setdefault(k, []).append((t0, t, tgt))
    return out


def main():
    n = matched = only_shooter = only_peer = 0
    delays, ddur, short_missing, short_all = [], [], 0, 0
    for run in sys.argv[1:]:
        logs = {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = intervals(os.path.join(run, f))
        for role, name in ROLE_NAMES.items():
            if role not in logs:
                continue
            own = {k: v for k, v in logs[role].items() if k[0] == name}
            for k, lst in own.items():
                for t0, t1, tgt in lst:
                    short = t1 - t0 < 300
                    for r, lg in logs.items():
                        if r == role:
                            continue
                        n += 1
                        short_all += short
                        cand = [x for x in lg.get(k, []) if -300 <= x[0] - t0 <= 900]
                        if cand:
                            matched += 1
                            delays.append(cand[0][0] - t0)
                            ddur.append((cand[0][1] - cand[0][0]) - (t1 - t0))
                        else:
                            only_shooter += 1
                            short_missing += short
            for r, lg in logs.items():
                if r == role:
                    continue
                for k, lst in lg.items():
                    if k[0] != name:
                        continue
                    for t0, t1, tgt in lst:
                        if not [x for x in own.get(k, []) if -300 <= t0 - x[0] <= 900]:
                            only_peer += 1
    delays.sort()
    ddur.sort()
    m = max(len(delays), 1)
    print('bank firings x other peers: %d; same bank fired %d (%.1f %%), only on the shooter %d (%.1f %%, '
          '%d of them shorter than 300 ms; short ones in total %d), only on a peer %d' % (
              n, matched, 100.0 * matched / max(n, 1), only_shooter, 100.0 * only_shooter / max(n, 1),
              short_missing, short_all, only_peer))
    if delays:
        print('  start delay on the peer: median %d ms, 10th %d, 90th %d; duration difference median %+d ms, '
              '10th %+d, 90th %+d' % (delays[m // 2], delays[m // 10], delays[m * 9 // 10], ddur[m // 2],
                                      ddur[m // 10], ddur[m * 9 // 10]))


if __name__ == '__main__':
    main()
