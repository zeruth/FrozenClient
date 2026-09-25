"""Read initialised data out of the reference executable: the constants, strings and tables a
decompilation names by address but cannot show (`DAT_00a3fd70`, `PTR_DAT_00ad3028`, ...).

    python tools/recomp/dumpdata.py 00a3fd70 00ad3028:12 ...     # addr[:count of dwords]
    python tools/recomp/dumpdata.py --file docs/ref/data-wanted.txt > docs/ref/data-values.txt

For every address it prints the raw dwords with their readings as int, float and double, and --
when a dword is itself an address inside the image -- the C string it points at (pointer tables of
strings are the common case). Addresses in the zero-filled tail of a section (.bss) report that
they have no initial value, which is itself the answer: the reference starts them at zero.

The executable defaults to the reference install named in CLAUDE.md; --exe overrides it. No
dependencies beyond the standard library: the PE headers are parsed directly."""
import argparse
import os
import struct
import sys

DEFAULT_EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".reference",
                           "WOTLK 3.3.5a - Windows", "WoW_WOTLK_3.3.5a", "WoW.exe")


class Image:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        if d[:2] != b"MZ":
            sys.exit("%s: not a PE file" % path)
        pe = struct.unpack_from("<I", d, 0x3c)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            sys.exit("%s: no PE signature" % path)
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsize = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", d, opt + 28)[0]
        self.sections = []
        for i in range(nsec):
            s = opt + optsize + i * 40
            name = d[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", d, s + 8)
            self.sections.append((name, self.base + vaddr, vsize, rawptr, rawsize))

    def section(self, va):
        for s in self.sections:
            if s[1] <= va < s[1] + max(s[2], s[4]):
                return s
        return None

    def read(self, va, n):
        """Bytes at va, or None where the section has no file data (zero-initialised)."""
        s = self.section(va)
        if not s:
            return None
        off = va - s[1]
        if off + n > s[4]:
            return None
        return self.data[s[3] + off:s[3] + off + n]

    def cstring(self, va, limit=200):
        s = self.section(va)
        if not s:
            return None
        off = va - s[1]
        if off >= s[4]:
            return None
        raw = self.data[s[3] + off:s[3] + min(s[4], off + limit)]
        end = raw.find(b"\0")
        if end < 0:
            return None
        text = raw[:end]
        if not text or any(b < 0x20 and b not in (9, 10, 13) for b in text) or any(b >= 0x7f for b in text):
            return None
        return text.decode("latin-1")


def dump(img, va, count):
    s = img.section(va)
    if not s:
        print("%08x  not inside the image" % va)
        return
    head = "%08x  [%s]" % (va, s[0])
    own = img.cstring(va)
    if own is not None and len(own) >= 2:
        print(head + "  string %r" % own)
    else:
        print(head)
    for i in range(count):
        a = va + 4 * i
        b = img.read(a, 4)
        if b is None:
            print("  %08x  (no initial value: zero-filled)" % a)
            continue
        u = struct.unpack("<I", b)[0]
        f = struct.unpack("<f", b)[0]
        line = "  %08x  %08x  int %-11d float %-14.8g" % (a, u, struct.unpack("<i", b)[0], f)
        b8 = img.read(a, 8)
        if b8 is not None and i % 2 == 0:
            line += " double %.17g" % struct.unpack("<d", b8)[0]
        target = img.cstring(u) if img.section(u) else None
        if target is not None:
            line += "  -> %r" % target
        print(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addrs", nargs="*", help="hex address, optionally :count of dwords (default 4)")
    ap.add_argument("--file", help="read addresses from a file, one per line (# comments allowed)")
    ap.add_argument("--exe", default=DEFAULT_EXE)
    args = ap.parse_args()
    items = list(args.addrs)
    if args.file:
        for line in open(args.file):
            line = line.split("#", 1)[0].strip()
            if line:
                items.append(line.split()[0])
    img = Image(args.exe)
    for it in items:
        addr, _, n = it.partition(":")
        dump(img, int(addr.replace("DAT_", "").replace("PTR_", ""), 16), int(n) if n else 4)


if __name__ == "__main__":
    main()
