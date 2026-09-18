#!/usr/bin/env python3
"""Reference <-> whoa function map: how much of the 3.3.5a client this port actually covers.

The reference binary has 27.7k functions and no symbols; whoa has ~9k. Nothing links the two
except the code itself, so this builds the link from evidence and keeps it across runs:

  1. reference inventory  tools/recomp/data/ref-functions.jsonl
       every function, its size, callees, callers and the string literals it references
       (Ghidra headless: tools/ghidra-scripts ExportFunctions.java; --export re-runs it)
  2. whoa inventory       from build/dist/bin/Whoa.pdb (names, code sizes, object files) and the
       source itself (bodies, string literals, calls, WHOA_UNIMPLEMENTED, reference annotations)
  3. matching, in order of trust
       override   tools/recomp/overrides.json          -- hand-confirmed, never lost
       annotated  `// ref: FUN_004f8ea0` above a whoa definition (or FUN_/Sub_ in its body)
       string     a literal both sides reference, weighted by how rare it is
       callgraph  a matched pair whose only unmatched callee on each side must be each other
  4. report               docs/recomp/REPORT.md + data/map.json + data/history.jsonl

Run it after every porting session:

    python tools/recomp/recomp.py            # inventory + match + report
    python tools/recomp/recomp.py --export   # also re-export the reference from Ghidra (2-3 min)
    python tools/recomp/recomp.py --pdb      # also re-dump Whoa.pdb (after a build)
    python tools/recomp/recomp.py --show 004f8ea0     # everything known about one reference fn
    python tools/recomp/recomp.py --show CGWorldFrame::OnWorldRender

The report is the checklist. Each mapped pair carries a status: ported (matched, has a body),
stub (matched but WHOA_UNIMPLEMENTED), verified (override says it was seen behaving like the
reference). Unmapped reference functions are ranked by weight = callers x size, restricted to the
world spine (reachable from CGWorldFrame::OnFrameRender) and globally, so the next thing to port is
always the top of a list rather than a guess.
"""

import argparse
import collections
import datetime
import glob
import io
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, 'data')
REF_JSONL = os.path.join(DATA, 'ref-functions.jsonl')
PDB_DUMP = os.path.join(DATA, 'whoa-pdb.txt')
OVERRIDES = os.path.join(HERE, 'overrides.json')
MAP_OUT = os.path.join(DATA, 'map.json')
HISTORY = os.path.join(DATA, 'history.jsonl')
REPORT = os.path.join(ROOT, 'docs', 'recomp', 'REPORT.md')

GHIDRA_PROJECTS = r'C:\Users\tyler\tools\ghidra-projects'
GHIDRA_SCRIPTS = os.path.join(HERE, 'ghidra')  # the exporters live with the tool
PDBUTIL = r'C:\Program Files\LLVM\bin\llvm-pdbutil.exe'
PDB = os.path.join(ROOT, 'build', 'dist', 'bin', 'Whoa.pdb')

# Roots of the frame loop in the reference: CGWorldFrame::OnFrameRender and the client's main
# idle. Reachability from these is what "world spine" means in the report.
SPINE_ROOTS = ['004fb080',   # CGWorldFrame::OnFrameRender
               '007831a0',   # CWorld::Update
               '0079a870',   # CMap::Render
               '004f8ea0']   # CGWorldFrame::OnWorldRender
# A linked, non-stub port counts as FAITHFUL when it reproduces at least this share of the
# reference's call sequence in order (see fidelity()).
FAITHFUL = 0.8
QUEUE_DIR = os.path.join(ROOT, 'docs', 'recomp', 'queue')
DECOMP = r'C:\Users\tyler\tools\decomp.sh'
TABLES_JSONL = os.path.join(DATA, 'ref-tables.jsonl')
SUSPECT_JSON = os.path.join(DATA, 'suspect.json')      # tracecompare: links the runtime contradicts
VERIFIED_JSON = os.path.join(DATA, 'verified.json')    # tracecompare: links the runtime confirms
TRACE_SUMMARY = os.path.join(DATA, 'trace-summary.json')
MATCHES_TSV = os.path.join(DATA, 'matches.tsv')

# Strings too common to tell functions apart.
NOISE_STRINGS = {'%s', '%d', '%i', '%u', '%f', '%x', '%08x', '\n', '\r\n', ' ', '', 'true', 'false', 'NULL', 'null',
                 'delete', 'delete[]', 'new', 'new[]', 'Unknown', 'unknown', '0', '1', '-1', '...', '.', ',', ':', ';'}

VENDOR_LIBS = {'storm', 'tempest', 'lua-5.1', 'stormlib-9.31', 'zlib-1.2', 'freetype-2.0', 'expat-2.0', 'bc', 'common', 'math'}


# ----------------------------------------------------------------------------------------------
# reference side

def export_reference():
    ghidra = glob.glob(r'C:\Users\tyler\tools\ghidra_*')
    if not ghidra:
        sys.exit('no Ghidra install under C:\\Users\\tyler\\tools')
    os.makedirs(DATA, exist_ok=True)
    cmd = [os.path.join(ghidra[0], 'support', 'analyzeHeadless.bat'), GHIDRA_PROJECTS, 'RunicWorld',
           '-process', 'RunicWorldGame.exe', '-noanalysis', '-scriptPath', GHIDRA_SCRIPTS,
           '-postScript', 'ExportFunctions.java', REF_JSONL]
    print('exporting reference functions from Ghidra (2-3 minutes)...')
    out = subprocess.run(cmd, capture_output=True, text=True)
    for line in out.stdout.splitlines() + out.stderr.splitlines():
        if 'ExportFunctions:' in line or 'ERROR' in line or 'Exception' in line:
            print('  ', line.strip())


def load_reference():
    refs = {}
    with io.open(REF_JSONL, encoding='utf-8') as f:
        for line in f:
            r = json.loads(line)
            r['addr'] = r['addr'].lower()
            r['callees'] = [c.lower() for c in r['callees'] if not c.startswith('ext:')]
            r['calls'] = [c.lower() for c in r.get('calls', r['callees']) if not c.startswith('ext:')]
            r.setdefault('branches', 0)
            r['consts'] = set(r.get('consts', []))
            refs[r['addr']] = r
    # a thunk is a jump to the real function: callers of the thunk call the target
    for r in refs.values():
        r['calls'] = [refs[c]['callees'][0] if c in refs and refs[c]['thunk'] and len(refs[c]['callees']) == 1 else c for c in r['calls']]
        r['callees'] = sorted(set(r['calls']))
    return refs


MODULE_STRING = re.compile(r'^(?:\.\\|\.\./|\.\./\.\./|\.\.\\)*(?:[\w.-]+[\\/])*([A-Za-z0-9_]+\.(?:cpp|c|h|inl))$')


def assign_modules(refs):
    """Module = the source file the reference's own asserts name. The linker lays each object's
    functions out contiguously, so the nearest preceding assert anchor names the file with good
    odds; the flag says whether the next anchor agrees."""
    anchors = []
    for r in refs.values():
        for s in r['strings']:
            m = MODULE_STRING.match(s)
            if m:
                anchors.append((int(r['addr'], 16), m.group(1)))
                break
    anchors.sort()
    addrs = sorted(int(a, 16) for a in refs)
    i = 0
    for a in addrs:
        while i + 1 < len(anchors) and anchors[i + 1][0] <= a:
            i += 1
        r = refs['%08x' % a]
        if not anchors or anchors[i][0] > a:
            r['module'], r['moduleSure'] = '?', False
            continue
        mod = anchors[i][1]
        nxt = anchors[i + 1][1] if i + 1 < len(anchors) else mod
        r['module'] = mod
        r['moduleSure'] = (nxt == mod) or anchors[i][0] == a
    return len(anchors)


