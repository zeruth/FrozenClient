#!/usr/bin/env python3
"""Reference <-> frozen function map: how much of the 3.3.5a client this port actually covers.

The reference binary has 27.7k functions and no symbols; frozen has ~9k. Nothing links the two
except the code itself, so this builds the link from evidence and keeps it across runs:

  1. reference inventory  tools/recomp/data/ref-functions.jsonl
       every function, its size, callees, callers and the string literals it references
       (Ghidra headless: tools/ghidra-scripts ExportFunctions.java; --export re-runs it)
  2. frozen inventory       from build/dist/bin/Frozen.pdb (names, code sizes, object files) and the
       source itself (bodies, string literals, calls, WHOA_UNIMPLEMENTED, reference annotations)
  3. matching, in order of trust
       override   tools/recomp/overrides.json          -- hand-confirmed, never lost
       annotated  `// ref: FUN_004f8ea0` above a frozen definition (or FUN_/Sub_ in its body)
       string     a literal both sides reference, weighted by how rare it is
       callgraph  a matched pair whose only unmatched callee on each side must be each other
       callorder  between two linked calls inside a linked pair, a single unlinked call each side
       order      definition order: between two linked anchors from one file, the unlinked frozen
                  definitions and the unlinked reference addresses pair up when their counts agree
  4. report               docs/recomp/REPORT.md + data/map.json + data/history.jsonl

Run it after every porting session:

    python tools/recomp/recomp.py            # inventory + match + report
    python tools/recomp/recomp.py --export   # also re-export the reference from Ghidra (2-3 min)
    python tools/recomp/recomp.py --pdb      # also re-dump Frozen.pdb (after a build)
    python tools/recomp/recomp.py --show 004f8ea0     # everything known about one reference fn
    python tools/recomp/recomp.py --show CGWorldFrame::OnWorldRender

The report is the checklist. Each mapped pair carries a status: ported (matched, has a body),
stub (matched but WHOA_UNIMPLEMENTED), verified (override says it was seen behaving like the
reference). Unmapped reference functions are ranked by weight = callers x size, restricted to the
world spine (reachable from CGWorldFrame::OnFrameRender) and globally, so the next thing to port is
always the top of a list rather than a guess.
"""

import argparse
import bisect
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
PDB_DUMP = os.path.join(DATA, 'frozen-pdb.txt')
OVERRIDES = os.path.join(HERE, 'overrides.json')
MAP_OUT = os.path.join(DATA, 'map.json')
HISTORY = os.path.join(DATA, 'history.jsonl')
REPORT = os.path.join(ROOT, 'docs', 'recomp', 'REPORT.md')

GHIDRA_PROJECTS = r'C:\Users\tyler\tools\ghidra-projects'
GHIDRA_SCRIPTS = os.path.join(HERE, 'ghidra')  # the exporters live with the tool
PDBUTIL = r'C:\Program Files\LLVM\bin\llvm-pdbutil.exe'
PDB = os.path.join(ROOT, 'build', 'dist', 'bin', 'Frozen.pdb')

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


# The render spine: what draws the world and the things in it. It is deliberately rooted in the
# model scene as well as the frame, because CM2Scene::Animate and ::Draw reach a large subtree
# (entity animation and the M2 passes) that nothing else does -- which is why this set comes out
# bigger than the OnFrameRender spine, not smaller. This is the queue for entity and environment
# rendering accuracy, where the visible bugs live and where coverage is thinnest.
# The modules that draw the world and the things in it. Reachability alone cannot isolate these:
# world text reaches the chat frame and model animation reaches the sound engine, so the render
# spine legitimately contains both. This list is the surface that "graphics accuracy" means, and
# --render queues unmapped functions inside it, worst covered first.
RENDER_MODULES = {
    # environment
    'Map.cpp', 'MapChunk.cpp', 'MapChunkLiquid.cpp', 'MapMem.cpp', 'MapLoad.cpp', 'MapArea.cpp',
    'MapObj.cpp', 'MapObjRead.cpp', 'MapObjGroup.cpp', 'DetailDoodad.cpp', 'WorldParam.cpp',
    'MapWeather.cpp', 'DayNight.cpp', 'Sky.cpp', 'MapShadow.cpp',
    # entities and their models
    'M2Scene.cpp', 'M2Shared.cpp', 'M2Model.cpp', 'ModelBlob.cpp', 'CharacterModelBase.cpp',
    'Unit_C.cpp', 'Player_C.cpp', 'GameObject_C.cpp', 'UnitMissileTrajectory_C.cpp',
    'MovementShared.cpp', 'CreepTendril.cpp', 'ObjectEffect.cpp',
    # textures, effects and the device
    'Texture.cpp', 'TextureCache.cpp', 'TextureBlob.cpp', 'FFXEffects.cpp', 'ShaderEffectManager.cpp',
    'CGxDevice.cpp', 'CGxDeviceD3d9Ex.cpp', 'CGxD3d9ExTexture.cpp', 'CGxDeviceOpenGl.cpp',
}

RENDER_ROOTS = ['004faf90',   # CGWorldFrame::RenderWorld
                '004f8ea0',   # CGWorldFrame::OnWorldRender
                '007831a0',   # CWorld::Update      (weather, day/night, map streaming)
                '0079a870',   # CMap::Render        (terrain, chunks, map objects)
                '00821a20',   # CM2Scene::Animate   (entity animation)
                '00823cb0']   # CM2Scene::Draw      (entity passes)


def spine(refs, roots=None):
    seen = set()
    stack = [r for r in (roots or SPINE_ROOTS) if r in refs]
    while stack:
        a = stack.pop()
        if a in seen:
            continue
        seen.add(a)
        stack.extend(c for c in refs[a]['callees'] if c in refs and c not in seen)
    return seen


# ----------------------------------------------------------------------------------------------
# frozen side

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
                    if lib == 'Frozen':
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
# `// ref: FUN_00767fc0` (this project) or frozen upstream's `// 0x7681F0 in the original`
REF_TAG_RE = re.compile(r'//\s*ref:\s*(?:FUN_|0x)?(00[4-9a-fA-F][0-9a-fA-F]{5}|[4-9a-fA-F][0-9a-fA-F]{5})\b'
                        r'|0x(00[4-9a-fA-F][0-9a-fA-F]{5}|[4-9a-fA-F][0-9a-fA-F]{5})\s+(?:with[^)]*)?in the original')


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
            # definition, or the address frozen baked into the name (CM2Model::Sub826350 IS
            # FUN_00826350). A FUN_/Sub mention inside the body names a callee, not this function
            # -- reading those as self-links is what linked SetBoneSequence to its own helper.
            above = text[max(0, text.rfind('\n\n', 0, m.start())):m.start()]
            refs = set((a or b).lower().zfill(8) for a, b in REF_TAG_RE.findall(above))
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


CLANG_JSON = os.path.join(DATA, 'frozen-clang.json')


def msvc_nested(name):
    """MSVC's PDB separates nested template closers: TSList<X,TSGetLink<X> >::Head."""
    while '>>' in name:
        name = name.replace('>>', '> >')
    return name


