#!/usr/bin/env python3
r"""Ask the reference MPQ archives whether a given file path exists, and pull files back out.

Whoa loads shaders by name out of the reference install's archives (Shaders\Vertex\vs_2_0\*.bls),
so before authoring a replacement it is worth asking whether the original is already sitting there.
A plain existence probe only walks the encrypted hash table; --extract and --list go the rest of
the way and decompress sectors, which is what makes each archive's own (listfile) readable.
"""

import bz2
import os
import struct
import sys
import zlib

# The MPQ crypt table, generated from the documented LCG seed.
_CRYPT = [0] * 0x500
_seed = 0x00100001

for _i in range(0x100):
    _idx = _i

    for _ in range(5):
        _seed = (_seed * 125 + 3) % 0x2AAAAB
        _a = (_seed & 0xFFFF) << 16
        _seed = (_seed * 125 + 3) % 0x2AAAAB
        _b = _seed & 0xFFFF
        _CRYPT[_idx] = _a | _b
        _idx += 0x100

# Block flags.
_FLAG_IMPLODE = 0x00000100
_FLAG_COMPRESS = 0x00000200
_FLAG_ENCRYPTED = 0x00010000
_FLAG_FIX_KEY = 0x00020000
_FLAG_SINGLE_UNIT = 0x01000000
_FLAG_EXISTS = 0x80000000

# Later archives patch earlier ones, which is the order the client mounts them in. Plain
# alphabetical order would put patch.MPQ after patch-3.MPQ and hand back stale data.
_MOUNT_ORDER = [
    'common.mpq',
    'common-2.mpq',
    'expansion.mpq',
    'lichking.mpq',
    'patch.mpq',
    'patch-2.mpq',
    'patch-3.mpq',
    # Locale archives mount after the base ones, and the numbered patches mount after the unnumbered
    # one. Getting this backwards is not cosmetic: the later archive wins, so an extract without
    # these entries returned patch-enUS.MPQ where the client actually loads patch-enUS-3.MPQ, and a
    # line number read out of the result pointed at code the client never ran.
    'locale-enus.mpq',
    'expansion-locale-enus.mpq',
    'lichking-locale-enus.mpq',
    'patch-enus.mpq',
    'patch-enus-2.mpq',
    'patch-enus-3.mpq',
]

# Resolved from the script's own location so the tool works from any working directory.
DATA = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    '.reference', 'WOTLK 3.3.5a - Windows', 'WoW_WOTLK_3.3.5a', 'Data')


def hash_string(text, kind):
    seed1 = 0x7FED7FED
    seed2 = 0xEEEEEEEE

    for ch in text.upper():
        value = ord(ch)
        seed1 = (_CRYPT[(kind << 8) + value] ^ (seed1 + seed2)) & 0xFFFFFFFF
        seed2 = (value + seed1 + seed2 + (seed2 << 5) + 3) & 0xFFFFFFFF

    return seed1


