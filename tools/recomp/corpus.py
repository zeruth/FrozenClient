"""The reference decompilation corpus: every function of the 3.3.5a client decompiled once, read
many times.

Why: a port session used to spend its time waiting on Ghidra (1-3 minutes per headless run) and
reading decompilations in which every field is `*(int *)(this + 0x1c0)` and every global is
`DAT_00cd8794`. This tool exports the whole binary once (ghidra/ExportDecompAll.java, parallel
across the machine's cores) and then renders any module or address range instantly, with every
name the project has already recovered substituted in (tools/recomp/names.json, plus the linked
frozen names from data/map.json).

The linker lays a translation unit's functions out contiguously in definition order, so a module
rendered in address order reads like the original source file: helpers next to their callers,
static tables between them. Port it top to bottom.

    python tools/recomp/corpus.py --export              # decompile everything -> data/corpus.jsonl (one-off)
    python tools/recomp/corpus.py --export 77e000 7d7000  # re-export one range (after renaming in Ghidra)
    python tools/recomp/corpus.py --module Map.cpp      # -> docs/recomp/modules/Map.cpp.c
    python tools/recomp/corpus.py --range 77e000 7d7000 --out world   # -> docs/recomp/modules/world.c
    python tools/recomp/corpus.py --addr 0079a870 007b6b00             # render to stdout
    python tools/recomp/corpus.py --status              # what the corpus holds, per module

names.json (committed; grows every session) has four maps:
    functions  ref addr -> name                 (beats the map's frozen name)
    globals    ref addr -> {name, type}         DAT_/PTR_ substitutions
    classes    class -> {"0x1c0": {name, type}} field names by offset
    this       ref addr -> class                which class a function's `this`/param_1 is
"""
import argparse
import glob
import io
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import recomp  # noqa: E402  (load_reference, assign_modules, paths)

ROOT = recomp.ROOT
DATA = recomp.DATA
CORPUS = os.path.join(DATA, 'corpus.jsonl')
NAMES = os.path.join(HERE, 'names.json')
MAP_JSON = os.path.join(DATA, 'map.json')
MODULES_DIR = os.path.join(ROOT, 'docs', 'recomp', 'modules')


# ----------------------------------------------------------------------------------------------
# export

def export(lo=None, hi=None):
    ghidra = glob.glob(r'C:\Users\tyler\tools\ghidra_*')
    if not ghidra:
        sys.exit('no Ghidra install under C:\\Users\\tyler\\tools')
    os.makedirs(DATA, exist_ok=True)
    partial = lo is not None
    out = CORPUS + '.part' if partial else CORPUS + '.new'
    cmd = [os.path.join(ghidra[0], 'support', 'analyzeHeadless.bat'), recomp.GHIDRA_PROJECTS, 'RunicWorld',
           '-process', 'RunicWorldGame.exe', '-noanalysis', '-scriptPath', recomp.GHIDRA_SCRIPTS,
           '-postScript', 'ExportDecompAll.java', out]
    if partial:
        cmd += [lo, hi]
    env = dict(os.environ, MAXMEM='6G')
    print('decompiling %s in Ghidra...' % ('%s-%s' % (lo, hi) if partial else 'the whole binary'))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    for line in proc.stdout:
        if 'ExportDecompAll' in line or 'ERROR' in line or 'Exception' in line:
            print('  ', line.strip())
    proc.wait()
    if not os.path.exists(out):
        sys.exit('export produced nothing')
    if partial:
        corpus = load_corpus() if os.path.exists(CORPUS) else {}
        n = 0
        with io.open(out, encoding='utf-8') as f:
            for line in f:
                r = json.loads(line)
                corpus[r['addr']] = r
                n += 1
        with io.open(CORPUS, 'w', encoding='utf-8', newline='\n') as f:
            for a in sorted(corpus):
                f.write(json.dumps(corpus[a], ensure_ascii=False) + '\n')
        os.remove(out)
        print('merged %d functions into %s (%d total)' % (n, os.path.relpath(CORPUS, ROOT), len(corpus)))
    else:
        if os.path.exists(CORPUS):
            os.replace(CORPUS, CORPUS + '.bak')
        os.replace(out, CORPUS)
        print('wrote', os.path.relpath(CORPUS, ROOT))


def load_corpus():
    corpus = {}
    if not os.path.exists(CORPUS):
        sys.exit('no corpus yet: run  python tools/recomp/corpus.py --export  (one-off, ~1 hour)')
    with io.open(CORPUS, encoding='utf-8') as f:
        for line in f:
            r = json.loads(line)
            corpus[r['addr'].lower()] = r
    return corpus


# ----------------------------------------------------------------------------------------------
# names

def load_names():
    if not os.path.exists(NAMES):
        return {'functions': {}, 'globals': {}, 'classes': {}, 'this': {}, 'modules': []}
    n = json.load(io.open(NAMES, encoding='utf-8'))
    for k in ('functions', 'globals', 'classes', 'this'):
        n.setdefault(k, {})
    n.setdefault('modules', [])
    n['functions'] = {k.lower().zfill(8): v for k, v in n['functions'].items()}
    n['globals'] = {k.lower().zfill(8): v for k, v in n['globals'].items()}
    n['this'] = {k.lower().zfill(8): v for k, v in n['this'].items()}
    return n


