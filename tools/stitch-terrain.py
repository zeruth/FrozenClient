#!/usr/bin/env python3
"""Pull the readable source runs out of carved fragments and report what of Terrain.cpp they hold.

A carved window is raw disk: parts of the file we want, parts of other files, and binary. This
extracts the long printable runs, throws away anything that is not plausibly C++, dedupes, and says
which of the lost functions each run contains -- so the pieces can be matched against the surviving
2104 lines and reassembled.

    python tools/stitch-terrain.py
    python tools/stitch-terrain.py --dump 3      # write run 3 out for inspection
"""

import glob
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAGS = os.path.join(ROOT, 'build', 'carved-frags')
CANDS = os.path.join(ROOT, 'build', 'carved')
OUT = os.path.join(ROOT, 'build', 'stitch')

MIN_RUN = 400

# Functions that were lost. Reporting which runs carry them is the whole point.
LOST = [
    'BuildWmoShadowGrid', 'WmoGroupContains', 'BuildWmoContainGrid', 'TerrainAreaIDAt',
    'TerrainInteriorAmbientAt', 'BlobShadowStrength', 'BlobShadowDrawWmo', 'BlobShadowBegin',
    'BlobShadowDraw', 'BlobShadowEnd', 'TerrainRebakeLiquidColors', 'SkyBodiesRender',
    'SkyRender', 'TerrainUpdateView', 'TerrainRender', 'TerrainSetWeather', 'TerrainLoad',
    'TerrainUnload', 'ChunkMatrixT', 'DetailDoodadRender', 'RenderFallback', 'CloudsRender',
    'GetHorizonFarClip', 'SKY_VIEWPORT_MIN_Z', 'SetAnimating(1)',
]

CPPISH = re.compile(r'[;{}]')


def runs_from(data):
    """Long printable stretches that look like source."""
    out = []
    cur = bytearray()

    for b in data:
        if 32 <= b < 127 or b in (9, 10, 13):
            cur.append(b)
        else:
            if len(cur) >= MIN_RUN:
                out.append(bytes(cur))

            cur = bytearray()

    if len(cur) >= MIN_RUN:
        out.append(bytes(cur))

    return out


def main():
    os.makedirs(OUT, exist_ok=True)

    paths = sorted(glob.glob(os.path.join(FRAGS, '*.bin')))
    paths += sorted(glob.glob(os.path.join(CANDS, '*.cpp')))

    if not paths:
        sys.exit('nothing carved yet: %s and %s are empty' % (FRAGS, CANDS))

    seen = {}
    rows = []

    for path in paths:
        data = open(path, 'rb').read()

        for run in runs_from(data):
            text = run.decode('latin-1', 'replace')

            if len(CPPISH.findall(text)) < 10:
                continue

            key = hashlib.md5(run).hexdigest()[:12]

            if key in seen:
                continue

            seen[key] = True
            found = [n for n in LOST if n in text]
            rows.append((len(run), key, os.path.basename(path), found, run))

    rows.sort(reverse=True)

    if '--dump' in sys.argv:
        idx = int(sys.argv[sys.argv.index('--dump') + 1])
        size, key, src, found, run = rows[idx]
        name = os.path.join(OUT, 'run-%02d-%s.cpp' % (idx, key))
        open(name, 'wb').write(run)
        print('wrote %s (%d bytes from %s)' % (name, size, src))
        return

    print('%d distinct source runs carved\n' % len(rows))
    print('%-4s %9s  %-34s %s' % ('#', 'bytes', 'from', 'lost functions present'))

    for i, (size, key, src, found, run) in enumerate(rows[:30]):
        print('%-4d %9d  %-34s %s' % (i, size, src[:34], ', '.join(found[:5]) or '-'))

    total = sum(r[0] for r in rows)
    withlost = [r for r in rows if r[3]]
    print()
    print('%d bytes of source recovered, %d runs contain lost functions' % (total, len(withlost)))

    # Write every run that carries lost code, so nothing is lost to a later overwrite.
    for i, (size, key, src, found, run) in enumerate(rows):
        if found:
            open(os.path.join(OUT, 'lost-%03d-%s.cpp' % (i, key)), 'wb').write(run)

    print('runs containing lost code written to %s' % OUT)


if __name__ == '__main__':
    main()
