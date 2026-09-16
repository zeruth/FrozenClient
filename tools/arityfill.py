#!/usr/bin/env python3
"""Generate correctly-shaped returns for bindings that under-return, using the caller's own names.

Padding with nils would be pointless: a value the binding never pushes is already nil. What matters
is the TYPE at each position, because the caller does different things with each -- and it announces
those types in the names it destructures into:

    local haveTotem, name, startTime, duration, icon = GetTotemInfo(slot)
          ^boolean   ^str  ^number    ^number   ^str

`startTime` and `duration` reach arithmetic; `name` and `icon` reach SetText and SetTexture. A nil in
the first pair raises, a nil in the second is handled. So each position is typed from the local name
and given 0, false or nil accordingly.

    python tools/arityfill.py --dry-run
    python tools/arityfill.py --apply
"""

import importlib.util
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

NUMERIC = re.compile(
    r'(num|count|^n[A-Z]|amount|rank|level|index|id$|ID$|slot|time|duration|start|expiration|'
    r'cost|money|price|quantity|charges|size|min$|max$|width|height|rating|played|wins|total|'
    r'points|percent|scale|offset|difficulty|type$|typeID|category)', re.I)

BOOLEAN = re.compile(r'^(is|has|can|are|should|in|active|enabled|joined|queued|collapsed|'
                     r'expanded|header|hidden|shown|locked|checked|selected|available|'
                     r'complete|completed|usable|have)[A-Z_]?', re.I)


def classify(local):
    """What to push for a position the caller calls `local`."""
    if local == '_':
        return 'nil'

    if BOOLEAN.match(local):
        return 'false'

    if NUMERIC.search(local):
        return '0'

    return 'nil'


def push_for(kind):
    if kind == '0':
        return '    lua_pushnumber(L, 0.0);'

    if kind == 'false':
        return '    lua_pushboolean(L, 0);'

    return '    lua_pushnil(L);'


def main():
    apply = '--apply' in sys.argv

    spec = importlib.util.spec_from_file_location('ar', os.path.join(ROOT, 'tools', 'arity.py'))
    ar = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ar)

    cpp = ar.cpp_returns()
    fx = ar.framexml_arity()

    # Re-read the destructured NAMES, which arity.py only keeps as a display string.
    names_at = {}

    for path in __import__('glob').glob(os.path.join(ar.FXDIR, '*')):
        try:
            text = io.open(path, encoding='latin-1').read()
        except OSError:
            continue

        for line in text.splitlines():
            m = re.search(r'(?:local\s+)?([A-Za-z_][\w., \t]*?)\s*=\s*([A-Za-z_]\w*)\s*\(', line)

            if not m:
                continue

            lhs, fname = m.group(1), m.group(2)

            if '(' in lhs or '[' in lhs:
                continue

            parts = [p.strip() for p in lhs.split(',') if p.strip()]

            if len(parts) < 2 or not all(re.match(r'^[A-Za-z_]\w*$', p) or p == '_' for p in parts):
                continue

            prev = names_at.get(fname)

            if not prev or len(parts) > len(prev):
                names_at[fname] = parts

    done = 0
    report = []

    for name, (wants, site) in sorted(fx.items()):
        info = cpp.get(name)

        if not info:
            continue

        path, func, gives = info

        if gives >= wants:
            continue

        locals_ = names_at.get(name)

        if not locals_ or len(locals_) != wants:
            continue

        kinds = [classify(p) for p in locals_]
        pushes = '\n'.join(push_for(k) for k in kinds)

        body = (
            '    // %d values, typed from what the caller destructures them into:\n'
            '    //   %s\n'
            '    // The data behind this is not available yet, so each position takes the neutral\n'
            '    // value for its type -- 0 where the caller does arithmetic, false where it\n'
            '    // branches, nil where it expects a name or a texture and already handles absence.\n'
            '%s\n\n    return %d;'
            % (wants, ', '.join(locals_), pushes, wants))

        full = os.path.join(ROOT, path)
        src = io.open(full, encoding='utf-8', errors='replace').read()

        old_unimpl = 'int32_t %s(lua_State* L) {\n    WHOA_UNIMPLEMENTED(0);\n}' % func
        old_body = re.search(
            r'int32_t %s\(lua_State\* \w+\)\s*\{(.*?)\n\}' % re.escape(func), src, re.S)

        if src.count(old_unimpl) == 1:
            new = 'int32_t %s(lua_State* L) {\n%s\n}' % (func, body)
            src = src.replace(old_unimpl, new)
        elif old_body and 'lua_push' not in old_body.group(1) and 'return 0;' in old_body.group(1):
            new = 'int32_t %s(lua_State* L) {\n%s\n}' % (func, body)
            src = src[:old_body.start()] + new + src[old_body.end():]
        else:
            report.append('%-34s SKIP (has a real body; needs hand review)' % name)
            continue

        if apply:
            io.open(full, 'w', encoding='utf-8', newline='\n').write(src)

        report.append('%-34s %d -> %d  [%s]' % (name, gives, wants, ' '.join(kinds)))
        done += 1

    print('\n'.join(report))
    print()
    print('%s %d bindings' % ('fixed' if apply else 'would fix', done))


if __name__ == '__main__':
    main()
