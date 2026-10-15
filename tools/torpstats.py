"""Sum the clients' torpedo replay statistics of a run (last stats line per client).

Usage: python -I torpstats.py <run dir> [more run dirs]
Prints per run: hits received, replayed, no_torpedo, expired, waiting/late_found, lost,
mean/max distance the replay moved the local copy (moved_*), and the DLL's
suppressed/allowed/pushed-out counters.
"""
import os
import re
import sys


def last_stats(path):
    st = None
    for line in open(path, errors='replace'):
        if 'bcmpfix_mp stats' in line:
            st = line
    return st


def main():
    for run in sys.argv[1:]:
        tot = {}
        torp = [0, 0, 0]
        n = 0
        for f in sorted(os.listdir(run)):
            if not re.match(r'dll_client\d*\.log$', f):
                continue
            line = last_stats(os.path.join(run, f))
            if not line:
                continue
            n += 1
            for k, v in re.findall(r"'(\w+)': (\d+)", line):
                tot[k] = tot.get(k, 0) + int(v)
            m = re.search(r'torp\(suppressed allowed\)=(\d+) (\d+)(?: (\d+))?', line)
            if m:
                for i in range(3):
                    torp[i] += int(m.group(i + 1) or 0)
        mn = tot.get('moved_n', 0)
        print('%s: %d clients, hits_recv %d replayed %d forced %d no_torpedo %d expired %d waiting %d late_found %d '
              'lost %d | moved mean %.2f max %.2f (n=%d) | suppressed %d allowed %d pushed %d' % (
                  os.path.basename(os.path.normpath(run)), n, tot.get('hits_recv', 0), tot.get('replayed', 0),
                  tot.get('forced', 0), tot.get('no_torpedo', 0), tot.get('expired', 0), tot.get('waiting', 0),
                  tot.get('late_found', 0), tot.get('lost', 0),
                  tot.get('moved_sum', 0) / 100.0 / mn if mn else float('nan'),
                  tot.get('moved_max', 0) / 100.0, mn, torp[0], torp[1], torp[2]))


if __name__ == '__main__':
    main()