def spine(refs):
    seen = set()
    stack = [r for r in SPINE_ROOTS if r in refs]
    while stack:
        a = stack.pop()
        if a in seen:
            continue
        seen.add(a)
        stack.extend(c for c in refs[a]['callees'] if c in refs and c not in seen)
    return seen


# ----------------------------------------------------------------------------------------------
# whoa side

def dump_pdb():
    os.makedirs(DATA, exist_ok=True)
    print('dumping', PDB)
    with io.open(PDB_DUMP, 'w', encoding='utf-8', errors='replace') as out:
        subprocess.run([PDBUTIL, 'dump', '-symbols', PDB], stdout=out, stderr=subprocess.STDOUT, text=True)


MOD_LINE = re.compile(r'^\s*Mod [0-9A-F]{4} \| `(.+?)`:')
PROC_LINE = re.compile(r'^\s*\d+ \| S_[GL]PROC32 \[size = \d+\] `(.+)`$')
SIZE_LINE = re.compile(r'code size = (\d+)')


def normalize_name(n):
    n = n.replace("`anonymous namespace'::", '')
    return n


def load_pdb_functions():
    """{qualified name: {'size', 'lib', 'obj'}} for every procedure compiled from src/."""
    fns = {}
    lib = obj = None
    pending = None
    if not os.path.exists(PDB_DUMP):
        return fns
    with io.open(PDB_DUMP, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = MOD_LINE.match(line)
            if m:
                path = m.group(1).replace('/', '\\')
                mm = re.search(r'([\w.-]+)\.dir\\(?:Release\\)?([\w.-]+)\.obj$', path)
                if mm:
                    lib, obj = mm.group(1), mm.group(2)
                    if lib == 'Whoa':
                        lib = 'app'
                    if lib in VENDOR_LIBS:
                        lib = 'lib/' + lib  # storm, tempest ...: part of the reference too, kept but labelled
                else:
                    lib = obj = None
                pending = None
                continue
            m = PROC_LINE.match(line)
            if m and lib:
                pending = normalize_name(m.group(1))
                continue
            if pending:
                m = SIZE_LINE.search(line)
                if m:
                    size = int(m.group(1))
                    e = fns.setdefault(pending, {'size': 0, 'lib': lib, 'obj': obj, 'count': 0})
                    e['size'] += size
                    e['count'] += 1
                    pending = None
    return fns


DEF_RE = re.compile(r'^(?![\s#])(?:[\w:<>,\*&~]+\s+)*?\**&?([\w~]+(?:::[\w~]+)*)\s*\(([^;{}]*?)\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{', re.M)
STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
CALL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
KEYWORDS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'static_cast', 'reinterpret_cast', 'const_cast',
            'dynamic_cast', 'defined', 'catch', 'alignof', 'decltype', 'new', 'delete'}
REF_ADDR_RE = re.compile(r'\b(?:FUN_|sub_|Sub_?|0x)(00[4-9a-fA-F][0-9a-fA-F]{5}|[4-9a-fA-F][0-9a-fA-F]{5})\b')
REF_TAG_RE = re.compile(r'//\s*ref:\s*(?:FUN_|0x)?(00[4-9a-fA-F][0-9a-fA-F]{5}|[4-9a-fA-F][0-9a-fA-F]{5})\b')


def unescape(s):
    return (s.replace('\\\\', '\x00').replace('\\n', '\n').replace('\\t', '\t').replace('\\r', '\r')
            .replace('\\"', '"').replace("\\'", "'").replace('\x00', '\\'))


def parse_sources():
    """Function bodies from src/**/*.cpp: name, file, strings, calls, stub flag, ref annotations."""
    fns = {}
    paths = glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True)
    paths += glob.glob(os.path.join(ROOT, 'lib', '**', '*.cpp'), recursive=True)  # storm, tempest: reference code too
    for path in paths:
        rel = os.path.relpath(path, ROOT).replace('\\', '/')
        text = io.open(path, encoding='utf-8', errors='replace').read()
        for m in DEF_RE.finditer(text):
            name = m.group(1)
            if name in KEYWORDS or name.startswith('operator'):
                continue
            # body by brace matching from the opening brace
            i = m.end() - 1
            depth = 0
            j = i
            n = len(text)
            while j < n:
                c = text[j]
                if c == '{':
                    depth += 1
                elif c == '}':
                    depth -= 1
                    if depth == 0:
                        break
                elif c == '"' or c == "'":
                    q = c
                    j += 1
                    while j < n and text[j] != q:
                        if text[j] == '\\':
                            j += 1
                        j += 1
                elif text.startswith('//', j):
                    j = text.find('\n', j)
                    if j < 0:
                        j = n
                elif text.startswith('/*', j):
                    j = text.find('*/', j + 2)
                    if j < 0:
                        j = n
                    j += 1
                j += 1
            body = text[i:j + 1]
            # The reference annotation is the `// ref:` tag in the comment block right above the
            # definition, or the address whoa baked into the name (CM2Model::Sub826350 IS
            # FUN_00826350). A FUN_/Sub mention inside the body names a callee, not this function
            # -- reading those as self-links is what linked SetBoneSequence to its own helper.
            above = text[max(0, text.rfind('\n\n', 0, m.start())):m.start()]
            refs = set(a.lower().zfill(8) for a in REF_TAG_RE.findall(above))
            if not refs:
                refs = set(a.lower().zfill(8) for a in re.findall(r'(?:^|::)(?:Sub|sub_)([0-9A-Fa-f]{6})$', name))
            strings = set()
            for sm in STRING_RE.finditer(body):
                s = unescape(sm.group(1))
                if len(s) >= 3 and s not in NOISE_STRINGS:
                    strings.add(s)
            callseq = [c for c in CALL_RE.findall(body) if c not in KEYWORDS and c != name.split('::')[-1]]
            calls = set(callseq)
            key = name
            e = fns.get(key)
            if e:
                e['strings'] |= strings
                e['calls'] |= calls
                e['callseq'] += callseq
                e['refs'] |= refs
                e['stub'] = e['stub'] and 'WHOA_UNIMPLEMENTED' in body
                e['files'].add(rel)
                continue
            fns[key] = {'name': name, 'files': {rel}, 'strings': strings, 'calls': calls, 'callseq': callseq, 'refs': refs,
                        'stub': 'WHOA_UNIMPLEMENTED' in body, 'lines': body.count('\n') + 1}
    return fns


CLANG_JSON = os.path.join(DATA, 'whoa-clang.json')


