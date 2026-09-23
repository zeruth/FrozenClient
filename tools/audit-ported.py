#!/usr/bin/env python3
"""Find overrides.json entries that claim status "ported" while the frozen function they name has
no real body.

`linked` says a reference function has a counterpart. `status` says what that counterpart is worth.
Nothing enforces the second: an overrides entry is hand-written, so it can say "ported" about a
function that is a `// TODO` and a constant return, and the report will count it among the ported
and let it compete for `faithful`. That is exactly the kind of drift this project keeps finding in
its own documents, and it is cheap to check.

Found on 2026-09-23, on its first run:

  00831ec0  CM2Model::CancelDeferredSequences   claimed ported, body was `// TODO` -- and it had two
                                                live call sites, so superseded animation requests
                                                were never actually cancelled. Ported the same day.
  00422130  SFile::IsStreamingMode              claimed ported, returns a constant 0, which gates
                                                off the whole texture priority path
  00488540  CScriptRegion::ProtectedFunctionsAllowed   claimed ported, returns a constant true
  0048ed30  CSimpleFrame::AttributeChangesAllowed      claimed ported, returns a constant true

It reuses livestubs.py's emptiness test rather than writing a second one, so the two tools cannot
drift apart. A bare `return <constant>;` counts as empty there, which is what catches the last
three above.

    python tools/audit-ported.py
"""

import importlib.util
import io
import json
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

spec = importlib.util.spec_from_file_location(
    'livestubs', os.path.join(ROOT, 'tools', 'livestubs.py'))
livestubs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(livestubs)


def empty_definitions():
    """Every qualified definition in src/ and lib/ whose body is empty, as name -> file."""
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

                    if m and livestubs.body_is_empty(lines, i):
                        found['%s::%s' % (m.group(1), m.group(2))] = os.path.relpath(path, ROOT)

    return found


def main():
    empty = empty_definitions()
    overrides = json.load(
        io.open(os.path.join(ROOT, 'tools', 'recomp', 'overrides.json'), encoding='utf-8'))

    hits = []

    for addr, entry in sorted(overrides.items()):
        if not isinstance(entry, dict) or entry.get('status') != 'ported':
            continue

        name = entry.get('frozen')

        if name and name in empty:
            hits.append((addr, name, empty[name]))

    if not hits:
        print('clean: every override claiming "ported" names a function with a real body')
        return 0

    print('%d override(s) claim "ported" but name an empty body:' % len(hits))

    for addr, name, path in hits:
        print('  %s  %-52s %s' % (addr, name, path))

    print()
    print('Either implement them, or change their status to "stub" so the report stops counting')
    print('them as ported and stops letting them compete for "faithful".')
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
