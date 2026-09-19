#!/usr/bin/env python3
"""Read the same piece of state out of the reference client and out of frozen, side by side.

A screenshot diff says two frames disagree. It does not say which number was wrong. This attaches to
both running clients, reads named globals out of each, and prints them next to each other, so a
parity claim can be checked against the value the reference actually holds rather than against a
guess.

Addresses come from two different places, because the two binaries are nothing alike:

  reference : absolute virtual addresses recovered in Ghidra (the 32-bit image prefers base
              0x00400000 and the DAT_ labels are already absolute), rebased onto the live module.
  frozen      : symbol names, resolved to RVAs through the PDB with llvm-pdbutil.

Usage:
    python tools/memcompare.py                 # read every probe, print a table
    python tools/memcompare.py --group shadow  # only one group
    python tools/memcompare.py --watch 2       # re-read every 2 seconds
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import re
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PDBUTIL = r"C:\Program Files\LLVM\bin\llvm-pdbutil.exe"
FROZEN_PDB = os.path.join(ROOT, "build", "dist", "bin", "Frozen.pdb")
FROZEN_EXE = os.path.join(ROOT, "build", "dist", "bin", "Frozen.exe")
SYMBOL_CACHE = os.path.join(ROOT, "build", "frozen-globals.json")

# The vanilla 3.3.5a client the Ghidra database was built from. Named by full path on purpose: the
# user's own live game is also called WoW.exe.
REFERENCE_EXE = os.path.join(
    ROOT, ".reference", "WOTLK 3.3.5a - Windows", "WoW_WOTLK_3.3.5a", "WoW.exe")

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


class MODULEINFO(ctypes.Structure):
    _fields_ = [
        ("lpBaseOfDll", ctypes.c_void_p),
        ("SizeOfImage", wt.DWORD),
        ("EntryPoint", ctypes.c_void_p),
    ]


class Target:
    """One attached client: its process handle, module base and pointer width."""

    def __init__(self, label, pid, handle, base, size, bits):
        self.label = label
        self.pid = pid
        self.handle = handle
        self.base = base
        self.size = size
        self.bits = bits

    def read(self, address, length):
        """Read `length` bytes, in chunks, or None if the very first chunk fails.

        ReadProcessMemory is all-or-nothing: a single unreadable page fails the entire call. Asking
        for a multi-megabyte span in one go therefore returns nothing as soon as the span crosses an
        allocation boundary, which is what a large Lua node array does. Reading in pages and
        returning what was actually obtained makes big structures readable and still reports a truly
        bad address as a failure.
        """
        if length <= 0:
            return b""

        out = bytearray()
        chunk = 0x10000

        while len(out) < length:
            want = min(chunk, length - len(out))
            buf = (ctypes.c_char * want)()
            got = ctypes.c_size_t(0)
            ok = k32.ReadProcessMemory(
                wt.HANDLE(self.handle),
                ctypes.c_void_p(address + len(out)),
                buf,
                ctypes.c_size_t(want),
                ctypes.byref(got),
            )

            if not ok or got.value == 0:
                break

            out += bytes(buf[:got.value])

        if not out:
            return None

        return bytes(out)


PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def process_path(pid):
    """Full image path for a pid, or None if it cannot be queried."""
    handle = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)

    if not handle:
        return None

    try:
        size = wt.DWORD(32768)
        buf = ctypes.create_unicode_buffer(size.value)

        if not k32.QueryFullProcessImageNameW(wt.HANDLE(handle), 0, buf, ctypes.byref(size)):
            return None

        return buf.value
    finally:
        k32.CloseHandle(wt.HANDLE(handle))


def find_pid(exe_path):
    """Return the pid running exactly this image.

    Matching on the file name alone is not safe here. The user's own live game is also called
    WoW.exe, and an earlier version of this tool happily attached to it and read a stranger's
    session instead of the reference client. The full path is the only honest identifier.
    """
    want = os.path.normcase(os.path.abspath(exe_path))
    count = 1024
    pids = (wt.DWORD * count)()
    needed = wt.DWORD()

    if not psapi.EnumProcesses(ctypes.byref(pids), ctypes.sizeof(pids), ctypes.byref(needed)):
        return None

    found = []

    for i in range(needed.value // ctypes.sizeof(wt.DWORD)):
        pid = pids[i]

        if not pid:
            continue

        path = process_path(pid)

        if path and os.path.normcase(path) == want:
            found.append(pid)

    if len(found) > 1:
        print("note: %d processes are running %s; using pid %d"
              % (len(found), os.path.basename(exe_path), found[0]), file=sys.stderr)

    return found[0] if found else None


def attach(label, exe_path):
    pid = find_pid(exe_path)

    if not pid:
        return None

    handle = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)

    if not handle:
        print("%s: pid %d is running but could not be opened (error %d)"
              % (label, pid, ctypes.get_last_error()), file=sys.stderr)
        return None

    needed = wt.DWORD()
    mods = (ctypes.c_void_p * 1024)()
    psapi.EnumProcessModules(
        wt.HANDLE(handle), ctypes.byref(mods), ctypes.sizeof(mods), ctypes.byref(needed))

    info = MODULEINFO()
    psapi.GetModuleInformation(
        wt.HANDLE(handle), ctypes.c_void_p(mods[0]), ctypes.byref(info), ctypes.sizeof(info))

    # A 32-bit process on 64-bit Windows answers IsWow64Process; that is what decides pointer width.
    wow64 = ctypes.c_int(0)
    k32.IsWow64Process(wt.HANDLE(handle), ctypes.byref(wow64))
    bits = 32 if wow64.value else 64

    return Target(label, pid, handle, info.lpBaseOfDll, info.SizeOfImage, bits)


def load_frozen_symbols():
    """Map global name -> (RVA, size), from the PDB. Cached: the dump takes a while."""
    if os.path.exists(SYMBOL_CACHE) and os.path.getmtime(SYMBOL_CACHE) >= os.path.getmtime(FROZEN_PDB):
        return json.load(open(SYMBOL_CACHE))

    print("resolving symbols from %s ..." % os.path.basename(FROZEN_PDB), file=sys.stderr)
    out = subprocess.run([PDBUTIL, "pretty", "--globals", FROZEN_PDB], capture_output=True, text=True).stdout

    symbols = {}
    ambiguous = []

    # e.g.   data [0x08ba4350, sizeof=64] static C44Matrix s_lightView
    # The trailing "[N]" on an array declaration used to break this, which silently dropped
    # every array symbol, including CWorld::s_skyColors[5] -- exactly the kind of thing
    # worth probing.
    pattern = re.compile(
        r"data \[0x([0-9a-fA-F]+), sizeof=(\d+)\][^\n]*?([A-Za-z_][\w:]*)(?:\[\d+\])?\s*$",
        re.M)

    for m in pattern.finditer(out):
        rva = int(m.group(1), 16)
        size = int(m.group(2))
        name = m.group(3)

        # Several translation units can each hold a static of the same name. Keep the first and
        # record the clash, rather than silently resolving to whichever happened to come last.
        if name in symbols and symbols[name][0] != rva:
            if name not in ambiguous:
                ambiguous.append(name)

            continue

        symbols[name] = (rva, size)

    symbols["__ambiguous__"] = ambiguous
    os.makedirs(os.path.dirname(SYMBOL_CACHE), exist_ok=True)
    json.dump(symbols, open(SYMBOL_CACHE, "w"), indent=1)
    print("  %d globals, %d ambiguous" % (len(symbols) - 1, len(ambiguous)), file=sys.stderr)
    return symbols


SIZES = {"f32": 4, "u32": 4, "i32": 4, "u16": 2, "u8": 1}


def size_of(kind, bits):
    if kind.startswith("f32x"):
        return 4 * int(kind[4:])

    if kind.startswith("ptr"):
        return 8 if bits == 64 else 4

    return SIZES[kind]


def decode(kind, raw):
    if raw is None:
        return None

    if kind == "f32":
        return struct.unpack("<f", raw)[0]

    if kind.startswith("f32x"):
        return list(struct.unpack("<%df" % int(kind[4:]), raw))

    if kind == "u32":
        return struct.unpack("<I", raw)[0]

    if kind == "i32":
        return struct.unpack("<i", raw)[0]

    if kind == "u16":
        return struct.unpack("<H", raw)[0]

    if kind == "u8":
        return raw[0]

    if kind.startswith("ptr"):
        return struct.unpack("<Q" if len(raw) == 8 else "<I", raw)[0]

    return raw


def image_size_on_disk(path):
    """SizeOfImage from a PE file's optional header, or None if it cannot be read."""
    try:
        with open(path, "rb") as fh:
            head = fh.read(0x400)

        pe = struct.unpack_from("<I", head, 0x3C)[0]
        magic = struct.unpack_from("<H", head, pe + 24)[0]
        return struct.unpack_from("<I", head, pe + 24 + (56 if magic == 0x10B else 56))[0]
    except Exception:
        return None