def overlay_clang(src):
    """Replace the regex parser's guesses with libclang's exact answers where clangparse.py has
    run: ordered resolved calls, literals, branch counts. Tags (refs) stay with the regex pass, which
    reads comments. STL and lambda calls are dropped from the sequence: the reference has neither."""
    if not os.path.exists(CLANG_JSON):
        return src, False
    clang = json.load(io.open(CLANG_JSON, encoding='utf-8'))
    for name, c in clang.items():
        seq = [x for x in c['callseq'] if not x.startswith('std::') and '(lambda' not in x and not x.startswith('?')]
        e = src.get(name)
        if not e:
            e = src[name] = {'name': name, 'files': set(c['files']), 'strings': set(), 'calls': set(), 'callseq': [],
                             'refs': set(), 'stub': c['stub'], 'lines': c['lines']}
        e['callseq'] = seq
        e['calls'] = set(seq)
        e['strings'] = set(s for s in c['strings'] if len(s) >= 3 and s not in NOISE_STRINGS)
        e['consts'] = set(c['consts'])
        e['branches'] = c['branches']
        e['stub'] = c['stub']
        e['exact'] = True
    return src, True


def merge_whoa(pdb, src):
    whoa = {}
    for name, s in src.items():
        p = pdb.get(name)
        files = sorted(s['files'])
        parts = files[0].split('/')
        whoa[name] = {'name': name, 'files': files, 'strings': s['strings'], 'calls': s['calls'], 'callseq': s['callseq'],
                      'refs': s['refs'], 'stub': s['stub'], 'lines': s['lines'], 'exact': s.get('exact', False),
                      'consts': s.get('consts', set()), 'branches': s.get('branches', -1),
                      'size': p['size'] if p else 0, 'lib': p['lib'] if p else (parts[1] if len(parts) > 2 else '?')}
    for name, p in pdb.items():
        if name not in whoa:
            whoa[name] = {'name': name, 'files': [], 'strings': set(), 'calls': set(), 'callseq': [], 'refs': set(), 'stub': False,
                          'lines': 0, 'size': p['size'], 'lib': p['lib'], 'exact': False, 'consts': set(), 'branches': -1}
    # resolve calls to whoa function keys: same class first, then a unique short-name match
    short = collections.defaultdict(list)
    for name in whoa:
        short[name.split('::')[-1]].append(name)
    for name, w in whoa.items():
        cls = name.rsplit('::', 1)[0] if '::' in name else None

        def resolve(c):
            cands = short.get(c)
            if not cands:
                return None
            if cls and cls + '::' + c in whoa:
                return cls + '::' + c
            if len(cands) == 1:
                return cands[0]
            return None

        if w['exact']:
            # libclang already resolved the callee: its qualified name is the key, unless the callee
            # is inline/header-only and the PDB never saw it
            w['seq'] = [c if c in whoa else '?' + c for c in w['callseq']]
            w['callees'] = set(c for c in w['seq'] if not c.startswith('?'))
            continue
        w['callees'] = set(k for k in (resolve(c) for c in w['calls']) if k)
        # ordered, unresolved names kept as '?name' so the sequence keeps its shape
        w['seq'] = [resolve(c) or '?' + c for c in w['callseq']]
    return whoa


def lcs_len(a, b):
    if not a or not b:
        return 0
    prev = [0] * (len(b) + 1)
    for x in a:
        cur = [0]
        for j, y in enumerate(b):
            cur.append(prev[j] + 1 if x == y else max(prev[j + 1], cur[j]))
        prev = cur
    return prev[-1]


def fidelity(refs, whoa, m, addr):
    """How much of the reference's call sequence the port reproduces, in order: LCS of the two call
    sequences over the longer one, with reference callees translated through the map. 1.0 means every
    call the reference makes, the port makes, in the same order. Unlinked reference callees can never
    match, so a low score also says 'dependencies still unidentified'."""
    name = m[addr][0]
    rseq = [m[c][0] if c in m else c for c in refs[addr]['calls']]
    wseq = whoa[name]['seq']
    if not rseq:
        return 1.0 if not whoa[name]['stub'] else 0.0
    # Recall of the reference's sequence: extra calls on the whoa side (helpers the reference
    # compiler inlined, constructors) do not count against it; missing or reordered ones do.
    return lcs_len(rseq, wseq) / float(len(rseq))


def precision(refs, whoa, m, addr):
    """Share of the port's calls that the reference also makes, in order. Low with high recall
    means the port does more than the reference: inlined helpers, or invented behaviour."""
    name = m[addr][0]
    rseq = [m[c][0] if c in m else c for c in refs[addr]['calls']]
    wseq = whoa[name]['seq']
    if not wseq:
        return 1.0
    return lcs_len(rseq, wseq) / float(len(wseq))


def fidelity_dims(refs, whoa, m, addr):
    """The other two structural checks, when both sides can answer them (libclang inventory):
    branch ratio = min/max of the conditional-branch counts (1.0 = same shape), const overlap =
    share of the reference's notable immediates the port's literals also contain. -1 = unknown."""
    r = refs[addr]
    w = whoa[m[addr][0]]
    br = -1.0
    if w['branches'] >= 0:
        a, b = r['branches'], w['branches']
        br = 1.0 if a == b else (min(a, b) / float(max(a, b)) if max(a, b) else 1.0)
    co = -1.0
    if w['exact'] and r['consts']:
        wc = set()
        for c in w['consts']:
            try:
                v = int(c.rstrip('uUlL'), 0) if not re.search(r'[.eE]', c) or c.lower().startswith('0x') else None
            except ValueError:
                v = None
            if v is not None:
                wc.add(v)
        rc = set()
        for c in r['consts']:
            try:
                rc.add(int(c, 16))
            except ValueError:
                pass
        co = len(rc & wc) / float(len(rc)) if rc else -1.0
    return br, co


def is_faithful(refs, whoa, m, addr, fid):
    """Call order >= FAITHFUL, and when the reference has real control flow (>= 4 branches) and the
    port's branch count is known, the shapes must be within a factor of two."""
    if fid < FAITHFUL or whoa[m[addr][0]]['stub']:
        return False
    br, co = fidelity_dims(refs, whoa, m, addr)
    if refs[addr]['branches'] >= 4 and br >= 0 and br < 0.5:
        return False
    return True


# ----------------------------------------------------------------------------------------------
# matching

def load_tables():
    """The reference's {name, function} binding tables, grouped: entries 8 bytes apart in data are
    one table (one widget class's methods, or one block of global functions). Returns a list of
    {'addr': table start, 'entries': [(name, fn)]}."""
    rows = []
    if not os.path.exists(TABLES_JSONL):
        return []
    with io.open(TABLES_JSONL, encoding='utf-8') as f:
        for line in f:
            r = json.loads(line)
            rows.append((int(r['table'], 16), r['name'], r['fn'].lower()))
    rows.sort()
    tables = []
    for at, name, fn in rows:
        if tables and at - tables[-1]['last'] == 8:
            tables[-1]['entries'].append((name, fn))
            tables[-1]['last'] = at
        else:
            tables.append({'addr': '%08x' % at, 'entries': [(name, fn)], 'last': at})
    return tables


WHOA_TABLE_RE = re.compile(r'(FrameScript_Method|FrameScript_Function)\s+([\w:]+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\};', re.S)
WHOA_ENTRY_RE = re.compile(r'\{\s*"(\w+)"\s*,\s*&?([\w:]+)\s*\}')