def overlay_clang(src):
    """Replace the regex parser's guesses with libclang's exact answers where clangparse.py has
    run: ordered resolved calls, literals, branch counts. Tags (refs) stay with the regex pass, which
    reads comments. STL and lambda calls are dropped from the sequence: the reference has neither."""
    if not os.path.exists(CLANG_JSON):
        return src, False
    clang = json.load(io.open(CLANG_JSON, encoding='utf-8'))
    for name, c in clang.items():
        seq = [msvc_nested(x) for x in c['callseq'] if not x.startswith('std::') and '(lambda' not in x and not x.startswith('?')]
        e = src.get(name)
        if not e:
            e = src[name] = {'name': name, 'files': set(c['files']), 'strings': set(), 'calls': set(), 'callseq': [],
                             'refs': set(), 'stub': c['stub'], 'lines': c['lines']}
        e['callseq'] = seq
        e['calls'] = set(seq)
        e['refs'] |= set(c.get('refs', []))  # tags above header-inline definitions, read by clangparse
        e['line'] = c.get('line', 0)
        e['files'] |= set(c['files'])
        e['strings'] = set(s for s in c['strings'] if len(s) >= 3 and s not in NOISE_STRINGS)
        e['consts'] = set(c['consts'])
        e['branches'] = c['branches']
        e['stub'] = c['stub']
        e['exact'] = True
    return src, True


TEMPLATE_ARGS_RE = re.compile(r'^([\w:]+)(<.+>)::([^:]+)$')


def expand_inlined(callseq, frozen, inlined, depth=0):
    """frozen call sequence with header-only inlined callees replaced by their own calls."""
    out = []
    for c in callseq:
        if c in frozen:
            out.append(c)
            continue
        pattern, args = c, ''
        m = TEMPLATE_ARGS_RE.match(c)
        if m and c not in inlined:
            pattern, args = m.group(1) + '::' + m.group(3), m.group(2)
        body = inlined.get(pattern)
        if body is None or depth >= 3:
            out.append('?' + c)
            continue
        if args:
            # the pattern's callees are spelled without arguments; give them this instantiation's
            body = [(n.replace('::', args + '::', 1) if n.replace('::', args + '::', 1) in frozen or n.replace('::', args + '::', 1) in inlined else n) if '<' not in n and '::' in n else n for n in body]
        out.extend(expand_inlined(body, frozen, inlined, depth + 1))
    return out


def merge_frozen(pdb, src):
    frozen = {}
    inlined = {}  # header-only functions with no PDB symbol: name -> callseq
    for name, s in src.items():
        p = pdb.get(name)
        if not p and s['files'] and all(f.endswith(('.hpp', '.h')) for f in s['files']):
            # defined in a header and inlined at every use: no function in frozen's binary, and the
            # reference inlined it too. Counting it as a callee would break the callgraph votes;
            # instead its calls are spliced into its callers below, as the compiler did.
            inlined[name] = s['callseq']
            continue
        files = sorted(s['files'])
        parts = files[0].split('/')
        frozen[name] = {'name': name, 'files': files, 'strings': s['strings'], 'calls': s['calls'], 'callseq': s['callseq'],
                      'refs': s['refs'], 'stub': s['stub'], 'lines': s['lines'], 'exact': s.get('exact', False),
                      'consts': s.get('consts', set()), 'branches': s.get('branches', -1), 'line': s.get('line', 0),
                      'size': p['size'] if p else 0, 'lib': p['lib'] if p else (parts[1] if len(parts) > 2 else '?')}
    for name, p in pdb.items():
        if name not in frozen:
            frozen[name] = {'name': name, 'files': [], 'strings': set(), 'calls': set(), 'callseq': [], 'refs': set(), 'stub': False,
                          'lines': 0, 'size': p['size'], 'lib': p['lib'], 'exact': False, 'consts': set(), 'branches': -1}
    # resolve calls to frozen function keys: same class first, then a unique short-name match
    short = collections.defaultdict(list)
    for name in frozen:
        short[name.split('::')[-1]].append(name)
    for name, w in frozen.items():
        cls = name.rsplit('::', 1)[0] if '::' in name else None

        def resolve(c):
            cands = short.get(c)
            if not cands:
                return None
            if cls and cls + '::' + c in frozen:
                return cls + '::' + c
            if len(cands) == 1:
                return cands[0]
            return None

        if w['exact']:
            # libclang already resolved the callee: its qualified name is the key. A callee that is
            # header-only and never got a PDB symbol was inlined, so its own calls stand in for it
            # (three levels deep), with template arguments carried into the pattern's callee names:
            # TSGrowableArray<unsigned int>::New expands to TSGrowableArray<unsigned int>::Reserve.
            w['seq'] = expand_inlined(w['callseq'], frozen, inlined)
            w['callees'] = set(c for c in w['seq'] if not c.startswith('?'))
            continue
        w['callees'] = set(k for k in (resolve(c) for c in w['calls']) if k)
        # ordered, unresolved names kept as '?name' so the sequence keeps its shape
        w['seq'] = [resolve(c) or '?' + c for c in w['callseq']]
    return frozen


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


CRT_NAME_RE = re.compile(r'^(?:FID_conflict_)?_{1,2}([A-Za-z]\w*)$')


def crt_token(name):
    """CRT calls the compiler kept as calls on both sides (malloc, memset, sscanf, _msize) are
    named `_malloc` by Ghidra and `?malloc` in an unresolved frozen sequence; both become crt:malloc
    so the two sequences can align on them."""
    m = CRT_NAME_RE.match(name)
    return 'crt:' + m.group(1).lower() if m else None


def ref_seq(refs, m, addr):
    out = []
    for c in refs[addr]['calls']:
        if c in m:
            out.append(m[c][0])
        elif c in refs and refs[c]['named'] and refs[c]['excluded']:
            out.append(crt_token(refs[c]['name']) or c)
        else:
            out.append(c)
    return out


def frozen_seq(frozen, name):
    out = []
    for c in frozen[name]['seq']:
        if c.startswith('?'):
            out.append(crt_token('_' + c[1:].lstrip('_')) or c)
        else:
            out.append(c)
    return out


def expand_inlined_calls(wseq, rset, frozen, _memo=None):
    """The reference compiler inlined the small gx wrappers: where the port calls GxRsSet five
    times, the reference shows five CGxDevice::IRsDirty, which is the same work done. Rewrite a
    port call into the calls it makes, but only on evidence and only for this pair: the callee is
    absent from the reference's own sequence while its expansion reaches something the reference
    does call. A callee the reference really calls is left alone, and so is one whose expansion has
    nothing in common with the reference, so this cannot manufacture agreement. The test is on the
    whole expansion, not the immediate children, because a wrapper often reaches the inlined work
    through another wrapper (GxRsSet -> CGxDevice::RsSet -> IRsDirty)."""
    memo = {} if _memo is None else _memo

    def one(c, depth):
        if c in rset or depth >= 4 or c not in frozen:
            return [c]
        key = (c, depth)
        if key in memo:
            return memo[key]
        memo[key] = [c]  # cycle guard while this call is being expanded
        sub = [x for x in frozen[c]['seq'] if not x.startswith('?')]
        out = [c]
        if sub:
            expanded = [t for x in sub for t in one(x, depth + 1)]
            if any(t in rset for t in expanded):
                out = expanded
        memo[key] = out
        return out

    return [t for c in wseq for t in one(c, 0)]


def fidelity(refs, frozen, m, addr):
    """How much of the reference's call sequence the port reproduces, in order: LCS of the two call
    sequences over the longer one, with reference callees translated through the map. 1.0 means every
    call the reference makes, the port makes, in the same order. Unlinked reference callees can never
    match, so a low score also says 'dependencies still unidentified'."""
    name = m[addr][0]
    rseq = ref_seq(refs, m, addr)
    wseq = frozen_seq(frozen, name)
    if not rseq:
        return 1.0 if not frozen[name]['stub'] else 0.0
    wseq = expand_inlined_calls(wseq, set(rseq), frozen)
    # Recall of the reference's sequence: extra calls on the frozen side (helpers the reference
    # compiler inlined, constructors) do not count against it; missing or reordered ones do.
    return lcs_len(rseq, wseq) / float(len(rseq))


