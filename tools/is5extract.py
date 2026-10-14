"""Minimal InstallShield 5 cabinet extractor (single volume, data1.hdr + data1.cab).

Usage: python -I is5extract.py <data1.hdr> <data1.cab> <outdir>
"""
import os
import struct
import sys
import zlib


def u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


def u16(d, o):
    return struct.unpack_from('<H', d, o)[0]


def cstr(d, o):
    e = d.index(b'\0', o)
    return d[o:e].decode('latin-1')


def inflate_chunks(data, expanded):
    out = bytearray()
    p = 0
    while p < len(data) and len(out) < expanded:
        n = u16(data, p)
        p += 2
        chunk = data[p:p + n]
        p += n
        d = zlib.decompressobj(-15)
        out += d.decompress(chunk + b'\0')
        out += d.flush()
    return bytes(out)


def main():
    hdr = open(sys.argv[1], 'rb').read()
    cab = open(sys.argv[2], 'rb')
    outdir = sys.argv[3]
    desc = u32(hdr, 0x0c)
    p = desc + 0xc
    ft_off = u32(hdr, p)
    dir_count = u32(hdr, desc + 0x1c)
    file_count = u32(hdr, desc + 0x28)
    base = desc + ft_off
    table = [u32(hdr, base + 4 * i) for i in range(dir_count + file_count)]
    dirs = [cstr(hdr, base + table[i]) for i in range(dir_count)]
    print('dirs', dir_count, 'files', file_count)
    ok = bad = 0
    for i in range(file_count):
        q = base + table[dir_count + i]
        name_off = u32(hdr, q)
        dir_idx = u32(hdr, q + 4)
        flags = u16(hdr, q + 8)
        exp = u32(hdr, q + 10)
        comp = u32(hdr, q + 14)
        data_off = u32(hdr, q + 0x12 + 0x14 + 4 - 4)
        name = cstr(hdr, base + name_off)
        if flags & 8:  # invalid
            continue
        dname = dirs[dir_idx] if dir_idx < len(dirs) else ''
        rel = os.path.join(dname, name).replace('\\', os.sep)
        dst = os.path.join(outdir, rel)
        os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
        cab.seek(data_off)
        raw = cab.read(comp)
        try:
            if flags & 4:  # compressed
                data = inflate_chunks(raw, exp)
            else:
                data = raw
            if len(data) != exp:
                raise ValueError('size mismatch %d != %d' % (len(data), exp))
            open(dst, 'wb').write(data)
            ok += 1
        except Exception as e:
            bad += 1
            print('FAIL', rel, flags, exp, comp, e)
    print('ok', ok, 'bad', bad)


if __name__ == '__main__':
    main()