def load_whoa_tables():
    """whoa's own binding arrays, in source order: [{'name': array, 'file', 'entries': [(lua name, fn)]}]."""
    tables = []
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        text = io.open(path, encoding='utf-8', errors='replace').read()
        for m in WHOA_TABLE_RE.finditer(text):
            entries = [(n, f) for n, f in WHOA_ENTRY_RE.findall(m.group(3))]
            if entries:
                tables.append({'name': m.group(2), 'file': os.path.relpath(path, ROOT).replace('\\', '/'), 'entries': entries})
    return tables


def pair_tables(ref_tables, whoa_tables):
    """Match whoa binding arrays to reference tables by shared names; a reference table can only be
    claimed once. Returns [(whoa table, ref table, shared name count)]."""
    pairs = []
    for wt in whoa_tables:
        wnames = set(n for n, _ in wt['entries'])
        best = None
        for rt in ref_tables:
            k = len(wnames & set(n for n, _ in rt['entries']))
            if k and (best is None or k > best[0]):
                best = (k, rt)
        if best and (best[0] >= 3 or best[0] == len(wnames)):
            pairs.append((wt, best[1], best[0]))
    # one reference table per whoa table, best claim wins
    claimed = {}
    for wt, rt, k in sorted(pairs, key=lambda p: -p[2]):
        if rt['addr'] not in claimed:
            claimed[rt['addr']] = (wt, rt, k)
    return list(claimed.values())


def load_overrides():
    if not os.path.exists(OVERRIDES):
        return {}
    return json.load(io.open(OVERRIDES, encoding='utf-8'))


def match(refs, whoa, overrides, tables):
    """addr -> (whoa name, confidence)"""
    m = {}
    used = set()
    evidence = {}

    unlinked = set(a for a, o in overrides.items() if o.get('status') == 'unlinked')

    def bind(addr, name, how, why=''):
        # an override may bind one whoa name to several reference functions: C++ overloads share a
        # key here (CDataStore::Put x4) and COMDAT folding leaves the reference with copies
        if addr in m or addr not in refs or name not in whoa or (name in used and how != 'override'):
            return False
        if addr in unlinked:
            return False  # judged to have no whoa counterpart; automatic evidence does not reopen it
        m[addr] = (name, how)
        used.add(name)
        evidence[addr] = why
        return True

    for addr, o in overrides.items():
        addr = addr.lower().zfill(8)
        if o.get('whoa'):
            bind(addr, o['whoa'], 'override')

    for name, w in sorted(whoa.items()):
        for addr in sorted(w['refs']):
            bind(addr, name, 'annotated', 'tag in ' + (w['files'][0] if w['files'] else '?'))

    # binding tables: whoa's FrameScript_Method/Function arrays paired with the reference's by
    # shared names, then each entry bound by name inside its pair (so CSimpleFrame's AddLine and
    # CSimpleHTML's AddLine each find their own)
    for wt, rt, k in tables:
        rfn = dict(rt['entries'])
        for lua_name, fn in wt['entries']:
            a = rfn.get(lua_name)
            if a:
                # whoa spells the handler as it likes; resolve the array's function name to a key
                key = fn if fn in whoa else next((n for n in whoa if n.endswith('::' + fn)), None)
                if key:
                    bind(a, key, 'table', 'binding "%s" in %s ~ table %s' % (lua_name, wt['name'], rt['addr']))

    # string anchors: rarity-weighted overlap, accepted when the best candidate is clearly best
    ref_by_string = collections.defaultdict(set)
    for addr, r in refs.items():
        for s in r['strings']:
            if len(s) >= 3 and s not in NOISE_STRINGS:
                ref_by_string[s].add(addr)
    whoa_by_string = collections.defaultdict(set)
    for name, w in whoa.items():
        for s in w['strings']:
            whoa_by_string[s].add(name)
    for s, rset in sorted(ref_by_string.items(), key=lambda kv: (len(kv[1]) * len(whoa_by_string.get(kv[0], ())), kv[0])):
        wset = whoa_by_string.get(s)
        if not wset or len(rset) != 1 or len(wset) != 1:
            continue
        bind(next(iter(rset)), next(iter(wset)), 'string', 'unique "%s"' % s[:60])
    scores = collections.defaultdict(float)
    for s, rset in ref_by_string.items():
        wset = whoa_by_string.get(s)
        if not wset or len(rset) > 8 or len(wset) > 8:
            continue
        wgt = 1.0 / (len(rset) * len(wset))
        for a in rset:
            for n in wset:
                scores[(a, n)] += wgt
    best_ref = collections.defaultdict(list)
    for (a, n), sc in scores.items():
        best_ref[a].append((sc, n))
    for a, cands in sorted(best_ref.items()):
        cands.sort(reverse=True)
        if cands[0][0] >= 0.5 and (len(cands) == 1 or cands[0][0] >= 2 * cands[1][0]):
            shared = sorted(s for s in refs[a]['strings'] if s in whoa[cands[0][1]]['strings'])
            bind(a, cands[0][1], 'string', 'score %.2f shared %s' % (cands[0][0], '; '.join(x[:40] for x in shared[:3])))

    # call-graph propagation: a matched pair with exactly one unmatched callee each side proposes
    # that those two are the same. One parent is not proof (a port that calls one helper the
    # reference does not, or vice versa, proposes nonsense); the pair has to be proposed by two
    # different parents, or be the only proposal for both of its members.
    for _ in range(4):
        votes = collections.defaultdict(set)
        for addr, (name, how) in list(m.items()):
            rc = [c for c in refs[addr]['callees'] if c not in m and c in refs and not refs[c]['thunk']]
            wc = [c for c in whoa[name]['callees'] if c not in used and c in whoa]
            if len(rc) == 1 and len(wc) == 1:
                votes[(rc[0], wc[0])].add(addr)
        by_ref = collections.Counter(p[0] for p in votes)
        by_whoa = collections.Counter(p[1] for p in votes)
        added = 0
        for (rc, wc), parents in sorted(votes.items(), key=lambda kv: -len(kv[1])):
            if len(parents) >= 2 or (by_ref[rc] == 1 and by_whoa[wc] == 1):
                added += bind(rc, wc, 'callgraph', 'only unmatched callee of %s' % ', '.join('%s=%s' % (p, m[p][0]) for p in sorted(parents)[:3]))
        if not added:
            break

    # call-order alignment: inside a linked pair, walk both call sequences; between two linked
    # anchors, a single unlinked call on each side is the same call. Same two-parent rule.
    for _ in range(3):
        votes = collections.defaultdict(set)
        for addr, (name, how) in list(m.items()):
            rseq = refs[addr]['calls']
            wseq = whoa[name]['seq']
            if len(rseq) < 2 or len(wseq) < 2 or len(rseq) > 80 or len(wseq) > 80:
                continue
            ri = wi = 0
            # anchor positions: pairs (i, j) where rseq[i] is linked to wseq[j]
            anchors = []
            j0 = 0
            for i, c in enumerate(rseq):
                if c in m:
                    target = m[c][0]
                    for j in range(j0, len(wseq)):
                        if wseq[j] == target:
                            anchors.append((i, j))
                            j0 = j + 1
                            break
            anchors = [(-1, -1)] + anchors + [(len(rseq), len(wseq))]
            for (i0, j0), (i1, j1) in zip(anchors, anchors[1:]):
                if i1 - i0 == 2 and j1 - j0 == 2:
                    rc, wc = rseq[i0 + 1], wseq[j0 + 1]
                    if rc in refs and not wc.startswith('?') and wc in whoa and rc not in m and wc not in used:
                        votes[(rc, wc)].add(addr)
        by_ref = collections.Counter(p[0] for p in votes)
        by_whoa = collections.Counter(p[1] for p in votes)
        added = 0
        for (rc, wc), parents in sorted(votes.items(), key=lambda kv: -len(kv[1])):
            if len(parents) >= 2 or (by_ref[rc] == 1 and by_whoa[wc] == 1):
                added += bind(rc, wc, 'callorder', 'same slot between linked calls in %s' % ', '.join('%s=%s' % (p, m[p][0]) for p in sorted(parents)[:3]))
        if not added:
            break
    with io.open(MATCHES_TSV, 'w', encoding='utf-8', newline='\n') as out:
        out.write('addr\thow\twhoa\tevidence\n')
        for a, (name, how) in sorted(m.items()):
            why = evidence.get(a, '').replace('\n', ' ').replace('\t', ' ')
            out.write('%s\t%s\t%s\t%s\n' % (a, how, name, why))
    return m


