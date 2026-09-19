#!/usr/bin/env python3
"""Exact frozen-side inventory through libclang: every function definition with its ordered calls,
string and numeric literals, and branch count, from the real compile flags.

The regex parser in recomp.py collapses overloads, misses lambdas and guesses at which `Foo(` a
call means. This resolves calls to their declarations, so the call sequence the fidelity score
compares against the reference is the real one.

    python tools/recomp/clangparse.py                 # parse everything (cached by content hash)
    python tools/recomp/clangparse.py src/world/Terrain.cpp   # one file, printed

Output: tools/recomp/data/frozen-clang.json  { qualified name: { files, callseq, strings, consts,
branches, stub, lines } }. recomp.py prefers it over the regex inventory when present.
"""

import hashlib
import io
import json
import os
import re
import sys
import time

import clang.cindex as ci

# The pip libclang (18) predates the MSVC STL's "Clang 20 or newer" gate, so TUs that touch the
# STL error out and lose their member calls. Prefer the LLVM install's libclang (22) when present.
SYSTEM_LIBCLANG = r'C:\Program Files\LLVM\bin\libclang.dll'
if os.path.exists(SYSTEM_LIBCLANG):
    ci.Config.set_library_file(SYSTEM_LIBCLANG)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data')
OUT = os.path.join(DATA, 'frozen-clang.json')
CACHE = os.path.join(DATA, 'clang-cache.json')
CACHE_VERSION = 7  # bump when the walk changes so cached entries are re-parsed
# the `// ref: FUN_xxxxxxxx` tag above a definition (same rule as recomp.py's REF_TAG_RE)
REF_TAG_RE = re.compile(r'//\s*ref:\s*(?:FUN_|0x)?(00[4-9a-fA-F][0-9a-fA-F]{5}|[4-9a-fA-F][0-9a-fA-F]{5})\b')
COMPILE_DB = [os.path.join(ROOT, 'cmake-build-release', 'compile_commands.json'),
              os.path.join(ROOT, 'build', 'compile_commands.json')]

K = ci.CursorKind
BRANCH_KINDS = {K.IF_STMT, K.FOR_STMT, K.WHILE_STMT, K.DO_STMT, K.CASE_STMT, K.CONDITIONAL_OPERATOR,
                K.CXX_FOR_RANGE_STMT}
DEF_KINDS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.FUNCTION_TEMPLATE, K.CONVERSION_FUNCTION}


def load_compile_db():
    for p in COMPILE_DB:
        if os.path.exists(p):
            db = json.load(io.open(p, encoding='utf-8'))
            return {os.path.normcase(os.path.abspath(e['file'])): e for e in db}
    sys.exit('no compile_commands.json (configure a Ninja build dir with CMAKE_EXPORT_COMPILE_COMMANDS)')


def split_command(cmd):
    """cl.exe style command line -> clang args in cl driver mode, without the output/source bits."""
    toks = re.findall(r'"([^"]*)"|(\S+)', cmd)
    toks = [a or b for a, b in toks]
    args = ['--driver-mode=cl', '-fms-compatibility', '-fms-extensions', '-Wno-everything']
    skip = False
    for t in toks[1:]:
        if skip:
            skip = False
            continue
        low = t.lower()
        if low in ('/c', '-c') or low.startswith('/fo') or low.startswith('/fd') or low.startswith('/fs') or low.startswith('/fp'):
            continue
        if low.endswith('.cpp') or low.endswith('.c') or low.endswith('.cc'):
            continue
        if low in ('/showincludes', '/mp', '/nologo', '/tp', '/tc') or low.startswith('/zi') or low.startswith('/zc') or low.startswith('/gm') or low.startswith('/gl'):
            continue
        if low.startswith('/external:'):
            continue
        args.append(t)
    return args


def qualified(cursor):
    parts = []
    c = cursor
    while c is not None and c.kind != K.TRANSLATION_UNIT:
        if c.kind in (K.NAMESPACE,) and c.spelling == '':
            pass  # anonymous namespace: the PDB drops it too
        elif c.kind in (K.NAMESPACE, K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.UNION_DECL) or c.kind in DEF_KINDS:
            if c.spelling:
                parts.append(c.spelling)
        c = c.semantic_parent
    return '::'.join(reversed(parts))


def msvc_type(t):
    """A clang type spelled the way MSVC's PDB spells template arguments: `enum E`, `X const *`,
    `unsigned __int64`, class names bare."""
    t = t.get_canonical()
    k = t.kind
    if k == ci.TypeKind.POINTER:
        return msvc_type(t.get_pointee()) + ' *'
    s = t.spelling
    const = t.is_const_qualified()
    if s.startswith('const '):
        s = s[6:]
    s = re.sub(r'^(struct|class|union|enum) ', '', s)
    if k == ci.TypeKind.ENUM:
        s = 'enum ' + s
    elif k == ci.TypeKind.ULONGLONG:
        s = 'unsigned __int64'
    elif k == ci.TypeKind.LONGLONG:
        s = '__int64'
    return s + (' const' if const else '')


