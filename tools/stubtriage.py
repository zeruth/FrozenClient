#!/usr/bin/env python3
"""Classify every stubbed Lua binding by what FrameXML actually does with its return value.

Picking stubs off one at a time does not scale against ~2000 of them. What scales is deciding by
FAMILY: a whole subsystem the client does not implement answers the same way everywhere, and the
only real question per binding is what SHAPE the caller expects back -- a number it will do
arithmetic on, a count it will loop to, a boolean it will branch on, or a string it will print.

Getting the shape wrong is what produced every "attempt to perform arithmetic on a nil value" in the
run logs, so the shape is read from the call site rather than guessed.

    python tools/stubtriage.py             # summary by family
    python tools/stubtriage.py --shapes    # summary by inferred return shape
    python tools/stubtriage.py --list FAM  # every stub in one family, with call sites
"""

import collections
import glob
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAMEXML = os.path.join(
    os.environ.get('WHOA_FX_DIR', ''), '') or None

# Families: a prefix or keyword that identifies a whole subsystem. Order matters -- the first match
# wins, so put the specific ones first.
FAMILIES = [
    ('battlenet',  ('BN', 'Battlenet')),
    ('voice',      ('Voice', 'Microphone', 'PushToTalk', 'SelfMute', 'Reverb', 'OutputDriver',
                    'ChatVolume', 'Ambience', 'IsTalking', 'MuteSound')),
    ('calendar',   ('Calendar',)),
    ('guild',      ('Guild', 'Petition', 'Tabard')),
    ('auction',    ('Auction',)),
    ('arena',      ('Arena',)),
    ('pvp',        ('PVP', 'Honor', 'Battlefield', 'WorldPVP')),
    ('lfg',        ('LFG', 'LFD', 'Raid Browser', 'PartyLFG')),
    ('vehicle',    ('Vehicle', 'Possess')),
    ('glyph',      ('Glyph', 'Talent', 'Inspect')),
    ('achievement',('Achievement', 'Statistic', 'Criteria')),
    ('trade',      ('Trade', 'Merchant', 'Buyback', 'Barber')),
    ('mail',       ('Mail', 'Inbox', 'SendMail')),
    ('quest',      ('Quest', 'GossipTitle', 'Gossip')),
    ('item',       ('Item', 'Bag', 'Container', 'Loot', 'Equipment', 'Socket', 'Gem')),
    ('spell',      ('Spell', 'Aura', 'Cooldown', 'Totem', 'PetAction', 'Shapeshift')),
    ('social',     ('Friend', 'Ignore', 'Who', 'Chat', 'Channel', 'Emote', 'Language')),
    ('map',        ('Map', 'Minimap', 'Zone', 'Area', 'POI', 'Corpse')),
    ('unit',       ('Unit', 'Player', 'Pet', 'Target', 'Focus', 'Party', 'Group')),
    ('video',      ('Video', 'Resolution', 'Gamma', 'Monitor', 'Gx', 'Render', 'Screenshot')),
    ('sound',      ('Sound', 'Music', 'SFX', 'Cinematic', 'Movie')),
    ('widget',     ('CSimple', 'CScript', 'CGTooltip', 'CGMinimap', 'CGQuest', 'Frame', 'Texture',
                    'FontString', 'EditBox', 'Slider', 'ScrollFrame', 'MessageFrame', 'Model')),
]


def family_of(name):
    for fam, keys in FAMILIES:
        for k in keys:
            if k.lower() in name.lower():
                return fam

    return 'misc'


def collect_stubs():
    """Every stubbed binding: name -> (file, kind)."""
    stubs = {}

    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        src = io.open(path, encoding='utf-8', errors='replace').read()
        rel = os.path.relpath(path, ROOT)

        bodies = dict(re.findall(r'int32_t (\w+)\(lua_State\* \w+\)\s*\{(.*?)\n\}', src, re.S))
        unimpl = {n for n, b in bodies.items() if 'WHOA_UNIMPLEMENTED' in b}

        for name, func in re.findall(r'\{\s*"([^"]+)"\s*,\s*&(\w+)\s*\}', src):
            if func in unimpl:
                stubs[name] = (rel, 'unimplemented')

        for name in re.findall(r'WHOA_LUA_STUB\((\w+)\)', src):
            stubs.setdefault(name, (rel, 'action'))

    return stubs