def precision(refs, frozen, m, addr):
    """Share of the port's calls that the reference also makes, in order. Low with high recall
    means the port does more than the reference: inlined helpers, or invented behaviour."""
    name = m[addr][0]
    rseq = ref_seq(refs, m, addr)
    wseq = frozen_seq(frozen, name)
    if not wseq:
        return 1.0
    return lcs_len(rseq, wseq) / float(len(wseq))


def fidelity_dims(refs, frozen, m, addr):
    """The other two structural checks, when both sides can answer them (libclang inventory):
    branch ratio = min/max of the conditional-branch counts (1.0 = same shape), const overlap =
    share of the reference's notable immediates the port's literals also contain. -1 = unknown."""
    r = refs[addr]
    w = frozen[m[addr][0]]
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


def is_faithful(refs, frozen, m, addr, fid):
    """Call order >= FAITHFUL, and when the reference has real control flow (>= 4 branches) and the
    port's branch count is known, the shapes must be within a factor of two."""
    if fid < FAITHFUL or frozen[m[addr][0]]['stub']:
        return False
    br, co = fidelity_dims(refs, frozen, m, addr)
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


FROZEN_TABLE_RE = re.compile(r'(FrameScript_Method|FrameScript_Function)\s+([\w:]+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\};', re.S)
FROZEN_ENTRY_RE = re.compile(r'\{\s*"(\w+)"\s*,\s*&?([\w:]+)\s*\}')


def load_frozen_tables():
    """frozen's own binding arrays, in source order: [{'name': array, 'file', 'entries': [(lua name, fn)]}]."""
    tables = []
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        text = io.open(path, encoding='utf-8', errors='replace').read()
        for m in FROZEN_TABLE_RE.finditer(text):
            entries = [(n, f) for n, f in FROZEN_ENTRY_RE.findall(m.group(3))]
            if entries:
                tables.append({'name': m.group(2), 'file': os.path.relpath(path, ROOT).replace('\\', '/'), 'entries': entries})
    return tables


def pair_tables(ref_tables, frozen_tables):
    """Match frozen binding arrays to reference tables by shared names; a reference table can only be
    claimed once. Returns [(frozen table, ref table, shared name count)]."""
    pairs = []
    for wt in frozen_tables:
        wnames = set(n for n, _ in wt['entries'])
        best = None
        for rt in ref_tables:
            k = len(wnames & set(n for n, _ in rt['entries']))
            if k and (best is None or k > best[0]):
                best = (k, rt)
        if best and (best[0] >= 3 or best[0] == len(wnames)):
            pairs.append((wt, best[1], best[0]))
    # one reference table per frozen table, best claim wins
    claimed = {}
    for wt, rt, k in sorted(pairs, key=lambda p: -p[2]):
        if rt['addr'] not in claimed:
            claimed[rt['addr']] = (wt, rt, k)
    return list(claimed.values())


HANDLERS_JSONL = os.path.join(DATA, 'ref-handlers.jsonl')


def load_handler_pairs():
    """Packet handlers by opcode: the reference's SetMessageHandler(opcode, fn) call sites
    (ExportCallArgs.java on FUN_006b0b80) against frozen's ClientServices::SetMessageHandler(SMSG_X,
    Fn) registrations, with SMSG_X resolved through src/net/Types.hpp. Returns [(ref fn, frozen
    name, opcode)]."""
    if not os.path.exists(HANDLERS_JSONL):
        return []
    ref = {}
    with io.open(HANDLERS_JSONL, encoding='utf-8') as f:
        for line in f:
            r = json.loads(line)
            a = r['args']
            if len(a) >= 2 and a[1].startswith('fn:'):
                try:
                    ref.setdefault(int(a[0], 16), set()).add(a[1][3:].lower())
                except ValueError:
                    pass
    enum = {}
    types = os.path.join(ROOT, 'src', 'net', 'Types.hpp')
    if os.path.exists(types):
        for m in re.finditer(r'\b([A-Z][A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)', io.open(types, encoding='utf-8').read()):
            enum[m.group(1)] = int(m.group(2), 0)
    pairs = []
    frozen_ops = set()
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        text = io.open(path, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'SetMessageHandler\(\s*([A-Z][A-Z0-9_]+)\s*,\s*&?([\w:]+)', text):
            op = enum.get(m.group(1))
            if op is None:
                continue
            frozen_ops.add(op)
            if op not in ref or len(ref[op]) != 1:
                continue
            pairs.append((next(iter(ref[op])), m.group(2), m.group(1)))
    names = {v: k for k, v in enum.items()}
    load_handler_pairs.coverage = {'ref': ref, 'frozen': frozen_ops, 'names': names}
    return pairs


load_handler_pairs.coverage = None


CVARS_JSONL = os.path.join(DATA, 'ref-cvars.jsonl')


def load_cvar_pairs():
    """CVar callbacks by cvar name: the reference's CVar::Register(name, help, flags, default,
    callback, ...) call sites (ExportCallArgs.java on FUN_00767fc0) against frozen's
    CVar::Register("name", ..., &Callback, ...). Returns [(ref fn, frozen callback name, cvar)] and
    stores the name sets for the coverage section."""
    if not os.path.exists(CVARS_JSONL):
        return []
    ref = {}
    with io.open(CVARS_JSONL, encoding='utf-8') as f:
        for line in f:
            r = json.loads(line)
            a = r['args']
            if a and a[0].startswith('str:'):
                name = a[0][4:]
                cb = a[4][3:].lower() if len(a) > 4 and a[4].startswith('fn:') else None
                ref[name.lower()] = (name, cb)
    frozen_cvars = {}
    pat = re.compile(r'CVar::Register\(\s*"([^"]+)"\s*,\s*(?:"(?:[^"\\]|\\.)*"|nullptr|[^,]+)\s*,\s*[^,]+,\s*(?:"(?:[^"\\]|\\.)*"|[^,]+)\s*,\s*&?([\w:]+)', re.S)
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        text = io.open(path, encoding='utf-8', errors='replace').read()
        for m in pat.finditer(text):
            frozen_cvars[m.group(1).lower()] = m.group(2)
    pairs = []
    for key, (name, cb) in ref.items():
        w = frozen_cvars.get(key)
        if cb and w and w != 'nullptr':
            pairs.append((cb, w, name))
    load_cvar_pairs.coverage = {'ref': ref, 'frozen': frozen_cvars}
    return pairs


load_cvar_pairs.coverage = None


def load_overrides():
    if not os.path.exists(OVERRIDES):
        return {}
    return json.load(io.open(OVERRIDES, encoding='utf-8'))


