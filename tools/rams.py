"""Ship-to-ship collisions (ramming test, mptest.py --ram) across all instances of a run.

Usage: python -I rams.py <run dir> [more run dirs]

The test pilot logs every collision its instance detects (C lines: both objects, the force, its own
ship) and, on the host, every collision a client reported and the host applied (CH lines). A
"contact" is a pair of ships with C lines on any instance within 2 s. For each contact:
- which instances detected it, above all the two owners of the ships (each owner flies its own ship
  exactly and the other one as a network copy);
- whether the host's hull values of the two ships dropped within 2 s (the host applies collision
  damage: its own ship's collisions directly, the clients' from their reports);
- the largest distance between each ship's owner position and its copy on the other instances in the
  3 s after the contact (the impact pushes the ship on its owner's screen; the copies follow late).
"""
import bisect
import math
import os
import re
import sys

CLUSTER_MS = 2000
deaths = []
DAMAGE_WINDOW_MS = 2000
POS_WINDOW_MS = 3000


def load(path):
    ships, coll, host_coll, ids, recv, applied = {}, [], [], {}, [], []
    global deaths
    for line in open(path, errors='replace'):
        p = line.split()
        if not p:
            continue
        if p[0] == 'D' and len(p) >= 3:
            deaths.append((int(p[1]), p[2]))
        if p[0] == 'CR' and len(p) >= 3:
            recv.append((int(p[1]), int(p[2])))
        elif p[0] == 'CA' and len(p) >= 4:
            applied.append((int(p[1]), p[2], p[3]))
        if p[0] == 'S' and len(p) >= 15:
            ids[p[5]] = p[3]
            ships.setdefault(p[3], []).append((int(p[1]), p[6] == '1', (float(p[7]), float(p[8]), float(p[9])), float(p[13])))
        elif p[0] in ('C', 'CH') and len(p) >= 5:
            m = re.search(r'force=(\S+)', line)
            try:
                f = float(m.group(1)) if m else 0.0
            except ValueError:
                f = 0.0
            (coll if p[0] == 'C' else host_coll).append((int(p[1]), p[2], p[3], f))
    return ships, coll, host_coll, ids, recv, applied


def at(track, t):
    ts = [x[0] for x in track]
    i = bisect.bisect_left(ts, t)
    if i <= 0 or i >= len(track):
        return None
    a, b = track[i - 1], track[i]
    if b[0] - a[0] > 500:
        return None
    f = (t - a[0]) / max(b[0] - a[0], 1)
    return tuple(a[2][k] + f * (b[2][k] - a[2][k]) for k in range(3))


def hull_at(track, t):
    ts = [x[0] for x in track]
    i = bisect.bisect_left(ts, t)
    if i >= len(track):
        i = len(track) - 1
    return track[i][3] if track else None


