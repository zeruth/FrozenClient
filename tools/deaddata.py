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
  dead guard   the dangerous subset of read-only: never written AND used in an if/while condition,
               so the branch behind it can never run and whatever is inside has never executed.
               Ranked separately because the read-only list is sorted by count, and the field that
               hid the CGUnit_C::GetDisplayID recursion was read exactly once.

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
# src/object joined these on 2026-09-23. CLAUDE.md counts entity rendering as part of the render
# surface, and leaving it out is why m_localDisplayID -- the field standing in front of the
# CGUnit_C::GetDisplayID recursion -- was invisible to this tool.
HEADER_DIRS = [os.path.join(ROOT, 'src', 'model'), os.path.join(ROOT, 'src', 'world'),
               os.path.join(ROOT, 'src', 'gx'), os.path.join(ROOT, 'src', 'object')]

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


# Structs whose members arrive as bytes rather than by assignment, so "never written" is wrong for
# them rather than merely noisy. Two families:
#   * parsed from a file in place -- the module docstring's own blind spot (M2*, BLP*, CAaBsp*, ...)
#   * object field blocks, filled by the network object-update path by FIELD INDEX rather than by
#     name (CGUnitData, CGPlayerData, ...). Same effect on this scan: health and dynamicFlags came
#     up as dead guards on 2026-09-23 and are nothing of the kind.
PARSED_IN_PLACE = re.compile(
    r'^(?:M2|SMO|BLP|CAaBsp|MapObj|MOGP|MCNK|ADT|WMO'
    r'|CG\w*Data$|CMovementStatus$|CMoveSpline$)')


def trivial_accessors(blob):
    """Map a trivial getter's name to the member it returns.

    `if (this->GetLocalDisplayID() && ...)` is a use of m_localDisplayID, and without this the
    dead-guard scan cannot see the one case it was written for.
    """
    out = {}
    for m in re.finditer(
            r'\b([A-Za-z_]\w*)::([A-Za-z_]\w*)\s*\(\s*\)\s*(?:const\s*)?\{'
            r'\s*return\s+this->([A-Za-z_]\w*)\s*;\s*\}', blob):
        out.setdefault(m.group(3), set()).add(m.group(2))

    return out


def conditions(blob):
    """Every `if (...)` and `while (...)` condition in the blob, as text.

    Balanced-paren matched rather than regexed to the first `)`, because conditions in this tree
    routinely contain calls.
    """
    out = []
    for m in re.finditer(r'\b(?:if|while)\s*\(', blob):
        i = m.end() - 1
        depth = 0
        for j in range(i, min(len(blob), i + 4000)):
            c = blob[j]
            if c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    out.append(blob[i + 1:j])
                    break

    return out


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

    # The dangerous subset of the above, and the reason this section is not sorted by count: a
    # member nothing writes, standing in an `if` or `while`, means the branch behind it can never
    # run. Whatever is in there has never executed and is not covered by anything. Every trap
    # CLAUDE.md lists has this shape -- including CGUnit_C::GetDisplayID, whose infinite recursion
    # sat behind m_localDisplayID, a field read once and written never.
    conds = conditions(blob)
    accessors = trivial_accessors(blob)
    guards = []

    for _r, nm in read_only:
        if all(PARSED_IN_PLACE.match(c) for c in members[nm]):
            continue

        # Direct use, plus any trivial getter that just returns this field.
        alts = [r'(?:\.|->)' + re.escape(nm) + r'\b']
        for g in accessors.get(nm, ()):
            alts.append(r'\b' + re.escape(g) + r'\s*\(\s*\)')
        pat = re.compile('|'.join(alts))

        n = sum(1 for c in conds if pat.search(c))
        if n:
            via = sorted(accessors.get(nm, ()))
            guards.append((n, nm, via))

    print()
    print('=== DEAD GUARDS: never written, yet used in an if/while condition ===')

    if not guards:
        print('  none.')
    else:
        print('  The branch behind each of these has never run. Check what is inside it.')
        print()
        for n, nm, via in sorted(guards, reverse=True):
            tail = ','.join(sorted(members[nm]))[:40]
            if via:
                tail += '   [via %s()]' % ', '.join(v + '' for v in via[:2])
            print('  %3d condition(s)  %-28s %s' % (n, nm, tail))


if __name__ == '__main__':
    main()
