"""Find ports that ROUND a float to an integer where the reference TRUNCATES it.

Ghidra prints `ROUND(x)` for every x87 `fistp`, but MSVC emits two kinds: a plain fistp, which
rounds to nearest in the default control word, and its inline truncation -- `fnstcw`, `or $0xc00`
(rounding control = chop), `fldcw`, `fistp`, `fldcw` -- which is C's (int)x. Porting the second kind
as lrintf/llrint/nearbyint/roundf rounds where the reference truncates. CameraSplitFloor
(FUN_005fe800) did, and every polynomial sine and cosine built on it came out wrong; 2026-10-04
found 68 more.

For each frozen function carrying a `ref: FUN_xxxxxxxx` tag, this counts the reference's chopped
and nearest fistps and frozen's round-to-nearest calls, and reports:

  WRONG   every reference fistp truncates, yet frozen rounds somewhere
  MIXED   the reference does both; check the calls against the fistps in source order
          (`--map ADDR` prints them)

Usage:
  llvm-objdump -d --no-show-raw-insn WoW.exe > wow_text.asm      (see CLAUDE.md)
  python tools/recomp/roundcheck.py wow_text.asm
  python tools/recomp/roundcheck.py wow_text.asm --map 0070d1e0
"""
import argparse
import bisect
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
ROUNDERS = re.compile(r"\b(lrintf?|llrintf?|lroundf?|llroundf?|roundf?|nearbyintf?|rintf?)\s*\(")
TAG = re.compile(r"ref:\s*FUN_([0-9a-fA-F]{8})")


def load_sizes():
    sizes = {}
    with open(os.path.join(ROOT, "tools", "recomp", "data", "ref-functions.jsonl"), encoding="utf-8") as f:
        for line in f:
            d = json.loads(line)
            sizes[int(d["addr"], 16)] = d.get("size", 0)
    return sizes


def load_asm(path):
    addrs = []
    texts = []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = re.match(r"\s*([0-9a-f]+):\s+(.*)", line)
            if m:
                addrs.append(int(m.group(1), 16))
                texts.append(m.group(2))
    return addrs, texts


def fistps(addrs, texts, start, size):
    """(address, chopped) for each fistp in the function, in order."""
    out = []
    pending = False
    i = bisect.bisect_left(addrs, start)
    while i < len(addrs) and addrs[i] < start + size:
        text = texts[i]
        if re.search(r"or[lw]\s+\$0xc00", text):
            pending = True
        if "fistp" in text:
            out.append((addrs[i], pending))
            pending = False
        i += 1
    return out


def frozen_sites():
    """(ref address, file, line, [rounding calls]) for every tagged definition in src/."""
    for d, _, files in os.walk(os.path.join(ROOT, "src")):
        for name in files:
            if not name.endswith(".cpp"):
                continue
            path = os.path.join(d, name)
            with open(path, encoding="utf-8", errors="ignore") as f:
                text = f.read().split("\n")
            tags = [(i, TAG.search(l).group(1)) for i, l in enumerate(text) if TAG.search(l)]
            for k, (i, addr) in enumerate(tags):
                end = tags[k + 1][0] if k + 1 < len(tags) else len(text)
                calls = []
                for j in range(i + 1, end):
                    # A macro after the tagged function defines OTHER functions (their tags
                    # follow the #define), so the range stops there.
                    if text[j].lstrip().startswith("#define"):
                        break
                    code = text[j].split("//", 1)[0]
                    for m in ROUNDERS.finditer(code):
                        calls.append((j + 1, m.group(1)))
                if calls:
                    yield int(addr, 16), os.path.relpath(path, ROOT), i + 1, calls


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("asm", help="llvm-objdump disassembly of WoW.exe")
    ap.add_argument("--map", metavar="ADDR", help="print one reference function's fistps, chopped or not")
    opts = ap.parse_args()

    sizes = load_sizes()
    addrs, texts = load_asm(opts.asm)

    if opts.map:
        start = int(opts.map, 16)
        for a, chopped in fistps(addrs, texts, start, sizes.get(start, 0)):
            print("%08x %s" % (a, "truncates" if chopped else "rounds"))
        return 0

    wrong = 0
    for addr, path, line, calls in frozen_sites():
        sites = fistps(addrs, texts, addr, sizes.get(addr, 0))
        chopped = sum(1 for _, c in sites if c)
        if not chopped:
            continue
        kind = "WRONG" if chopped == len(sites) else "MIXED"
        if kind == "WRONG":
            wrong += 1
        where = ", ".join("%d %s" % (l, c) for l, c in calls)
        print("%-5s FUN_%08x %s:%d  reference %d/%d truncating; frozen rounds at %s" % (
            kind, addr, path, line, chopped, len(sites), where))

    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
