"""Disassembler for Python 1.5 .pyc files (Star Trek Bridge Commander scripts).

Usage: python -I dis15.py <file.pyc|dir> [outdir]
Writes <name>.dis next to outdir mirror when a directory is given.
"""
import os
import struct
import sys

MAGIC_15 = 0x0A0D4E99

OPNAMES = {
    0: 'STOP_CODE', 1: 'POP_TOP', 2: 'ROT_TWO', 3: 'ROT_THREE', 4: 'DUP_TOP',
    10: 'UNARY_POSITIVE', 11: 'UNARY_NEGATIVE', 12: 'UNARY_NOT', 13: 'UNARY_CONVERT',
    15: 'UNARY_INVERT',
    19: 'BINARY_POWER', 20: 'BINARY_MULTIPLY', 21: 'BINARY_DIVIDE', 22: 'BINARY_MODULO',
    23: 'BINARY_ADD', 24: 'BINARY_SUBTRACT', 25: 'BINARY_SUBSCR',
    30: 'SLICE+0', 31: 'SLICE+1', 32: 'SLICE+2', 33: 'SLICE+3',
    40: 'STORE_SLICE+0', 41: 'STORE_SLICE+1', 42: 'STORE_SLICE+2', 43: 'STORE_SLICE+3',
    50: 'DELETE_SLICE+0', 51: 'DELETE_SLICE+1', 52: 'DELETE_SLICE+2', 53: 'DELETE_SLICE+3',
    60: 'STORE_SUBSCR', 61: 'DELETE_SUBSCR', 62: 'BINARY_LSHIFT', 63: 'BINARY_RSHIFT',
    64: 'BINARY_AND', 65: 'BINARY_XOR', 66: 'BINARY_OR',
    71: 'PRINT_ITEM', 72: 'PRINT_NEWLINE',
    80: 'BREAK_LOOP', 82: 'LOAD_LOCALS', 83: 'RETURN_VALUE', 85: 'EXEC_STMT',
    87: 'POP_BLOCK', 88: 'END_FINALLY', 89: 'BUILD_CLASS',
    90: 'STORE_NAME', 91: 'DELETE_NAME', 92: 'UNPACK_TUPLE', 93: 'UNPACK_LIST',
    95: 'STORE_ATTR', 96: 'DELETE_ATTR', 97: 'STORE_GLOBAL', 98: 'DELETE_GLOBAL',
    100: 'LOAD_CONST', 101: 'LOAD_NAME', 102: 'BUILD_TUPLE', 103: 'BUILD_LIST',
    104: 'BUILD_MAP', 105: 'LOAD_ATTR', 106: 'COMPARE_OP', 107: 'IMPORT_NAME',
    108: 'IMPORT_FROM',
    110: 'JUMP_FORWARD', 111: 'JUMP_IF_FALSE', 112: 'JUMP_IF_TRUE', 113: 'JUMP_ABSOLUTE',
    114: 'FOR_LOOP', 116: 'LOAD_GLOBAL',
    120: 'SETUP_LOOP', 121: 'SETUP_EXCEPT', 122: 'SETUP_FINALLY',
    124: 'LOAD_FAST', 125: 'STORE_FAST', 126: 'DELETE_FAST', 127: 'SET_LINENO',
    130: 'RAISE_VARARGS', 131: 'CALL_FUNCTION', 132: 'MAKE_FUNCTION', 133: 'BUILD_SLICE',
}
HAVE_ARGUMENT = 90
REL_JUMPS = {110, 111, 112, 114, 120, 121, 122}
ABS_JUMPS = {113}
NAME_OPS = {90, 91, 95, 96, 97, 98, 101, 105, 107, 108, 116}
CMP_OPS = ['<', '<=', '==', '!=', '>', '>=', 'in', 'not in', 'is', 'is not',
           'exception match', 'BAD']


class Code:
    pass


