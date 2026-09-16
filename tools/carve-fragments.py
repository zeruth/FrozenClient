#!/usr/bin/env python3
"""Second-pass carve: find the LATER fragments of Terrain.cpp, not just the header.

The first pass keyed on the file's opening bytes and every hit ran into binary data after a while.
That is the signature of a FRAGMENTED file: NTFS scattered it, so a linear window from the header
only recovers the first extent. The rest is elsewhere on the volume and has to be found on its own
content.

This pass searches for strings that live in the middle and end of the file, and dumps a window on
BOTH sides of each hit so the surrounding extent comes with it. The pieces can then be stitched.

MUST BE RUN FROM AN ELEVATED (Administrator) SHELL. It only ever reads.

    python tools/carve-fragments.py
"""

import ctypes
import ctypes.wintypes as wt
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', 'carved-frags')

# Distinctive lines from across the lost region. Each should appear in the newest version of the
# file and almost nowhere else on the volume. Ordered roughly by position in the file.
NEEDLES = [
    b'void BuildWmoShadowGrid(WmoGroup& grp)',
    b'bool WmoGroupContains(WmoGroup& grp',
    b'uint32_t TerrainAreaIDAt(const C3Vector& pos)',
    b'bool TerrainInteriorAmbientAt(const C3Vector& pos',
    b'float BlobShadowStrength()',
    b'void BlobShadowDrawWmo(',
    b'void TerrainRebakeLiquidColors()',
    b'void SkyBodiesRender()',
    b'void SkyRender()',
    b'void TerrainUpdateView(',
    b'const float SKY_VIEWPORT_MIN_Z',
    b's_skyboxModel->SetAnimating(1)',
]

BEFORE = 192 * 1024
AFTER = 192 * 1024
CHUNK = 8 * 1024 * 1024


def open_volume(letter):
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p,
                                wt.DWORD, wt.DWORD, wt.HANDLE]
    k32.CreateFileW.restype = wt.HANDLE

    path = chr(92) * 2 + '.' + chr(92) + letter + ':'
    h = k32.CreateFileW(path, 0x80000000, 0x1 | 0x2, None, 3, 0, None)

    if h is None or h == ctypes.c_void_p(-1).value:
        err = ctypes.get_last_error()

        if err == 5:
            sys.exit('access denied -- run this from an Administrator shell')

        sys.exit('could not open %s (error %d)' % (path, err))

    return k32, h


def main():
    letter = sys.argv[sys.argv.index('--volume') + 1].strip(':') if '--volume' in sys.argv else 'C'

    os.makedirs(OUT, exist_ok=True)
    k32, h = open_volume(letter)
    k32.ReadFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD,
                             ctypes.POINTER(wt.DWORD), ctypes.c_void_p]

    buf = ctypes.create_string_buffer(CHUNK)
    got = wt.DWORD(0)
    offset = 0
    prev = b''
    seen = set()
    hits = 0

    print('scanning %s: for %d mid-file markers ...' % (letter, len(NEEDLES)))

    while True:
        if not k32.ReadFile(h, buf, CHUNK, ctypes.byref(got), None) or got.value == 0:
            break

        data = prev + buf.raw[:got.value]

        for needle in NEEDLES:
            start = 0

            while True:
                i = data.find(needle, start)

                if i < 0:
                    break

                start = i + 1
                lo = max(0, i - BEFORE)
                blob = data[lo:i + AFTER]
                key = hashlib.md5(blob[:4096]).hexdigest()[:10]

                if key in seen:
                    continue

                seen.add(key)
                hits += 1
                tag = needle.split(b'(')[0].split()[-1].decode('latin-1', 'replace')
                tag = ''.join(c for c in tag if c.isalnum())[:24] or 'frag'
                name = os.path.join(OUT, 'frag-%02d-%s-%s.bin' % (hits, tag, key))

                with open(name, 'wb') as fh:
                    fh.write(blob)

                print('  %-30s @ %d -> %s' % (tag, offset + i, os.path.basename(name)))

        prev = data[-max(BEFORE, 4096):]
        offset += got.value

        if offset % (4 * 1024 * 1024 * 1024) < CHUNK:
            print('  ... %d GB, %d fragments' % (offset // (1024 ** 3), hits))

    k32.CloseHandle(h)
    print()
    print('done: %d fragment(s) in %s' % (hits, OUT))


if __name__ == '__main__':
    main()