# ----------------------------------------------------------------------------------------------
# report

def fmt_bytes(n):
    return '%.1fk' % (n / 1024.0) if n < 1024 * 1024 else '%.2fM' % (n / 1048576.0)


def lua_coverage(ref_tables, pairs, whoa):
    """Per reference binding table: how many of its names whoa registers, and which are missing or
    stubbed. The pairing decides which whoa array answers for which reference table."""
    by_ref = {rt['addr']: (wt, k) for wt, rt, k in pairs}
    rows = []
    total = have = stubbed = 0
    for rt in ref_tables:
        if len(rt['entries']) < 4:
            continue  # not a binding table: a two-entry pair in some other structure
        names = [n for n, _ in rt['entries']]
        wt = by_ref.get(rt['addr'], (None, 0))[0]
        wnames = {n: f for n, f in wt['entries']} if wt else {}
        missing = [n for n in names if n not in wnames]
        stubs = []
        for n in names:
            f = wnames.get(n)
            if f:
                key = f if f in whoa else next((x for x in whoa if x.endswith('::' + f)), None)
                if key and whoa[key]['stub']:
                    stubs.append(n)
        total += len(names)
        have += len(names) - len(missing)
        stubbed += len(stubs)
        rows.append({'addr': rt['addr'], 'count': len(names), 'whoa': wt['name'] if wt else '', 'file': wt['file'] if wt else '',
                     'missing': missing, 'stubs': stubs, 'first': names[0]})
    rows.sort(key=lambda r: -(len(r['missing']) + len(r['stubs'])))
    return total, have, stubbed, rows