def decrypt(data, key):
    out = bytearray()
    seed2 = 0xEEEEEEEE

    for i in range(0, len(data) - 3, 4):
        seed2 = (seed2 + _CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        value = struct.unpack_from('<I', data, i)[0]
        value = (value ^ (key + seed2)) & 0xFFFFFFFF
        out += struct.pack('<I', value)
        key = (((~key << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed2 = (value + seed2 + (seed2 << 5) + 3) & 0xFFFFFFFF

    # Encryption runs on whole dwords, so any trailing bytes are stored verbatim.
    out += data[len(out):]

    return bytes(out)


def decompress_sector(raw, imploded):
    """Expand one sector. Every compressed sector but a PKWARE one leads with a mask byte."""
    if imploded:
        raise NotImplementedError("PKWARE implode is not supported")

    mask = raw[0]
    body = raw[1:]

    if mask == 0x02:
        return zlib.decompress(body)

    if mask == 0x10:
        return bz2.decompress(body)

    raise NotImplementedError("compression mask 0x%02X is not supported" % mask)


class Archive:
    """One opened MPQ v1 archive: header position, sector size, and both decrypted tables."""

    def __init__(self, path):
        self.path = path
        self.fh = open(path, 'rb')
        blob = self.fh.read(0x8000)
        self.base = None

        # The header is not always at byte 0 (some archives carry a stub).
        for offset in range(0, len(blob) - 4, 0x200):
            if blob[offset:offset + 4] == b'MPQ\x1a':
                self.base = offset
                break

        if self.base is None:
            raise ValueError("no MPQ header in %s" % path)

        self.fh.seek(self.base)
        header = self.fh.read(32)
        self.sector_size = 512 << struct.unpack_from('<H', header, 0x0E)[0]
        hash_pos, block_pos = struct.unpack_from('<II', header, 0x10)
        self.hash_count, self.block_count = struct.unpack_from('<II', header, 0x18)

        self.fh.seek(self.base + hash_pos)
        self.hash_table = decrypt(self.fh.read(self.hash_count * 16),
                                  hash_string("(hash table)", 3))

        self.fh.seek(self.base + block_pos)
        self.block_table = decrypt(self.fh.read(self.block_count * 16),
                                   hash_string("(block table)", 3))

    def close(self):
        self.fh.close()

    def find(self, name):
        """Return the block index for an internal path, or None when the archive lacks it."""
        index = hash_string(name, 0) & (self.hash_count - 1)
        want_a = hash_string(name, 1)
        want_b = hash_string(name, 2)

        for probe in range(self.hash_count):
            slot = (index + probe) & (self.hash_count - 1)
            a, b, locale, platform, block = struct.unpack_from('<IIHHI', self.hash_table, slot * 16)

            # An empty-and-never-used slot ends the probe; a deleted one does not.
            if block == 0xFFFFFFFF:
                return None

            if a == want_a and b == want_b and block != 0xFFFFFFFE:
                return block

        return None

    def read(self, name):
        """Decompress one file out of the archive and return its bytes, or None if absent."""
        block = self.find(name)

        if block is None:
            return None

        pos, packed, size, flags = struct.unpack_from('<IIII', self.block_table, block * 16)

        if not flags & _FLAG_EXISTS:
            return None

        compressed = bool(flags & _FLAG_COMPRESS)
        imploded = bool(flags & _FLAG_IMPLODE)
        key = None

        if flags & _FLAG_ENCRYPTED:
            key = hash_string(name.replace('/', '\\').rsplit('\\', 1)[-1], 3)

            # FILE_FIX_KEY folds the block's own position into the key.
            if flags & _FLAG_FIX_KEY:
                key = ((key + pos) ^ size) & 0xFFFFFFFF

        start = self.base + pos

        # A single-unit file is one chunk with no sector-offset table in front of it.
        if flags & _FLAG_SINGLE_UNIT:
            self.fh.seek(start)
            raw = self.fh.read(packed)

            if key is not None:
                raw = decrypt(raw, key)

            if packed >= size or not (compressed or imploded):
                return raw[:size]

            return decompress_sector(raw, imploded)[:size]

        count = (size + self.sector_size - 1) // self.sector_size
        self.fh.seek(start)
        table = self.fh.read((count + 1) * 4)

        if key is not None:
            table = decrypt(table, (key - 1) & 0xFFFFFFFF)

        offsets = struct.unpack('<%dI' % (count + 1), table)
        out = bytearray()

        for i in range(count):
            want = min(self.sector_size, size - len(out))
            self.fh.seek(start + offsets[i])
            raw = self.fh.read(offsets[i + 1] - offsets[i])

            if key is not None:
                raw = decrypt(raw, (key + i) & 0xFFFFFFFF)

            # A sector that did not shrink is stored verbatim and carries no mask byte.
            if len(raw) >= want or not (compressed or imploded):
                out += raw[:want]
            else:
                out += decompress_sector(raw, imploded)[:want]

        return bytes(out)


# The install's locale. Files exist in several and only this one matches the client's line numbers.
LOCALE = 'enUS'


def archive_paths(data):
    """Archive file paths in mount order, unknown archives last.

    Includes the LOCALE subdirectories (Data/enUS and friends). Interface\FrameXML lives there, not
    in the top-level archives, and scanning only the top level made every FrameXML path report NOT
    FOUND -- including UIParent.lua, which the client demonstrably loads.
    """
    names = [n for n in os.listdir(data) if n.lower().endswith('.mpq')]
    extra = []

    for entry in sorted(os.listdir(data)):
        sub = os.path.join(data, entry)

        # Only the configured locale. The client mounts one; mounting them all here meant an
        # extract could return a ruRU copy, and since later patch archives win, the foreign one won
        # even when ordered last. Line numbers do not match between locales, so a .lua read this way
        # points at unrelated code.
        if os.path.isdir(sub) and entry.lower() != LOCALE.lower():
            continue

        if os.path.isdir(sub):
            extra += [os.path.join(entry, n) for n in sorted(os.listdir(sub))
                      if n.lower().endswith('.mpq')]

    names += extra

    def rank(name):
        lowered = os.path.basename(name).lower()
        known = _MOUNT_ORDER.index(lowered) if lowered in _MOUNT_ORDER else len(_MOUNT_ORDER)

        # Prefer the configured locale ahead of everything else. Without this an enUS install can
        # hand back a ruRU copy of a file, which matters the moment you read a line number out of a
        # .lua: the locales do not line up, and a line number from the wrong file points at
        # unrelated code. Ranking on the basename also matters -- a locale file arrives here as
        # "ruRU/patch-ruRU.MPQ", which never matched the mount-order table at all.
        foreign = 1 if (os.path.dirname(name) and LOCALE not in name) else 0

        return (foreign, known, lowered)

    return [os.path.join(data, name) for name in sorted(names, key=rank)]


def archive_has(path, name):
    archive = Archive(path)

    try:
        return archive.find(name) is not None
    finally:
        archive.close()


def cmd_probe(names):
    for name in names:
        found = []

        for path in archive_paths(DATA):
            try:
                if archive_has(path, name):
                    found.append(os.path.basename(path))
            except Exception as err:
                found.append("%s(error: %s)" % (os.path.basename(path), err))

        print("%-52s %s" % (name, ", ".join(found) if found else "NOT FOUND"))


def cmd_extract(name, out_path):
    # Walk in reverse mount order so the newest patch's copy of the file wins.
    for path in reversed(archive_paths(DATA)):
        archive = Archive(path)

        try:
            blob = archive.read(name)
        finally:
            archive.close()

        if blob is None:
            continue

        with open(out_path, 'wb') as fh:
            fh.write(blob)

        print("%s: %d bytes from %s -> %s" % (name, len(blob), os.path.basename(path), out_path))

        return

    sys.exit("%s: NOT FOUND" % name)


def cmd_list(out_path):
    names = set()

    for path in archive_paths(DATA):
        archive = Archive(path)

        try:
            blob = archive.read("(listfile)")
        except Exception as err:
            print("%s: %s" % (os.path.basename(path), err), file=sys.stderr)
            continue
        finally:
            archive.close()

        if blob is None:
            print("%s: no (listfile)" % os.path.basename(path), file=sys.stderr)
            continue

        entries = [line.strip() for line in blob.decode('latin-1').splitlines()]
        entries = [line for line in entries if line]
        names.update(entries)
        print("%s: %d entries" % (os.path.basename(path), len(entries)), file=sys.stderr)

    merged = sorted(names, key=str.lower)

    if out_path:
        with open(out_path, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write('\n'.join(merged) + '\n')

        print("%d unique paths -> %s" % (len(merged), out_path), file=sys.stderr)
    else:
        print('\n'.join(merged))


def main():
    args = sys.argv[1:]
    usage = ("usage: mpq-probe.py <archive-internal path> [...]\n"
             "       mpq-probe.py --extract <archive-internal path> <output file>\n"
             "       mpq-probe.py --list [output file]")

    if not args:
        sys.exit(usage)

    if args[0] == '--list':
        cmd_list(args[1] if len(args) > 1 else None)

        return

    if args[0] == '--extract':
        if len(args) != 3:
            sys.exit(usage)

        cmd_extract(args[1], args[2])

        return

    cmd_probe(args)


if __name__ == "__main__":
    main()