def instantiated_name(call, ref):
    """For a call to a member of a class template, the name of the instantiation the PDB holds:
    TSBaseArray<unsigned int>::operator[] rather than the pattern TSBaseArray::operator[]. The
    arguments come from the object the call is made on (or the constructed type), so a call from
    inside template code, where they are still dependent, keeps the pattern name."""
    # libclang resolves the call to the member of the implicit specialisation, whose parent is a
    # plain class cursor carrying the instantiated type (TSBaseArray<unsigned int>); from inside the
    # pattern the parent is the CLASS_TEMPLATE and the arguments are still dependent
    parent = ref.semantic_parent
    if parent is None or parent.kind not in (K.CLASS_DECL, K.STRUCT_DECL):
        return None
    t = parent.type
    n = t.get_num_template_arguments()
    if n is None or n <= 0:
        return None
    args = []
    for i in range(n):
        a = t.get_template_argument_type(i)
        if a is None or a.kind in (ci.TypeKind.INVALID, ci.TypeKind.UNEXPOSED):
            return None
        args.append(msvc_type(a))
    name = '%s<%s>::%s' % (parent.spelling, ','.join(args), ref.spelling)
    while '>>' in name:
        name = name.replace('>>', '> >')  # MSVC separates nested closers in the PDB
    return name


def walk_body(body, out):
    # Calls are recorded in the order the compiled code makes them, which is what the reference
    # inventory holds: a call's arguments are evaluated before the call itself (post-order), and
    # MSVC x86 evaluates them right to left, the callee object last. So for a CALL_EXPR the
    # children are walked in reverse and the call is appended after them.
    children = list(body.get_children())
    if body.kind == K.CALL_EXPR:
        children.reverse()
    for c in children:
        k = c.kind
        if k == K.CALL_EXPR:
            if k != K.LAMBDA_EXPR:
                walk_body(c, out)
            ref = c.referenced
            if ref is not None and ref.kind in DEF_KINDS:
                out['callseq'].append(instantiated_name(c, ref) or qualified(ref))
            elif c.spelling:
                out['callseq'].append('?' + c.spelling)
            continue
        elif k == K.STRING_LITERAL:
            s = c.spelling
            if s.startswith('"') and s.endswith('"'):
                s = s[1:-1]
            out['strings'].add(s.encode('utf-8').decode('unicode_escape', errors='replace'))
        elif k in (K.INTEGER_LITERAL, K.FLOATING_LITERAL):
            toks = list(c.get_tokens())
            if toks:
                out['consts'].add(toks[0].spelling)
        elif k in BRANCH_KINDS:
            out['branches'] += 1
        elif k == K.LAMBDA_EXPR:
            pass  # a lambda's body is its own function to the PDB; skipped rather than mixed in
        if k != K.LAMBDA_EXPR:
            walk_body(c, out)


def parse_file(index, path, args, text):
    tu = index.parse(path, args=args, options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES * 0)
    errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errors and len(sys.argv) > 1:
        # a TU that does not compile under libclang loses calls (unresolved member calls have no
        # referenced decl); print the first few so the flags or headers can be fixed
        print('  %d errors in %s' % (len(errors), os.path.relpath(path, ROOT)))
        for d in errors[:5]:
            print('    %s:%d: %s' % (os.path.basename(str(d.location.file)), d.location.line, d.spelling[:120]))
    fns = {}
    norm = os.path.normcase(os.path.abspath(path))
    root = os.path.normcase(os.path.abspath(ROOT)) + os.sep
    header_text = {}
    for c in tu.cursor.walk_preorder():
        if c.kind not in DEF_KINDS or not c.is_definition():
            continue
        loc = c.location
        if loc.file is None:
            continue
        fpath = os.path.normcase(os.path.abspath(loc.file.name))
        header = None
        if fpath != norm:
            # a definition in one of our headers (inline members, constructors, small accessors):
            # the PDB has it as a function of its own, so it needs an inventory entry too. It is
            # seen from every TU that includes the header; main() keeps the first.
            rel = os.path.relpath(fpath, ROOT).replace('\\', '/')
            if not fpath.startswith(root) or not rel.startswith(('src/', 'lib/', 'vendor/')):
                continue
            header = rel
        name = qualified(c)
        body = next((ch for ch in c.get_children() if ch.kind == K.COMPOUND_STMT), None)
        if body is None:
            continue
        out = {'callseq': [], 'strings': set(), 'consts': set(), 'branches': 0}
        walk_body(body, out)
        ext = body.extent
        if header:
            if fpath not in header_text:
                header_text[fpath] = io.open(fpath, encoding='utf-8', errors='replace').read()
            htext = header_text[fpath]
            src = htext[ext.start.offset:ext.end.offset]
            start = c.extent.start.offset
            above = htext[max(0, htext.rfind('\n\n', 0, start)):start]
            refs = sorted(set(a.lower().zfill(8) for a in REF_TAG_RE.findall(above)))
        else:
            src = text[ext.start.offset:ext.end.offset] if text else ''
            refs = []
        e = fns.setdefault(name, {'callseq': [], 'strings': set(), 'consts': set(), 'branches': 0, 'stub': True, 'lines': 0, 'refs': [], 'header': header, 'line': c.location.line})
        e['refs'] = sorted(set(e['refs']) | set(refs))
        e['callseq'] += out['callseq']
        e['strings'] |= out['strings']
        e['consts'] |= out['consts']
        e['branches'] += out['branches']
        # Two stub idioms in this tree: the WHOA_UNIMPLEMENTED macro, and a body with no
        # statements at all carrying a TODO. An empty body without a TODO is left alone,
        # because some functions are empty on purpose to match an empty reference.
        empty = next(body.get_children(), None) is None
        e['stub'] = e['stub'] and ('WHOA_UNIMPLEMENTED' in src or (empty and 'TODO' in src))
        e['lines'] += src.count('\n') + 1
    return fns



