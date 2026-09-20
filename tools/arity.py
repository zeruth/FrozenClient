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


# A binding table whose variable name ends in Methods holds WIDGET methods, called as obj:Name().
# Everything else -- s_ScriptFunctions, s_UnitFunctions, s_SystemFunctions, s_stubs, extra_funcs --
# holds globals, called as Name().
#
# Telling them apart is not cosmetic. FrameXML's "local x, y = GetCursorPosition()" calls the
# GLOBAL, which returns two; CSimpleEditBox also registers a GetCursorPosition method returning
# one, and matching that against the global reported a bug in correct code. Same trap for any name
# a widget and a global share.
TABLE_RE = re.compile(
    r'(?:FrameScript_Method|FrameScript_Function|ScriptFunction|luaL_Reg)\s+'
    r'([A-Za-z_][\w:]*)\s*\[[^\]]*\]\s*=\s*\{(.*?)\n\};', re.S)

ENTRY_RE = re.compile(r'\{\s*"([^"]+)"\s*,\s*&?([\w:]+)\s*\}')


def cpp_returns():
    """Global Lua name -> (file, function, max return count). Widget methods are skipped."""
    out = {}

    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        src = io.open(path, encoding='utf-8', errors='replace').read()
        rel = os.path.relpath(path, ROOT)

        bodies = dict(re.findall(r'int32_t (\w+)\(lua_State\* \w+\)\s*\{(.*?)\n\}', src, re.S))

        for table, body in TABLE_RE.findall(src):
            if table.rsplit('::', 1)[-1].lower().endswith('methods'):
                continue

            for name, func in ENTRY_RE.findall(body):
                fn = bodies.get(func.rsplit('::', 1)[-1])

                if fn is None or 'WHOA_UNIMPLEMENTED' in fn:
                    continue

                counts = [int(n) for n in re.findall(r'return\s+(\d+)\s*;', fn)]

                if not counts:
                    continue

                # Two globals under one name would make the answer depend on file order. Keep the
                # more generous one so a genuine shortfall is never invented by the tie-break.
                prev = out.get(name)

                if not prev or max(counts) > prev[2]:
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
            # A commented-out destructure is not a caller. GetXPExhaustion was reported off
            # MainMenuBar.lua:313, which is "--exhaustionCurrXP, exhaustionMaxXP = ...".
            if line.lstrip().startswith('--'):
                continue

            m = pattern.search(line)

            if not m:
                continue

            lhs, name = m.group(1), m.group(2)

            # Only simple name lists; anything with an index or a call is not a destructure.
            if '(' in lhs or '[' in lhs:
                continue

            # "local a, b = F(x), G(y)" destructures two calls, not one call returning two.
            # Counting the locals against either one invents a shortfall. Seen both ways: the same
            # function twice (GetCVarBool) and two different ones (GetScreenWidth with
            # GetScreenHeight). So the test is on the number of CALLS to the right of the equals,
            # not on the name.
            rhs = line[m.end(1):]

            if len(re.findall(r'[A-Za-z_]\w*\s*\(', rhs)) > 1:
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


# Shortfalls that are the reference's own behaviour, not frozen's gap. Each needs a reason, and
# the reason has to be about the REFERENCE returning fewer values too -- "not implemented yet" is
# not a suppression, it is the thing this tool is for.
KNOWN = {
    'UnitPowerType':
        'the reference also returns 2 for an ordinary unit; the 5-value form needs a vehicle with '
        'an alternate power display, and frozen has neither. Documented at the binding.',
}


def main():
    cpp = cpp_returns()
    fx = framexml_arity()

    rows = []

    for name, (wants, site) in fx.items():
        if name in KNOWN and '--all' not in sys.argv:
            continue

        info = cpp.get(name)

        if not info:
            continue

        path, func, gives = info

        if gives < wants or '--all' in sys.argv:
            rows.append((wants - gives, name, gives, wants, path, site))

    rows.sort(reverse=True)

    print('%d bindings return fewer values than FrameXML destructures' % len([r for r in rows if r[0] > 0]))
    print('(%d suppressed as the reference behaving the same way; --all to include them)'
          % len(KNOWN))
    print()
    print('A shortfall is only a BUG if the missing values matter. Where the binding returns nil')
    print('for what it does answer -- Script_ReturnNil and friends -- the caller gets nil for the')
    print('rest too, and nil is usually the right "nothing here". The ones that bite are a large')
    print('gap, meaning the binding is simply unimplemented, or a returned 0 sitting in a slot the')
    print('caller tests, because 0 is truthy in Lua.')
    print()

    for short, name, gives, wants, path, site in rows:
        if short <= 0 and '--all' not in sys.argv:
            continue

        print('%-34s returns %d, caller takes %d  (%+d)' % (name, gives, wants, -short))
        print('    %s' % path)
        print('    %s' % site)


if __name__ == '__main__':
    main()
