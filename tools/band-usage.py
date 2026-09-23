"""Which LightIntBand / LightFloatBand bands does the reference actually read?

InterpBandColor is FUN_007ebf30 and InterpFloatBand is FUN_007ebf90. Both take the band index as
their LAST argument, so at a __cdecl call site it is the FIRST thing pushed -- the push furthest
back from the call. Collect the immediates pushed in the window before each call and report which
band numbers appear, so frozen's own reads can be compared against them.
"""
import collections
import io
import os
import re

SP = os.path.dirname(os.path.abspath(__file__))
lines = io.open(os.path.join(SP, 'wow_text.asm'), encoding='utf-8', errors='replace').read().split('\n')

ADDR = re.compile(r'^\s+([0-9a-f]{6}):')
PUSH_IMM = re.compile(r'pushl\s+\$(0x[0-9a-f]+|\d+)\s*$')
CALL = re.compile(r'calll\s+(0x[0-9a-f]+)')

TARGETS = {'0x7ebf30': 'InterpBandColor (LightIntBand, 18 bands)',
           '0x7ebf90': 'InterpFloatBand (LightFloatBand, 6 bands)'}

rows = []
for ln in lines:
    m = ADDR.match(ln)
    if m:
        rows.append((int(m.group(1), 16), ln))

found = collections.defaultdict(collections.Counter)
sites = collections.defaultdict(list)

for i, (a, ln) in enumerate(rows):
    c = CALL.search(ln)
    if not c or c.group(1) not in TARGETS:
        continue
    # walk back for pushed immediates belonging to this call
    imms = []
    for j in range(max(0, i - 14), i):
        p = PUSH_IMM.search(rows[j][1])
        if p:
            imms.append(int(p.group(1), 0))
    if imms:
        band = imms[0]          # first pushed = last argument
        found[c.group(1)][band] += 1
        sites[c.group(1)].append((a, band))
    else:
        found[c.group(1)]['<computed>'] += 1

for tgt, label in TARGETS.items():
    print('%s -- %d call sites' % (label, sum(found[tgt].values())))
    for band, n in sorted(found[tgt].items(), key=lambda kv: (isinstance(kv[0], str), kv[0])):
        print('   band %-12s %d call(s)' % (band, n))
    print()
