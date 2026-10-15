"""Where the phaser start delay comes from (test-mode telemetry of the phaser hit authority).

Usage: python -I phasertiming.py <run dir> [more run dirs]
Lines written by the DLL in test mode:
  PO t ship bits hits on   owner: its banks' state changed
  PT t ship bits hits on   owner: the script took the state to send it
  PR t ship bits hits on   other peer: a report arrived
  PS t ship bank           other peer: the bank started firing
  PH t ship bank           other peer: the beam reached its hit state
For every bank start and every hit on the owner's machine: the time until the report is sent, until
it arrives on each other peer, and until the bank starts (hits) there; by owner and peer role.
"""
import os
import re
import sys


def load(path):
    ev = []
    for line in open(path, errors='replace'):
        if line[:1] != 'P' or line[2:3] != ' ':
            continue
        p = line.split()
        if p[0] in ('PO', 'PT', 'PR') and len(p) >= 6:
            ev.append((p[0], int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5])))
        elif p[0] in ('PS', 'PH') and len(p) >= 4:
            ev.append((p[0], int(p[1]), int(p[2]), int(p[3])))
    return ev


def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * q))] if v else float('nan')


def first(lst, t0, cond, limit=1500):
    for e in lst:
        if e[1] >= t0 and e[1] - t0 <= limit and cond(e):
            return e[1]
    return None


def main():
    stats = {}
    for run in sys.argv[1:]:
        logs = {}
        for f in os.listdir(run):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = load(os.path.join(run, f))
        for owner, ev in logs.items():
            prev = {}
            for e in ev:
                if e[0] != 'PO':
                    continue
                _, t0, ship, bits, hits, on = e
                pb, ph = prev.get(ship, (0, 0))
                prev[ship] = (bits, hits)
                for kind, new, mask_i, rx in (('start', bits & ~pb, 3, 'PS'), ('hit', hits & ~ph, 4, 'PH')):
                    for i in range(32):
                        if not new & (1 << i):
                            continue
                        ts = first(ev, t0, lambda x: x[0] == 'PT' and x[2] == ship and x[mask_i] & (1 << i))
                        for peer, pev in logs.items():
                            if peer == owner:
                                continue
                            tr = first(pev, t0 - 50, lambda x: x[0] == 'PR' and x[2] == ship and x[mask_i] & (1 << i))
                            tx = first(pev, t0 - 50, lambda x: x[0] == rx and x[2] == ship and x[3] == i)
                            key = (kind, 'host' if owner == 'host' else 'client', 'host' if peer == 'host' else 'client')
                            s = stats.setdefault(key, {'n': 0, 'send': [], 'arrive': [], 'act': [], 'total': [], 'noarr': 0,
                                                       'noact': 0})
                            s['n'] += 1
                            if ts is not None:
                                s['send'].append(ts - t0)
                            if tr is None:
                                s['noarr'] += 1
                            else:
                                s['arrive'].append(tr - t0)
                            if tx is None:
                                s['noact'] += 1
                            else:
                                s['total'].append(tx - t0)
                                if tr is not None:
                                    s['act'].append(tx - tr)
    for key in sorted(stats):
        s = stats[key]
        print('%-5s owner %-6s peer %-6s n=%4d | send +%s | arrival med %s (10th %s, 90th %s) | %s after arrival: '
              'med %s | total med %s (10th %s, 90th %s) | no arrival %d, no %s %d' % (
                  key[0], key[1], key[2], s['n'], pct(s['send'], 0.5), pct(s['arrive'], 0.5), pct(s['arrive'], 0.1),
                  pct(s['arrive'], 0.9), key[0], pct(s['act'], 0.5), pct(s['total'], 0.5), pct(s['total'], 0.1),
                  pct(s['total'], 0.9), s['noarr'], key[0], s['noact']))


if __name__ == '__main__':
    main()
