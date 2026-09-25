"""Recover a struct layout, or a module's global statics, from the decompilation corpus in one
pass instead of one function at a time.

Two modes, both text-only (no Ghidra run):

  python tools/recomp/layout.py --this 007b5950 007b6110 ...    # functions whose this/param_1 is one class
  python tools/recomp/layout.py --class CMapChunk               # the functions names.json says have this class
      -> one row per field offset: width and type seen at each access, read/write, and which
         functions touch it. Feed the names into names.json "classes" and the corpus renders
         `this->name` from then on. --emit prints a C++ struct skeleton with the gaps padded.

  python tools/recomp/layout.py --globals 77e000 7d7000          # every DAT_ the functions in a range touch
  python tools/recomp/layout.py --globals-of 007b6b00 007b5950   # every DAT_ these functions touch
      -> one row per global address: width seen, read/write, referencing functions, and the name
         names.json already has. This is the map for a module whose "class" is a set of statics
         (CMap in 3.3.5a is one), and it is how the DAT_ names get recovered module by module.
"""
import argparse
import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import recomp   # noqa: E402
import corpus   # noqa: E402

WIDTH = {
    'float': (4, 'float'), 'double': (8, 'double'), 'int': (4, 'int32_t'), 'uint': (4, 'uint32_t'),
    'undefined4': (4, 'uint32_t'), 'undefined8': (8, 'uint64_t'), 'longlong': (8, 'int64_t'),
    'ulonglong': (8, 'uint64_t'), 'short': (2, 'int16_t'), 'ushort': (2, 'uint16_t'),
    'undefined2': (2, 'uint16_t'), 'char': (1, 'char'), 'byte': (1, 'uint8_t'), 'undefined': (1, 'uint8_t'),
    'undefined1': (1, 'uint8_t'), 'bool': (1, 'bool'), 'code': (4, 'void (*)()'),
}

# *(type *)(this + 0x1c0)   *(type *)((int)this + 0x1c0)   *(type *)(param_1 + 0x1c0)
FIELD = re.compile(r'\*\(\s*([\w]+)\s*(\**)\s*\)\s*\(\s*(?:\(int\))?\s*(this|param_1)\s*\+\s*(0x[0-9a-fA-F]+|\d+)\s*\)')
# (this + 0x1c0) taken as an address
ADDR = re.compile(r'(?<![\w*])\(\s*(?:\(int\))?\s*(this|param_1)\s*\+\s*(0x[0-9a-fA-F]+|\d+)\s*\)')
# this->field_0x1c0 when Ghidra auto-created a struct
AUTO = re.compile(r'\b(this|param_1)->field_(0x[0-9a-fA-F]+)')
# *(type *)(DAT_00cd8794 ...)  and bare DAT_ uses
GLOBAL_CAST = re.compile(r'\*\(\s*([\w]+)\s*(\**)\s*\)\s*\(?\s*&?_?(?:DAT|PTR_DAT|PTR_FUN|PTR|UNK)_([0-9a-f]{8})')
GLOBAL = re.compile(r'(?<![\w])_?(?:DAT|PTR_DAT|PTR_FUN|PTR|UNK)_([0-9a-f]{8})')


def type_of(base, stars):
    if stars:
        return (4, base + ' ' + stars)
    return WIDTH.get(base, (0, base))


def is_write(line, mo):
    """The access is the left side of an assignment when the rest of the line starts with '='
    (and not '==')."""
    rest = line[mo.end():].lstrip()
    return rest.startswith('=') and not rest.startswith('==')


def analyse_this(addrs, corp, names, m):
    fields = {}
    for a in addrs:
        e = corp.get(a)
        if not e or e.get('error'):
            continue
        fname = corpus.function_name(a, names, m) or 'FUN_' + a
        for line in e['c'].splitlines():
            for mo in FIELD.finditer(line):
                off = int(mo.group(4), 0)
                w, t = type_of(mo.group(1), mo.group(2))
                f = fields.setdefault(off, {'types': {}, 'r': 0, 'w': 0, 'fns': set(), 'addr': 0})
                f['types'][t] = f['types'].get(t, 0) + 1
                f['w' if is_write(line, mo) else 'r'] += 1
                f['fns'].add(fname)
            for mo in AUTO.finditer(line):
                off = int(mo.group(2), 16)
                f = fields.setdefault(off, {'types': {}, 'r': 0, 'w': 0, 'fns': set(), 'addr': 0})
                f['types']['?'] = f['types'].get('?', 0) + 1
                f['w' if is_write(line, mo) else 'r'] += 1
                f['fns'].add(fname)
            for mo in ADDR.finditer(line):
                off = int(mo.group(2), 0)
                f = fields.setdefault(off, {'types': {}, 'r': 0, 'w': 0, 'fns': set(), 'addr': 0})
                f['addr'] += 1
                f['fns'].add(fname)
    return fields