def match(refs, frozen, overrides, tables):
    """addr -> (frozen name, confidence)"""
    m = {}
    used = set()
    evidence = {}

    unlinked = set(a for a, o in overrides.items() if o.get('status') == 'unlinked')

    HAND = ('override', 'annotated')

    def bind(addr, name, how, why=''):
        # Hand evidence -- an overrides.json entry or a // ref: tag -- may bind one frozen name to
        # several reference functions, because C++ overloads collapse to a single key here
        # (TextureCreate x3, CDataStore::Put x4) and COMDAT folding leaves the reference with
        # copies. Automatic evidence may not: one guess per name, or the matchers would spray a
        # popular name across a whole neighbourhood.
        if addr in m or addr not in refs or name not in frozen or (name in used and how not in HAND):
            return False
        if addr in unlinked:
            return False  # judged to have no frozen counterpart; automatic evidence does not reopen it
        m[addr] = (name, how)
        used.add(name)
        evidence[addr] = why
        return True

    for addr, o in overrides.items():
        addr = addr.lower().zfill(8)
        if o.get('frozen'):
            bind(addr, o['frozen'], 'override')

    for name, w in sorted(frozen.items()):
        for addr in sorted(w['refs']):
            bind(addr, name, 'annotated', 'tag in ' + (w['files'][0] if w['files'] else '?'))

    # binding tables: frozen's FrameScript_Method/Function arrays paired with the reference's by
    # shared names, then each entry bound by name inside its pair (so CSimpleFrame's AddLine and
    # CSimpleHTML's AddLine each find their own)
    for wt, rt, k in tables:
        rfn = dict(rt['entries'])
        for lua_name, fn in wt['entries']:
            a = rfn.get(lua_name)
            if a:
                # frozen spells the handler as it likes; resolve the array's function name to a key
                key = fn if fn in frozen else next((n for n in frozen if n.endswith('::' + fn)), None)
                if key:
                    bind(a, key, 'table', 'binding "%s" in %s ~ table %s' % (lua_name, wt['name'], rt['addr']))

    # packet handlers: the same opcode registered on both sides names the same function
    for ref_fn, fn, opcode in load_handler_pairs():
        key = fn if fn in frozen else next((n for n in frozen if n.endswith('::' + fn)), None)
        if key:
            bind(ref_fn, key, 'handler', 'SetMessageHandler(%s) on both sides' % opcode)

    # cvar callbacks: the same cvar name registered with a callback on both sides
    for ref_fn, fn, cvar in load_cvar_pairs():
        key = fn if fn in frozen else next((n for n in frozen if n.endswith('::' + fn)), None)
        if key:
            bind(ref_fn, key, 'cvar', 'callback of CVar::Register("%s") on both sides' % cvar)

    # string anchors: rarity-weighted overlap, accepted when the best candidate is clearly best
    ref_by_string = collections.defaultdict(set)
    for addr, r in refs.items():
        for s in r['strings']:
            if len(s) >= 3 and s not in NOISE_STRINGS:
                ref_by_string[s].add(addr)
    frozen_by_string = collections.defaultdict(set)
    for name, w in frozen.items():
        for s in w['strings']:
            frozen_by_string[s].add(name)
    for s, rset in sorted(ref_by_string.items(), key=lambda kv: (len(kv[1]) * len(frozen_by_string.get(kv[0], ())), kv[0])):
        wset = frozen_by_string.get(s)
        if not wset or len(rset) != 1 or len(wset) != 1:
            continue
        bind(next(iter(rset)), next(iter(wset)), 'string', 'unique "%s"' % s[:60])
    scores = collections.defaultdict(float)
    for s, rset in ref_by_string.items():
        wset = frozen_by_string.get(s)
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
            shared = sorted(s for s in refs[a]['strings'] if s in frozen[cands[0][1]]['strings'])
            bind(a, cands[0][1], 'string', 'score %.2f shared %s' % (cands[0][0], '; '.join(x[:40] for x in shared[:3])))

    # call-graph propagation: a matched pair with exactly one unmatched callee each side proposes
    # that those two are the same. One parent is not proof (a port that calls one helper the
    # reference does not, or vice versa, proposes nonsense); the pair has to be proposed by two
    # different parents, or be the only proposal for both of its members.
    for _ in range(4):
        votes = collections.defaultdict(set)
        for addr, (name, how) in list(m.items()):
            # Two readings of "the only unmatched callee", strict first. Strict counts every
            # unmatched callee, which is right when they are all part of the port. Relaxed
            # ignores excluded ones -- CRT, and the folded nullsub at 005eeb70 that is a single
            # ret with 1692 callers -- which is right when an unported helper is the only thing
            # standing between two functions that must be each other. Taking strict first keeps
            # the links the first reading already found and lets the second add to them.
            cand = [c for c in refs[addr]['callees'] if c not in m and c in refs and not refs[c]['thunk']]
            rc = cand if len(cand) == 1 else [c for c in cand if not refs[c]['excluded']]
            wc_all = [c for c in frozen[name]['callees'] if c not in used and c in frozen]
            # template instantiations (TSBaseArray<X>::operator[]) are usually inlined in the
            # reference, so they must not block a vote; they can still be the vote when alone
            wc = [c for c in wc_all if '<' not in c] or (wc_all if len(wc_all) == 1 else [])
            if len(rc) == 1 and len(wc) == 1:
                votes[(rc[0], wc[0])].add(addr)
        by_ref = collections.Counter(p[0] for p in votes)
        by_frozen = collections.Counter(p[1] for p in votes)
        added = 0
        for (rc, wc), parents in sorted(votes.items(), key=lambda kv: -len(kv[1])):
            if len(parents) >= 2 or (by_ref[rc] == 1 and by_frozen[wc] == 1):
                added += bind(rc, wc, 'callgraph', 'only unmatched callee of %s' % ', '.join('%s=%s' % (p, m[p][0]) for p in sorted(parents)[:3]))
        if not added:
            break

    # call-order alignment: inside a linked pair, walk both call sequences; between two linked
    # anchors, a single unlinked call on each side is the same call. Same two-parent rule.
    for _ in range(3):
        votes = collections.defaultdict(set)
        for addr, (name, how) in list(m.items()):
            rseq = refs[addr]['calls']
            wseq = frozen[name]['seq']
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
                    if rc in refs and not wc.startswith('?') and wc in frozen and rc not in m and wc not in used:
                        votes[(rc, wc)].add(addr)
        by_ref = collections.Counter(p[0] for p in votes)
        by_frozen = collections.Counter(p[1] for p in votes)
        added = 0
        for (rc, wc), parents in sorted(votes.items(), key=lambda kv: -len(kv[1])):
            if len(parents) >= 2 or (by_ref[rc] == 1 and by_frozen[wc] == 1):
                added += bind(rc, wc, 'callorder', 'same slot between linked calls in %s' % ', '.join('%s=%s' % (p, m[p][0]) for p in sorted(parents)[:3]))
        if not added:
            break

    # definition-order propagation: MSVC lays a translation unit's functions out in definition
    # order, so between two linked anchors from the same file the unlinked frozen definitions and the
    # unlinked reference addresses pair up in order when their counts agree. Anchors that break the
    # monotonic order (a wrong link) are left out via the longest increasing subsequence.
    rev = {name: addr for addr, (name, how) in m.items()}
    by_file = collections.defaultdict(list)
    for name, w in frozen.items():
        if w.get('line') and w['files']:
            by_file[w['files'][0]].append((w['line'], name))
    ref_order = sorted(int(a, 16) for a, r in refs.items() if not r['thunk'] and not r['excluded'])
    # the binding tables name reference functions (Show, running); a frozen function whose short
    # name is one of those, in a file that already has anchors, is an anchor candidate too. The
    # order check below keeps only the candidates that sit where the address order says they should.
    tabname = collections.defaultdict(set)
    tab_by_addr = collections.defaultdict(set)
    for rt in load_tables():
        for n, fn in rt['entries']:
            tabname[n.lower()].add(fn)
            tab_by_addr[fn].add(n.lower())
    for f, lst in by_file.items():
        lst.sort()
        anchors = [(line, int(rev[name], 16), name, None) for line, name in lst if name in rev]
        if len(anchors) < 2:
            continue
        for line, name in lst:
            if name in rev or name in used:
                continue
            short = name.rsplit('::', 1)[-1]
            cands = set(tabname.get(short.lower(), ())) | set(tabname.get(short.rsplit('_', 1)[-1].lower(), ()))
            cands = set(fn for fn in cands if fn in refs and fn not in m and not refs[fn]['excluded'])
            if len(cands) == 1:
                fn = cands.pop()
                anchors.append((line, int(fn, 16), name, fn))
        anchors.sort()
        best = [1] * len(anchors)
        prev = [-1] * len(anchors)
        for i in range(len(anchors)):
            for j in range(i):
                if anchors[j][1] < anchors[i][1] and best[j] + 1 > best[i]:
                    best[i], prev[i] = best[j] + 1, j
        i = max(range(len(anchors)), key=lambda k: best[k])
        chain = []
        while i >= 0:
            chain.append(anchors[i])
            i = prev[i]
        chain.reverse()
        # a table-named candidate that kept its place in the address order is itself a link
        for line, addr, name, fn in chain:
            if fn is not None:
                bind(fn, name, 'order', 'binding table name %s, in definition order in %s' % (name.rsplit('::', 1)[-1], f))
        chain = [(l, a, n) for l, a, n, fn in chain if fn is None or n in used]
        for (l0, a0, n0), (l1, a1, n1) in zip(chain, chain[1:]):
            W = [name for line, name in lst if l0 < line < l1 and name not in used]
            lo, hi = bisect.bisect_right(ref_order, a0), bisect.bisect_left(ref_order, a1)
            R = ['%08x' % a for a in ref_order[lo:hi] if '%08x' % a not in m]
            if W and len(W) == len(R):
                # veto: a binding table naming any address in the interval must agree with the
                # frozen function it would pair with; frozen's aggregate script files do not always
                # follow one reference translation unit, and this is where that shows
                vetoed = False
                for name, addr in zip(W, R):
                    names = tab_by_addr.get(addr)
                    if not names:
                        continue
                    short = name.rsplit('::', 1)[-1].lower()
                    tail = short.rsplit('_', 1)[-1]
                    if not any(t == short or t == tail or t.startswith(tail) or tail.startswith(t) for t in names):
                        vetoed = True
                        break
                if vetoed:
                    continue
                for name, addr in zip(W, R):
                    bind(addr, name, 'order', 'definition order between %s and %s in %s' % (n0, n1, f))

    with io.open(MATCHES_TSV, 'w', encoding='utf-8', newline='\n') as out:
        out.write('addr\thow\tfrozen\tevidence\n')
        for a, (name, how) in sorted(m.items()):
            why = evidence.get(a, '').replace('\n', ' ').replace('\t', ' ')
            out.write('%s\t%s\t%s\t%s\n' % (a, how, name, why))
    return m


