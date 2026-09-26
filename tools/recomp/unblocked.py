#!/usr/bin/env python
"""Unmapped reference functions every one of whose callees is already mapped.

Why this exists: the ranked queues in docs/recomp/REPORT.md order by weight (callers x size),
which finds the most VALUABLE thing to port next but not the most PORTABLE one. Over and over a
top-ranked function turned out to sit on four to eight callees frozen does not have, so porting it
would land a shell below an empty branch. This asks the opposite question -- what can be ported
right now, with nothing else needed first -- and it is the question worth asking when the cheap
leaves in an area are gone.

It reads the same two files the report does, so it always reflects the CURRENT map:

    tools/recomp/data/ref-functions.jsonl   the reference inventory
    tools/recomp/data/map.json              what is linked, as of the last recomp.py run

The inventory's `module` field is null -- recomp.py works the module out at runtime from the
assert-string anchors in linker order -- so this borrows assign_modules from it rather than
guessing. Filtering on the raw field instead silently matches nothing, which is exactly the trap
this tool is meant to keep people out of.

Note that docs/recomp/queue/*.c is NOT a source of truth. Those files accumulate across sessions
and go stale as their functions get ported; reading them as a worklist finds things that were
linked weeks ago.

    python tools/recomp/unblocked.py                 # render surface, by callers x size
    python tools/recomp/unblocked.py --all           # every module, not just the render surface
    python tools/recomp/unblocked.py --module Map.cpp
    python tools/recomp/unblocked.py --max-size 800  # only ones small enough for one sitting
    python tools/recomp/unblocked.py --summary       # where the remaining work actually is
"""

import argparse
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REFS = os.path.join(HERE, 'data', 'ref-functions.jsonl')
MAP = os.path.join(HERE, 'data', 'map.json')

# Kept in step with RENDER_MODULES in recomp.py; see the comment there about CreepTendril.cpp.
RENDER_MODULES = {
    'Map.cpp', 'MapChunk.cpp', 'MapChunkLiquid.cpp', 'MapMem.cpp', 'MapLoad.cpp', 'MapArea.cpp',
    'MapObj.cpp', 'MapObjRead.cpp', 'MapObjGroup.cpp', 'DetailDoodad.cpp', 'WorldParam.cpp',
    'MapWeather.cpp', 'DayNight.cpp', 'Sky.cpp', 'MapShadow.cpp',
    'M2Scene.cpp', 'M2Shared.cpp', 'M2Model.cpp', 'ModelBlob.cpp', 'CharacterModelBase.cpp',
    'Unit_C.cpp', 'Player_C.cpp', 'GameObject_C.cpp', 'UnitMissileTrajectory_C.cpp',
    'MovementShared.cpp', 'ObjectEffect.cpp',
    'Texture.cpp', 'TextureCache.cpp', 'TextureBlob.cpp', 'FFXEffects.cpp',
    'ShaderEffectManager.cpp', 'CGxDevice.cpp', 'CGxDeviceD3d9Ex.cpp', 'CGxD3d9ExTexture.cpp',
    'CGxDeviceOpenGl.cpp',
}


def is_template_instantiation(r):
    # Container and template code tags its allocations with the MSVC mangled type name, so a
    # function carrying one is almost always one instantiation of something frozen keeps as a
    # single template. Those can never be linked one-to-one by name -- see the collapsed-key note
    # on override 004b9760 -- so it is worth knowing how many of a module's gap they are.
    for text in (r.get('strings') or []):
        if '.?AV' in text or '.PAV' in text or '.?AU' in text or '.PAU' in text:
            return True

    return False


def summarise(refs, mapped, args):
    from collections import Counter

    pool = []
    for addr, r in refs.items():
        if addr in mapped:
            continue

        module = r.get('module')

        if args.module:
            if module != args.module:
                continue
        elif not args.all and module not in RENDER_MODULES:
            continue

        pool.append(r)

    unblocked = set()
    for r in pool:
        if not [c for c in (r.get('callees') or []) if c in refs and c not in mapped]:
            unblocked.add(r['addr'])

    print('%d unmapped%s' % (len(pool), '' if args.all or args.module else ' on the render surface'))
    print('  %d of them have every callee linked already (portable now)' % len(unblocked))
    print('  %d carry a mangled type name, so are container/template instantiations'
          % sum(1 for r in pool if is_template_instantiation(r)))
    print()
    print('  module                       unmapped  portable  templated')

    counts = Counter(r.get('module') for r in pool)

    for module, total in counts.most_common():
        rows = [r for r in pool if r.get('module') == module]
        print('  %-26s %8d  %8d  %9d'
              % (module[:26], total,
                 sum(1 for r in rows if r['addr'] in unblocked),
                 sum(1 for r in rows if is_template_instantiation(r))))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--all', action='store_true', help='every module, not just the render surface')
    ap.add_argument('--module', metavar='FILE.cpp', help='only this reference module')
    ap.add_argument('--max-size', type=int, default=0, metavar='N',
                    help='only functions of at most N code bytes')
    ap.add_argument('--limit', type=int, default=40, metavar='N')
    ap.add_argument('--summary', action='store_true',
                    help='per-module totals for everything unmapped, not just the unblocked')
    args = ap.parse_args()

    mapped = set(json.load(io.open(MAP, encoding='utf-8')).keys())

    refs = {}
    for line in io.open(REFS, encoding='utf-8'):
        r = json.loads(line)
        refs[r['addr']] = r

    sys.path.insert(0, HERE)
    import recomp
    recomp.assign_modules(refs)

    if args.summary:
        summarise(refs, mapped, args)

        return

    rows = []
    for addr, r in refs.items():
        if addr in mapped:
            continue

        module = r.get('module')

        if args.module:
            if module != args.module:
                continue
        elif not args.all and module not in RENDER_MODULES:
            continue

        size = r.get('size') or 0

        if args.max_size and size > args.max_size:
            continue

        # Callees outside the inventory (imports, thunks) cannot block anything.
        blockers = [c for c in (r.get('callees') or []) if c in refs and c not in mapped]

        if blockers:
            continue

        callers = r.get('callers') or 0
        rows.append((callers * size, addr, module or '?', size, callers))

    rows.sort(reverse=True)

    print('%d unmapped functions with every callee already linked%s'
          % (len(rows), '' if args.all or args.module else ' (render surface)'))
    print()
    print('  ref       module                    size  callers  weight')

    for weight, addr, module, size, callers in rows[:args.limit]:
        print('  %-8s  %-24s %5d  %7d  %6d' % (addr, module[:24], size, callers, weight))


if __name__ == '__main__':
    main()