def print_fields(fields, cls, names, emit):
    known = names['classes'].get(cls, {}) if cls else {}
    known = {int(k, 16) if k.startswith('0x') else int(k): v for k, v in known.items()}
    print('%-7s %-5s %-22s %-4s %-4s %-4s %-24s %s' % ('offset', 'width', 'types seen', 'rd', 'wr', '&', 'name', 'functions'))
    for off in sorted(fields):
        f = fields[off]
        types = sorted(f['types'].items(), key=lambda kv: -kv[1])
        width = max((WIDTH.get(t, (0,))[0] if t in WIDTH else (4 if '*' in t else 0)) for t, _ in types) if types else 0
        tstr = ','.join('%s%s' % (t, ('x%d' % n) if n > 1 else '') for t, n in types)[:22]
        k = known.get(off)
        kname = (k['name'] if isinstance(k, dict) else k) if k else ''
        fns = sorted(f['fns'])
        print('0x%-5x %-5s %-22s %-4d %-4d %-4d %-24s %s' % (
            off, width or '?', tstr, f['r'], f['w'], f['addr'], kname, ', '.join(fns[:4]) + (' +%d' % (len(fns) - 4) if len(fns) > 4 else '')))
    if emit:
        print()
        print('struct %s {' % (cls or 'Unknown'))
        cur = 0
        for off in sorted(fields):
            f = fields[off]
            types = sorted(f['types'].items(), key=lambda kv: -kv[1])
            t = types[0][0] if types else 'uint8_t'
            width = WIDTH.get(t, (4 if '*' in t else 1, t))[0] if t in WIDTH or '*' in t else 1
            ctype = WIDTH[t][1] if t in WIDTH else (t if '*' in t else 'uint8_t')
            if off > cur:
                print('    uint8_t pad_0x%x[0x%x];' % (cur, off - cur))
            k = known.get(off)
            kname = (k['name'] if isinstance(k, dict) else k) if k else 'field_0x%x' % off
            print('    %-14s %s;  // 0x%x' % (ctype, kname, off))
            cur = off + width
        print('};')


def analyse_globals(addrs, corp, names, m):
    globs = {}
    for a in addrs:
        e = corp.get(a)
        if not e or e.get('error'):
            continue
        fname = corpus.function_name(a, names, m) or 'FUN_' + a
        for line in e['c'].splitlines():
            seen = set()
            for mo in GLOBAL_CAST.finditer(line):
                g = mo.group(3)
                w, t = type_of(mo.group(1), mo.group(2))
                d = globs.setdefault(g, {'types': {}, 'r': 0, 'w': 0, 'fns': set()})
                d['types'][t] = d['types'].get(t, 0) + 1
                seen.add(g)
            for mo in GLOBAL.finditer(line):
                g = mo.group(1)
                d = globs.setdefault(g, {'types': {}, 'r': 0, 'w': 0, 'fns': set()})
                d['w' if is_write(line, mo) else 'r'] += 1
                d['fns'].add(fname)
    return globs


def print_globals(globs, names):
    print('%-8s %-18s %-4s %-4s %-28s %s' % ('addr', 'types seen', 'rd', 'wr', 'name', 'functions'))
    for g in sorted(globs):
        d = globs[g]
        types = sorted(d['types'].items(), key=lambda kv: -kv[1])
        tstr = ','.join('%s%s' % (t, ('x%d' % n) if n > 1 else '') for t, n in types)[:18]
        k = names['globals'].get(g)
        kname = (k['name'] if isinstance(k, dict) else k) if k else ''
        fns = sorted(d['fns'])
        print('%s %-18s %-4d %-4d %-28s %s' % (g, tstr, d['r'], d['w'], kname, ', '.join(fns[:4]) + (' +%d' % (len(fns) - 4) if len(fns) > 4 else '')))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--this', nargs='+', metavar='HEX', help='functions whose this/param_1 is the class')
    ap.add_argument('--class', dest='cls', metavar='NAME', help='class name: uses names.json "this" to find the functions, and "classes" for known names')
    ap.add_argument('--emit', action='store_true', help='with --this/--class: print a C++ struct skeleton')
    ap.add_argument('--globals', nargs=2, metavar='HEX', help='globals touched by every function in an address range')
    ap.add_argument('--globals-of', nargs='+', metavar='HEX', help='globals touched by these functions')
    args = ap.parse_args()

    refs = recomp.load_reference()
    recomp.assign_modules(refs)
    names = corpus.load_names()
    corpus.apply_module_ranges(refs, names)
    corp = corpus.load_corpus()
    m = corpus.load_map()
    norm = lambda a: a.lower().replace('0x', '').zfill(8)

    if args.this or args.cls:
        addrs = [norm(a) for a in (args.this or [])]
        if args.cls:
            addrs += [a for a, c in names['this'].items() if c == args.cls]
        if not addrs:
            sys.exit('no functions: give --this addresses or add "this" entries to names.json for the class')
        fields = analyse_this(sorted(set(addrs)), corp, names, m)
        print('%d functions, %d field offsets' % (len(set(addrs)), len(fields)))
        print_fields(fields, args.cls, names, args.emit)
    elif args.globals or args.globals_of:
        if args.globals:
            addrs = corpus.select_range(refs, args.globals[0], args.globals[1])
        else:
            addrs = [norm(a) for a in args.globals_of]
        globs = analyse_globals(addrs, corp, names, m)
        print('%d functions, %d globals' % (len(addrs), len(globs)))
        print_globals(globs, names)
    else:
        ap.print_help()


if __name__ == '__main__':
    main()