# ----------------------------------------------------------------------------------------------
# report

def fmt_bytes(n):
    return '%.1fk' % (n / 1024.0) if n < 1024 * 1024 else '%.2fM' % (n / 1048576.0)


def lua_coverage(ref_tables, pairs, frozen, frozen_tables=()):
    """Per reference binding table: how many of its names frozen registers, and which are missing or
    stubbed. A name counts wherever frozen registers it: the reference splits the globals into many
    small tables and frozen keeps a few big arrays, so the one-to-one pairing (used for linking) is
    not the measure. The paired array is still shown when there is one."""
    by_ref = {rt['addr']: (wt, k) for wt, rt, k in pairs}
    registered = {}  # lua name -> (frozen fn, array name) anywhere in frozen
    for wt in frozen_tables:
        for n, f in wt['entries']:
            registered.setdefault(n, (f, wt['name']))
    rows = []
    total = have = stubbed = 0
    for rt in ref_tables:
        if len(rt['entries']) < 4:
            continue  # not a binding table: a two-entry pair in some other structure
        names = [n for n, _ in rt['entries']]
        wt = by_ref.get(rt['addr'], (None, 0))[0]
        wnames = {n: registered[n][0] for n in names if n in registered}
        if not wt:
            arrays = sorted(set(registered[n][1] for n in names if n in registered))
            wt = {'name': ', '.join(arrays[:2]) + (' ...' if len(arrays) > 2 else ''), 'file': ''} if arrays else None
        missing = [n for n in names if n not in wnames]
        stubs = []
        for n in names:
            f = wnames.get(n)
            if f:
                key = f if f in frozen else next((x for x in frozen if x.endswith('::' + f)), None)
                if key and frozen[key]['stub']:
                    stubs.append(n)
        total += len(names)
        have += len(names) - len(missing)
        stubbed += len(stubs)
        rows.append({'addr': rt['addr'], 'count': len(names), 'frozen': wt['name'] if wt else '', 'file': wt['file'] if wt else '',
                     'missing': missing, 'stubs': stubs, 'first': names[0]})
    rows.sort(key=lambda r: -(len(r['missing']) + len(r['stubs'])))
    return total, have, stubbed, rows


