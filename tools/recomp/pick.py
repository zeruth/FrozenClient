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

**The class column** answers the same question automatically for widget files: a widget script
function reads its class's object-type id, and the expected id per file is catalogued, so the column
reads "ok" or "WRONG:<global>". It has caught five mis-offers.

It is blank for GameScript.cpp and the other global-function files, which have no object-type global
-- and that is where the wrong GetCVarAbsoluteMin was tagged on 2026-09-19. The **entry column** is
the candidate's address in the reference's binding table, printed so the run it sits in can be
compared by eye against a binding known to belong to the same table. It is not a verdict: frozen's
aggregate files draw from several reference tables, so a span test over a file's tagged entries does
not discriminate, which was tried and dropped rather than shipped as false confidence.

A global in the last column is only *suspicious*: some are constants the port would not need at all.
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

# Which object-type global each widget's script functions read. A widget script function's first act
# is to allocate its class's id into one of these, and the address shows up in the reference
# function's data references -- so the candidate the binding-table matcher offers can be checked
# against the file it was offered for, without decompiling anything.
#
# This exists because that matcher offers an address per NAME, and a name in several method tables
# resolves to whichever copy it saw first. Five times in one session it named another class's copy:
# HasScript and the font's type pair (both the animation group's), SetRotation (the texture's),
# IsObjectType (the texture's), SetOrientation (the status bar's, offered for the slider). Each cost
# a decompile to catch. Catalogue in docs/ref/INDEX.txt.
CLASS_GLOBAL = {
    'CSimpleFontStringScript.cpp': '00b4792c',
    'CSimpleTextureScript.cpp': '00b4793c',
    'CScriptRegionScript.cpp': '00b49978',
    'CSimpleFrameScript.cpp': '00b49984',
    'CSimpleFontScript.cpp': '00b499b0',
    'CSimpleModelScript.cpp': '00b499ec',
    'CGTooltipScript.cpp': '00c5cf4c',
    'CGCooldownScript.cpp': '00c2423c',
    'CGCharacterModelBaseScript.cpp': '00c0e4d4',
    'CSimpleStatusBarScript.cpp': '00dce440',
    'CSimpleMessageFrameScript.cpp': '00dce4a4',
    'CSimpleScrollFrameScript.cpp': '00dce4bc',
    'CSimpleSliderScript.cpp': '00dce4d4',
    'CSimpleButtonScript.cpp': '00dce650',
}

# Every object-type global known, whichever file it belongs to, so one can be recognised as such
# even in a file with no expectation recorded.
ALL_CLASS_GLOBALS = set(CLASS_GLOBAL.values()) | {'00b499dc', '00b4997c'}

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
    entry_of = {}
    for line in io.open(os.path.join(DATA, 'ref-tables.jsonl'), encoding='utf-8'):
        d = json.loads(line)
        tables.setdefault(d['name'], set()).add(d['fn'])
        entry_of.setdefault(d['fn'], set()).add(int(d['table'], 16))



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

                    # Does this function belong where it was offered? Two independent checks: the
                    # object-type global it reads (widgets only), and whether its method-table entry
                    # falls in the run this file's already-tagged bindings occupy (any file).
                    base = os.path.basename(rel)
                    expect = CLASS_GLOBAL.get(base)
                    seen_globals = {d for d in r.get('data', []) if d in ALL_CLASS_GLOBALS}

                    verdict = ''
                    if expect and seen_globals:
                        verdict = 'ok' if expect in seen_globals else 'WRONG:' + sorted(seen_globals)[0]

                    entries = sorted(entry_of.get(addr, ()))
                    entry = '%08x' % entries[0] if entries else ''

                    rows.append((-(known / len(callees)), len(globals_), r['size'],
                                 m.group(2), addr, base, globals_, verdict, entry))

    rows.sort()
    seen = set()
    print('%-5s %-6s %-5s %-30s %-9s %-9s %-34s %-16s %s'
          % ('cover', 'risky', 'size', 'name', 'addr', 'entry', 'file', 'class', 'globals'))

    shown = 0
    for cover, nglobals, size, name, addr, base, globals_, verdict, entry in rows:
        if (name, base) in seen:
            continue
        seen.add((name, base))
        print('%4.0f%% %6d %5d  %-30s %s  %-9s %-34s %-16s %s'
              % (-cover * 100, nglobals, size, name, addr, entry, base, verdict,
                 ' '.join(globals_[:4])))
        shown += 1
        if shown >= 40:
            break


if __name__ == '__main__':
    main()
