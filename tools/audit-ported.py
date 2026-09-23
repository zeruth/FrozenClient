#!/usr/bin/env python3
"""Find reference functions the report counts as "ported" whose frozen counterpart has no real body.

`linked` says a reference function has a counterpart. `ported` is supposed to say that counterpart
does the work. Nothing enforces the second. A frozen function whose body is a `// TODO`, or a
`// TODO` and a constant return, is indistinguishable from a real port to recomp.py unless it
carries a WHOA_UNIMPLEMENTED marker or a stub-shaped name -- so it is counted among the ported and
allowed to compete for `faithful`.

Two shapes of body count, and they are judged differently. A body with **no statements at all** is
a finding whatever its size: there is no size at which doing nothing is the port. A body that is a
**single bare return** is only a finding when the reference function is large, because a small
reference function really is one load and a return, and `return s_something;` really is its port.
THRESHOLD is where that stops being plausible.

Found on its first run, 2026-09-23:

  00831ec0  CM2Model::CancelDeferredSequences   claimed ported, body was `// TODO`, two live call
                                                sites -- superseded animation requests were never
                                                cancelled. Ported the same day.
  00422130  SFile::IsStreamingMode              returns a constant 0, which gates off the entire
                                                texture priority path
  twelve     CWorldParam::*Callback             every graphics-quality CVar callback, all empty, so
                                                the settings they back do nothing

It reuses livestubs.py's emptiness test rather than writing a second one, so the two cannot drift
apart.

    python tools/audit-ported.py
"""

import importlib.util
import io
import json
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# Below this reference size, a bare `return x;` is a plausible faithful port rather than a stub.
THRESHOLD = 32

spec = importlib.util.spec_from_file_location(
    'livestubs', os.path.join(ROOT, 'tools', 'livestubs.py'))
livestubs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(livestubs)


def empty_definitions():
    """Qualified definitions in src/ and lib/ with no real body, as name -> (kind, file).

    kind is 'empty' (nothing but comments) or 'return' (comments and one bare return).
    """
    found = {}

    for base in (os.path.join(ROOT, 'src'), os.path.join(ROOT, 'lib')):
        for dirpath, _dirs, files in os.walk(base):
            for name in files:
                if not name.endswith('.cpp'):
                    continue

                path = os.path.join(dirpath, name)

                try:
                    lines = io.open(path, encoding='utf-8', errors='replace').read().split('\n')
                except OSError:
                    continue

                for i, line in enumerate(lines):
                    m = livestubs.DEF.match(line.strip())

                    if not m:
                        continue

                    kind = livestubs.body_kind(lines, i)

                    if kind:
                        found['%s::%s' % (m.group(1), m.group(2))] = (
                            kind, os.path.relpath(path, ROOT))

    return found


def main():
    empty = empty_definitions()
    path = os.path.join(ROOT, 'tools', 'recomp', 'data', 'map.json')

    if not os.path.exists(path):
        print('no map.json -- run tools/recomp/recomp.py first')
        return 0

    world = json.load(io.open(path, encoding='utf-8'))

    findings = []
    borderline = []

    for addr, entry in sorted(world.items()):
        if not isinstance(entry, dict) or entry.get('status') != 'ported':
            continue

        name = entry.get('frozen')

        if not name or name not in empty:
            continue

        kind, src = empty[name]
        size = entry.get('refSize') or 0
        row = (addr, name, entry.get('how', '?'), size, src, kind)

        # No statements at all is never the port, at any size. A bare return might be.
        if kind == 'empty' or size >= THRESHOLD:
            findings.append(row)
        else:
            borderline.append(row)

    findings.sort(key=lambda r: -r[3])

    if findings:
        print('%d reference function(s) counted as "ported" with no real body:' % len(findings))
        print()

        for addr, name, how, size, src, kind in findings:
            print('  %s  %-46s %5d bytes  %-6s via %-9s %s'
                  % (addr, name, size, kind, how, src))

        print()
        print('Either implement them, or record status "stub" in overrides.json so the report stops')
        print('counting them as ported and stops letting them compete for "faithful".')

    if borderline:
        print()
        print('%d bare returns below the %d-byte threshold (probably the real port):'
              % (len(borderline), THRESHOLD))

        for addr, name, how, size, _src, _kind in sorted(borderline, key=lambda r: -r[3]):
            print('  %s  %-46s %5d bytes  via %s' % (addr, name, size, how))

    if not findings:
        print('clean: no reference function at or above %d bytes is counted as ported while its'
              % THRESHOLD)
        print('frozen counterpart has an empty body.')
        return 0

    return 1


if __name__ == '__main__':
    raise SystemExit(main())
