#!/usr/bin/env python3
"""Find frozen C++ functions with an empty body that something still calls.

A stub nothing calls is just unported work, and the report already counts it. A stub something
calls is different: it is a silent behavioural hole, because the caller believes the work
happened. Neither the fidelity score nor the linked/faithful counts surface these -- a function
can be linked, tagged, and scored while doing nothing at all.

This found two real bugs on 2026-09-23:

  CM2Model::AnimateMTSimple   five call sites, and it is the function that writes matrixF4, so
                              every model on that path drew with a stale transform.
  CM2Model::UnsetBoneSequence three call sites, including SetBoneSequence's own id -1 path, so
                              bones never stopped playing what they had.

Not every hit is a bug. Check the gate before porting: the four stubbed draws in CM2SceneRender
have a call site each but are unreachable, because the element gather that would produce their
element types is itself stubbed. tools/stubtriage.py and tools/stubfill.py cover the Lua binding
stubs; this covers the engine.

    python tools/livestubs.py            # render path only (the default)
    python tools/livestubs.py --all      # every subsystem
"""

import collections
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')
ROOT = os.path.normpath(ROOT)

# A qualified definition whose line ends in the opening brace.
DEF = re.compile(r'^[A-Za-z_][\w:<>,*&\s]*?\b([A-Za-z_]\w*)::([~A-Za-z_]\w*)\s*\([^;]*\)\s*(?:const\s*)?\{\s*$')

# A body of nothing, comments, or a bare return counts as empty.
BARE_RETURN = re.compile(r'^return\s*[-\w:.]*\s*;$')


def body_kind(lines, i):
    """'empty' (nothing but comments), 'return' (comments and one bare return), or None.

    The `*` case is why this tracks block-comment state instead of testing line prefixes. Treating
    any line that starts with `*` as a comment continuation also swallows `*this = ...` and
    `*out = ...`, which are ordinary statements -- that bug had `C44Matrix::Rotate`, a one-line
    matrix multiply, reported as an empty body on 2026-09-23.
    """
    depth = lines[i].count('{') - lines[i].count('}')
    j = i + 1
    in_comment = False
    saw_return = False

    while j < len(lines) and depth > 0:
        raw = lines[j]
        depth += raw.count('{') - raw.count('}')

        if depth <= 0:
            break

        line = raw.strip()

        if in_comment:
            if '*/' not in line:
                j += 1
                continue

            line = line.split('*/', 1)[1].strip()
            in_comment = False

        while line.startswith('/*'):
            if '*/' in line[2:]:
                line = line.split('*/', 1)[1].strip()
            else:
                in_comment = True
                line = ''

        if line.startswith('//'):
            line = ''

        if line:
            if not BARE_RETURN.match(line):
                return None

            saw_return = True

        j += 1

    return 'return' if saw_return else 'empty'


def body_is_empty(lines, i):
    """A body of nothing, comments, or a bare return."""
    return body_kind(lines, i) is not None


def main():
    show_all = '--all' in sys.argv[1:]

    files = []
    for base, _dirs, names in os.walk(ROOT):
        for n in names:
            if n.endswith(('.cpp', '.hpp')):
                files.append(os.path.join(base, n))

    stubs = {}
    for path in files:
        try:
            lines = io_read(path).split('\n')
        except OSError:
            continue

        for i, line in enumerate(lines):
            m = DEF.match(line)

            if m and body_is_empty(lines, i):
                stubs.setdefault('%s::%s' % (m.group(1), m.group(2)), []).append((path, i + 1))

    calls = collections.Counter()
    for path in files:
        try:
            text = io_read(path)
        except OSError:
            continue

        for key in stubs:
            fn = key.split('::')[1]

            if fn.startswith('~'):
                continue

            pattern = r'(?<![\w:])(?:this->|[\w\]\)]->|\w+\.)?' + re.escape(fn) + r'\s*\('
            calls[key] += len(re.findall(pattern, text))

    rows = []
    for key, defs in stubs.items():
        fn = key.split('::')[1]

        if fn.startswith('~') or fn.startswith('operator'):
            continue

        live = calls[key] - len(defs)

        if live > 0:
            rel = os.path.relpath(defs[0][0], os.path.join(ROOT, '..')).replace('\\', '/')
            rows.append((live, key, rel, defs[0][1]))

    rows.sort(reverse=True)

    if not show_all:
        rows = [r for r in rows if any(p in r[2] for p in ('/model/', '/world/', '/gx/'))]

    print('%d empty-bodied functions still have call sites%s'
          % (len(rows), '' if show_all else ' in the render path'))
    print()

    for live, key, rel, line in rows:
        print('  %3d calls  %-52s %s:%d' % (live, key, rel, line))


def io_read(path):
    with open(path, encoding='utf-8', errors='replace') as fh:
        return fh.read()


if __name__ == '__main__':
    main()