def main():
    for run in [a for a in sys.argv[1:] if not a.startswith('-')]:
        logs = {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = load(os.path.join(run, f))
        if 'host' not in logs:
            continue
        # owner of each ship name: the instance that flies it (local flag in most samples)
        owner = {}
        for role, (ships, _, _, _, _, _) in logs.items():
            for name, tr in ships.items():
                if sum(1 for x in tr if x[1]) > len(tr) / 2:
                    owner[name] = role
        names = set(owner)
        events = []
        for role, (_, coll, _, _, _, _) in logs.items():
            for t, a, b, f in coll:
                if a in names and b in names and a != b:
                    events.append((t, tuple(sorted((a, b))), role, f))
        events.sort()
        contacts = []
        for t, pair, role, f in events:
            for c in contacts:
                if c['pair'] == pair and t - c['last'] <= CLUSTER_MS:
                    c['last'] = t
                    c['seen'].setdefault(role, max(f, 0.0))
                    c['seen'][role] = max(c['seen'][role], f)
                    break
            else:
                contacts.append({'t': t, 'last': t, 'pair': pair, 'seen': {role: f}})
        host_ships = logs['host'][0]
        hcoll = logs['host'][2]
        ids = {}
        for role in logs:
            ids.update(logs[role][3])
        hrecv, happl = logs['host'][4], logs['host'][5]
        happl_named = [(t, ids.get(s, s), ids.get(d, d)) for t, s, d in happl]
        n_appl_matched = 0
        n_died = 0
        n = len(contacts)
        owners_both = owners_one = owners_none = damaged = reported = 0
        one_damaged = both_damaged = 0
        nodmg_seen_by_owner = 0
        peaks = []
        rows = []
        for c in contacts:
            a, b = c['pair']
            oa, ob = owner.get(a), owner.get(b)
            seen = c['seen']
            n_own = (oa in seen) + (ob in seen)
            if n_own == 2:
                owners_both += 1
            elif n_own == 1:
                owners_one += 1
            else:
                owners_none += 1
            drops = []
            for s in (a, b):
                tr = host_ships.get(s, [])
                h0, h1 = hull_at(tr, c['t'] - 300), hull_at(tr, c['t'] + DAMAGE_WINDOW_MS)
                drops.append(h0 is not None and h1 is not None and h1 < h0 - 0.5)
            dmg = drops[0] or drops[1]
            damaged += dmg
            both_damaged += drops[0] and drops[1]
            one_damaged += dmg and not (drops[0] and drops[1])
            rep = sum(1 for t, x, y in happl_named if -500 <= t - c['t'] <= CLUSTER_MS + 1000 and tuple(sorted((x, y))) == c['pair'])
            reported += rep > 0
            n_appl_matched += rep
            if not dmg and n_own:
                nodmg_seen_by_owner += 1
            # position: owner track vs every other instance's copy, 0..3 s after the contact
            peak = 0.0
            died = [s for s in (a, b) if any(c['t'] - 9000 <= td <= c['t'] + POS_WINDOW_MS and n == s for td, n in deaths)]
            for s in (a, b):
                o = owner.get(s)
                if not o or s in died:
                    continue  # a ship destroyed by the impact or a wreck (up to 9 s after its death): no comparable position
                own = [x for x in logs[o][0].get(s, []) if x[1]]
                for role, (ships, _, _, _, _, _) in logs.items():
                    if role == o or s not in ships:
                        continue
                    for t, loc, pos, hull in ships[s]:
                        if c['t'] <= t <= c['t'] + POS_WINDOW_MS:
                            q = at(own, t)
                            if q:
                                peak = max(peak, math.sqrt(sum((pos[k] - q[k]) ** 2 for k in range(3))))
            peaks.append(peak)
            n_died += len(died) > 0
            rows.append((c['t'], a, b, sorted(seen), '%s%s' % ('A' if drops[0] else '-', 'B' if drops[1] else '-'), rep, peak))
        print('== %s: ship-to-ship contacts %d' % (os.path.basename(run.rstrip('/\\')), n))
        if not n:
            continue
        print('  detected by both owners %d, by one owner %d, by neither owner %d' % (owners_both, owners_one, owners_none))
        print('  host hull dropped: %d of %d (both ships %d, one ship %d); contacts with a client report applied '
              'by the host %d (reports received %d, applied %d, applied ones matched to a contact %d)'
              % (damaged, n, both_damaged, one_damaged, reported, len(hrecv), len(happl), n_appl_matched))
        print('  no damage although an owner saw the contact: %d; contacts in which a ship was destroyed: %d'
              % (nodmg_seen_by_owner, n_died))
        peaks.sort()
        print('  largest copy-vs-owner distance within 3 s after a contact: median %.1f, 90th %.1f, max %.1f units'
              % (peaks[len(peaks) // 2], peaks[int(len(peaks) * 0.9)], peaks[-1]))
        if '-v' in sys.argv:
            for r in rows:
                print('   t=%d %s<->%s seen by %s damage %s report %d peak %.1f' % (r[0], r[1], r[2], ','.join(r[3]), r[4], r[5], r[6]))


if __name__ == '__main__':
    main()
