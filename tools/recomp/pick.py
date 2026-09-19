"""Rank stubbed Lua bindings by how likely they are to be implementable right now.

    python tools/recomp/pick.py                 # the whole surface
    python tools/recomp/pick.py CSimple CGTool  # only files matching one of these

Two filters, and the second exists because the first is not enough.

**Callee coverage** -- what fraction of the reference function's callees frozen already has a
counterpart for. A stub at 100% calls nothing unported. This is the right first cut and it produced
the guild-command batch.

**Risky globals** -- how many writable module globals the reference reads that are not just its
class's object-type id. Coverage answers "can the port reach what this CALLS" and says nothing about
whether the DATA exists, which is how five arena-team commands and GetTitleName came to look
implementable and were not: the arena ones read a team table at 00c0f840 and GetTitleName reads
CharTitles.dbc at 00ad3390, neither of which frozen has. Both scored 100% coverage.

A candidate with 100% coverage and zero risky globals is usually a morning's work. One with risky
globals needs whatever those globals are first, and the address is printed so it can be chased.

Caveats worth knowing. The binding-table matcher offers an address per NAME, and a name that
appears in several method tables can resolve to the wrong class -- check the object-type global in
the body against docs/ref/INDEX.txt before tagging anything. And a global here is only *suspicious*:
some of them are constants the port would not need at all.
"""

import io
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DATA = os.path.join(HERE, 'data')
REFERENCE = os.path.join(ROOT, '.reference', 'WOTLK 3.3.5a - Windows', 'WoW_WOTLK_3.3.5a', 'WoW.exe')

# Object-type ids: every widget script function lazily allocates its class's id into one of these,
# so a read of one says nothing about what state the function needs. Catalogued in
# docs/ref/INDEX.txt; 00d3f778 is the counter they allocate from.
BENIGN = {
    '00d3f778',
    '00b4792c', '00b4793c', '00b49978', '00b49984', '00b499b0', '00b499dc', '00b499ec',
    '00b4997c', '00c5cf4c', '00c2423c', '00c0e4d4',
    '00dce440', '00dce4a4', '00dce4bc', '00dce4d4', '00dce650',
}


def rdata_ranges():
    """VA ranges of the read-only sections, so constants and vtables can be told from state."""
    if not os.path.exists(REFERENCE):
        return []

    data = io.open(REFERENCE, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsections = struct.unpack_from('<H', data, pe + 6)[0]
    optsize = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    sect = pe + 24 + optsize

    out = []
    for i in range(nsections):
        off = sect + i * 40
        name = data[off:off + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsize, va, rawsize, _ = struct.unpack_from('<IIII', data, off + 8)
        if name in ('.rdata', '.text'):
            out.append((base + va, base + va + max(vsize, rawsize)))

    return out


def main():
    want = [a.lower() for a in sys.argv[1:]]

    refs = {}
    for line in io.open(os.path.join(DATA, 'ref-functions.jsonl'), encoding='utf-8'):
        d = json.loads(line)
        refs[d['addr']] = d

    mapped = json.load(io.open(os.path.join(DATA, 'map.json'), encoding='utf-8'))

    tables = {}
    for line in io.open(os.path.join(DATA, 'ref-tables.jsonl'), encoding='utf-8'):
        d = json.loads(line)
        tables.setdefault(d['name'], set()).add(d['fn'])

    ro = rdata_ranges()

    def risky(addr):
        if addr.startswith('Stack') or addr in BENIGN:
            return False
        try:
            va = int(addr, 16)
        except ValueError:
            return False

        return not any(lo <= va < hi for lo, hi in ro)

    rows = []
    for dirpath, _, files in os.walk(os.path.join(ROOT, 'src', 'ui')):
        for f in files:
            if not f.endswith('Script.cpp'):
                continue
            rel = os.path.relpath(os.path.join(dirpath, f), ROOT).replace('\\', '/')
            if want and not any(w in rel.lower() for w in want):
                continue

            src = io.open(os.path.join(dirpath, f), encoding='utf-8', errors='replace').read()
            for m in re.finditer(r'int32_t (\w+?)_(\w+)\(lua_State\* L\) \{\n    WHOA_UNIMPLEMENTED', src):
                for addr in sorted(tables.get(m.group(2), ())):
                    r = refs.get(addr)
                    if not r:
                        continue

                    callees = [c for c in r['callees'] if c in refs and not refs[c].get('thunk')]
                    if not callees:
                        continue

                    known = sum(1 for c in callees if c in mapped)
                    globals_ = sorted({d for d in r.get('data', []) if risky(d)})
                    rows.append((-(known / len(callees)), len(globals_), r['size'],
                                 m.group(2), addr, os.path.basename(rel), globals_))

    rows.sort()
    seen = set()
    print('%-5s %-6s %-5s %-30s %-9s %-34s %s'
          % ('cover', 'risky', 'size', 'name', 'addr', 'file', 'globals'))

    shown = 0
    for cover, nglobals, size, name, addr, base, globals_ in rows:
        if (name, base) in seen:
            continue
        seen.add((name, base))
        print('%4.0f%% %6d %5d  %-30s %s  %-34s %s'
              % (-cover * 100, nglobals, size, name, addr, base, ' '.join(globals_[:4])))
        shown += 1
        if shown >= 40:
            break


if __name__ == '__main__':
    main()