def build_report(refs, whoa, m, overrides, anchors, ref_tables=(), pairs=()):
    lua_total, lua_have, lua_stubbed, lua_rows = lua_coverage(ref_tables, pairs, whoa)
    sp = spine(refs)
    real = {a: r for a, r in refs.items() if not r['thunk'] and not r['excluded']}
    total = len(real)
    total_bytes = sum(r['size'] for r in real.values())

    def status(addr):
        name, how = m[addr]
        o = overrides.get(addr, {})
        if o.get('status'):
            return o['status']
        return 'stub' if whoa[name]['stub'] else 'ported'

    by_status = collections.Counter()
    bytes_by_status = collections.Counter()
    by_how = collections.Counter()
    fid = {}
    faithful = faithful_bytes = 0
    for a in real:
        if a in m:
            st = status(a)
            by_how[m[a][1]] += 1
            fid[a] = fidelity(refs, whoa, m, a)
            if st != 'stub' and is_faithful(refs, whoa, m, a, fid[a]):
                faithful += 1
                faithful_bytes += real[a]['size']
        else:
            st = 'unmapped'
        by_status[st] += 1
        bytes_by_status[st] += real[a]['size']

    # per module
    mods = collections.defaultdict(lambda: {'fns': 0, 'bytes': 0, 'mapped': 0, 'mappedBytes': 0, 'stub': 0, 'verified': 0, 'spine': 0})
    for a, r in real.items():
        e = mods[r['module']]
        e['fns'] += 1
        e['bytes'] += r['size']
        e['spine'] += a in sp
        if a in m:
            e['mapped'] += 1
            e['mappedBytes'] += r['size']
            st = status(a)
            e['stub'] += st == 'stub'
            e['verified'] += st == 'verified'

    def weight(a):
        r = refs[a]
        return (r['callers'] + 1) * r['size']

    unmapped = [a for a in real if a not in m]
    top_spine = sorted((a for a in unmapped if a in sp), key=weight, reverse=True)[:40]
    top_all = sorted(unmapped, key=weight, reverse=True)[:40]
    stubs = sorted((a for a in m if status(a) == 'stub'), key=weight, reverse=True)[:40]
    # divergence smell: reference strings the whoa counterpart does not carry
    smells = []
    for a, (name, how) in m.items():
        rs = set(s for s in refs[a]['strings'] if len(s) >= 3 and s not in NOISE_STRINGS and not MODULE_STRING.match(s))
        missing = sorted(rs - whoa[name]['strings'])
        if missing and how != 'override':
            smells.append((len(missing), a, name, missing))
    smells.sort(reverse=True)

    whoa_unlinked = sorted((n for n, w in whoa.items() if n not in {v[0] for v in m.values()} and w['size']),
                           key=lambda n: whoa[n]['size'], reverse=True)

    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    snapshot = {'date': now, 'refFunctions': total, 'refBytes': total_bytes,
                'mapped': total - by_status['unmapped'], 'mappedBytes': total_bytes - bytes_by_status['unmapped'],
                'ported': by_status['ported'], 'stub': by_status['stub'], 'verified': by_status['verified'],
                'faithful': faithful, 'faithfulBytes': faithful_bytes,
                'spine': len(sp & set(real)), 'spineMapped': sum(1 for a in sp if a in m and a in real),
                'whoaFunctions': len(whoa), 'whoaStubs': sum(1 for w in whoa.values() if w['stub']),
                'luaTotal': lua_total, 'luaHave': lua_have, 'luaStubbed': lua_stubbed}
    prev = None
    if os.path.exists(HISTORY):
        lines = io.open(HISTORY, encoding='utf-8').read().splitlines()
        if lines:
            prev = json.loads(lines[-1])

    def delta(k):
        if not prev or k not in prev:
            return ''
        d = snapshot[k] - prev[k]
        return ' (%+d)' % d if d else ' (=)'

    def pct(n, d):
        return '%.1f%%' % (100.0 * n / d) if d else '-'

    L = []
    L.append('# Recomp map: reference 3.3.5a (12340) vs whoa')
    L.append('')
    L.append('Generated %s by `tools/recomp/recomp.py`. Do not edit; put facts in `tools/recomp/overrides.json`' % now)
    L.append('or `// ref: FUN_xxxxxxxx` tags above whoa definitions and re-run.')
    L.append('')
    L.append('## Totals')
    L.append('')
    L.append('| | functions | code bytes |')
    L.append('|---|---:|---:|')
    L.append('| reference (non-thunk) | %d | %s |' % (total, fmt_bytes(total_bytes)))
    L.append('| mapped to a whoa function | %d%s (%s) | %s (%s) |' % (snapshot['mapped'], delta('mapped'), pct(snapshot['mapped'], total), fmt_bytes(snapshot['mappedBytes']), pct(snapshot['mappedBytes'], total_bytes)))
    L.append('| &nbsp;&nbsp;ported | %d%s | %s |' % (by_status['ported'], delta('ported'), fmt_bytes(bytes_by_status['ported'])))
    L.append('| &nbsp;&nbsp;stub (WHOA_UNIMPLEMENTED) | %d%s | %s |' % (by_status['stub'], delta('stub'), fmt_bytes(bytes_by_status['stub'])))
    L.append('| &nbsp;&nbsp;verified (override) | %d%s | %s |' % (by_status['verified'], delta('verified'), fmt_bytes(bytes_by_status['verified'])))
    L.append('| **faithful** (linked, not stub, call order >= %.0f%%) | **%d%s (%s)** | **%s (%s)** |' % (FAITHFUL * 100, faithful, delta('faithful'), pct(faithful, total), fmt_bytes(faithful_bytes), pct(faithful_bytes, total_bytes)))
    L.append('| unmapped | %d | %s |' % (by_status['unmapped'], fmt_bytes(bytes_by_status['unmapped'])))
    L.append('| world spine (reachable from OnFrameRender) | %d, mapped %d%s (%s) | |' % (snapshot['spine'], snapshot['spineMapped'], delta('spineMapped'), pct(snapshot['spineMapped'], snapshot['spine'])))
    L.append('| whoa functions (src/, from PDB + source) | %d, stubs %d | |' % (snapshot['whoaFunctions'], snapshot['whoaStubs']))
    L.append('')
    L.append('Match evidence: ' + ', '.join('%s %d' % kv for kv in sorted(by_how.items())) + '. Module anchors: %d assert strings.' % anchors)
    if prev:
        L.append('')
        L.append('Previous run: %s -- mapped %d, ported %d, stub %d, spine mapped %d.' % (prev['date'], prev['mapped'], prev['ported'], prev['stub'], prev['spineMapped']))
    L.append('')
    L.append('## Lua API coverage (binding tables)')
    L.append('')
    L.append('The reference registers %d Lua bindings across %d tables (widget methods per class, and the global function blocks). whoa registers %d of them%s; %d of those are WHOA_UNIMPLEMENTED stubs%s. A missing name is a FrameXML call that raises "attempt to call a nil value"; a stub returns nothing, which is the arity bug class tools/arity.py hunts.' % (
        lua_total, len([r for r in lua_rows]), lua_have, delta('luaHave'), lua_stubbed, delta('luaStubbed')))
    L.append('')
    L.append('| ref table | entries | whoa array | missing | stubbed | first missing / stubbed names |')
    L.append('|---|---:|---|---:|---:|---|')
    for r in lua_rows:
        if not r['missing'] and not r['stubs']:
            continue
        names = ', '.join(r['missing'][:6]) + (' ...' if len(r['missing']) > 6 else '')
        if r['stubs']:
            names += ' / stubs: ' + ', '.join(r['stubs'][:4]) + (' ...' if len(r['stubs']) > 4 else '')
        L.append('| %s (%s..) | %d | `%s` | %d | %d | %s |' % (r['addr'], r['first'], r['count'], r['whoa'] or '-', len(r['missing']), len(r['stubs']), names))
    L.append('')
    L.append('## Coverage by reference module')
    L.append('')
    L.append('Module = the source file named by the reference\'s own assert strings near the function (linker order); `?` = no anchor before it.')
    L.append('')
    L.append('| module | ref fns | bytes | mapped | bytes | stub | verified | spine |')
    L.append('|---|---:|---:|---:|---:|---:|---:|---:|')
    for mod, e in sorted(mods.items(), key=lambda kv: kv[1]['bytes'], reverse=True):
        if e['bytes'] < 2048 and mod != '?':
            continue
        L.append('| %s | %d | %s | %d (%s) | %s | %d | %d | %d |' % (mod, e['fns'], fmt_bytes(e['bytes']), e['mapped'], pct(e['mapped'], e['fns']), pct(e['mappedBytes'], e['bytes']), e['stub'], e['verified'], e['spine']))
    L.append('')

    def row(a):
        r = refs[a]
        w = m.get(a)
        who = ('`%s` [%s]' % (w[0], w[1])) if w else ''
        return '| %s | %s | %d | %d | %s | %s |' % (a, r['module'] + ('' if r['moduleSure'] else '?'), r['size'], r['callers'], who, ', '.join(s[:40] for s in r['strings'][:2]).replace('|', '\\|'))

    L.append('## Next to port: unmapped, on the world spine (by callers x size)')
    L.append('')
    L.append('| addr | module | size | callers | whoa | strings |')
    L.append('|---|---|---:|---:|---|---|')
    L.extend(row(a) for a in top_spine)
    L.append('')
    L.append('## Next to port: unmapped, anywhere')
    L.append('')
    L.append('| addr | module | size | callers | whoa | strings |')
    L.append('|---|---|---:|---:|---|---|')
    L.extend(row(a) for a in top_all)
    L.append('')
    L.append('## Mapped but stubbed (WHOA_UNIMPLEMENTED)')
    L.append('')
    L.append('| addr | module | size | callers | whoa | strings |')
    L.append('|---|---|---:|---:|---|---|')
    L.extend(row(a) for a in stubs)
    L.append('')
    L.append('## Divergence smells: reference strings the whoa counterpart never mentions')
    L.append('')
    L.append('A reference function that formats, asserts or looks up a string its port does not is missing a branch, an error path or a data lookup. Top 40 by count.')
    L.append('')
    L.append('| addr | whoa | missing |')
    L.append('|---|---|---|')
    for n, a, name, missing in smells[:40]:
        L.append('| %s | `%s` | %s |' % (a, name, '; '.join(s[:50] for s in missing[:4]).replace('|', '\\|').replace('\n', '\\n')))
    L.append('')
    L.append('## Largest whoa functions with no reference link')
    L.append('')
    L.append('Either the port added behaviour the reference does not have, or the link is simply unknown: tag it with `// ref: FUN_xxxxxxxx` once found.')
    L.append('')
    L.append('| whoa | lib | code bytes | file |')
    L.append('|---|---|---:|---|')
    for n in whoa_unlinked[:40]:
        w = whoa[n]
        L.append('| `%s` | %s | %d | %s |' % (n, w['lib'], w['size'], w['files'][0] if w['files'] else ''))
    L.append('')
    L.append('## Linked ports with the lowest call-order fidelity')
    L.append('')
    L.append('The port exists but does not make the calls the reference makes, in the order it makes them. Either the port guessed, or its callees are not yet linked (then `--show` lists them as bare addresses). Non-stub, largest first.')
    L.append('')
    L.append('| addr | whoa | call order | ref calls | whoa calls | ref branches | whoa branches | consts | size |')
    L.append('|---|---|---:|---:|---:|---:|---:|---:|---:|')
    low = sorted((a for a in fid if status(a) != 'stub' and not is_faithful(refs, whoa, m, a, fid[a]) and len(refs[a]['calls']) >= 3), key=lambda a: -refs[a]['size'])
    for a in low[:40]:
        br, co = fidelity_dims(refs, whoa, m, a)
        w = whoa[m[a][0]]
        L.append('| %s | `%s` | %.0f%% | %d | %d | %d | %s | %s | %d |' % (
            a, m[a][0], fid[a] * 100, len(refs[a]['calls']), len(w['seq']), refs[a]['branches'],
            str(w['branches']) if w['branches'] >= 0 else '?', ('%.0f%%' % (co * 100)) if co >= 0 else '?', refs[a]['size']))
    L.append('')
    L.append('## Runtime: last call trace (tools/recomp/calltrace.py + tracecompare.py)')
    L.append('')
    if os.path.exists(TRACE_SUMMARY):
        ts = json.load(io.open(TRACE_SUMMARY, encoding='utf-8'))
        L.append('Traced %s: %d frames on each client, frame-level call order agreement %.0f%%, %d functions verified (same per-frame count), %d links contradicted (table below).' % (
            ts['date'], ts['frames'], ts['orderAvg'] * 100, ts['verified'], len(json.load(io.open(SUSPECT_JSON, encoding='utf-8'))) if os.path.exists(SUSPECT_JSON) else 0))
        L.append('')
        L.append('| reference calls every frame, whoa never (hits) | whoa calls, reference never (hits) |')
        L.append('|---|---|')
        miss = ts.get('missing', [])[:20]
        add = ts.get('added', [])[:20]
        for i in range(max(len(miss), len(add))):
            a = '`%s` %d' % (miss[i][1], miss[i][0]) if i < len(miss) else ''
            b = '`%s` %d' % (add[i][1], add[i][0]) if i < len(add) else ''
            L.append('| %s | %s |' % (a, b))
        L.append('')
        L.append('Per-frame count mismatches (ref \\| whoa), largest first:')
        L.append('')
        for d, name, rc, wc in ts.get('mismatch', [])[:15]:
            L.append('- `%s` %s \\| %s' % (name, rc, wc))
        if os.path.exists(SUSPECT_JSON):
            sus = json.load(io.open(SUSPECT_JSON, encoding='utf-8'))
            L.append('')
            L.append('Links the trace contradicts -- a wrong link, or a real divergence; each needs a verdict in overrides.json (corrected link / `diverged` / `unlinked`):')
            L.append('')
            L.append('| addr | whoa | link evidence | why | ref per frame | whoa per frame |')
            L.append('|---|---|---|---|---|---|')
            for a, v in sorted(sus.items()):
                how = m[a][1] if a in m else 'unlinked'
                L.append('| %s | `%s` | %s | %s | %s | %s |' % (a, v['whoa'], how, v['why'], v['ref'], v['whoa_']))
    else:
        L.append('No trace yet. Run both clients into the world, then `calltrace.py ref`, `calltrace.py whoa`, `tracecompare.py`.')
    L.append('')
    L.append('## Iterations')
    L.append('')
    L.append('| run | linked | faithful | stub | spine linked | lua bindings | lua stubs |')
    L.append('|---|---:|---:|---:|---:|---:|---:|')
    hist = []
    if os.path.exists(HISTORY):
        hist = [json.loads(x) for x in io.open(HISTORY, encoding='utf-8').read().splitlines() if x.strip()]
    for h in (hist + [snapshot])[-25:]:
        L.append('| %s | %d (%s) | %d (%s) | %d | %d/%d | %d/%d | %d |' % (
            h['date'], h['mapped'], pct(h['mapped'], h['refFunctions']), h.get('faithful', 0), pct(h.get('faithful', 0), h['refFunctions']),
            h['stub'], h['spineMapped'], h['spine'], h.get('luaHave', 0), h.get('luaTotal', 0), h.get('luaStubbed', 0)))
    L.append('')
    L.append('## How to move a row')
    L.append('')
    L.append('1. Pick the top unmapped spine function. `python tools/recomp/recomp.py --show <addr>` prints its callers, callees, strings and the closest whoa candidates; `C:\\Users\\tyler\\tools\\decomp.sh <out> <addr>` decompiles it.')
    L.append('2. Port it (or find the existing port) and put `// ref: FUN_<addr>` above the whoa definition.')
    L.append('3. When a run shows it behaving like the reference, add `{"<addr>": {"whoa": "<name>", "status": "verified", "note": "..."}}` to overrides.json.')
    L.append('4. Re-run the tool; the totals line shows the delta against the previous run.')
    return '\n'.join(L) + '\n', snapshot