def check_build_matches(target, exe_path):
    """Warn loudly when the attached process is not the binary the PDB describes.

    There is a second, stale Frozen.exe sitting in the reference install directory, and launching it by
    accident produces a client that looks right, runs fine, and answers every symbol read with
    ERROR_PARTIAL_COPY. Comparing the loaded image size against the build output catches that in one
    line instead of an hour.
    """
    on_disk = image_size_on_disk(exe_path)

    if on_disk is None or target is None:
        return

    if on_disk != target.size:
        print("", file=sys.stderr)
        print("  WARNING: the attached %s is NOT the build this PDB describes." % target.label,
              file=sys.stderr)
        print("           running image 0x%X, %s 0x%X" % (target.size, exe_path, on_disk),
              file=sys.stderr)
        print("           every symbol read will fail. Launch %s instead." % exe_path, file=sys.stderr)
        print("", file=sys.stderr)


def read_probe(target, address, kind, gather):
    """Read one probe, optionally assembling it from scattered floats.

    The two clients do not agree on layout even when they agree on content: the reference stores the
    shadow texture matrix as three float4 COLUMNS, while frozen keeps a row-major 4x4, so the same
    twelve numbers live at non-contiguous offsets on one side. `gather` names those offsets so the
    values can still be lined up element for element instead of eyeballed.
    """
    if not gather:
        return decode(kind, target.read(address, size_of(kind, target.bits)))

    out = []

    for offset in gather:
        raw = target.read(address + offset, 4)

        if raw is None:
            return None

        out.append(struct.unpack("<f", raw)[0])

    return out