def content_key(path):
    """Cache key for a source file.

    Was the mtime alone, which is wrong twice over: an mtime can repeat inside the filesystem's
    granularity during a fast write-parse-write cycle, and a checkout can hand back different
    content with a newer stamp that looks fresh but is served from cache anyway. Measured on
    2026-09-19: four WHOA_UNIMPLEMENTED bindings were cached as non-stubs and stayed that way
    across runs, so they were counted ported while still stubs. Hashing the bytes costs one read
    per file and cannot go stale.
    """
    h = hashlib.blake2b(digest_size=16)
    h.update(io.open(path, 'rb').read())

    return h.hexdigest()

def main():
    db = load_compile_db()
    index = ci.Index.create()
    only = [os.path.normcase(os.path.abspath(os.path.join(ROOT, a))) for a in sys.argv[1:]]
    cache = {}
    if os.path.exists(CACHE) and not only:
        cache = json.load(io.open(CACHE, encoding='utf-8'))
    result = {}
    n = 0
    t0 = time.time()
    for path, e in sorted(db.items()):
        rel = os.path.relpath(path, ROOT).replace('\\', '/')
        if not rel.startswith(('src/', 'lib/', 'vendor/')):
            continue
        if only and path not in only:
            continue
        c = cache.get(rel)
        if c and c.get('key') == content_key(path) and c.get('v') == CACHE_VERSION:
            fns = c['fns']
        else:
            text = io.open(path, encoding='utf-8', errors='replace').read()
            fns = parse_file(index, path, split_command(e['command']), text)
            fns = {k: {'callseq': v['callseq'], 'strings': sorted(v['strings']), 'consts': sorted(v['consts']),
                       'branches': v['branches'], 'stub': v['stub'], 'lines': v['lines'], 'refs': v['refs'],
                       'header': v['header'], 'line': v['line']} for k, v in fns.items()}
            cache[rel] = {'key': content_key(path), 'v': CACHE_VERSION, 'fns': fns}
            n += 1
        for k, v in fns.items():
            if v.get('header') and k in result:
                continue  # the same header definition seen from another TU
            r = result.setdefault(k, {'files': [], 'callseq': [], 'strings': set(), 'consts': set(), 'branches': 0, 'stub': True, 'lines': 0, 'refs': set(), 'line': v.get('line', 0)})
            r['files'].append(v.get('header') or rel)
            r['refs'] |= set(v.get('refs', []))
            r['callseq'] += v['callseq']
            r['strings'] |= set(v['strings'])
            r['consts'] |= set(v['consts'])
            r['branches'] += v['branches']
            r['stub'] = r['stub'] and v['stub']
            r['lines'] += v['lines']
    if only:
        for k, v in sorted(result.items()):
            print('%-50s calls %3d branches %3d consts %3d strings %3d%s' % (k[:50], len(v['callseq']), v['branches'], len(v['consts']), len(v['strings']), ' STUB' if v['stub'] else ''))
            print('   ', ' '.join(v['callseq'][:25]))
        return
    for r in result.values():
        r['strings'] = sorted(r['strings'])
        r['consts'] = sorted(r['consts'])
        r['refs'] = sorted(r['refs'])
    os.makedirs(DATA, exist_ok=True)
    json.dump(cache, io.open(CACHE, 'w', encoding='utf-8'))
    json.dump(result, io.open(OUT, 'w', encoding='utf-8'), indent=0)
    print('parsed %d files (%d fresh) in %.0fs: %d functions -> %s' % (len([1 for p in db if os.path.relpath(p, ROOT).replace('\\', '/').startswith(('src/', 'lib/'))]), n, time.time() - t0, len(result), os.path.relpath(OUT, ROOT)))


if __name__ == '__main__':
    main()
