"""Print decompiled function(s) from decomp_all.c by name, address or regex.

Usage: python -I fn.py <name|0xaddr|/regex/> [more...]
"""
import os
import re
import sys

DECOMP = os.environ.get('DECOMP') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 're', 'decomp_all.c')
IDX = DECOMP + '.idx'


def build_index():
    idx = []
    with open(DECOMP, 'rb') as f:
        off = 0
        for line in f:
            if line.startswith(b'//==== '):
                m = re.match(rb'//==== (.*) @ ([0-9a-fA-F]+)', line)
                if m:
                    idx.append((m.group(1).decode('latin-1'), int(m.group(2), 16), off))
            off += len(line)
    with open(IDX, 'w') as f:
        for n, a, o in idx:
            f.write('%s %08x %d\n' % (n, a, o))
    return idx


def load_index():
    if not os.path.exists(IDX) or os.path.getmtime(IDX) < os.path.getmtime(DECOMP):
        return build_index()
    out = []
    for line in open(IDX):
        n, a, o = line.rsplit(' ', 2)
        out.append((n, int(a, 16), int(o)))
    return out


ETMAP = {}
_cp = os.path.join(os.path.dirname(DECOMP), 'consts_et.txt')
if os.path.exists(_cp):
    for _l in open(_cp):
        _a, _n = _l.split()
        ETMAP[int(_a, 16)] = _n


def annotate(text):
    def rep(m):
        v = int(m.group(1), 16)
        return ETMAP.get(v, m.group(0)) if v in ETMAP else m.group(0)
    text = re.sub(r'&?(?:DAT|LAB|FUN)_(008000[0-9a-f]{2}|0080010[0-9a-f])', lambda m: ETMAP.get(int(m.group(1), 16), m.group(0)), text)
    text = re.sub(r'0x(800[0-9a-f]{3})', lambda m: ETMAP.get(int(m.group(1), 16), m.group(0)), text)
    return text


def main():
    idx = load_index()
    offs = sorted(o for _, _, o in idx)
    nxt = {o: (offs[i + 1] if i + 1 < len(offs) else None) for i, o in enumerate(offs)}
    with open(DECOMP, 'rb') as f:
        qs = []
        for q in sys.argv[1:]:
            if q.startswith('@'):
                qs += [x.strip() for x in open(q[1:]) if x.strip()]
            elif q.startswith('range:'):
                lo, hi = [int(x, 16) for x in q[6:].split('-')]
                qs += [n for n, a, o in idx if lo <= a < hi]
            else:
                qs.append(q)
        for q in qs:
            hits = []
            if q.startswith('0x'):
                a = int(q, 16)
                # containing function: greatest start <= a
                best = None
                for n, fa, o in idx:
                    if fa <= a and (best is None or fa > best[1]):
                        best = (n, fa, o)
                hits = [best] if best else []
            elif q.startswith('re:'):
                r = re.compile(q[3:])
                hits = [h for h in idx if r.search(h[0])]
                if len(hits) > 40:
                    for h in hits:
                        print('%s @ %08x' % (h[0], h[1]))
                    continue
            else:
                hits = [h for h in idx if h[0] == q]
            for n, a, o in hits:
                f.seek(o)
                end = nxt[o]
                data = f.read((end - o) if end else -1)
                sys.stdout.write(annotate(data.decode('latin-1').replace('\r', '')))


if __name__ == '__main__':
    main()