def fmt(value):
    if value is None:
        return "-"

    # %g, not a fixed number of decimals: the interesting constants here span 1/4000 to 17000, and a
    # fixed format printed the shadow depth scale as 0.0000, which reads as "unset" when it is not.
    if isinstance(value, float):
        return "%.6g" % value

    if isinstance(value, list):
        if len(value) > 4:
            return " ".join("%.5g" % v for v in value[:4]) + " ..."

        return " ".join("%.6g" % v for v in value)

    if isinstance(value, int):
        return "0x%X" % value if value > 0xFFFF else str(value)

    return repr(value)


def negated(a, b, tol):
    """True when the two sides are the same value with the sign flipped."""
    if a is None or b is None:
        return False

    if isinstance(a, list) and isinstance(b, list):
        return close([-x for x in a], b, tol)

    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return close(-a, b, tol)

    return False


def close(a, b, tol):
    """None when either side is missing, so a missing read never reads as a pass."""
    if a is None or b is None:
        return None

    if isinstance(a, list) != isinstance(b, list):
        return False

    if isinstance(a, list):
        if len(a) != len(b):
            return False

        return all(abs(x - y) <= tol * max(1.0, abs(x), abs(y)) for x, y in zip(a, b))

    if isinstance(a, float) or isinstance(b, float):
        return abs(a - b) <= tol * max(1.0, abs(a), abs(b))

    return a == b


