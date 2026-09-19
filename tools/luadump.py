#!/usr/bin/env python3
"""Enumerate a running client's Lua globals by walking its Lua state in memory.

Frozen logs "Function not yet implemented" when a stub is CALLED, which only ever names the subset the
current screen happened to touch. The honest measure of Lua parity is the whole global table in each
client, compared name by name, which is what this reads.

Both clients use Lua 5.1 (frozen vendors 5.1.3), so one set of structure offsets serves both once the
pointer width is accounted for. The reference is 32-bit and frozen is 64-bit.

The reference's lua_State global has no recovered address, so it is found by scanning the image's
writable data for a pointer that validates as a lua_State: type tag LUA_TTHREAD, a non-null global
state, and a globals field that is itself a table. Three independent checks make a false positive
very unlikely.

Usage:
    python tools/luadump.py --exe <path> [--out names.txt]
    python tools/luadump.py --diff          # both clients, and what differs
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import memcompare

LUA_TSTRING = 4
LUA_TTABLE = 5
LUA_TFUNCTION = 6
LUA_TTHREAD = 8

TYPE_NAMES = {0: "nil", 1: "boolean", 2: "lightuserdata", 3: "number",
              4: "string", 5: "table", 6: "function", 7: "userdata", 8: "thread"}


class Layout:
    """Structure offsets for one pointer width."""

    def __init__(self, bits):
        self.bits = bits
        self.ptr = 8 if bits == 64 else 4
        self.fmt = "<Q" if bits == 64 else "<I"

        # The type tag sits 8 bytes into every collectable object in BOTH clients, but for
        # different reasons: stock 64-bit Lua has an 8-byte `next` pointer, while the reference's
        # 32-bit fork carries a 4-byte `next` plus one extra 4-byte field that stock Lua does not
        # have. That extra word shifts every later field by 4 and is why stock 32-bit offsets found
        # nothing. It was measured, not guessed: the live string "CreateFrame" has its length at
        # +16 and its characters at +20, where stock would put them at +12 and +16.
        self.header_tt = 8
        self.tvalue_size = 16
        self.tvalue_tt = 8
        self.node_size = 40 if bits == 64 else 32
        self.node_key = 16

        if bits == 64:
            self.header_size = 16
            self.state_l_gt = 120
            self.table_lsizenode = 11
            self.table_array = 24
            self.table_node = 32
            self.table_sizearray = 56
            self.tstring_len = 16
            self.tstring_data = 24
        else:
            # Measured from the live globals table, found by its own `_G` self-reference. The fork
            # carries an extra 4-byte field in `Table` as well as in the object header, so `array`
            # and `node` sit 4 bytes later than the shifted-stock prediction:
            #   +0 next, +4 extra, +8 tt, +9 marked, +10 flags, +11 lsizenode,
            #   +12 metatable, +16 extra, +20 array, +24 node, +28 lastfree
            self.header_size = 12
            self.state_l_gt = 80
            self.table_lsizenode = 11
            self.table_array = 20
            self.table_node = 24
            self.table_sizearray = 36
            self.tstring_len = 16
            self.tstring_data = 20


class Reader:
    def __init__(self, target):
        self.target = target
        self.layout = Layout(target.bits)

    def ptr(self, address):
        raw = self.target.read(address, self.layout.ptr)

        if raw is None:
            return None

        return struct.unpack(self.layout.fmt, raw)[0]

    def u8(self, address):
        raw = self.target.read(address, 1)
        return None if raw is None else raw[0]

    def i32(self, address):
        raw = self.target.read(address, 4)
        return None if raw is None else struct.unpack("<i", raw)[0]

    def tvalue(self, address):
        """(value pointer, type tag) at a TValue."""
        value = self.ptr(address)
        tt = self.i32(address + self.layout.tvalue_tt)
        return value, tt

    def string(self, address):
        """The bytes of a TString object."""
        length = self.ptr(address + self.layout.tstring_len)

        if length is None or length > 4096:
            return None

        raw = self.target.read(address + self.layout.tstring_data, length)

        if raw is None:
            return None

        return raw.decode("latin-1")

    def looks_like_state(self, address):
        tt = self.u8(address + self.layout.header_tt)

        if tt != LUA_TTHREAD:
            return False

        # l_G sits two pointers past the stack fields; a live state always has one.
        global_state = self.ptr(address + (32 if self.layout.bits == 64 else 20))

        if not global_state:
            return False

        _, gt_type = self.tvalue(address + self.layout.state_l_gt)
        return gt_type == LUA_TTABLE

    def globals_table(self, state):
        value, tt = self.tvalue(state + self.layout.state_l_gt)
        return value if tt == LUA_TTABLE else None

    def node_array(self, table):
        """The whole node array of a table in one read, or None.

        Reading it field by field meant thousands of separate calls into the target process for a
        single table, which is what made scanning for the right Lua state take longer than any
        useful timeout. One read per table, parsed locally, is orders of magnitude faster.
        """
        lsizenode = self.u8(table + self.layout.table_lsizenode)
        node = self.ptr(table + self.layout.table_node)

        if lsizenode is None or node is None or lsizenode > 20 or not node:
            return None, 0

        count = 1 << lsizenode
        total = count * self.layout.node_size

        # Read across gaps rather than stopping at the first one. A large node array can straddle an
        # unreadable page, and a reader that stops there silently returns a PREFIX of the table,
        # which looks exactly like a smaller table. That is what made the reference's globals come
        # back as 4014 entries with obvious names missing. Unreadable stretches become zeros, which
        # the caller skips as empty slots.
        out = bytearray()
        step = 0x10000

        while len(out) < total:
            want = min(step, total - len(out))
            chunk = self.target.read(node + len(out), want)

            if chunk and len(chunk) == want:
                out += chunk
                continue

            # A 64K request that straddles an allocation boundary fails as a whole, zeroing 2048
            # slots at a stroke; that alone hid a fifth of the reference's globals. Retry the
            # failed span a page at a time so only genuinely unreadable pages are lost.
            recovered = bytearray()

            for page in range(0, want, 0x1000):
                size = min(0x1000, want - page)
                piece = self.target.read(node + len(out) + page, size)

                if piece and len(piece) == size:
                    recovered += piece
                elif piece:
                    recovered += piece + bytes(size - len(piece))
                else:
                    recovered += bytes(size)

            out += bytes(recovered)

        return bytes(out), count

    def count_string_keys(self, table):
        """How many string-keyed entries a table has, without decoding any of them.

        Candidate scoring only needs the count, and skipping the string reads keeps the scan cheap.
        """
        raw, count = self.node_array(table)

        if not raw:
            return 0

        key_tt = self.layout.node_key + self.layout.tvalue_tt
        step = self.layout.node_size
        total = 0

        for i in range(count):
            base = i * step

            if base + key_tt + 4 > len(raw):
                break

            if struct.unpack_from("<i", raw, base + key_tt)[0] == LUA_TSTRING:
                total += 1

        return total

    def table_entries(self, table):
        """Every string-keyed entry of a table, as (name, type tag)."""
        out = []
        raw, count = self.node_array(table)

        if not raw:
            return out

        step = self.layout.node_size
        key_off = self.layout.node_key
        key_tt = key_off + self.layout.tvalue_tt
        val_tt = self.layout.tvalue_tt

        for i in range(count):
            base = i * step

            if base + step > len(raw):
                break

            if struct.unpack_from("<i", raw, base + key_tt)[0] != LUA_TSTRING:
                continue

            key_value = struct.unpack_from(self.layout.fmt, raw, base + key_off)[0]

            if not key_value:
                continue

            name = self.string(key_value)

            if not name:
                continue

            out.append((name, struct.unpack_from("<i", raw, base + val_tt)[0]))

        return out


def find_state(target, reader):
    """Scan the module's writable data for a pointer to a lua_State.

    Every candidate is scored by how many string-keyed globals its table actually yields, and the
    best one wins. Taking the first structural match instead found an address inside the image whose
    bytes happened to satisfy the type checks and whose "globals table" was empty, which is the
    failure mode this guards against: a lua_State is heap-allocated, so anything inside the module
    is a coincidence rather than a state.
    """
    step = 0x10000
    width = reader.layout.ptr
    lo, hi = target.base, target.base + target.size
    best = (0, None, None)

    for offset in range(0, target.size, step):
        block = target.read(target.base + offset, min(step, target.size - offset))

        if not block:
            continue

        for i in range(0, len(block) - width, width):
            candidate = struct.unpack_from(reader.layout.fmt, block, i)[0]

            if candidate < 0x10000 or lo <= candidate < hi:
                continue

            if not reader.looks_like_state(candidate):
                continue

            table = reader.globals_table(candidate)

            if not table:
                continue

            count = reader.count_string_keys(table)

            if count > best[0]:
                best = (count, candidate, target.base + offset + i)

    if best[0] < 100:
        return None, None

    return best[1], best[2]


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_ulonglong),
        ("AllocationBase", ctypes.c_ulonglong),
        ("AllocationProtect", wt.DWORD),
        ("__align", wt.DWORD),
        ("RegionSize", ctypes.c_ulonglong),
        ("State", wt.DWORD),
        ("Protect", wt.DWORD),
        ("Type", wt.DWORD),
        ("__align2", wt.DWORD),
    ]


def scan_heap_for_state(target, reader):
    """Find the lua_State by its shape, anywhere in the process.

    Scanning the module's data for a POINTER to the state only works if the client keeps one there.
    The reference does not, so the structure itself has to be found: a type tag of LUA_TTHREAD, a
    non-null global state, and a globals field that is a table with a populated node array. Those
    three together are specific enough that nothing else in the address space matches.
    """
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    info = MEMORY_BASIC_INFORMATION()
    address = 0
    width = reader.layout.ptr
    tt_offset = reader.layout.header_tt
    gt_offset = reader.layout.state_l_gt + reader.layout.tvalue_tt
    best = (0, None)
    limit = 1 << 47

    # <width bytes of `next`><tt == LUA_TTHREAD><filler up to l_gt's tag><LUA_TTABLE>
    gap = gt_offset - (tt_offset + 1)
    pattern = re.compile(
        b"(?s)" + b"." * tt_offset + bytes([LUA_TTHREAD]) + b"." * gap
        + struct.pack("<i", LUA_TTABLE))

    while address < limit:
        if not k32.VirtualQueryEx(
                wt.HANDLE(target.handle), ctypes.c_void_p(address),
                ctypes.byref(info), ctypes.sizeof(info)):
            break

        size = info.RegionSize
        base = info.BaseAddress

        # MEM_COMMIT, readable, and not an image mapping: the heap the allocator hands Lua.
        readable = info.Protect & 0xCC  # READWRITE | WRITECOPY variants
        if info.State == 0x1000 and readable and info.Type == 0x20000 and size <= (64 << 20):
            block = target.read(base, size)

            if block:
                # Find candidates with one compiled pattern rather than a Python loop over every
                # aligned offset: a byte-at-a-time scan of a few hundred megabytes does not finish
                # in any useful time. The pattern pins the two type tags at their fixed distance,
                # which is selective enough that only a handful of positions survive to be checked
                # properly.
                for m in pattern.finditer(block):
                    i = m.start()

                    if i % width:
                        continue

                    candidate = base + i

                    if not reader.looks_like_state(candidate):
                        continue

                    table = reader.globals_table(candidate)

                    if not table:
                        continue

                    count = reader.count_string_keys(table)

                    if count > best[0]:
                        best = (count, candidate)

        if size == 0:
            break

        address = base + size

    return (best[1], 0) if best[0] >= 100 else (None, None)


# Names that exist in any Lua state this project cares about. Deliberately includes base-library
# functions, because the reference may be sitting on the glue screen where GlueXML is loaded and the
# in-world API is not, and a locator that only recognises in-world names fails there for no reason.
KNOWN_GLOBALS = ("tostring", "pairs", "type", "getfenv", "GetLocale",
                 "CreateFrame", "UnitName", "GetTime", "UIParent")


def scan_heap_for_globals_table(target, reader):
    """Find the globals table directly, without relying on the lua_State layout.

    Locating the state first is neater, but it assumes the exact field order of `lua_State`, and the
    reference's Lua is a Blizzard fork whose layout does not match the stock 5.1.3 frozen vendors, so
    that search comes up empty against it. A `Table` is a much smaller and much more stable
    structure, and the globals table is unmistakable once found: it is the one holding thousands of
    string keys, several of which are names no other table would carry.
    """
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    info = MEMORY_BASIC_INFORMATION()
    address = 0
    width = reader.layout.ptr
    lsizenode_off = reader.layout.table_lsizenode
    node_off = reader.layout.table_node
    best = (0, None)
    limit = 1 << 47

    while address < limit:
        if not k32.VirtualQueryEx(
                wt.HANDLE(target.handle), ctypes.c_void_p(address),
                ctypes.byref(info), ctypes.sizeof(info)):
            break

        size = info.RegionSize
        base = info.BaseAddress

        if info.State == 0x1000 and (info.Protect & 0xCC) and info.Type == 0x20000 and size <= (64 << 20):
            block = target.read(base, size)

            if block:
                for i in range(0, len(block) - node_off - width, width):
                    if block[i + reader.layout.header_tt] != LUA_TTABLE:
                        continue

                    lsizenode = block[i + lsizenode_off]

                    # The globals table is big; small tables are not worth a round trip.
                    if lsizenode < 8 or lsizenode > 20:
                        continue

                    node = struct.unpack_from(reader.layout.fmt, block, i + node_off)[0]

                    if not node or node % width:
                        continue

                    count = reader.count_string_keys(base + i)

                    if count > best[0]:
                        best = (count, base + i)

        if size == 0:
            break

        address = base + size

    if best[0] < 200:
        return None

    names = {n for n, _ in reader.table_entries(best[1])}

    if not any(k in names for k in KNOWN_GLOBALS):
        return None

    return best[1]


def dump(label, exe, state_symbol=None):
    target = memcompare.attach(label, exe)

    if not target:
        print("%s: not running" % label, file=sys.stderr)
        return None

    reader = Reader(target)
    state = None

    if state_symbol:
        symbols = memcompare.load_frozen_symbols()
        sym = symbols.get(state_symbol)

        if sym:
            state = reader.ptr(target.base + sym[0])

            if state and not reader.looks_like_state(state):
                state = None

    # Heap scan first. The older pointer scan walks the module a word at a time in Python and hits
    # the memory reader once per structurally-plausible candidate, which takes minutes; the heap
    # scan filters with one compiled pattern and only validates the few positions that survive.
    if not state:
        state, _ = scan_heap_for_state(target, reader)

        if state:
            print("%s: lua_State 0x%X found by heap scan" % (label, state), file=sys.stderr)

    if not state:
        state, where = find_state(target, reader)

        if state:
            print("%s: lua_State 0x%X found via 0x%X" % (label, state, where), file=sys.stderr)


    table = reader.globals_table(state) if state else None

    if not table:
        table = scan_heap_for_globals_table(target, reader)

        if table:
            print("%s: globals table 0x%X found directly" % (label, table), file=sys.stderr)

    if not table:
        print("%s: globals table not found" % label, file=sys.stderr)
        return None

    entries = reader.table_entries(table)
    print("%s: %d globals" % (label, len(entries)), file=sys.stderr)
    return dict(entries)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe")
    ap.add_argument("--symbol", default="FrameScript::s_compat_lua")
    ap.add_argument("--out")
    ap.add_argument("--diff", action="store_true")
    args = ap.parse_args()

    if args.diff:
        ref = dump("reference", memcompare.REFERENCE_EXE)
        ours = dump("frozen", memcompare.FROZEN_EXE, args.symbol)

        if not ref or not ours:
            sys.exit("need both clients in the world")

        ref_functions = {k for k, t in ref.items() if t == LUA_TFUNCTION}
        our_functions = {k for k, t in ours.items() if t == LUA_TFUNCTION}
        missing = sorted(ref_functions - our_functions)
        extra = sorted(our_functions - ref_functions)

        print()
        print("reference Lua functions : %d" % len(ref_functions))
        print("frozen Lua functions      : %d" % len(our_functions))
        print("missing from frozen       : %d" % len(missing))
        print("present only in frozen    : %d" % len(extra))

        if missing:
            print()
            print("missing (first 60):")

            for name in missing[:60]:
                print("  %s" % name)

        out = os.path.join(memcompare.ROOT, "build", "lua-missing.txt")
        open(out, "w").write("\n".join(missing) + "\n")
        print()
        print("full list -> %s" % out)
        return

    if not args.exe:
        sys.exit("need --exe or --diff")

    names = dump("client", args.exe, args.symbol if "Frozen" in args.exe else None)

    if names and args.out:
        with open(args.out, "w") as fh:
            for name in sorted(names):
                fh.write("%s\t%s\n" % (name, TYPE_NAMES.get(names[name], "?")))

        print("wrote %s" % args.out)


if __name__ == "__main__":
    main()
