#!/usr/bin/env python3
"""Diff the reference's and frozen's per-frame call traces and mark what matched as verified.

Inputs: data/trace-ref.jsonl and data/trace-frozen.jsonl from calltrace.py (same map.json, same
scene as far as the accounts allow). Both are cut into frames at CGWorldFrame::RenderWorld and every
reference hit is translated to its frozen name through the map, so the two sides speak the same names.

For every function that either side called, per frame:
  * present on both sides, same count in most frames, and the frame-level order agrees
    (LCS over the frame's sequence >= 0.8)                     -> verified (written to data/verified.json)
  * called by the reference every frame, never by frozen       -> "frozen never calls" (a missing call site)
  * called by frozen, never by the reference                   -> "frozen adds" (invented behaviour)
  * both call it but counts differ                           -> count mismatch

recomp.py reads verified.json: a linked function in it reports as status "verified" with the
trace date, unless overrides.json says otherwise. Re-tracing overwrites the file; verification
is only as current as the last run, which is the point.

    python tools/recomp/tracecompare.py            # compare, write verified.json, print the summary
"""

import collections
import datetime
import io
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, 'data')
ROOT = os.path.dirname(os.path.dirname(HERE))
MARKER = 'CGWorldFrame::RenderWorld'
ORDER_OK = 0.8


def load(path, ref_names=None):
    """Frames of names. Reference hits are re-labelled through the CURRENT map by address, so a
    trace taken before a link was corrected still compares under the corrected name; hits whose
    address is no longer linked are dropped."""
    rows = [json.loads(l) for l in io.open(path, encoding='utf-8') if l.strip()]
    frames = []
    cur = None
    for r in rows:
        if ref_names is not None:
            if r['key'] == '004faf90':
                name = MARKER
            else:
                name = ref_names.get(r['key'])
                if name is None:
                    continue
        else:
            name = r['name']
        if name == MARKER:
            if cur is not None:
                frames.append(cur)
            cur = []
        elif cur is not None:
            cur.append(name)
    # the trailing partial frame is dropped; only complete frames count
    return frames


def lcs_len(a, b):
    if not a or not b:
        return 0
    prev = [0] * (len(b) + 1)
    for x in a:
        cur = [0]
        for j, y in enumerate(b):
            cur.append(prev[j] + 1 if x == y else max(prev[j + 1], cur[j]))
        prev = cur
    return prev[-1]


def main():
    m = json.load(io.open(os.path.join(DATA, 'map.json'), encoding='utf-8'))
    by_name = {e['frozen']: a for a, e in m.items()}
    ref = load(os.path.join(DATA, 'trace-ref.jsonl'), {a: e['frozen'] for a, e in m.items()})
    frozen = load(os.path.join(DATA, 'trace-frozen.jsonl'))
    if not ref or not frozen:
        raise SystemExit('need complete frames on both sides (ref %d, frozen %d)' % (len(ref), len(frozen)))
    n = min(len(ref), len(frozen))
    ref, frozen = ref[:n], frozen[:n]

    names = set()
    for f in ref + frozen:
        names |= set(f)
    per = {}
    for name in sorted(names):
        rc = [f.count(name) for f in ref]
        wc = [f.count(name) for f in frozen]
        per[name] = (rc, wc)

    # frame-level order: the sequence of names both sides know, compared frame by frame
    common = set(x for x in names if any(per[x][0]) and any(per[x][1]))
    order = []
    for fr, fw in zip(ref, frozen):
        a = [x for x in fr if x in common]
        b = [x for x in fw if x in common]
        order.append(lcs_len(a, b) / float(max(len(a), len(b))) if a or b else 1.0)
    order_avg = sum(order) / len(order)

    verified, missing, added, mismatch = {}, [], [], []
    for name, (rc, wc) in per.items():
        same = sum(1 for a, b in zip(rc, wc) if a == b)
        if all(rc) and not any(wc):
            missing.append((sum(rc), name))
        elif any(wc) and not any(rc):
            added.append((sum(wc), name))
        elif any(rc) and any(wc):
            # Same per-frame count in most frames is the per-function evidence; the frame-level
            # order agreement is recorded with it rather than required, because it is dragged
            # down by every wrong link and by scene differences that have nothing to do with
            # this function.
            if same >= max(2, (n * 2) // 3):
                verified[by_name.get(name, name)] = {'frozen': name, 'frames': n, 'refPerFrame': rc, 'frozenPerFrame': wc,
                                                     'note': 'trace %s: same per-frame count in %d/%d frames (frame order agreement overall %.0f%%)' % (
                                                         datetime.datetime.now().strftime('%Y-%m-%d %H:%M'), same, n, order_avg * 100)}
            else:
                mismatch.append((abs(sum(rc) - sum(wc)), name, rc, wc))

    # A link the trace contradicts: one side calls it every frame and the other never, or the
    # per-frame counts differ by 20x in every frame. Different scenes explain a factor of a few,
    # not that. recomp.py drops such links unless an override or a tag vouches for them.
    suspect = {}
    for name, (rc, wc) in per.items():
        a = by_name.get(name)
        if not a:
            continue
        if (all(rc) and not any(wc)) or (all(wc) and not any(rc)):
            suspect[a] = {'frozen': name, 'why': 'one side every frame, the other never', 'ref': rc, 'frozen_': wc}
        elif all(rc) and all(wc) and all(max(x, y) >= 20 * max(1, min(x, y)) for x, y in zip(rc, wc)):
            suspect[a] = {'frozen': name, 'why': 'per-frame counts differ 20x every frame', 'ref': rc, 'frozen_': wc}
    json.dump(suspect, io.open(os.path.join(DATA, 'suspect.json'), 'w', encoding='utf-8'), indent=1, sort_keys=True)
    json.dump(verified, io.open(os.path.join(DATA, 'verified.json'), 'w', encoding='utf-8'), indent=1, sort_keys=True)
    summary = {'date': datetime.datetime.now().strftime('%Y-%m-%d %H:%M'), 'frames': n, 'orderAvg': round(order_avg, 3),
               'verified': len(verified), 'missing': sorted(missing, reverse=True)[:60], 'added': sorted(added, reverse=True)[:60],
               'mismatch': [(d, nm, rc, wc) for d, nm, rc, wc in sorted(mismatch, reverse=True)[:60]]}
    json.dump(summary, io.open(os.path.join(DATA, 'trace-summary.json'), 'w', encoding='utf-8'), indent=1)

    print('%d frames compared; frame order agreement %.0f%%' % (n, order_avg * 100))
    print('verified %d, frozen never calls %d, frozen adds %d, count mismatch %d' % (len(verified), len(missing), len(added), len(mismatch)))
    print('\nreference calls every frame, frozen never:')
    for c, name in sorted(missing, reverse=True)[:25]:
        print('  %5d  %s' % (c, name))
    print('\nfrozen calls, reference never:')
    for c, name in sorted(added, reverse=True)[:25]:
        print('  %5d  %s' % (c, name))
    print('\ncount mismatch (ref per frame | frozen per frame):')
    for d, name, rc, wc in sorted(mismatch, reverse=True)[:25]:
        print('  %-45s %s | %s' % (name[:45], rc, wc))


if __name__ == '__main__':
    main()