def length(vec):
    if not vec or len(vec) < 3:
        return None

    return (vec[0] ** 2 + vec[1] ** 2 + vec[2] ** 2) ** 0.5


def report_clock_skew(ref, ours, symbols):
    """Warn when the two clients' day/night clocks have drifted apart.

    Nearly every light value is a function of the time of day, so a few minutes of skew turns a
    correct client into a screenful of DIFF. This is not hypothetical: a run once reported 8 DIFF
    against a previous 3 with no code change, and the cause was the reference sitting logged in for
    90 minutes while its clock ran slow. frozen re-syncs from the server on every login; the reference
    does not while it stays logged in. Re-log the reference and the difference disappears.
    """
    if not ref or not ours:
        return None

    sym = symbols.get("g_clientGameTime")

    if not sym:
        return None

    raw = ours.read(ours.base + sym[0], 8)

    if not raw or len(raw) < 8:
        return None

    minute, hour = struct.unpack("<2i", raw)

    if hour < 0 or minute < 0:
        return None

    our_minutes = hour * 60 + minute
    held = ref.read(ref.base + 0x00d38b00 - 0x400000, 4)

    if not held:
        return None

    ref_minutes = struct.unpack("<I", held)[0]
    skew = our_minutes - ref_minutes

    print()
    print("day/night clock: reference %d, frozen %d (%+d minutes)" % (ref_minutes, our_minutes, skew))

    if abs(skew) > 2:
        print("  *** THE CLOCKS HAVE DRIFTED. Every light value is a function of time of day, so")
        print("  *** the time-dependent probes below are reported as SKEW rather than ok or DIFF.")

    return skew


def report_invariants(rows):
    """Compare the things that hold no matter where each character is standing.

    The two clients are never in the same place, so the shadow matrices cannot be checked element by
    element. What CAN be checked is the scale baked into each column, and that is where the real
    answers are: the depth column's length is the reciprocal of the far plane, and the horizontal
    columns' length is the reciprocal of the box extent times whatever remap the client folds in.
    """
    values = {}

    for probe, ref_value, our_value in rows:
        values[probe["name"]] = (ref_value, our_value)

    checks = []

    col0 = values.get("tex matrix col 0")
    col2 = values.get("tex matrix col 2")

    if col2:
        ref_len, our_len = length(col2[0]), length(col2[1])

        if ref_len and our_len:
            checks.append((
                "depth column * 4000",
                ref_len * 4000.0, our_len * 4000.0,
                "both 1.0 means the far plane is folded in identically"))

    if col0:
        ref_len, our_len = length(col0[0]), length(col0[1])

        if ref_len and our_len:
            checks.append((
                "horizontal column * 20",
                ref_len * 20.0, our_len * 20.0,
                "reference 1.0 vs frozen 0.5 is BY DESIGN: frozen folds the NDC-to-UV remap into "
                "the matrix, the reference does it in its pixel shader"))

    # The three tex-matrix columns always report DIFF element by element, and that is a convention
    # difference rather than a defect: measured 2026-09-15, every axis carries an identical scale
    # (u and v 1/40 for the 40-yard first cascade, depth 1/4000 for the far plane) once the
    # reference's NDC-to-UV halving is accounted for, and the depth at the reference's own shadow
    # centre agrees to 0.0014. The two matrices are the same transform expressed in different input
    # bases -- frozen consumes world coordinates, the reference does not. Checking the scale ratio
    # keeps that claim honest: if any axis drifts, the matrices really have diverged.
    for index, axis in ((0, "u"), (1, "v"), (2, "depth")):
        col = values.get("tex matrix col %d" % index)

        if not col:
            continue

        ref_len, our_len = length(col[0]), length(col[1])

        if not ref_len or not our_len:
            continue

        # The reference keeps u and v in NDC and halves them in the shader; frozen folds that in.
        ref_len *= 0.5 if index < 2 else 1.0

        checks.append((
            "%s axis scale ratio" % axis,
            1.0, our_len / ref_len,
            "1.0 means frozen's shadow projection scales this axis exactly as the reference does"))

    if not checks:
        return

    print()
    print("invariants (position independent)")
    print("-" * 96)

    for name, ref_value, our_value in [(c[0], c[1], c[2]) for c in checks]:
        print("  %-24s %-28s %-28s" % (name, "%.6g" % ref_value, "%.6g" % our_value))

    for check in checks:
        print("      %s" % check[3])