def apply_module_ranges(refs, names):
    """Curated module ranges from names.json override the assert-string anchoring. The anchoring
    only sees files that assert: in the world region World.cpp, WorldScene.cpp, MapObj.cpp and
    MapShadow.cpp have no anchors, so their functions were smeared onto the nearest file that
    does (CMap::Update came out as DetailDoodad.cpp). A range here is evidence-backed (RTTI, a
    documented function, a vtable) and provisional at its edges; the note says what backs it."""
    for r in names['modules']:
        lo, hi = int(r['lo'], 16), int(r['hi'], 16)
        for a, ref in refs.items():
            x = int(a, 16)
            if lo <= x < hi:
                ref['module'] = r['module']
                ref['moduleSure'] = False
                ref['moduleHow'] = 'range'


def load_map():
    if not os.path.exists(MAP_JSON):
        return {}
    return json.load(io.open(MAP_JSON, encoding='utf-8'))


def function_name(addr, names, m):
    """The best name we have for a reference function: names.json, then the linked frozen name."""
    if addr in names['functions']:
        return names['functions'][addr]
    e = m.get(addr)
    if e and e.get('frozen'):
        return e['frozen']
    return None


FUN_RE = re.compile(r'\b(?:FUN|thunk_FUN)_([0-9a-f]{8})\b')
# Ghidra prefixes a global that overlaps a smaller symbol with '_' (_DAT_00ce04c0); same address.
DAT_RE = re.compile(r'(?<![\w])_?(?:DAT|PTR_DAT|PTR_FUN|PTR|UNK)_([0-9a-f]{8})\b')
# *(type *)(this + 0x1c0)   *(type *)((int)this + 0x1c0)   *(type *)(param_1 + 0x1c0)
FIELD_RE = re.compile(r'\*\(([\w ]+?\s*\**)\)\((?:\(int\))?(this|param_1)\s*\+\s*(0x[0-9a-fA-F]+|\d+)\)')
# (this + 0x1c0) as an address (no deref)
ADDR_RE = re.compile(r'(?<![\w*])\((?:\(int\))?(this|param_1)\s*\+\s*(0x[0-9a-fA-F]+|\d+)\)')


def render_body(c, addr, names, m):
    """Substitute recovered names into one function's decompilation."""
    def fun(mo):
        a = mo.group(1)
        n = function_name(a, names, m)
        return n if n else mo.group(0)
    c = FUN_RE.sub(fun, c)

    def dat(mo):
        a = mo.group(1)
        g = names['globals'].get(a)
        if g:
            return g['name'] if isinstance(g, dict) else g
        return mo.group(0)
    c = DAT_RE.sub(dat, c)

    cls = names['this'].get(addr)
    fields = names['classes'].get(cls, {}) if cls else {}
    if fields:
        table = {}
        for off, fld in fields.items():
            table[int(off, 16) if isinstance(off, str) and off.startswith('0x') else int(off)] = fld

        def field(mo):
            off = int(mo.group(3), 0)
            f = table.get(off)
            if not f:
                return mo.group(0)
            return 'this->' + (f['name'] if isinstance(f, dict) else f)
        c = FIELD_RE.sub(field, c)

        def addr_of(mo):
            off = int(mo.group(2), 0)
            f = table.get(off)
            if not f:
                return mo.group(0)
            return '(&this->' + (f['name'] if isinstance(f, dict) else f) + ')'
        c = ADDR_RE.sub(addr_of, c)
    return c


# ----------------------------------------------------------------------------------------------
# render

def status_of(addr, m):
    e = m.get(addr)
    if not e:
        return '-'
    s = e.get('status', 'ported')
    if s == 'ported' and e.get('faithful'):
        s = 'faithful'
    return s