def build_report(refs, frozen, m, overrides, anchors, ref_tables=(), pairs=(), frozen_tables=()):
    lua_total, lua_have, lua_stubbed, lua_rows = lua_coverage(ref_tables, pairs, frozen, frozen_tables)
    sp = spine(refs)
    rsp = spine(refs, RENDER_ROOTS)
    real = {a: r for a, r in refs.items() if not r['thunk'] and not r['excluded']}
    total = len(real)
    total_bytes = sum(r['size'] for r in real.values())

    def status(addr):
        name, how = m[addr]
        o = overrides.get(addr, {})
        if o.get('status'):
            return o['status']
        return 'stub' if frozen[name]['stub'] else 'ported'

    by_status = collections.Counter()
    bytes_by_status = collections.Counter()
    by_how = collections.Counter()
    fid = {}
    faithful = faithful_bytes = 0
    for a in real:
        if a in m:
            st = status(a)
            by_how[m[a][1]] += 1
            fid[a] = fidelity(refs, frozen, m, a)
            # 'faithful' as an override status is a hand verdict for ports the static measure
            # misjudges -- a loop over a table where the reference unrolls, say -- and must
            # carry the reason in its note
            if st != 'stub' and (st in ('faithful', 'verified') or is_faithful(refs, frozen, m, a, fid[a])):
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
    # divergence smell: reference strings the frozen counterpart does not carry
    smells = []
    for a, (name, how) in m.items():
        rs = set(s for s in refs[a]['strings'] if len(s) >= 3 and s not in NOISE_STRINGS and not MODULE_STRING.match(s))
        missing = sorted(rs - frozen[name]['strings'])
        if missing and how != 'override':
            smells.append((len(missing), a, name, missing))
    smells.sort(reverse=True)

    frozen_unlinked = sorted((n for n, w in frozen.items() if n not in {v[0] for v in m.values()} and w['size']),
                           key=lambda n: frozen[n]['size'], reverse=True)

    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    snapshot = {'date': now, 'refFunctions': total, 'refBytes': total_bytes,
                'mapped': total - by_status['unmapped'], 'mappedBytes': total_bytes - bytes_by_status['unmapped'],
                'ported': by_status['ported'], 'stub': by_status['stub'], 'verified': by_status['verified'],
                'faithful': faithful, 'faithfulBytes': faithful_bytes,
                'spine': len(sp & set(real)), 'spineMapped': sum(1 for a in sp if a in m and a in real),
                'renderSpine': len(rsp & set(real)), 'renderSpineMapped': sum(1 for a in rsp if a in m and a in real),
                'renderSurface': sum(1 for a, r in real.items() if r.get('module') in RENDER_MODULES),
                'renderSurfaceMapped': sum(1 for a, r in real.items() if r.get('module') in RENDER_MODULES and a in m),
                'frozenFunctions': len(frozen), 'frozenStubs': sum(1 for w in frozen.values() if w['stub']),
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
    L.append('# Recomp map: reference 3.3.5a (12340) vs frozen')
    L.append('')
    L.append('Generated %s by `tools/recomp/recomp.py`. Do not edit; put facts in `tools/recomp/overrides.json`' % now)
    L.append('or `// ref: FUN_xxxxxxxx` tags above frozen definitions and re-run.')
    L.append('')
    L.append('## Totals')
    L.append('')
    L.append('| | functions | code bytes |')
    L.append('|---|---:|---:|')
    L.append('| reference (non-thunk) | %d | %s |' % (total, fmt_bytes(total_bytes)))
    L.append('| mapped to a frozen function | %d%s (%s) | %s (%s) |' % (snapshot['mapped'], delta('mapped'), pct(snapshot['mapped'], total), fmt_bytes(snapshot['mappedBytes']), pct(snapshot['mappedBytes'], total_bytes)))
    L.append('| &nbsp;&nbsp;ported | %d%s | %s |' % (by_status['ported'], delta('ported'), fmt_bytes(bytes_by_status['ported'])))
    L.append('| &nbsp;&nbsp;stub (unimplemented body) | %d%s | %s |' % (by_status['stub'], delta('stub'), fmt_bytes(bytes_by_status['stub'])))
    L.append('| &nbsp;&nbsp;verified (override) | %d%s | %s |' % (by_status['verified'], delta('verified'), fmt_bytes(bytes_by_status['verified'])))
    L.append('| **faithful** (linked, not stub, call order >= %.0f%%) | **%d%s (%s)** | **%s (%s)** |' % (FAITHFUL * 100, faithful, delta('faithful'), pct(faithful, total), fmt_bytes(faithful_bytes), pct(faithful_bytes, total_bytes)))
    L.append('| unmapped | %d | %s |' % (by_status['unmapped'], fmt_bytes(bytes_by_status['unmapped'])))
    L.append('| world spine (reachable from OnFrameRender) | %d, mapped %d%s (%s) | |' % (snapshot['spine'], snapshot['spineMapped'], delta('spineMapped'), pct(snapshot['spineMapped'], snapshot['spine'])))
    L.append('| &nbsp;&nbsp;render spine (world update + map + M2 scene) | %d, mapped %d%s (%s) | |' % (snapshot['renderSpine'], snapshot['renderSpineMapped'], delta('renderSpineMapped'), pct(snapshot['renderSpineMapped'], snapshot['renderSpine'])))
    L.append('| **render surface** (the modules that draw the world) | **%d, mapped %d%s (%s)** | |' % (snapshot['renderSurface'], snapshot['renderSurfaceMapped'], delta('renderSurfaceMapped'), pct(snapshot['renderSurfaceMapped'], snapshot['renderSurface'])))
    L.append('| frozen functions (src/, from PDB + source) | %d, stubs %d | |' % (snapshot['frozenFunctions'], snapshot['frozenStubs']))
    L.append('')
    L.append('Match evidence: ' + ', '.join('%s %d' % kv for kv in sorted(by_how.items())) + '. Module anchors: %d assert strings.' % anchors)
    if prev:
        L.append('')
        L.append('Previous run: %s -- mapped %d, ported %d, stub %d, spine mapped %d.' % (prev['date'], prev['mapped'], prev['ported'], prev['stub'], prev['spineMapped']))
    L.append('')
    L.append('## Lua API coverage (binding tables)')
    L.append('')
    L.append('The reference registers %d Lua bindings across %d tables (widget methods per class, and the global function blocks). frozen registers %d of them%s; %d of those are WHOA_UNIMPLEMENTED stubs%s. A missing name is a FrameXML call that raises "attempt to call a nil value"; a stub returns nothing, which is the arity bug class tools/arity.py hunts.' % (
        lua_total, len([r for r in lua_rows]), lua_have, delta('luaHave'), lua_stubbed, delta('luaStubbed')))
    L.append('')
    L.append('| ref table | entries | frozen array | missing | stubbed | first missing / stubbed names |')
    L.append('|---|---:|---|---:|---:|---|')
    for r in lua_rows:
        if not r['missing'] and not r['stubs']:
            continue
        names = ', '.join(r['missing'][:6]) + (' ...' if len(r['missing']) > 6 else '')
        if r['stubs']:
            names += ' / stubs: ' + ', '.join(r['stubs'][:4]) + (' ...' if len(r['stubs']) > 4 else '')
        L.append('| %s (%s..) | %d | `%s` | %d | %d | %s |' % (r['addr'], r['first'], r['count'], r['frozen'] or '-', len(r['missing']), len(r['stubs']), names))
    L.append('')
    cov = load_handler_pairs.coverage
    if cov:
        both = sorted(op for op in cov['ref'] if op in cov['frozen'])
        ref_only = sorted(op for op in cov['ref'] if op not in cov['frozen'])
        L.append('## Packet handler coverage (SetMessageHandler)')
        L.append('')
        L.append('The reference registers handlers for %d opcodes; frozen registers %d of them. An opcode with no frozen handler is a server message the client silently drops.' % (len(cov['ref']), len(both)))
        L.append('')
        L.append('Reference-only, by opcode (handler address): ' + ', '.join(
            '%s %s' % (cov['names'].get(op, '0x%x' % op), '/'.join(sorted(cov['ref'][op]))) for op in ref_only))
        L.append('')
    cv = load_cvar_pairs.coverage
    if cv:
        missing = sorted(n for k, (n, cb) in cv['ref'].items() if k not in cv['frozen'])
        L.append('## CVar coverage (CVar::Register)')
        L.append('')
        L.append('The reference registers %d cvars by literal name; frozen registers %d of them. Missing ones are settings the reference client honours and this one cannot even store.' % (
            len(cv['ref']), len(cv['ref']) - len(missing)))
        L.append('')
        L.append('Missing: ' + ', '.join(missing))
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
    L.append('| addr | module | size | callers | frozen | strings |')
    L.append('|---|---|---:|---:|---|---|')
    L.extend(row(a) for a in top_spine)
    L.append('')
    L.append('## Next to port: unmapped, anywhere')
    L.append('')
    L.append('| addr | module | size | callers | frozen | strings |')
    L.append('|---|---|---:|---:|---|---|')
    L.extend(row(a) for a in top_all)
    L.append('')
    L.append('## Mapped but stubbed (WHOA_UNIMPLEMENTED)')
    L.append('')
    L.append('| addr | module | size | callers | frozen | strings |')
    L.append('|---|---|---:|---:|---|---|')
    L.extend(row(a) for a in stubs)
    L.append('')
    L.append('## Divergence smells: reference strings the frozen counterpart never mentions')
    L.append('')
    L.append('A reference function that formats, asserts or looks up a string its port does not is missing a branch, an error path or a data lookup. Top 40 by count.')
    L.append('')
    L.append('| addr | frozen | missing |')
    L.append('|---|---|---|')
    for n, a, name, missing in smells[:40]:
        L.append('| %s | `%s` | %s |' % (a, name, '; '.join(s[:50] for s in missing[:4]).replace('|', '\\|').replace('\n', '\\n')))
    L.append('')
    L.append('## Largest frozen functions with no reference link')
    L.append('')
    L.append('Either the port added behaviour the reference does not have, or the link is simply unknown: tag it with `// ref: FUN_xxxxxxxx` once found.')
    L.append('')
    L.append('| frozen | lib | code bytes | file |')
    L.append('|---|---|---:|---|')
    for n in frozen_unlinked[:40]:
        w = frozen[n]
        L.append('| `%s` | %s | %d | %s |' % (n, w['lib'], w['size'], w['files'][0] if w['files'] else ''))
    L.append('')
    L.append('## Linked ports with the lowest call-order fidelity')
    L.append('')
    L.append('The port exists but does not make the calls the reference makes, in the order it makes them. Either the port guessed, or its callees are not yet linked (then `--show` lists them as bare addresses). Non-stub, largest first.')
    L.append('')
    L.append('| addr | frozen | call order | ref calls | frozen calls | ref branches | frozen branches | consts | size |')
    L.append('|---|---|---:|---:|---:|---:|---:|---:|---:|')
    # diverged (deliberate difference, reason recorded) and vendor (same third-party library on both
    # sides) ports are not expected to match call for call, so they stay out of this list
    low = sorted((a for a in fid if status(a) not in ('stub', 'diverged', 'vendor', 'faithful', 'verified') and not is_faithful(refs, frozen, m, a, fid[a]) and len(refs[a]['calls']) >= 3), key=lambda a: -refs[a]['size'])
    for a in low[:40]:
        br, co = fidelity_dims(refs, frozen, m, a)
        w = frozen[m[a][0]]
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
        L.append('| reference calls every frame, frozen never (hits) | frozen calls, reference never (hits) |')
        L.append('|---|---|')
        miss = ts.get('missing', [])[:20]
        add = ts.get('added', [])[:20]
        for i in range(max(len(miss), len(add))):
            a = '`%s` %d' % (miss[i][1], miss[i][0]) if i < len(miss) else ''
            b = '`%s` %d' % (add[i][1], add[i][0]) if i < len(add) else ''
            L.append('| %s | %s |' % (a, b))
        L.append('')
        L.append('Per-frame count mismatches (ref \\| frozen), largest first:')
        L.append('')
        for d, name, rc, wc in ts.get('mismatch', [])[:15]:
            L.append('- `%s` %s \\| %s' % (name, rc, wc))
        if os.path.exists(SUSPECT_JSON):
            sus = json.load(io.open(SUSPECT_JSON, encoding='utf-8'))
            L.append('')
            L.append('Links the trace contradicts -- a wrong link, or a real divergence; each needs a verdict in overrides.json (corrected link / `diverged` / `unlinked`):')
            L.append('')
            L.append('| addr | frozen | link evidence | why | ref per frame | frozen per frame |')
            L.append('|---|---|---|---|---|---|')
            for a, v in sorted(sus.items()):
                how = m[a][1] if a in m else 'unlinked'
                L.append('| %s | `%s` | %s | %s | %s | %s |' % (a, v['frozen'], how, v['why'], v['ref'], v['frozen_']))
    else:
        L.append('No trace yet. Run both clients into the world, then `calltrace.py ref`, `calltrace.py frozen`, `tracecompare.py`.')
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
    L.append('1. Pick the top unmapped spine function. `python tools/recomp/recomp.py --show <addr>` prints its callers, callees, strings and the closest frozen candidates; `C:\\Users\\tyler\\tools\\decomp.sh <out> <addr>` decompiles it.')
    L.append('2. Port it (or find the existing port) and put `// ref: FUN_<addr>` above the frozen definition.')
    L.append('3. When a run shows it behaving like the reference, add `{"<addr>": {"frozen": "<name>", "status": "verified", "note": "..."}}` to overrides.json.')
    L.append('4. Re-run the tool; the totals line shows the delta against the previous run.')
    return '\n'.join(L) + '\n', snapshot


def write_map(refs, frozen, m, overrides):
    out = {}
    for a, (name, how) in sorted(m.items()):
        o = overrides.get(a, {})
        out[a] = {'frozen': name, 'how': how, 'status': o.get('status') or ('stub' if frozen[name]['stub'] else 'ported'),
                  'fidelity': round(fidelity(refs, frozen, m, a), 3),
                  'branchRatio': round(fidelity_dims(refs, frozen, m, a)[0], 3),
                  'constOverlap': round(fidelity_dims(refs, frozen, m, a)[1], 3),
                  'faithful': is_faithful(refs, frozen, m, a, fidelity(refs, frozen, m, a)),
                  'module': refs[a]['module'], 'refSize': refs[a]['size'], 'frozenSize': frozen[name]['size'],
                  'files': frozen[name]['files']}
    json.dump(out, io.open(MAP_OUT, 'w', encoding='utf-8'), indent=1, sort_keys=True)


def diff_seq(target, refs, frozen, m):
    """Align the reference's call sequence (callees named through the map) with the port's, and
    print them side by side: the calls the port skips, the calls it adds, in order."""
    a = target.lower().replace('0x', '').zfill(8)
    if a not in m:
        print('%s is not linked' % a)
        return
    name = m[a][0]
    rseq = ref_seq(refs, m, a)
    wseq = expand_inlined_calls(frozen_seq(frozen, name), set(rseq), frozen)
    n, k = len(rseq), len(wseq)
    L = [[0] * (k + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        for j in range(k - 1, -1, -1):
            L[i][j] = L[i + 1][j + 1] + 1 if rseq[i] == wseq[j] else max(L[i + 1][j], L[i][j + 1])
    print('%s  <->  %s   recall %.0f%%' % (a, name, 100.0 * (L[0][0] / float(n) if n else 1.0)))
    print('  %-48s | %s' % ('reference', 'frozen'))
    i = j = 0
    while i < n or j < k:
        if i < n and j < k and rseq[i] == wseq[j]:
            print('  %-48s | %s' % (rseq[i][:48], wseq[j][:60])); i += 1; j += 1
        elif j < k and (i == n or L[i][j + 1] >= L[i + 1][j]):
            print('  %-48s | + %s' % ('', wseq[j][:58])); j += 1
        else:
            r = rseq[i]
            tag = '' if r in frozen or r.startswith('crt:') else '  (unlinked %s)' % (refs[r]['name'] if r in refs else r)
            print('  - %-46s |%s' % (r[:46], tag)); i += 1


def show(target, refs, frozen, m):
    rev = {v[0]: a for a, v in m.items()}
    a = None
    if re.fullmatch(r'(0x)?[0-9a-fA-F]{6,8}', target):
        a = target.lower().replace('0x', '').zfill(8)
    elif target in rev:
        a = rev[target]
    elif target in frozen:
        w = frozen[target]
        print('frozen %s  size %d  files %s  stub %s  refs %s' % (target, w['size'], w['files'], w['stub'], sorted(w['refs'])))
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
        w = frozen[name]
        print('  frozen: %s [%s] size %d files %s stub %s' % (name, how, w['size'], w['files'], w['stub']))
        print('  frozen strings missing on ref side:', sorted(w['strings'] - set(r['strings']))[:10])
        print('  ref strings missing on frozen side:', sorted(set(r['strings']) - w['strings'])[:10])
    else:
        # candidates by shared strings
        cands = collections.Counter()
        for n, w in frozen.items():
            k = len(w['strings'] & set(r['strings']))
            if k:
                cands[n] = k
        print('  unmapped. candidates by shared strings:', cands.most_common(8))


def queue_next(args, refs, frozen, m):
    """Pick the next functions to work and decompile them into docs/recomp/queue/<addr>.c, one
    Ghidra run for the batch. Each file starts with a header: module, size, callers, the linked
    callees (so the port can call the frozen names) and the unlinked ones (so they get tagged next)."""
    sp = spine(refs, RENDER_ROOTS if args.render else None)
    if args.render:
        sp = set(a for a in sp if refs[a].get('module') in RENDER_MODULES)
    real = {a: r for a, r in refs.items() if not r['thunk'] and not r['excluded']}

    def weight(a):
        return (refs[a]['callers'] + 1) * refs[a]['size']

    if args.cluster:
        # a subsystem: the root plus every unlinked function reachable through its callees, to
        # --depth levels, so what one port needs is decompiled in one run instead of one at a time
        root = args.cluster.lower().replace('0x', '').zfill(8)
        seen, frontier, pool = {root}, [root], [root]
        for _ in range(args.depth):
            nxt = []
            for a in frontier:
                for c in refs.get(a, {}).get('callees', []):
                    if c in refs and c not in seen and not refs[c]['thunk'] and not refs[c]['excluded']:
                        seen.add(c)
                        if c not in m:
                            pool.append(c)
                            nxt.append(c)
            frontier = nxt
        args.next = max(args.next or 0, len(pool))
    elif args.fix:
        overrides = load_overrides()
        skip = set(k.lower().zfill(8) for k, v in overrides.items() if isinstance(v, dict) and v.get('status') in ('diverged', 'vendor', 'faithful', 'verified'))
        pool = [a for a in m if a in real and a not in skip and not frozen[m[a][0]]['stub'] and not is_faithful(refs, frozen, m, a, fidelity(refs, frozen, m, a)) and len(refs[a]['calls']) >= 3]
    elif args.helpers:
        # the small, everywhere-called leaves (allocators, string ops, CVar lookup): every one of
        # them identified lifts the fidelity of hundreds of callers and feeds the call-order matcher
        pool = [a for a in real if a not in m and refs[a]['callers'] >= 20]
    else:
        pool = [a for a in real if a not in m and refs[a]['size'] >= 48]
    if args.spine or args.render:
        pool = [a for a in pool if a in sp]
    if args.module:
        pool = [a for a in pool if refs[a]['module'].lower() == args.module.lower()]
    if not args.cluster:
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
                '// when ported: put  // ref: FUN_%s  above the frozen definition, re-run recomp.py' % a, '']
        body = by_addr.get(a, '// (decompilation missing: run decomp.sh by hand)\n')
        io.open(os.path.join(QUEUE_DIR, a + '.c'), 'w', encoding='utf-8', newline='\n').write('\n'.join(head) + body)
        print('  %s  %-22s size %5d callers %4d  -> docs/recomp/queue/%s.c' % (a, r['module'], r['size'], r['callers'], a))
    if os.path.exists(tmp):
        os.remove(tmp)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--export', action='store_true', help='re-export the reference inventory from Ghidra')
    ap.add_argument('--pdb', action='store_true', help='re-dump Frozen.pdb')
    ap.add_argument('--show', metavar='ADDR|NAME', help='print everything known about one function')
    ap.add_argument('--diff', metavar='ADDR', help='align the reference call sequence with the port and show what it skips')
    ap.add_argument('--no-history', action='store_true', help='do not append this run to history.jsonl')
    ap.add_argument('--next', type=int, metavar='N', help='decompile the next N functions to port into docs/recomp/queue/')
    ap.add_argument('--spine', action='store_true', help='with --next: only functions on the world spine')
    ap.add_argument('--render', action='store_true', help='with --next: only the render spine (world update, map and M2 scene roots)')
    ap.add_argument('--module', metavar='FILE.cpp', help='with --next: only functions anchored to this reference module')
    ap.add_argument('--fix', action='store_true', help='with --next: queue linked-but-unfaithful ports instead of unlinked functions')
    ap.add_argument('--helpers', action='store_true', help='with --next: queue the most-called unlinked leaves (allocators, string ops ...)')
    ap.add_argument('--cluster', metavar='ADDR', help='decompile this reference function and every unlinked function it reaches within --depth calls')
    ap.add_argument('--depth', type=int, default=2, help='with --cluster: how many call levels to follow (default 2)')
    args = ap.parse_args()

    if args.export or not os.path.exists(REF_JSONL):
        export_reference()
    if args.pdb or not os.path.exists(PDB_DUMP):
        dump_pdb()
        # the source changed too, or there would be no new PDB: refresh the exact inventory
        # (incremental: clangparse caches by file mtime, so this is seconds after the first run)
        if os.path.exists(CLANG_JSON):
            print('refreshing the libclang inventory for changed files...')
            subprocess.run([sys.executable, os.path.join(HERE, 'clangparse.py')])

    refs = load_reference()
    anchors = assign_modules(refs)
    overrides = {k.lower().zfill(8): v for k, v in load_overrides().items() if isinstance(v, dict)}
    # Not part of the port: CRT routines Ghidra's library matcher named (_memset, _ftol2 ...) and
    # anything an override marks excluded (nullsubs, compiler helpers, third-party code)
    for a, r in refs.items():
        r['excluded'] = (r['named'] and r['name'].startswith('_')) or overrides.get(a, {}).get('status') == 'excluded'
    src, exact = overlay_clang(parse_sources())
    frozen = merge_frozen(load_pdb_functions(), src)
    overrides = {k.lower().zfill(8): v for k, v in load_overrides().items() if isinstance(v, dict)}
    ref_tables = load_tables()
    frozen_tables = load_frozen_tables()
    pairs = pair_tables(ref_tables, frozen_tables)
    m = match(refs, frozen, overrides, pairs)

    # Runtime evidence from the last calltrace/tracecompare run. Matching links become verified.
    # Contradicted ones are only REPORTED: one trace of one scene cannot tell a wrong link from a
    # real behavioural divergence (the reference re-picking bone sequences 200x a frame where frozen
    # does it 7x is the second kind, and is exactly what we want to see). Judging them is the
    # cycle's job; the verdict goes in overrides.json as a corrected link, "diverged", or
    # "unlinked" (no frozen counterpart; the address is then never auto-matched again).
    verified = json.load(io.open(VERIFIED_JSON, encoding='utf-8')) if os.path.exists(VERIFIED_JSON) else {}
    for a, v in verified.items():
        if a in m and m[a][0] == v['frozen'] and a not in overrides:
            overrides[a] = {'frozen': v['frozen'], 'status': 'verified', 'note': v['note'], 'auto': True}

    if args.diff:
        diff_seq(args.diff, refs, frozen, m)
    if args.show:
        show(args.show, refs, frozen, m)
        return

    if args.next or args.cluster:
        queue_next(args, refs, frozen, m)
        return

    report, snapshot = build_report(refs, frozen, m, overrides, anchors, ref_tables, pairs, frozen_tables)
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    io.open(REPORT, 'w', encoding='utf-8', newline='\n').write(report)
    write_map(refs, frozen, m, overrides)
    if not args.no_history:
        with io.open(HISTORY, 'a', encoding='utf-8') as h:
            h.write(json.dumps(snapshot) + '\n')
    print('reference %d fns, mapped %d (ported %d, stub %d, verified %d), spine %d/%d; frozen %d fns' % (
        snapshot['refFunctions'], snapshot['mapped'], snapshot['ported'], snapshot['stub'], snapshot['verified'],
        snapshot['spineMapped'], snapshot['spine'], snapshot['frozenFunctions']))
    print('wrote', os.path.relpath(REPORT, ROOT))


if __name__ == '__main__':
    main()