def scan_framexml(fxdir):
    """For each global called in FrameXML, record how its result is used."""
    pattern = re.compile(r'(?<![:.\w])([A-Za-z_]\w*)\s*\(')
    arith = re.compile(r'\b(max|min|ceil|floor|abs|format)\s*\(')
    usage = collections.defaultdict(lambda: {'calls': 0, 'arith': 0, 'assigned': 0, 'cond': 0,
                                             'loop': 0, 'sites': []})

    for path in glob.glob(os.path.join(fxdir, '*')):
        try:
            text = io.open(path, encoding='latin-1').read()
        except OSError:
            continue

        base = os.path.basename(path)

        for lineno, line in enumerate(text.splitlines(), 1):
            for m in pattern.finditer(line):
                name = m.group(1)
                u = usage[name]
                u['calls'] += 1

                if len(u['sites']) < 4:
                    u['sites'].append('%s:%d' % (base, lineno))

                before = line[:m.start()]

                if arith.search(before) or re.search(r'[-+*/]\s*$', before.strip()):
                    u['arith'] += 1

                if re.search(r'\blocal\b.*=\s*$', before) or re.search(r'=\s*$', before):
                    u['assigned'] += 1

                if re.search(r'\b(if|elseif|while|and|or|not)\b[^=]*$', before):
                    u['cond'] += 1

                if re.search(r'\bfor\b.*=\s*1\s*,\s*$', before):
                    u['loop'] += 1

    return usage


def shape_of(u):
    """What the caller needs back."""
    if not u or not u['calls']:
        return 'uncalled'

    if u['loop'] or u['arith']:
        return 'number'      # nil here is a hard error

    if u['cond'] and not u['assigned']:
        return 'boolean'     # nil reads as false, silently

    if u['assigned']:
        return 'value'       # destructured; arity matters

    return 'ignored'


def main():
    fxdir = None

    for candidate in (os.environ.get('WHOA_FX_DIR'),
                      os.path.join(ROOT, 'build', 'framexml')):
        if candidate and os.path.isdir(candidate):
            fxdir = candidate
            break

    if not fxdir:
        sys.exit('set WHOA_FX_DIR to the extracted FrameXML directory')

    stubs = collect_stubs()
    usage = scan_framexml(fxdir)

    rows = []

    for name, (path, kind) in stubs.items():
        u = usage.get(name)
        rows.append((name, path, kind, family_of(name), shape_of(u), u['calls'] if u else 0))

    if '--list' in sys.argv:
        want = sys.argv[sys.argv.index('--list') + 1]

        for name, path, kind, fam, shape, calls in sorted(rows, key=lambda r: -r[5]):
            if fam != want:
                continue

            sites = ', '.join(usage[name]['sites']) if name in usage else ''
            print('%-40s %-9s %-8s %4d  %s' % (name, shape, kind, calls, sites))

        return

    if '--shapes' in sys.argv:
        counts = collections.Counter(r[4] for r in rows)
        print('%d stubbed bindings by what the caller needs back:' % len(rows))

        for shape, n in counts.most_common():
            print('  %-10s %5d' % (shape, n))

        print()
        print('the ones that BREAK things (nil where a number is required):')

        for name, path, kind, fam, shape, calls in sorted(rows, key=lambda r: -r[5]):
            if shape == 'number':
                print('  %-40s %-12s %4d calls' % (name, fam, calls))

        return

    byfam = collections.Counter(r[3] for r in rows)
    called = collections.Counter(r[3] for r in rows if r[5])

    print('%d stubbed bindings across %d families' % (len(rows), len(byfam)))
    print()
    print('%-14s %7s %8s' % ('family', 'stubs', 'called'))

    for fam, n in byfam.most_common():
        print('%-14s %7d %8d' % (fam, n, called.get(fam, 0)))


if __name__ == '__main__':
    main()