def write_map(refs, whoa, m, overrides):
    out = {}
    for a, (name, how) in sorted(m.items()):
        o = overrides.get(a, {})
        out[a] = {'whoa': name, 'how': how, 'status': o.get('status') or ('stub' if whoa[name]['stub'] else 'ported'),
                  'fidelity': round(fidelity(refs, whoa, m, a), 3),
                  'branchRatio': round(fidelity_dims(refs, whoa, m, a)[0], 3),
                  'constOverlap': round(fidelity_dims(refs, whoa, m, a)[1], 3),
                  'faithful': is_faithful(refs, whoa, m, a, fidelity(refs, whoa, m, a)),
                  'module': refs[a]['module'], 'refSize': refs[a]['size'], 'whoaSize': whoa[name]['size'],
                  'files': whoa[name]['files']}
    json.dump(out, io.open(MAP_OUT, 'w', encoding='utf-8'), indent=1, sort_keys=True)


def show(target, refs, whoa, m):
    rev = {v[0]: a for a, v in m.items()}
    a = None
    if re.fullmatch(r'(0x)?[0-9a-fA-F]{6,8}', target):
        a = target.lower().replace('0x', '').zfill(8)
    elif target in rev:
        a = rev[target]
    elif target in whoa:
        w = whoa[target]
        print('whoa %s  size %d  files %s  stub %s  refs %s' % (target, w['size'], w['files'], w['stub'], sorted(w['refs'])))
        print('  strings:', sorted(w['strings'])[:20])
        print('  callees:', sorted(w['callees'])[:30])
        print('  no reference link')
        return
    if a not in refs:
        print('unknown', target)
        return
    r = refs[a]
    print('ref %s  %s  module %s%s  size %d  callers %d  callSites %d' % (a, r['name'], r['module'], '' if r['moduleSure'] else '?', r['size'], r['callers'], r['callSites']))
    print('  strings:', r['strings'][:20])
    print('  callees:', ' '.join('%s%s' % (c, ('=' + m[c][0]) if c in m else '') for c in r['callees']))
    callers = [x for x, rr in refs.items() if a in rr['callees']]
    print('  callers:', ' '.join('%s%s' % (c, ('=' + m[c][0]) if c in m else '') for c in callers[:30]))
    if a in m:
        name, how = m[a]
        w = whoa[name]
        print('  whoa: %s [%s] size %d files %s stub %s' % (name, how, w['size'], w['files'], w['stub']))
        print('  whoa strings missing on ref side:', sorted(w['strings'] - set(r['strings']))[:10])
        print('  ref strings missing on whoa side:', sorted(set(r['strings']) - w['strings'])[:10])
    else:
        # candidates by shared strings
        cands = collections.Counter()
        for n, w in whoa.items():
            k = len(w['strings'] & set(r['strings']))
            if k:
                cands[n] = k
        print('  unmapped. candidates by shared strings:', cands.most_common(8))


