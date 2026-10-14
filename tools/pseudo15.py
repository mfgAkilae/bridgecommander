"""Pseudo-source generator for Python 1.5 bytecode (BC scripts).

Turns stack code into one statement per source line, with explicit gotos for
control flow. Not a full decompiler, but much easier to read than raw dis.

Usage: python -I pseudo15.py <file.pyc|dir> [outdir]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dis15 import (Code, load, s, const_repr, OPNAMES, HAVE_ARGUMENT, REL_JUMPS,  # noqa: E402
                   ABS_JUMPS, CMP_OPS)

BINOPS = {19: '**', 20: '*', 21: '/', 22: '%', 23: '+', 24: '-', 62: '<<', 63: '>>',
          64: '&', 65: '^', 66: '|'}
UNOPS = {10: '+', 11: '-', 12: 'not ', 15: '~'}


class Special(str):
    """Marker value on the simulated stack."""


def decode(code):
    i = 0
    out = []
    while i < len(code):
        op = code[i]
        if op >= HAVE_ARGUMENT:
            arg = code[i + 1] | (code[i + 2] << 8)
            out.append((i, op, arg, i + 3))
            i += 3
        else:
            out.append((i, op, None, i + 1))
            i += 1
    return out


def pseudo(c, out, indent=''):
    args = []
    if c.varnames:
        args = [s(v) for v in c.varnames[:c.argcount]]
        if c.flags & 4:
            args.append('*' + s(c.varnames[c.argcount]))
        if c.flags & 8:
            args.append('**' + s(c.varnames[c.argcount + (1 if c.flags & 4 else 0)]))
    out.append('%sdef %s(%s):    # line %d' % (indent, s(c.name), ', '.join(args), c.firstlineno))
    ind = indent + '    '
    instrs = decode(c.code)
    targets = set()
    for off, op, arg, nxt in instrs:
        if op in REL_JUMPS:
            targets.add(nxt + arg)
        elif op in ABS_JUMPS:
            targets.add(arg)

    stack = []
    snapshots = {}
    nested = []           # (name, code) to print after
    unpack = None         # [names, n, source]
    lineno = c.firstlineno
    defaults_of = {}

    def emit(text):
        out.append('%s%-70s # L%d' % (ind, text, lineno))

    def pop():
        if stack:
            return stack.pop()
        return Special('<?>')

    def name_of(op, arg):
        if op in (124, 125, 126):
            return s(c.varnames[arg])
        return s(c.names[arg])

    def store(target, value):
        nonlocal unpack
        if unpack is not None:
            unpack[0].append(target)
            if len(unpack[0]) == unpack[1]:
                lhs = ', '.join(unpack[0])
                if unpack[2].startswith('<for '):
                    emit('for %s in %s:' % (lhs, unpack[2][5:-1]))
                else:
                    emit('%s = %s' % (lhs, unpack[2]))
                unpack = None
            return
        if isinstance(value, Special):
            if value.startswith('<for '):
                emit('for %s in %s:' % (target, value[5:-1]))
                return
            if value.startswith('<def '):
                nm = value[5:-1]
                emit('def %s = <function %s>' % (target, nm))
                return
            if value.startswith('<class '):
                emit('class %s = %s' % (target, value))
                return
            if value.startswith('<import '):
                emit('import %s  -> %s' % (value[8:-1], target))
                return
        emit('%s = %s' % (target, value))

    for off, op, arg, nxt in instrs:
        if off in targets:
            if off in snapshots and not stack:
                stack = list(snapshots[off])
            out.append('%s  L%d:' % (indent, off))
        if op == 127:
            lineno = arg
            continue
        if op == 100:
            k = c.consts[arg]
            if isinstance(k, Code):
                nested.append(k)
                stack.append(Special('<code %s>' % s(k.name)))
            else:
                stack.append(const_repr(k))
        elif op in (101, 116, 124):
            stack.append(name_of(op, arg))
        elif op == 105:
            stack.append('%s.%s' % (pop(), name_of(op, arg)))
        elif op in BINOPS:
            b = pop()
            a = pop()
            stack.append('(%s %s %s)' % (a, BINOPS[op], b))
        elif op in UNOPS:
            stack.append('%s%s' % (UNOPS[op], pop()))
        elif op == 13:
            stack.append('`%s`' % pop())
        elif op == 25:
            b = pop()
            a = pop()
            stack.append('%s[%s]' % (a, b))
        elif op in (30, 31, 32, 33):
            hi = lo = ''
            if op in (32, 33):
                hi = pop()
            if op in (31, 33):
                lo = pop()
            stack.append('%s[%s:%s]' % (pop(), lo, hi))
        elif op in (40, 41, 42, 43):
            hi = lo = ''
            if op in (42, 43):
                hi = pop()
            if op in (41, 43):
                lo = pop()
            obj = pop()
            emit('%s[%s:%s] = %s' % (obj, lo, hi, pop()))
        elif op in (50, 51, 52, 53):
            hi = lo = ''
            if op in (52, 53):
                hi = pop()
            if op in (51, 53):
                lo = pop()
            emit('del %s[%s:%s]' % (pop(), lo, hi))
        elif op == 106:
            b = pop()
            a = pop()
            stack.append('(%s %s %s)' % (a, CMP_OPS[arg] if arg < len(CMP_OPS) else '?', b))
        elif op == 131:
            npos = arg & 0xff
            nkw = arg >> 8
            kws = []
            for _ in range(nkw):
                v = pop()
                k = pop()
                kws.insert(0, '%s=%s' % (k.strip("'"), v))
            pos = [pop() for _ in range(npos)][::-1]
            f = pop()
            stack.append('%s(%s)' % (f, ', '.join(pos + kws)))
        elif op == 102:
            items = [pop() for _ in range(arg)][::-1]
            stack.append('(%s%s)' % (', '.join(items), ',' if arg == 1 else ''))
        elif op == 103:
            items = [pop() for _ in range(arg)][::-1]
            stack.append('[%s]' % ', '.join(items))
        elif op == 104:
            stack.append(Special('{}'))
        elif op == 60:
            key = pop()
            obj = pop()
            val = pop()
            if isinstance(obj, Special) and obj.startswith('{'):
                # dict literal being built: DUP_TOP left a copy below
                inner = obj[1:-1]
                newd = Special('{%s%s%s: %s}' % (inner, ', ' if inner else '', key, val))
                # replace the remaining copy of the dict on the stack
                for idx in range(len(stack) - 1, -1, -1):
                    if stack[idx] is obj or stack[idx] == obj:
                        stack[idx] = newd
                        break
            else:
                emit('%s[%s] = %s' % (obj, key, val))
        elif op == 61:
            key = pop()
            emit('del %s[%s]' % (pop(), key))
        elif op in (90, 97, 125):
            store(name_of(op, arg), pop())
        elif op == 95:
            obj = pop()
            store('%s.%s' % (obj, name_of(op, arg)), pop())
        elif op in (91, 98, 126):
            emit('del %s' % name_of(op, arg))
        elif op == 96:
            emit('del %s.%s' % (pop(), name_of(op, arg)))
        elif op in (92, 93):
            src = pop()
            unpack = [[], arg, src]
        elif op == 1:
            v = pop()
            if isinstance(v, Special):
                if v.startswith('<import '):
                    continue
                if v in ('<cond>', '<exc>', '<val>', '<tb>'):
                    continue
            emit(str(v))
        elif op == 2:
            a = pop()
            b = pop()
            stack += [a, b]
        elif op == 3:
            a = pop()
            b = pop()
            cc = pop()
            stack += [a, cc, b]
        elif op == 4:
            v = stack[-1] if stack else Special('<?>')
            stack.append(v)
        elif op == 71:
            emit('print %s,' % pop())
        elif op == 72:
            emit('print')
        elif op == 83:
            emit('return %s' % pop())
        elif op == 80:
            emit('break')
        elif op == 82:
            stack.append(Special('locals()'))
        elif op == 85:
            g = pop()
            loc = pop()
            emit('exec %s in %s, %s' % (pop(), loc, g))
        elif op == 87:
            emit('pop_block')
        elif op == 88:
            emit('end_finally')
        elif op == 89:
            methods = pop()
            bases = pop()
            name = pop()
            stack.append(Special('<class %s%s>' % (name.strip("'"), bases)))
        elif op == 107:
            stack.append(Special('<import %s>' % name_of(op, arg)))
        elif op == 108:
            mod = stack[-1] if stack else '?'
            emit('from %s import %s' % (str(mod)[8:-1], name_of(op, arg)))
        elif op in (111, 112):
            tgt = nxt + arg
            cond = stack[-1] if stack else '<?>'
            word = 'if not' if op == 111 else 'if'
            emit('%s %s: goto L%d' % (word, cond, tgt))
            if stack:
                stack[-1] = Special('<cond>')
            snapshots.setdefault(tgt, list(stack))
        elif op == 110:
            tgt = nxt + arg
            emit('goto L%d' % tgt)
            snapshots.setdefault(tgt, list(stack))
            stack = []
        elif op == 113:
            emit('goto L%d' % arg)
            stack = []
        elif op == 114:
            tgt = nxt + arg
            idx = pop()
            seq = pop()
            snapshots.setdefault(tgt, list(stack))
            stack += [seq, idx, Special('<for %s>' % seq)]
        elif op == 120:
            emit('loop:  (exit L%d)' % (nxt + arg))
            snapshots.setdefault(nxt + arg, list(stack))
        elif op == 121:
            tgt = nxt + arg
            emit('try:  (except L%d)' % tgt)
            snapshots.setdefault(tgt, list(stack) + [Special('<tb>'), Special('<val>'), Special('<exc>')])
        elif op == 122:
            emit('try:  (finally L%d)' % (nxt + arg))
        elif op == 130:
            items = [pop() for _ in range(arg)][::-1]
            emit('raise %s' % ', '.join(items))
        elif op == 132:
            code_ = pop()
            defs = [pop() for _ in range(arg)][::-1]
            nm = code_[6:-1] if isinstance(code_, Special) else str(code_)
            if defs:
                defaults_of[nm] = defs
            stack.append(Special('<def %s>' % nm))
        elif op == 133:
            items = [pop() for _ in range(arg)][::-1]
            stack.append(':'.join(items))
        elif op == 0:
            pass
        else:
            emit('?? %s %s' % (OPNAMES.get(op, op), arg))
    for k in nested:
        out.append('')
        if s(k.name) in defaults_of:
            out.append('%s# defaults: %s' % (ind, ', '.join(defaults_of[s(k.name)])))
        pseudo(k, out, ind)


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
                dst = os.path.join(outdir, rel[:-4] + '.pseudo.py')
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                out = []
                try:
                    pseudo(load(p), out)
                except Exception as e:
                    out.append('ERROR %r' % e)
                    print('ERR', rel, repr(e))
                open(dst, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    else:
        out = []
        pseudo(load(src), out)
        print('\n'.join(out))


if __name__ == '__main__':
    main()