class Reader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def byte(self):
        b = self.d[self.p]
        self.p += 1
        return b

    def short(self):
        v = struct.unpack_from('<h', self.d, self.p)[0]
        self.p += 2
        return v

    def long(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]
        self.p += 4
        return v

    def obj(self):
        t = chr(self.byte())
        if t == '0':
            return None
        if t == 'N':
            return None
        if t == '.':
            return Ellipsis
        if t == 'i':
            return self.long()
        if t == 'l':
            n = self.long()
            size = abs(n)
            val = 0
            for i in range(size):
                digit = struct.unpack_from('<H', self.d, self.p)[0]
                self.p += 2
                val |= digit << (15 * i)
            return -val if n < 0 else val
        if t == 'f':
            n = self.byte()
            s = self.d[self.p:self.p + n].decode('latin-1')
            self.p += n
            return float(s)
        if t == 'x':
            n = self.byte()
            re_ = float(self.d[self.p:self.p + n])
            self.p += n
            n = self.byte()
            im = float(self.d[self.p:self.p + n])
            self.p += n
            return complex(re_, im)
        if t == 's':
            n = self.long()
            s = self.d[self.p:self.p + n]
            self.p += n
            return s
        if t in '([':
            n = self.long()
            items = [self.obj() for _ in range(n)]
            return tuple(items) if t == '(' else items
        if t == '{':
            d = {}
            while True:
                k = self.obj_or_null()
                if k is NULL:
                    break
                d[k] = self.obj()
            return d
        if t == 'c':
            c = Code()
            c.argcount = self.short()
            c.nlocals = self.short()
            c.stacksize = self.short()
            c.flags = self.short()
            c.code = self.obj()
            c.consts = self.obj()
            c.names = self.obj()
            c.varnames = self.obj()
            c.filename = self.obj()
            c.name = self.obj()
            c.firstlineno = self.short()
            c.lnotab = self.obj()
            return c
        raise ValueError('unknown marshal type %r at %d' % (t, self.p - 1))

    def obj_or_null(self):
        if chr(self.d[self.p]) == '0':
            self.p += 1
            return NULL
        return self.obj()


NULL = object()


def s(b):
    if isinstance(b, bytes):
        return b.decode('latin-1')
    return b


def const_repr(c):
    if isinstance(c, Code):
        return '<code %s>' % s(c.name)
    if isinstance(c, bytes):
        return repr(c.decode('latin-1'))
    if isinstance(c, tuple):
        return '(' + ', '.join(const_repr(x) for x in c) + (',)' if len(c) == 1 else ')')
    return repr(c)


def disassemble(c, out, indent=''):
    out.append('%s# code %s(%s) line %d flags=0x%x' % (
        indent, s(c.name), ', '.join(s(v) for v in c.varnames[:c.argcount]) if c.varnames else '',
        c.firstlineno, c.flags))
    code = c.code
    i = 0
    n = len(code)
    targets = set()
    while i < n:
        op = code[i]
        if op >= HAVE_ARGUMENT:
            arg = code[i + 1] | (code[i + 2] << 8)
            if op in REL_JUMPS:
                targets.add(i + 3 + arg)
            elif op in ABS_JUMPS:
                targets.add(arg)
            i += 3
        else:
            i += 1
    i = 0
    while i < n:
        op = code[i]
        name = OPNAMES.get(op, 'OP_%d' % op)
        mark = '>>' if i in targets else '  '
        if op >= HAVE_ARGUMENT:
            arg = code[i + 1] | (code[i + 2] << 8)
            if op == 127:
                out.append('%s  ---- line %d' % (indent, arg))
                i += 3
                continue
            extra = ''
            if op == 100:
                extra = const_repr(c.consts[arg])
            elif op in NAME_OPS:
                extra = s(c.names[arg])
            elif op in (124, 125, 126):
                extra = s(c.varnames[arg]) if c.varnames else '?'
            elif op == 106:
                extra = CMP_OPS[arg] if arg < len(CMP_OPS) else '?'
            elif op in REL_JUMPS:
                extra = 'to %d' % (i + 3 + arg)
            elif op in ABS_JUMPS:
                extra = 'to %d' % arg
            elif op == 131:
                extra = '%d pos, %d kw' % (arg & 0xff, arg >> 8)
            out.append('%s%s %5d %-16s %5d  %s' % (indent, mark, i, name, arg, extra))
            i += 3
        else:
            out.append('%s%s %5d %s' % (indent, mark, i, name))
            i += 1
    for k in c.consts:
        if isinstance(k, Code):
            out.append('')
            disassemble(k, out, indent + '    ')


def load(path):
    data = open(path, 'rb').read()
    magic = struct.unpack_from('<I', data, 0)[0]
    if magic != MAGIC_15:
        raise ValueError('bad magic %08x' % magic)
    return Reader(data, 8).obj()


def main():
    src = sys.argv[1]
    if os.path.isdir(src):
        outdir = sys.argv[2]
        for root, _, files in os.walk(src):
            for f in files:
                if not f.endswith('.pyc'):
                    continue
                p = os.path.join(root, f)
                rel = os.path.relpath(p, src)
                dst = os.path.join(outdir, rel[:-4] + '.dis')
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                out = []
                try:
                    disassemble(load(p), out)
                except Exception as e:  # keep going over the whole tree
                    out.append('ERROR %r' % e)
                    print('ERR', rel, e)
                open(dst, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    else:
        out = []
        disassemble(load(src), out)
        print('\n'.join(out))


if __name__ == '__main__':
    main()
