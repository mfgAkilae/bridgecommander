"""Generate address->name lists for stbc.exe v1.1 to feed into Ghidra.

1. SWIG PyMethodDef tables: {char* name, PyCFunction, int flags, char* doc}
2. Event-handler registrations FUN_006da130(handler, "Class::Handler") parsed from decomp.

Output: names.txt lines "<hex addr> <name>"
"""
import re
import struct
import sys

EXE = sys.argv[1]
DECOMP = sys.argv[2]
OUT = sys.argv[3]

d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
IB = 0x400000
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    name = d[o:o + 8].rstrip(b'\0').decode()
    vs, va, rs, ra = struct.unpack_from('<IIII', d, o + 8)
    secs.append((name, va, vs, ra, rs))


def va2off(va):
    rva = va - IB
    for name, sva, vs, ra, rs in secs:
        if sva <= rva < sva + max(vs, rs):
            off = ra + (rva - sva)
            if off < ra + rs:
                return off
    return None


def in_sec(va, nm):
    rva = va - IB
    for name, sva, vs, ra, rs in secs:
        if name == nm and sva <= rva < sva + vs:
            return True
    return False


def cstr(va, maxlen=200):
    off = va2off(va)
    if off is None:
        return None
    e = d.find(b'\0', off, off + maxlen)
    if e < 0:
        return None
    s = d[off:e]
    if not s or any(c < 0x20 or c > 0x7e for c in s):
        return None
    return s.decode()


names = {}
# --- SWIG method tables in .data
for name, sva, vs, ra, rs in secs:
    if name != '.data':
        continue
    for off in range(ra, ra + rs - 16, 4):
        np_, fp, fl, dp = struct.unpack_from('<IIII', d, off)
        if not in_sec(fp, '.text'):
            continue
        if fl not in (0, 1):
            continue
        s = cstr(np_)
        if not s or not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', s) or re.match(r'^[A-Z0-9_]+$', s):
            continue
        names.setdefault(fp, 'py_' + s)

# --- handler registrations from decompiled code
txt = open(DECOMP, encoding='utf-8', errors='replace').read()
fidx = {}
for line in open(DECOMP + '.idx'):
    n_, a_, o_ = line.rsplit(' ', 2)
    fidx[n_] = int(a_, 16)
for m in re.finditer(r'FUN_006da130\(&?(\w+),s_\w+?_([0-9a-f]{8})\)', txt):
    g = m.group(1)
    mm = re.match(r'(?:LAB|DAT|FUN)_([0-9a-f]{8})$', g)
    if mm:
        fa = int(mm.group(1), 16)
    elif g in fidx:
        fa = fidx[g]
    else:
        print('unresolved', g)
        continue
    s = cstr(int(m.group(2), 16))
    if s:
        nm = re.sub(r'[^A-Za-z0-9_]+', '_', s.replace('::', '__')).strip('_')
        names[fa] = nm

with open(OUT, 'w') as f:
    for a in sorted(names):
        f.write('%08x %s\n' % (a, names[a]))
print('names', len(names))