def queue_next(args, refs, whoa, m):
    """Pick the next functions to work and decompile them into docs/recomp/queue/<addr>.c, one
    Ghidra run for the batch. Each file starts with a header: module, size, callers, the linked
    callees (so the port can call the whoa names) and the unlinked ones (so they get tagged next)."""
    sp = spine(refs)
    real = {a: r for a, r in refs.items() if not r['thunk'] and not r['excluded']}

    def weight(a):
        return (refs[a]['callers'] + 1) * refs[a]['size']

    if args.fix:
        pool = [a for a in m if a in real and not whoa[m[a][0]]['stub'] and not is_faithful(refs, whoa, m, a, fidelity(refs, whoa, m, a)) and len(refs[a]['calls']) >= 3]
    elif args.helpers:
        # the small, everywhere-called leaves (allocators, string ops, CVar lookup): every one of
        # them identified lifts the fidelity of hundreds of callers and feeds the call-order matcher
        pool = [a for a in real if a not in m and refs[a]['callers'] >= 20]
    else:
        pool = [a for a in real if a not in m and refs[a]['size'] >= 48]
    if args.spine:
        pool = [a for a in pool if a in sp]
    if args.module:
        pool = [a for a in pool if refs[a]['module'].lower() == args.module.lower()]
    pool.sort(key=(lambda a: refs[a]['callers']) if args.helpers else weight, reverse=True)
    picks = pool[:args.next]
    if not picks:
        print('nothing to queue')
        return
    os.makedirs(QUEUE_DIR, exist_ok=True)
    tmp = os.path.join(QUEUE_DIR, '_batch.txt')
    print('decompiling %d functions (one Ghidra run)...' % len(picks))
    subprocess.run(['bash', DECOMP, tmp] + picks, capture_output=True, text=True)
    text = io.open(tmp, encoding='utf-8', errors='replace').read() if os.path.exists(tmp) else ''
    # decomp.sh writes all functions into one file, each under a "// ===== FUN_xxx" style banner
    parts = re.split(r'(?m)^(?=// =+ )', text)
    by_addr = {}
    for part in parts:
        mm = re.search(r'([0-9a-fA-F]{8})', part[:120])
        if mm:
            by_addr[mm.group(1).lower()] = part
    for a in picks:
        r = refs[a]
        linked = ['%s = %s' % (c, m[c][0]) for c in r['callees'] if c in m]
        unlinked = [c for c in r['callees'] if c not in m and c in refs]
        head = ['// ref FUN_%s  module %s%s  size %d  callers %d  spine %s' % (a, r['module'], '' if r['moduleSure'] else '?', r['size'], r['callers'], a in sp),
                '// strings: ' + '; '.join(s[:60] for s in r['strings'][:8]),
                '// linked callees: ' + ('; '.join(linked) if linked else '-'),
                '// unlinked callees: ' + (' '.join(unlinked) if unlinked else '-'),
                '// when ported: put  // ref: FUN_%s  above the whoa definition, re-run recomp.py' % a, '']
        body = by_addr.get(a, '// (decompilation missing: run decomp.sh by hand)\n')
        io.open(os.path.join(QUEUE_DIR, a + '.c'), 'w', encoding='utf-8', newline='\n').write('\n'.join(head) + body)
        print('  %s  %-22s size %5d callers %4d  -> docs/recomp/queue/%s.c' % (a, r['module'], r['size'], r['callers'], a))
    if os.path.exists(tmp):
        os.remove(tmp)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--export', action='store_true', help='re-export the reference inventory from Ghidra')
    ap.add_argument('--pdb', action='store_true', help='re-dump Whoa.pdb')
    ap.add_argument('--show', metavar='ADDR|NAME', help='print everything known about one function')
    ap.add_argument('--no-history', action='store_true', help='do not append this run to history.jsonl')
    ap.add_argument('--next', type=int, metavar='N', help='decompile the next N functions to port into docs/recomp/queue/')
    ap.add_argument('--spine', action='store_true', help='with --next: only functions on the world spine')
    ap.add_argument('--module', metavar='FILE.cpp', help='with --next: only functions anchored to this reference module')
    ap.add_argument('--fix', action='store_true', help='with --next: queue linked-but-unfaithful ports instead of unlinked functions')
    ap.add_argument('--helpers', action='store_true', help='with --next: queue the most-called unlinked leaves (allocators, string ops ...)')
    args = ap.parse_args()

    if args.export or not os.path.exists(REF_JSONL):
        export_reference()
    if args.pdb or not os.path.exists(PDB_DUMP):
        dump_pdb()

    refs = load_reference()
    anchors = assign_modules(refs)
    overrides = {k.lower().zfill(8): v for k, v in load_overrides().items() if isinstance(v, dict)}
    # Not part of the port: CRT routines Ghidra's library matcher named (_memset, _ftol2 ...) and
    # anything an override marks excluded (nullsubs, compiler helpers, third-party code)
    for a, r in refs.items():
        r['excluded'] = (r['named'] and r['name'].startswith('_')) or overrides.get(a, {}).get('status') == 'excluded'
    src, exact = overlay_clang(parse_sources())
    whoa = merge_whoa(load_pdb_functions(), src)
    overrides = {k.lower().zfill(8): v for k, v in load_overrides().items() if isinstance(v, dict)}
    ref_tables = load_tables()
    pairs = pair_tables(ref_tables, load_whoa_tables())
    m = match(refs, whoa, overrides, pairs)

    # Runtime evidence from the last calltrace/tracecompare run. Matching links become verified.
    # Contradicted ones are only REPORTED: one trace of one scene cannot tell a wrong link from a
    # real behavioural divergence (the reference re-picking bone sequences 200x a frame where whoa
    # does it 7x is the second kind, and is exactly what we want to see). Judging them is the
    # cycle's job; the verdict goes in overrides.json as a corrected link, "diverged", or
    # "unlinked" (no whoa counterpart; the address is then never auto-matched again).
    verified = json.load(io.open(VERIFIED_JSON, encoding='utf-8')) if os.path.exists(VERIFIED_JSON) else {}
    for a, v in verified.items():
        if a in m and m[a][0] == v['whoa'] and a not in overrides:
            overrides[a] = {'whoa': v['whoa'], 'status': 'verified', 'note': v['note'], 'auto': True}

    if args.show:
        show(args.show, refs, whoa, m)
        return

    if args.next:
        queue_next(args, refs, whoa, m)
        return

    report, snapshot = build_report(refs, whoa, m, overrides, anchors, ref_tables, pairs)
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    io.open(REPORT, 'w', encoding='utf-8', newline='\n').write(report)
    write_map(refs, whoa, m, overrides)
    if not args.no_history:
        with io.open(HISTORY, 'a', encoding='utf-8') as h:
            h.write(json.dumps(snapshot) + '\n')
    print('reference %d fns, mapped %d (ported %d, stub %d, verified %d), spine %d/%d; whoa %d fns' % (
        snapshot['refFunctions'], snapshot['mapped'], snapshot['ported'], snapshot['stub'], snapshot['verified'],
        snapshot['spineMapped'], snapshot['spine'], snapshot['whoaFunctions']))
    print('wrote', os.path.relpath(REPORT, ROOT))


if __name__ == '__main__':
    main()