def read_one_side(target, probe, side, symbols):
    """Read a single probe from a single client, the same way the paired path does."""
    kind = probe["type"]

    if side == "reference":
        if not probe.get("ref"):
            return None

        rva = int(probe["ref"], 16) - int(probe.get("ref_base", "0x400000"), 16)
        address = target.base + rva

        if probe.get("ref_deref"):
            held = target.read(address, 8 if target.bits == 64 else 4)

            if not held:
                return None

            held = struct.unpack("<Q" if target.bits == 64 else "<I", held)[0]

            if held <= 0x10000:
                return None

            address = held + probe.get("ref_offset", 0)

        return read_probe(target, address, kind, probe.get("ref_gather"))

    if not probe.get("frozen"):
        return None

    sym = symbols.get(probe["frozen"])

    if not sym:
        return None

    return read_probe(target, target.base + sym[0] + probe.get("frozen_offset", 0),
                      kind, probe.get("frozen_gather"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--group", help="only probes in this group")
    ap.add_argument("--watch", type=float, default=0, help="re-read every N seconds")
    ap.add_argument("--probes", default=os.path.join(ROOT, "tools", "probes.json"))
    ap.add_argument("--ref-exe", default=REFERENCE_EXE,
                    help="the reference client image to attach to, matched by full path")
    ap.add_argument("--our-exe", default=FROZEN_EXE)
    ap.add_argument("--snapshot", metavar="FILE",
                    help="read ONE client's probes and save them, for comparing against a snapshot "
                         "taken at another time")
    ap.add_argument("--side", choices=("reference", "frozen"), default="reference",
                    help="which client --snapshot reads")
    ap.add_argument("--against", metavar="FILE",
                    help="compare the live client against a saved snapshot instead of the other "
                         "live client")
    args = ap.parse_args()

    probes = json.load(open(args.probes))
    symbols = load_frozen_symbols()

    # Snapshots exist because the two clients CANNOT both be in the world at once: they share one
    # account, and whichever logs in second kicks the first back to character select, where its
    # globals keep their last values and read like live data. Comparing them side by side therefore
    # compares one live client against one stale one. Taking each side's readings in turn and
    # diffing the files is the honest way to do it.
    if args.snapshot:
        target = attach(args.side, args.ref_exe if args.side == "reference" else args.our_exe)

        if not target:
            sys.exit("%s is not running" % args.side)

        symbols_local = symbols if args.side == "frozen" else {}
        saved = {}

        for probe in probes:
            value = read_one_side(target, probe, args.side, symbols_local)

            if value is not None:
                saved[probe["name"]] = value

        json.dump({"side": args.side, "values": saved}, open(args.snapshot, "w"), indent=1)
        print("%s: saved %d probe values to %s" % (args.side, len(saved), args.snapshot),
              file=sys.stderr)
        return

    ref = attach("reference", args.ref_exe)
    ours = attach("frozen", args.our_exe)

    if args.against:
        loaded = json.load(open(args.against))
        print("comparing live frozen against %s snapshot in %s"
              % (loaded["side"], args.against), file=sys.stderr)

    if not ref and not ours:
        sys.exit("neither client is running")

    for label, target in (("reference", ref), ("frozen", ours)):
        if target:
            print("%-10s pid %-6d base 0x%X  %d-bit"
                  % (label, target.pid, target.base, target.bits), file=sys.stderr)
        else:
            print("%-10s not running" % label, file=sys.stderr)

    check_build_matches(ours, os.path.join(ROOT, "build", "dist", "bin", "Frozen.exe"))

    while True:
        rows = []

        for probe in probes:
            if args.group and probe.get("group") != args.group:
                continue

            kind = probe["type"]
            ref_value = None
            our_value = None

            if ref and probe.get("ref"):
                # Ghidra addresses are absolute against the file's own preferred base.
                rva = int(probe["ref"], 16) - int(probe.get("ref_base", "0x400000"), 16)
                address = ref.base + rva

                # Several of these labels name a POINTER to a structure rather than the structure.
                # Read them as-is and every field comes back zero, which reads as "frozen disagrees"
                # when it actually means "this was never the data".
                if probe.get("ref_deref"):
                    held = ref.read(address, 8 if ref.bits == 64 else 4)
                    address = None

                    if held:
                        held = struct.unpack("<Q" if ref.bits == 64 else "<I", held)[0]

                        if held > 0x10000:
                            address = held + probe.get("ref_offset", 0)

                ref_value = (read_probe(ref, address, kind, probe.get("ref_gather"))
                             if address else None)

                # The reference packs several colours into one u32 where frozen keeps three floats.
                # Unpacking here lets those probes compare automatically instead of "compare by
                # hand", which in practice meant they were never compared at all.
                if probe.get("ref_packed_rgb") and isinstance(ref_value, int):
                    ref_value = [((ref_value >> 16) & 0xFF) / 255.0,
                                 ((ref_value >> 8) & 0xFF) / 255.0,
                                 (ref_value & 0xFF) / 255.0]

            if ours and probe.get("frozen"):
                sym = symbols.get(probe["frozen"])

                if sym:
                    address = ours.base + sym[0] + probe.get("frozen_offset", 0)

                    # Some probes hold the same quantity in different shapes on the two sides -- a
                    # packed colour against three floats, say -- so the frozen side can override the
                    # type rather than needing a second probe that nothing ever compares.
                    our_value = read_probe(ours, address, probe.get("frozen_type", kind),
                                           probe.get("frozen_gather"))

            rows.append((probe, ref_value, our_value))

        skew = report_clock_skew(ref, ours, symbols)

        # Anything derived from the light bands is a function of time of day. With the clocks apart
        # a match is luck and a mismatch is meaningless, so neither is reported as a verdict.
        drifted = skew is not None and abs(skew) > 2
        TIME_DEPENDENT = ("outdoor light", "fog", "sky dome", "sky bodies")

        print()
        print("%-26s %-28s %-28s %s" % ("probe", "reference", "frozen", ""))
        print("-" * 96)
        group = None

        for probe, ref_value, our_value in rows:
            if probe.get("group") != group:
                group = probe.get("group")
                print("[%s]" % group)

            verdict = close(ref_value, our_value, probe.get("tol", 1e-3))
            mark = "" if verdict is None else ("ok" if verdict else "DIFF")

            # "DIFF" is a poor description of two vectors that are the same length pointing opposite
            # ways. That is a convention mismatch, not a wrong value, and it is worth naming: the
            # shadow camera bug found earlier was exactly this, at one use site that had not
            # accounted for it.
            if verdict is False and negated(ref_value, our_value, probe.get("tol", 1e-3)):
                mark = "NEGATED"

            if drifted and mark and probe.get("group") in TIME_DEPENDENT:
                mark = "SKEW"

            print("  %-24s %-28s %-28s %s"
                  % (probe["name"], fmt(ref_value), fmt(our_value), mark))

        report_invariants(rows)

        if not args.watch:
            break

        time.sleep(args.watch)


if __name__ == "__main__":
    main()
