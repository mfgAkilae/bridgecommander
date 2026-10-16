"""Decode watchdog samples (WATCHDOG lines of bcmpfix.log) against stbc.exe.

Usage: python -I stackdecode.py <dll log> [more logs]
For each sample: the instruction pointer and, among the stack values, those that are return
addresses into stbc.exe (the bytes before them are a CALL), with the call's target.
"""
import re
import struct
import sys

import capstone

EXE = r'E:\dev\bc\game\stbc.exe'
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
sec = pe + 24 + opt
secs = [struct.unpack_from('<IIII', d, sec + 40 * i + 8) for i in range(nsec)]
vs, va0, rs, ro = secs[0]
code = d[ro:ro + rs]
base = 0x400000 + va0
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def in_code(a):
    return base <= a < base + len(code)


def call_before(a):
    """the CALL instruction that returns to a, or None"""
    for back in (5, 6, 2, 3, 7):
        o = a - back - base
        if o < 0:
            continue
        ins = list(md.disasm(code[o:o + back], a - back))
        if len(ins) == 1 and ins[0].mnemonic == 'call' and ins[0].address + ins[0].size == a:
            return ins[0]
    return None


def main():
    for path in sys.argv[1:]:
        for line in open(path, errors='replace'):
            m = re.search(r'WATCHDOG no frame for ([\d.]+) s: eip ([0-9a-f]+) \((.*?)\) esp [0-9a-f]+, stack:(.*)', line)
            if not m:
                continue
            print('--- %s s, eip %s (%s)' % (m.group(1), m.group(2), m.group(3)))
            for v in m.group(4).split():
                a = int(v, 16)
                if not in_code(a):
                    continue
                c = call_before(a)
                if c:
                    print('    ret %08x  <- %08x %s %s' % (a, c.address, c.mnemonic, c.op_str))


if __name__ == '__main__':
    main()
