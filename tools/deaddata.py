#!/usr/bin/env python3
"""Find render-struct members that are computed and never consumed, or consumed and never filled.

The diffuse-colour defect found on 2026-09-23 had a signature neither of the other tools can see.
`modelLight.diffuseColorTrack` was animated every frame and read nowhere, while the value that
should have come from it was taken from the neighbouring track. The function was not a stub, so
livestubs was blind to it, and its call order was fine, so the fidelity score was blind to it. It
is the DATA that went nowhere.

Two shapes are worth flagging:

  write-only   something computes it and nothing consumes it. Either dead work, or -- as above --
               a consumer that is reading the wrong field.
  read-only    something consumes it and nothing ever fills it, so it is permanently whatever the
               constructor left. Sometimes correct (a reference default that is never animated,
               like CM2Light's attenuation), sometimes a missing port.

**Read the hits; do not total them.** Known blind spots, all of which produce false positives on
the read-only side:

  * fields filled by parsing a file in place rather than by assignment -- every M2Vertex, M2Batch
    and CAaBspNode member looks unwritten because the bytes arrive as a struct.
  * writes through a member's own methods (`m.Identity()`, `stack.Push()`), which cannot be told
    from reads without a real parser. matrixB4 looked unwritten for this reason.
  * anything reached through a pointer alias rather than the member name.

The write-only side is the sounder half: a write is a write, and something that is written and
never mentioned again is suspicious whatever the parser misses.

    python tools/deaddata.py
"""

import collections
import io
import os
import re

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
HEADER_DIRS = [os.path.join(ROOT, 'src', 'model'), os.path.join(ROOT, 'src', 'world'),
               os.path.join(ROOT, 'src', 'gx')]

MEMBER = re.compile(
    r'^\s{4,}(?:static\s+)?(?:const\s+)?'
    r'(?:uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|float|double|bool|char|'
    r'C[234][iu]?Vector|C44Matrix|C33Matrix|CImVector|CAaSphere|CAaBox|C4Plane|C3Ray|CRect|HTEXTURE)\s+'
    r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;')

# Names too common to attribute to one class.
TOO_COMMON = {'x', 'y', 'z', 'w', 'r', 'c', 'd', 'a', 'b', 'i', 'n', 'count', 'size', 'flags',
              'type', 'value', 'data', 'index', 'time', 'name', 'id', 'min', 'max', 'left',
              'right', 'top', 'bottom', 'width', 'height', 'start', 'end', 'next', 'prev'}


def collect_members():
    members = {}
    for d in HEADER_DIRS:
        for base, _dirs, names in os.walk(d):
            for fn in names:
                if not fn.endswith('.hpp'):
                    continue
                text = io.open(os.path.join(base, fn), encoding='utf-8', errors='replace').read()
                cls = None
                for line in text.split('\n'):
                    m = re.match(r'\s*(?:class|struct)\s+([A-Za-z_]\w*)', line)
                    if m:
                        cls = m.group(1)
                    mm = MEMBER.match(line)
                    if mm and cls:
                        nm = mm.group(1)
                        if nm in TOO_COMMON or len(nm) < 4:
                            continue
                        members.setdefault(nm, set()).add(cls)
    return members


def read_sources():
    blob = []
    for base, _dirs, names in os.walk(os.path.join(ROOT, 'src')):
        for fn in names:
            if fn.endswith(('.cpp', '.hpp')):
                blob.append(io.open(os.path.join(base, fn), encoding='utf-8',
                                    errors='replace').read())
    return '\n'.join(blob)


def main():
    members = collect_members()
    blob = read_sources()

    print('%d candidate members across %d classes'
          % (len(members), len({c for v in members.values() for c in v})))

    writes = collections.Counter()
    reads = collections.Counter()

    for nm in members:
        q = re.escape(nm)
        # Everything that mutates the member: plain assignment, compound assignment, increment,
        # assignment into a subfield or an element, and a method call on it.
        w = len(re.findall(
            r'(?:\.|->)' + q + r'\b'
            r'(?:\s*\[[^\]]*\]|\s*\.\s*\w+|\s*->\s*\w+)*'
            r'\s*(?:=(?!=)|\+=|-=|\*=|/=|\|=|&=|\^=|<<=|>>=|\+\+|--|\.\s*[A-Z]\w*\s*\()', blob))
        total = len(re.findall(r'(?:\.|->)' + q + r'\b', blob))
        writes[nm] = w
        reads[nm] = max(total - w, 0)

    write_only = sorted(((writes[n], n) for n in members if reads[n] == 0 and writes[n] > 0),
                        reverse=True)
    read_only = sorted(((reads[n], n) for n in members if writes[n] == 0 and reads[n] > 0),
                       reverse=True)

    print()
    print('=== WRITE-ONLY: computed, never consumed (the sounder half) ===')
    for w, nm in write_only[:30]:
        print('  %3d writes  %-32s %s' % (w, nm, ','.join(sorted(members[nm]))[:50]))

    print()
    print('=== READ-ONLY: consumed, never filled (advisory -- see the blind spots above) ===')
    for r, nm in read_only[:30]:
        print('  %3d reads   %-32s %s' % (r, nm, ','.join(sorted(members[nm]))[:50]))


if __name__ == '__main__':
    main()
