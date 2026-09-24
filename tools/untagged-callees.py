"""Which ported render functions lose the most fidelity to callees that are merely UNTAGGED?

DrawBatch went 0.60 -> 1.00 on 2026-09-23 without a line of behaviour changing: its four missing
calls were real functions frozen already had, just unlinked. That is the cheapest kind of progress
available, and it is findable -- rank ported render functions by how many of their reference
callees have no frozen counterpart yet.
"""
import collections
import io
import json
import os

ROOT = r'C:\Users\tyler\runicworld-client'
DATA = os.path.join(ROOT, 'tools', 'recomp', 'data')

mp = json.load(io.open(os.path.join(DATA, 'map.json'), encoding='utf-8'))

refs = {}
for line in io.open(os.path.join(DATA, 'ref-functions.jsonl'), encoding='utf-8'):
    d = json.loads(line)
    refs[d['addr']] = d

RENDER = ('M2Scene.cpp', 'M2Shared.cpp', 'CharacterModelBase.cpp', 'Texture.cpp',
          'TextureBlob.cpp', 'TextureCache.cpp', 'CGxDevice.cpp', 'CGxDeviceD3d9Ex.cpp',
          'MapChunk.cpp', 'Map.cpp', 'MapLoad.cpp', 'MapArea.cpp', 'MapObjRead.cpp',
          'MapChunkLiquid.cpp', 'DetailDoodad.cpp', 'MapMem.cpp', 'ShaderEffectManager.cpp',
          'ObjectEffect.cpp', 'Unit_C.cpp', 'Player_C.cpp')

rows = []
for a, v in mp.items():
    if not isinstance(v, dict) or v.get('module') not in RENDER:
        continue
    if v.get('status') not in ('ported', 'faithful'):
        continue
    if v.get('faithful'):
        continue
    r = refs.get(a)
    if not r:
        continue

    callees = [c for c in dict.fromkeys(r.get('calls', []))]
    unlinked = [c for c in callees if c not in mp and not refs.get(c, {}).get('thunk')]
    if not unlinked:
        continue

    rows.append((len(unlinked), len(callees), v['fidelity'], a, v['frozen'], v['module'], unlinked))

rows.sort(key=lambda t: (-t[0], t[2]))

print('ported render functions that are not faithful, ranked by untagged callees')
print()
for n, total, fid, a, frozen, mod, unl in rows[:18]:
    print('  %2d of %2d callees untagged  fid %.2f  %s  %-38s %s'
          % (n, total, fid, a, frozen[:38], mod))
    sizes = []
    for c in unl[:6]:
        rr = refs.get(c)
        sizes.append('%s(%s callers)' % (c, rr['callers'] if rr else '?'))
    print('       ' + ', '.join(sizes))
