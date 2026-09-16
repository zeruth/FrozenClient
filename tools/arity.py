#!/usr/bin/env python3
"""Find bindings that return fewer values than FrameXML destructures.

This is the bug class that broke the spellbook: GetSpellTabInfo returned 4 values where FrameXML
does

    local name, texture, offset, numSpells, highestRankOffset, highestRankNumSpells = GetSpellTabInfo(...)

so the last two were nil, and the very next line assigned one of them to a variable it then did
arithmetic on. Nothing raises at the call itself -- the error surfaces somewhere else entirely,
which is what makes these expensive to track down one at a time.

The check is mechanical: count the locals on the left of an assignment from the binding, and compare
against the largest `return N` in its C++ body.

    python tools/arity.py            # mismatches only
    python tools/arity.py --all      # every binding with a destructuring call site
"""

import glob
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FXDIR = os.path.join(ROOT, 'build', 'framexml')


def cpp_returns():
    """Registered Lua name -> (file, function, max return count)."""
    out = {}

    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        src = io.open(path, encoding='utf-8', errors='replace').read()
        rel = os.path.relpath(path, ROOT)

        bodies = dict(re.findall(r'int32_t (\w+)\(lua_State\* \w+\)\s*\{(.*?)\n\}', src, re.S))

        for name, func in re.findall(r'\{\s*"([^"]+)"\s*,\s*&(\w+)\s*\}', src):
            body = bodies.get(func)

            if body is None:
                continue

            if 'WHOA_UNIMPLEMENTED' in body:
                continue

            counts = [int(n) for n in re.findall(r'return\s+(\d+)\s*;', body)]

            if counts:
                out[name] = (rel, func, max(counts))

    return out


def framexml_arity():
    """Lua name -> (max locals destructured, an example site)."""
    # local a, b, c = Name(...)   /   a, b = Name(...)
    pattern = re.compile(r'(?:local\s+)?([A-Za-z_][\w., \t]*?)\s*=\s*([A-Za-z_]\w*)\s*\(')
    out = {}

    for path in glob.glob(os.path.join(FXDIR, '*')):
        try:
            text = io.open(path, encoding='latin-1').read()
        except OSError:
            continue

        base = os.path.basename(path)

        for lineno, line in enumerate(text.splitlines(), 1):
            m = pattern.search(line)

            if not m:
                continue

            lhs, name = m.group(1), m.group(2)

            # Only simple name lists; anything with an index or a call is not a destructure.
            if '(' in lhs or '[' in lhs:
                continue

            names = [p.strip() for p in lhs.split(',') if p.strip()]

            if len(names) < 2:
                continue

            if not all(re.match(r'^[A-Za-z_]\w*$', p) or p == '_' for p in names):
                continue

            prev = out.get(name)

            if not prev or len(names) > prev[0]:
                out[name] = (len(names), '%s:%d  %s' % (base, lineno, line.strip()[:90]))

    return out


def main():
    cpp = cpp_returns()
    fx = framexml_arity()

    rows = []

    for name, (wants, site) in fx.items():
        info = cpp.get(name)

        if not info:
            continue

        path, func, gives = info

        if gives < wants or '--all' in sys.argv:
            rows.append((wants - gives, name, gives, wants, path, site))

    rows.sort(reverse=True)

    print('%d bindings return fewer values than FrameXML destructures' % len([r for r in rows if r[0] > 0]))
    print()

    for short, name, gives, wants, path, site in rows:
        if short <= 0 and '--all' not in sys.argv:
            continue

        print('%-34s returns %d, caller takes %d  (%+d)' % (name, gives, wants, -short))
        print('    %s' % path)
        print('    %s' % site)


if __name__ == '__main__':
    main()
