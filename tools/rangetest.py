"""Phaser range and target switch tests (mptest.py --range / --switch): the phaser hits of the
host's ship as the host and each client show them.

Usage: python -I rangetest.py <run dir> [more]
--range: the host's ship stands still and fires at RT, which the host moves to each distance for
15 s; per distance, the hits on RT. --switch: it keeps firing while RT1 and RT2 take turns ahead
of it (the other one behind it, out of the arcs), every 12 s; per step, the hits on each of them.
Counts the H lines (src=Ace) from 2 s after each move to its end.
"""
import os
import re
import sys


def lines(path, kinds):
    out = []
    for line in open(path, errors='replace'):
        p = line.split()
        if p and p[0] in kinds:
            out.append(p)
    return out


def main():
    for run in sys.argv[1:]:
        host = os.path.join(run, 'tlog_host.txt')
        xs = lines(host, ('X',))
        steps = [(int(p[2]), float(p[3])) for p in xs if len(p) >= 4 and p[1] == 'RANGE']
        switch = [(int(p[2]), p[3]) for p in xs if len(p) >= 4 and p[1] == 'SWITCH']
        if not steps and not switch:
            print(run, 'no RANGE or SWITCH steps')
            continue
        logs = {}
        for f in sorted(os.listdir(run)):
            m = re.match(r'tlog_(host|client\d*)\.txt$', f)
            if m:
                logs[m.group(1)] = [(int(p[1]), p[2], ' '.join(p[3:])) for p in lines(os.path.join(run, f), ('H',))]
        roles = ['host'] + sorted(r for r in logs if r != 'host')
        print('=== %s' % os.path.basename(run))
        if switch:
            print('  %-14s' % 'ahead / target' + ''.join('%10s' % r for r in roles))
            tot = {}
            for i, (t, name) in enumerate(switch):
                t1 = switch[i + 1][0] if i + 1 < len(switch) else t + 12000
                for tgt in ('RT1', 'RT2'):
                    row = [sum(1 for (th, tg, rest) in logs[r] if t + 2000 <= th < t1 and tg == tgt and
                               'wtype=0' in rest and 'src=Ace' in rest) for r in roles]
                    kind = 'ahead' if tgt == name else 'behind'
                    for r, n in zip(roles, row):
                        tot[(kind, r)] = tot.get((kind, r), 0) + n
                    print('  %-14s' % ('%s / %s' % (name, tgt)) + ''.join('%10d' % n for n in row))
            for kind in ('ahead', 'behind'):
                print('  %-14s' % ('total ' + kind) + ''.join('%10d' % tot.get((kind, r), 0) for r in roles))
            continue
        print('  %-9s' % 'distance' + ''.join('%10s' % r for r in roles))
        for i, (t, d) in enumerate(steps):
            t1 = steps[i + 1][0] if i + 1 < len(steps) else t + 15000
            row = []
            for r in roles:
                n = sum(1 for (th, tgt, rest) in logs[r]
                        if t + 2000 <= th < t1 and tgt.startswith('RT') and 'wtype=0' in rest and 'src=Ace' in rest)
                row.append(n)
            print('  %-9.0f' % d + ''.join('%10d' % n for n in row))


if __name__ == '__main__':
    main()
