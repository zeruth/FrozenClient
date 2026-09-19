"""Find {name, function} binding-table pairs by scanning the reference image directly.

Why this exists: ghidra/ExportScriptTables.java walks `listing.getDefinedData(true)`, so a pair is
only emitted when Ghidra has already typed the name as a string. Where it has not, the pair is
missing and nothing says so -- the lookup just comes back empty, which is indistinguishable from a
binding the reference does not have. That cost real time in one session: five CSimpleFont bindings
looked unresolvable, and CopyFontObject appeared to be absent from the client entirely, when all
six were simply un-typed strings sitting in a table the export walked straight past.

This scanner does not care what Ghidra thinks. It walks the data sections four bytes at a time and
emits a pair whenever one dword points at an identifier-shaped C string and the next points into
.text. That is the same shape ExportScriptTables looks for, arrived at from the other side.

    python tools/recomp/scantables.py                 # report what the Ghidra export is missing
    python tools/recomp/scantables.py --write         # merge the missing pairs into ref-tables.jsonl

It is deliberately a separate tool rather than a rewrite: the Ghidra export knows what a function
entry really is, and this only knows what lands in .text, so the two disagree at the edges and the
Ghidra one should stay authoritative where it speaks.
"""

import io
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, '.reference', 'WOTLK 3.3.5a - Windows', 'WoW_WOTLK_3.3.5a', 'WoW.exe')
TABLES = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'ref-tables.jsonl')

IDENT = re.compile(rb'^[A-Za-z_][A-Za-z0-9_]{1,63}$')


def sections(buf):
    pe = struct.unpack_from('<I', buf, 0x3c)[0]
    count = struct.unpack_from('<H', buf, pe + 6)[0]
    opt = struct.unpack_from('<H', buf, pe + 20)[0]
    base = struct.unpack_from('<I', buf, pe + 24 + 28)[0]
    out = []
    for i in range(count):
        o = pe + 24 + opt + i * 40
        out.append({
            'name': buf[o:o + 8].rstrip(b'\0').decode('latin1'),
            'va': struct.unpack_from('<I', buf, o + 12)[0],
            'vsize': struct.unpack_from('<I', buf, o + 8)[0],
            'rsize': struct.unpack_from('<I', buf, o + 16)[0],
            'raw': struct.unpack_from('<I', buf, o + 20)[0],
        })
    return base, out


def scan(buf, base, secs):
    text = next((s for s in secs if s['name'] == '.text'), None)
    if not text:
        return []

    lo = base + text['va']
    hi = lo + text['vsize']

    def offset(va):
        for s in secs:
            start = base + s['va']
            if start <= va < start + max(s['vsize'], s['rsize']):
                d = va - start
                return s['raw'] + d if d < s['rsize'] else None
        return None

    rdata = [s for s in secs if s['name'] == '.rdata']

    def in_rdata(va):
        for s in rdata:
            start = base + s['va']
            if start <= va < start + max(s['vsize'], s['rsize']):
                return True
        return False

    def cstring(va):
        o = offset(va)
        if o is None:
            return None
        end = buf.find(b'\0', o, o + 80)
        return buf[o:end] if end != -1 else None

    pairs = []
    for s in secs:
        if s['name'] not in ('.rdata', '.data'):
            continue
        blob = buf[s['raw']:s['raw'] + s['rsize']]
        for i in range(0, len(blob) - 8, 4):
            name_ptr, fn_ptr = struct.unpack_from('<II', blob, i)
            if not (lo <= fn_ptr < hi):
                continue
            # The name has to live in read-only data. Without this, runs of code bytes that
            # happen to spell an identifier before a .text pointer come through as pairs -- "Vj",
            # "RSSj" and friends -- and they are indistinguishable from real ones downstream.
            if not in_rdata(name_ptr):
                continue

            name = cstring(name_ptr)
            if not name or len(name) < 3 or not IDENT.match(name):
                continue
            entry_va = base + s['va'] + i
            pairs.append({
                'name': name.decode('latin1'),
                'fn': '%08x' % fn_ptr,
                'table': '%08x' % entry_va,
            })
    return pairs


def main():
    if not os.path.exists(EXE):
        print('reference exe not found: %s' % EXE)
        return 1

    buf = io.open(EXE, 'rb').read()
    base, secs = sections(buf)
    found = scan(buf, base, secs)

    known = set()
    if os.path.exists(TABLES):
        for line in io.open(TABLES, encoding='utf-8'):
            try:
                d = json.loads(line)
            except ValueError:
                continue
            known.add((d['name'], d['fn'].lower()))

    missing = [p for p in found if (p['name'], p['fn']) not in known]

    print('scanned %d candidate pairs; ghidra export has %d; %d not in it'
          % (len(found), len(known), len(missing)))

    for p in sorted(missing, key=lambda x: x['table'])[:40]:
        print('  %s  %-32s %s' % (p['table'], p['name'], p['fn']))
    if len(missing) > 40:
        print('  ... and %d more' % (len(missing) - 40))

    if '--write' in sys.argv and missing:
        with io.open(TABLES, 'a', encoding='utf-8', newline='\n') as f:
            for p in missing:
                f.write(json.dumps(p) + '\n')
        print('appended %d pairs to %s' % (len(missing), TABLES))

    return 0


if __name__ == '__main__':
    sys.exit(main())
