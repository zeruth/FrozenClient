#!/usr/bin/env python3
"""Recover the previous contents of src/world/Terrain.cpp from unallocated disk clusters.

A bad edit truncated the file from ~6100 lines to 2104. Python's open(path, 'w') truncates and
rewrites, so the ORIGINAL bytes are very likely still sitting in clusters NTFS now considers free.
They stay there until something else is written over them, which is why this should be run as soon
as possible and with as little other disk activity as possible.

MUST BE RUN FROM AN ELEVATED (Administrator) SHELL -- reading a raw volume needs it.

    python tools/carve-terrain.py                 # scan C: and write candidates
    python tools/carve-terrain.py --volume D      # a different drive

It only ever READS the volume. Candidates are written under build/carved/.
"""

import ctypes
import ctypes.wintypes as wt
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', 'carved')

# The first bytes of the file, which did not change. Every copy of Terrain.cpp that was ever
# flushed to disk starts with this.
SIGNATURE = b'#include "world/Terrain.hpp"\r\n#include "world/TerrainShadersD3d9.hpp"'
SIGNATURE_LF = b'#include "world/Terrain.hpp"\n#include "world/TerrainShadersD3d9.hpp"'

# Strings that only the LOST portion contained -- used to tell a full copy from the damaged one.
MARKERS = [b'WmoGroupContains', b'SkyRender', b'TerrainAreaIDAt', b'BlobShadowStrength']

WINDOW = 512 * 1024      # how much to grab after a hit
CHUNK = 8 * 1024 * 1024  # read size


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
            sys.exit('access denied reading %s -- run this from an Administrator shell' % path)

        sys.exit('could not open %s (error %d)' % (path, err))

    return k32, h


def main():
    letter = 'C'

    if '--volume' in sys.argv:
        letter = sys.argv[sys.argv.index('--volume') + 1].strip(':')

    os.makedirs(OUT, exist_ok=True)
    k32, h = open_volume(letter)

    k32.ReadFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD,
                             ctypes.POINTER(wt.DWORD), ctypes.c_void_p]

    buf = ctypes.create_string_buffer(CHUNK)
    got = wt.DWORD(0)
    offset = 0
    found = 0
    tail = b''

    print('scanning %s: read-only, looking for the file header ...' % letter)

    while True:
        ok = k32.ReadFile(h, buf, CHUNK, ctypes.byref(got), None)

        if not ok or got.value == 0:
            break

        data = tail + buf.raw[:got.value]

        for sig in (SIGNATURE, SIGNATURE_LF):
            start = 0

            while True:
                i = data.find(sig, start)

                if i < 0:
                    break

                start = i + 1
                blob = data[i:i + WINDOW]
                score = sum(1 for m in MARKERS if m in blob)

                if score == 0:
                    continue

                found += 1
                name = os.path.join(OUT, 'terrain-candidate-%02d-score%d.cpp' % (found, score))

                with open(name, 'wb') as fh:
                    fh.write(blob)

                print('  hit at ~offset %d, %d/%d markers -> %s'
                      % (offset + i, score, len(MARKERS), os.path.basename(name)))

        # Keep an overlap so a signature straddling a chunk boundary is still seen.
        tail = data[-(len(SIGNATURE) + WINDOW):]
        offset += got.value

        if offset % (2 * 1024 * 1024 * 1024) < CHUNK:
            print('  ... %d GB scanned, %d candidates' % (offset // (1024 ** 3), found))

    k32.CloseHandle(h)

    print()
    print('done: %d candidate(s) in %s' % (found, OUT))
    print('the best one is the highest score; trim it at the final closing brace of SkyRender.')


if __name__ == '__main__':
    main()