def render(addrs, refs, corpus, names, m, title):
    """One file: a header table, then every function in address order with a banner naming its
    callers and callees in the vocabulary the project has so far."""
    L = []
    L.append('// %s' % title)
    L.append('// Rendered by tools/recomp/corpus.py from the Ghidra decompilation corpus. Do not edit;')
    L.append('// put names in tools/recomp/names.json and re-render. Functions are in address order,')
    L.append('// which is the order the original source defined them in.')
    L.append('//')
    n_linked = sum(1 for a in addrs if a in m)
    n_stub = sum(1 for a in addrs if status_of(a, m) == 'stub')
    L.append('// %d functions, %d linked (%d stubs), %d unlinked' % (len(addrs), n_linked, n_stub, len(addrs) - n_linked))
    L.append('//')
    L.append('// %-8s %5s %4s %-9s %-4s %-44s %s' % ('addr', 'size', 'clrs', 'status', 'sure', 'name', 'strings'))
    for a in addrs:
        r = refs.get(a, {})
        name = function_name(a, names, m) or r.get('name', 'FUN_' + a)
        strings = ', '.join('"%s"' % s[:28] for s in list(r.get('strings', []))[:2])
        sure = '' if r.get('moduleSure', True) else '?'
        L.append('// %-8s %5d %4d %-9s %-4s %-44s %s' % (a, r.get('size', 0), r.get('callers', 0), status_of(a, m), sure, name[:44], strings))
    L.append('')

    for a in addrs:
        r = refs.get(a, {})
        entry = corpus.get(a)
        name = function_name(a, names, m) or r.get('name', 'FUN_' + a)
        L.append('// ' + '=' * 96)
        L.append('// %s  FUN_%s  size %d  callers %d  module %s%s' % (
            name, a, r.get('size', 0), r.get('callers', 0), r.get('module', '?'),
            '' if r.get('moduleSure', True) else ' (unsure)'))
        e = m.get(a)
        if e:
            L.append('// frozen: %s  [%s, %s, fidelity %.2f]  %s' % (
                e.get('frozen'), e.get('how'), status_of(a, m), e.get('fidelity', 0) or 0, ', '.join(e.get('files', [])[:1])))
        callees = []
        for c in r.get('callees', []):
            callees.append(function_name(c, names, m) or 'FUN_' + c)
        if callees:
            L.append('// calls: ' + ', '.join(sorted(set(callees))))
        if r.get('strings'):
            L.append('// strings: ' + ' | '.join(s[:60] for s in r['strings'][:6]))
        cls = names['this'].get(a)
        if cls:
            L.append('// this: %s' % cls)
        if entry is None:
            L.append('// (not in corpus: run  corpus.py --export %s %s)' % (a, '%08x' % (int(a, 16) + 1)))
        elif entry.get('error'):
            L.append('// decompile failed: ' + entry['error'])
        else:
            L.append(render_body(entry['c'], a, names, m).rstrip())
        L.append('')
    return '\n'.join(L) + '\n'


def select_module(refs, module):
    return sorted(a for a, r in refs.items() if r.get('module') == module)


def select_range(refs, lo, hi):
    lo, hi = int(lo, 16), int(hi, 16)
    return sorted(a for a in refs if lo <= int(a, 16) < hi)


def status(refs, corpus):
    mods = {}
    for a, r in refs.items():
        mod = r.get('module', '?')
        t = mods.setdefault(mod, [0, 0])
        t[0] += 1
        if a in corpus and not corpus[a].get('error'):
            t[1] += 1
    total = sum(t[0] for t in mods.values())
    have = sum(t[1] for t in mods.values())
    print('corpus: %d of %d reference functions decompiled (%d with errors)' % (
        have, total, sum(1 for r in corpus.values() if r.get('error'))))
    missing = sorted(((t[0] - t[1], mod) for mod, t in mods.items() if t[0] != t[1]), reverse=True)
    for n, mod in missing[:30]:
        print('  %-36s missing %d of %d' % (mod, n, mods[mod][0]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--export', nargs='*', metavar='HEX', help='decompile everything (no args) or a range (lo hi) into the corpus')
    ap.add_argument('--module', metavar='FILE.cpp', help='render every function anchored to this reference module')
    ap.add_argument('--range', nargs=2, metavar='HEX', help='render an address range (lo hi, hi exclusive)')
    ap.add_argument('--addr', nargs='+', metavar='HEX', help='render these functions to stdout')
    ap.add_argument('--out', metavar='NAME', help='output name under docs/recomp/modules/ (default: the module name)')
    ap.add_argument('--status', action='store_true', help='corpus coverage per module')
    args = ap.parse_args()

    if args.export is not None:
        if len(args.export) == 0:
            export()
        elif len(args.export) == 2:
            export(args.export[0], args.export[1])
        else:
            sys.exit('--export takes no arguments or  lo hi')
        if not (args.module or args.range or args.addr or args.status):
            return

    refs = recomp.load_reference()
    recomp.assign_modules(refs)
    names = load_names()
    apply_module_ranges(refs, names)
    corpus = load_corpus()
    if args.status:
        status(refs, corpus)
        return
    m = load_map()

    if args.addr:
        addrs = [a.lower().replace('0x', '').zfill(8) for a in args.addr]
        sys.stdout.write(render(addrs, refs, corpus, names, m, 'functions ' + ' '.join(addrs)))
        return

    if args.module:
        addrs = select_module(refs, args.module)
        title = 'reference module %s' % args.module
        out = args.out or args.module
    elif args.range:
        addrs = select_range(refs, args.range[0], args.range[1])
        title = 'reference range %s-%s' % tuple(args.range)
        out = args.out or ('range-%s-%s' % tuple(args.range))
    else:
        ap.print_help()
        return
    if not addrs:
        sys.exit('nothing selected')
    os.makedirs(MODULES_DIR, exist_ok=True)
    path = os.path.join(MODULES_DIR, out + '.c')
    io.open(path, 'w', encoding='utf-8', newline='\n').write(render(addrs, refs, corpus, names, m, title))
    missing = sum(1 for a in addrs if a not in corpus)
    print('wrote %s: %d functions%s' % (os.path.relpath(path, ROOT), len(addrs),
                                       (' (%d not in corpus)' % missing) if missing else ''))


if __name__ == '__main__':
    main()
