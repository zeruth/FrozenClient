"""Recover the names of the reference's imported (external) functions.

The Ghidra export lists calls to imports as `EXTERNAL:000001xx` -- an external-location id, not a
name -- while the decompiled text in the corpus calls them by name (`ShowWindow(...)`). Without the
name, recomp.py could never match such a call against the port's `crt:showwindow`, so every
function that calls the Windows API was charged for the calls it does make.

The mapping is recovered by voting: for each function, the external ids it calls are paired with
the identifiers its decompiled text calls that are not internal functions, and an id takes the name
that co-occurs with it most often, provided that name is not claimed by a stronger id. Writes
data/externals.json, {"external:000001xx": "ShowWindow"}; recomp.py turns those into crt: tokens.

    python tools/recomp/externals.py
"""
import collections
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, 'data')

CALL_RE = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(')
SKIP = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'code', 'CONCAT44', 'CONCAT22', 'CONCAT31',
        'CONCAT11', 'ROUND', 'NAN', 'SUB41', 'SUB42', 'ZEXT', 'SEXT', 'ABS', 'SQRT', 'POPCOUNT', 'CARRY4',
        'SBORROW4', 'LOCK', 'UNLOCK', 'halt_baddata', 'trunc'}


def main():
    refs = {}
    with open(os.path.join(DATA, 'ref-functions.jsonl'), encoding='utf-8') as f:
        for line in f:
            r = json.loads(line)
            refs[r['addr'].lower()] = r
    internal = {r['name'] for r in refs.values()}

    votes = collections.defaultdict(collections.Counter)
    with open(os.path.join(DATA, 'corpus.jsonl'), encoding='utf-8') as f:
        for line in f:
            c = json.loads(line)
            r = refs.get(c['addr'].lower())
            if not r:
                continue
            ext = sorted({x.lower() for x in r.get('calls', r.get('callees', [])) if x.upper().startswith('EXTERNAL:')})
            if not ext:
                continue
            names = {n for n in CALL_RE.findall(c['c'])
                     if n not in SKIP and n not in internal and not n.startswith(('FUN_', 'thunk_', 'LAB_', 'DAT_', 'PTR_'))
                     and not re.match(r'^(u?int|float|double|char|byte|bool|short|long|undefined)\d*$', n)}
            if not names:
                continue
            weight = 1.0 / len(ext)
            for e in ext:
                for n in names:
                    votes[e][n] += weight / len(names) if len(ext) > 1 else 1.0 / len(names)

    # strongest pairs first, each name to one id
    pairs = sorted(((cnt, e, n) for e, ctr in votes.items() for n, cnt in ctr.items()), reverse=True)
    out = {}
    taken = set()
    for cnt, e, n in pairs:
        if e in out or n in taken:
            continue
        out[e] = n
        taken.add(n)

    with open(os.path.join(DATA, 'externals.json'), 'w', encoding='utf-8') as f:
        json.dump(dict(sorted(out.items())), f, indent=1)
    print('externals: %d ids named (of %d seen)' % (len(out), len(votes)))


if __name__ == '__main__':
    main()
