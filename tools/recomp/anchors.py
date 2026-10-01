#!/usr/bin/env python3
"""Recover module anchors the Ghidra export missed, straight from the reference binary.

The module of a reference function is the source file its assert strings name (recomp.py,
assign_modules). The export in data/ref-functions.jsonl carries the strings each function
references, but it missed a whole class of them: on 2026-10-01 the binary was found to hold
'.\\MapWeather.cpp', '.\\MapShadow.cpp', '.\\MapObj.cpp', '.\\MapObjGroup.cpp', '.\\WorldScene.cpp'
and '.\\M2Model.cpp' while no exported function carried any of them. The code does reference
them -- `push $0x00a3e9dc` and friends -- so the weather module was being counted as the event
system (outside the render surface), the shadow map as DetailDoodad.cpp, and M2Model.cpp as
M2Scene.cpp.

This scans the binary itself: every string in the image that looks like a source path, every
4-byte little-endian reference to it from .text, and the function that holds the reference
(nearest preceding start in ref-functions.jsonl). The result is data/ref-anchors.jsonl, one line
per (function, module), which assign_modules merges with the exported strings. A function that
already carries an exported path string keeps it; these only fill the gaps.

    python tools/recomp/anchors.py            # writes tools/recomp/data/ref-anchors.jsonl

A raw 4-byte match can collide with an unrelated immediate, so each line records how many
references were found; a module seen once in one function is weaker than one seen four times
in four. Nothing here is a guess about what a function does -- it is the same evidence the
assert anchors use, read from a place the exporter did not look.
"""

import bisect
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
DATA = os.path.join(HERE, 'data')
EXE = os.path.join(ROOT, '.reference', 'WOTLK 3.3.5a - Windows', 'WoW_WOTLK_3.3.5a', 'WoW.exe')
OUT = os.path.join(DATA, 'ref-anchors.jsonl')

# Same shape recomp.py's MODULE_STRING accepts, as bytes, NUL-terminated in the image.
PATH_RE = re.compile(rb'(?:\.\\|\.\./|\.\./\.\./|\.\.\\)*(?:[\w.\-]+[\\/])*([A-Za-z0-9_]+\.(?:cpp|c|h|inl))\x00')


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    optsz = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    out = []
    off = pe + 24 + optsz
    for i in range(nsec):
        name, vsize, va, rawsize, rawptr = struct.unpack_from('<8sIIII', data, off + i * 40)
        out.append((name.rstrip(b'\0').decode(), rawptr, rawsize, base + va))
    return out


def main():
    if not os.path.exists(EXE):
        sys.exit('reference binary not found: %s' % EXE)
    data = open(EXE, 'rb').read()
    secs = sections(data)
    text = next(s for s in secs if s[0] == '.text')

    def va_of(fo):
        for name, rawptr, rawsize, va in secs:
            if rawptr <= fo < rawptr + rawsize:
                return va + (fo - rawptr)
        return None

    # Every source-path string in the image, keyed by the VA of its first byte. A string that
    # starts mid-way through another path (the '.\\' prefix stripped) is a different VA and is
    # kept too: the code may push either form.
    #
    # The code pushes the address of the string it was given, and that is not always where a
    # regex match starts: '.\\MapWeather.cpp' sits directly behind another string's last byte
    # with no NUL between, so a greedy directory component swallows that byte and the match
    # starts one early. Register every plausible start inside the match -- the match itself,
    # each '.\\' or '..\\' prefix, and each byte after a separator -- so whichever one the
    # code points at is found.
    strings = {}
    for m in PATH_RE.finditer(data):
        mod = m.group(1).decode()
        s = m.group(0)
        starts = {0}
        for i in range(len(s)):
            if s[i:i + 2] == b'.\\' or s[i:i + 3] == b'..\\' or s[i:i + 3] == b'../':
                starts.add(i)
            if i > 0 and s[i - 1] in b'\\/':
                starts.add(i)
        for i in starts:
            va = va_of(m.start() + i)
            if va is not None and va not in strings:
                strings[va] = mod

    funcs = []
    for line in open(os.path.join(DATA, 'ref-functions.jsonl'), encoding='utf-8'):
        j = json.loads(line)
        if not j['thunk']:
            funcs.append(int(j['addr'], 16))
    funcs.sort()

    # One pass over .text reading every aligned-or-not 4-byte window.
    name, rawptr, rawsize, va0 = text
    blob = data[rawptr:rawptr + rawsize]
    hits = {}
    unpack = struct.Struct('<I').unpack_from
    for i in range(len(blob) - 3):
        v = unpack(blob, i)[0]
        mod = strings.get(v)
        if mod is None:
            continue
        # The byte before an immediate string pointer is nearly always `push imm32` (0x68) or
        # `mov r32, imm32` (0xB8..0xBF); a `mov [mem], imm32` (0xC7 ...) is the other form. Keep
        # those and drop the rest, which removes most accidental 4-byte collisions.
        prev = blob[i - 1] if i > 0 else 0
        prev3 = blob[i - 3] if i > 2 else 0
        prev4 = blob[i - 4] if i > 3 else 0
        if not (prev == 0x68 or 0xB8 <= prev <= 0xBF or prev3 == 0xC7 or prev4 == 0xC7):
            continue
        ref_va = va0 + i
        k = bisect.bisect_right(funcs, ref_va) - 1
        if k < 0:
            continue
        key = (funcs[k], mod)
        hits[key] = hits.get(key, 0) + 1

    with open(OUT, 'w', encoding='utf-8') as f:
        for (addr, mod), n in sorted(hits.items()):
            f.write(json.dumps({'addr': '%08x' % addr, 'module': mod, 'refs': n}) + '\n')
    mods = {}
    for (addr, mod), n in hits.items():
        mods[mod] = mods.get(mod, 0) + 1
    print('%d path strings in the image, %d (function, module) anchors across %d modules -> %s'
          % (len(strings), len(hits), len(mods), os.path.relpath(OUT, ROOT)))


if __name__ == '__main__':
    main()
