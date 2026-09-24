"""Find member functions that call themselves through `this->` unqualified.

Inside `Foo::Bar(...)`, a call to `this->Bar(...)` is either deliberate re-entry with changed state
or -- far more often in a port like this one -- a slip for `this->Base::Bar(...)`. The second is
unbounded recursion, and it is invisible until the guard in front of it stops short-circuiting.

That is not hypothetical here. `CGUnit_C::GetDisplayID` carried exactly this shape, guarded behind
`GetLocalDisplayID()`, which returns a field nothing ever writes. It would have stack-overflowed
the moment anything set a local display id, which is what the reference uses for transform effects.
Found by hand on 2026-09-23; this is the sweep for the rest.

Usage:  python tools/selfcall.py [--all]

Without --all, only calls that forward the enclosing function's OWN parameters unchanged are
reported -- including the no-parameter case, which is the clearest of all. Anything that passes
something else is overload dispatch or recursion on different data, both of which are fine:
`RCString::Copy(const RCString&)` calling `this->Copy(source.GetString())` is correct, and so is an
XML loader recursing into a child node. Pass --all to see every self-call regardless.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOTS = ('src', 'lib')

# `Ret Class::Method(args) [const] {`  -- captures the class, the method and the argument text
DEF = re.compile(
    r'^[A-Za-z_][\w:<>,\s\*&]*?'          # return type
    r'\b([A-Za-z_]\w*)::([~]?[A-Za-z_]\w*)'  # Class::Method
    r'\s*\(([^;{]*?)\)\s*'                 # (args)
    r'(?:const\s*)?(?:noexcept\s*)?\{',    # trailing qualifiers then {
    re.M | re.S)


def body_of(text, brace_pos):
    """Return the text between the brace at brace_pos and its match."""
    depth = 0
    for i in range(brace_pos, len(text)):
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return text[brace_pos + 1:i]
    return text[brace_pos + 1:]


def strip_noise(body):
    """Remove comments and string literals so they cannot produce matches."""
    body = re.sub(r'/\*.*?\*/', ' ', body, flags=re.S)
    body = re.sub(r'//[^\n]*', ' ', body)
    body = re.sub(r'"(?:[^"\\]|\\.)*"', '""', body)

    return body


def split_top(text):
    """Split on top-level commas."""
    text = text.strip()
    if not text or text == 'void':
        return []
    out = []
    depth = 0
    cur = []
    for c in text:
        if c in '(<[':
            depth += 1
        elif c in ')>]':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(c)
    out.append(''.join(cur).strip())

    return [a for a in out if a]


def param_names(argtext):
    """The declared parameter NAMES, in order. Unnamed parameters come back as ''."""
    names = []
    for a in split_top(argtext):
        a = a.split('=')[0].strip()             # drop any default
        a = re.sub(r'\[\s*\]$', '', a).strip()    # drop a trailing []
        m = re.search(r'([A-Za-z_]\w*)$', a)
        names.append(m.group(1) if m else '')

    return names


def forwards_own_params(calltext, argtext):
    """True when the call passes exactly the enclosing function's parameters, unchanged."""
    params = param_names(argtext)
    passed = split_top(calltext)

    if len(params) != len(passed):
        return False

    # A no-argument function calling itself with no arguments is the clearest case of all.
    if not params:
        return True

    for want, got in zip(params, passed):
        if not want or got != want:
            return False

    return True


def main():
    show_all = '--all' in sys.argv[1:]
    hits = []
    scanned = 0

    for base in ROOTS:
        for dirpath, _dirs, files in os.walk(os.path.join(ROOT, base)):
            for fn in files:
                if not fn.endswith('.cpp'):
                    continue
                path = os.path.join(dirpath, fn)
                rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
                try:
                    text = io.open(path, encoding='utf-8', errors='replace').read()
                except OSError:
                    continue
                scanned += 1

                for m in DEF.finditer(text):
                    cls, method, args = m.group(1), m.group(2), m.group(3)
                    body = strip_noise(body_of(text, m.end() - 1))

                    # `this->Method(` with nothing qualifying it
                    for call in re.finditer(r'this\s*->\s*' + re.escape(method) + r'\s*\(([^;]*?)\)',
                                            body):
                        # `this->Base::Method(...)` is the correct form and must not be flagged;
                        # the regex above cannot match it because of the `::`, but a call written
                        # `this->Class::Method` would appear with the class name glued on.
                        if not show_all and not forwards_own_params(call.group(1), args):
                            continue

                        line = text[:m.start()].count('\n') + 1 + body[:call.start()].count('\n')
                        hits.append((rel, line, cls, method, args.strip()[:46]))

    print('scanned %d .cpp files under %s' % (scanned, ', '.join(ROOTS)))
    print()

    if not hits:
        print('no member function forwards its own parameters to itself through `this->`.')
        print('(pass --all to see overload dispatch and recursion on different data too)')
        return

    print('%d self-call(s) forwarding their own parameters unchanged. Each is either deliberate'
          ' re-entry with state changed elsewhere, or a missing `Base::`:' % len(hits))
    print()
    for rel, line, cls, method, args in sorted(hits):
        print('  %s:%d' % (rel, line))
        print('      in %s::%s(%s)  ->  this->%s(...)' % (cls, method, args, method))


if __name__ == '__main__':
    main()
