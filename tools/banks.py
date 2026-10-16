"""Phaser bank firing intervals: the shooter's own machine vs every other peer.

Usage: python -I banks.py [--polled] [--shooters ai|players] <run dir> [more]
For each firing interval of a bank on its owner's machine: did each other peer fire the same
bank in an overlapping interval (start within -300..+900 ms)? Start delay and duration difference
of the matched intervals; intervals only the shooter had, and intervals only a peer had.

The intervals come from the DLL's test telemetry when the run has it (PO: the owner's banks change,
PS / PE: another peer's copy of a bank starts / stops, all on the frame they happen). Otherwise from
the test pilot's B lines, which poll IsFiring every 50 ms: on the shooter's machine a bank starts in
the weapon update after the pilot's StartFiring and stops inside its StopFiring, so the polled start
comes about 40 ms late and the stop about 10 ms early, and a beam looks about 50 ms shorter there.
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
    """the instance that flies the ship: the player's own, the AI ships on the host"""
    return name == ROLE_NAMES.get(role) or (role == 'host' and name.startswith('AI'))


def intervals(path):
    on, out = {}, {}
    for line in open(path, errors='replace'):
        p = line.split()
        if len(p) >= 6 and p[0] == 'B':
            t, ship, bank, st = int(p[1]), base_name(p[2]), int(p[3]), int(p[4])
            k = (ship, bank)
            if st and k not in on:
                on[k] = (t, p[5])
            elif not st and k in on:
                t0, tgt = on.pop(k)
                out.setdefault(k, []).append((t0, t, tgt))
    return out


def ship_ids(path):
    """object ID -> ship name, from the S lines"""
    ids = {}
    for line in open(path, errors='replace'):
        p = line.split()
        if len(p) >= 7 and p[0] == 'S':
            try:
                ids[int(p[5])] = base_name(p[3])
            except ValueError:
                pass
    return ids


def intervals_exact(path, role):
    """the same intervals from the DLL telemetry, or None if the log has none"""
    ids = ship_ids(path)
    out, on, prev = {}, {}, {}
    seen = False  # a log with peer stops (PE, since v1.7.3); older runs use the B lines
    for line in open(path, errors='replace'):
        p = line.split()
        if not p or p[0] not in ('PO', 'PS', 'PE'):
            continue
        if p[0] == 'PE':
            seen = True
        t, ship = int(p[1]), int(p[2])
        name = ids.get(ship)
        if not name:
            continue
        if p[0] == 'PO' and owns(role, name):
            bits, pb = int(p[3]), prev.get(ship, 0)
            prev[ship] = bits
            for i in range(32):
                k = (name, i)
                if bits & ~pb & (1 << i):
                    on[k] = t
                elif pb & ~bits & (1 << i) and k in on:
                    out.setdefault(k, []).append((on.pop(k), t, ''))
        elif p[0] in ('PS', 'PE') and not owns(role, name):
            k = (name, int(p[3]))
            if p[0] == 'PS':
                on[k] = t
            elif k in on:
                out.setdefault(k, []).append((on.pop(k), t, ''))
    return out if seen else None


def main():
    n = matched = only_shooter = only_peer = 0
    source = set()
    delays, ddur, short_missing, short_all = [], [], 0, 0
    args = sys.argv[1:]
    polled = '--polled' in args  # the B lines even when the run has telemetry
    shooters = 'all'
    if '--shooters' in args:
        shooters = args[args.index('--shooters') + 1]
        args.remove(shooters)
    runs = [a for a in args if not a.startswith('--')]
    for run in runs:
        w0, w1 = analyze.common_window(run)
        logs = {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = intervals_exact(os.path.join(run, f), m.group(1))
        if polled or any(v is None for v in logs.values()):
            for f in os.listdir(run):
                m = re.match(r'tlog_(host|client\d*)\.txt$', f)
                if m:
                    logs[m.group(1)] = intervals(os.path.join(run, f))
            source.add('polled B lines')
        else:
            source.add('DLL telemetry')
        for role, name in ROLE_NAMES.items():
            if role not in logs:
                continue
            own = {k: v for k, v in logs[role].items() if owns(role, k[0]) and
                   (shooters == 'all' or (shooters == 'ai') == k[0].startswith('AI'))}
            for k, lst in own.items():
                for t0, t1, tgt in lst:
                    if not w0 <= t0 <= w1:
                        continue
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
                    if not owns(role, k[0]) or (shooters != 'all' and (shooters == 'ai') != k[0].startswith('AI')):
                        continue
                    for t0, t1, tgt in lst:
                        if not w0 <= t0 <= w1:
                            continue
                        if not [x for x in own.get(k, []) if -300 <= t0 - x[0] <= 900]:
                            only_peer += 1
    delays.sort()
    ddur.sort()
    m = max(len(delays), 1)
    print('(%s)' % ', '.join(sorted(source)))
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
