#!/usr/bin/env python3
"""Sample a spinning client's threads and say which function each is in.

For a hang there is no exception to catch, so crashstack.py has nothing to trigger on. This instead
suspends each thread briefly, reads its instruction pointer, and maps it to the nearest symbol from
the PDB -- a poor man's profiler, which is all that is needed to find a busy loop.

    python tools/samplehang.py [--samples 5] [--exe build/dist/bin/Frozen.exe]
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import collections
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import memcompare

k32 = ctypes.WinDLL("kernel32", use_last_error=True)

TH32CS_SNAPTHREAD = 0x00000004
THREAD_ALL_ACCESS = 0x1FFFFF
CONTEXT_FULL_AMD64 = 0x10000B


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", ctypes.c_long),
                ("tpDeltaPri", ctypes.c_long), ("dwFlags", wt.DWORD)]


class M128A(ctypes.Structure):
    _fields_ = [("Low", ctypes.c_ulonglong), ("High", ctypes.c_longlong)]


class CONTEXT64(ctypes.Structure):
    _pack_ = 16
    _fields_ = [
        ("P1Home", ctypes.c_ulonglong), ("P2Home", ctypes.c_ulonglong),
        ("P3Home", ctypes.c_ulonglong), ("P4Home", ctypes.c_ulonglong),
        ("P5Home", ctypes.c_ulonglong), ("P6Home", ctypes.c_ulonglong),
        ("ContextFlags", wt.DWORD), ("MxCsr", wt.DWORD),
        ("SegCs", wt.WORD), ("SegDs", wt.WORD), ("SegEs", wt.WORD),
        ("SegFs", wt.WORD), ("SegGs", wt.WORD), ("SegSs", wt.WORD),
        ("EFlags", wt.DWORD),
        ("Dr0", ctypes.c_ulonglong), ("Dr1", ctypes.c_ulonglong), ("Dr2", ctypes.c_ulonglong),
        ("Dr3", ctypes.c_ulonglong), ("Dr6", ctypes.c_ulonglong), ("Dr7", ctypes.c_ulonglong),
        ("Rax", ctypes.c_ulonglong), ("Rcx", ctypes.c_ulonglong), ("Rdx", ctypes.c_ulonglong),
        ("Rbx", ctypes.c_ulonglong), ("Rsp", ctypes.c_ulonglong), ("Rbp", ctypes.c_ulonglong),
        ("Rsi", ctypes.c_ulonglong), ("Rdi", ctypes.c_ulonglong),
        ("R8", ctypes.c_ulonglong), ("R9", ctypes.c_ulonglong), ("R10", ctypes.c_ulonglong),
        ("R11", ctypes.c_ulonglong), ("R12", ctypes.c_ulonglong), ("R13", ctypes.c_ulonglong),
        ("R14", ctypes.c_ulonglong), ("R15", ctypes.c_ulonglong),
        ("Rip", ctypes.c_ulonglong),
        ("FltSave", ctypes.c_byte * 512),
        ("VectorRegister", M128A * 26), ("VectorControl", ctypes.c_ulonglong),
        ("DebugControl", ctypes.c_ulonglong), ("LastBranchToRip", ctypes.c_ulonglong),
        ("LastBranchFromRip", ctypes.c_ulonglong), ("LastExceptionToRip", ctypes.c_ulonglong),
        ("LastExceptionFromRip", ctypes.c_ulonglong),
    ]


def threads_of(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    entry = THREADENTRY32()
    entry.dwSize = ctypes.sizeof(THREADENTRY32)
    out = []

    if k32.Thread32First(snap, ctypes.byref(entry)):
        while True:
            if entry.th32OwnerProcessID == pid:
                out.append(entry.th32ThreadID)

            if not k32.Thread32Next(snap, ctypes.byref(entry)):
                break

    k32.CloseHandle(snap)

    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="build/dist/bin/Frozen.exe")
    ap.add_argument("--samples", type=int, default=5)
    args = ap.parse_args()

    target = memcompare.attach("frozen", args.exe)

    if not target:
        sys.exit("that client is not running")

    symbols = memcompare.load_frozen_symbols()

    # Sort once so each sample is a binary search rather than a scan.
    # Symbol values are not all 2-tuples (some carry gather lists), so take the first element as
    # the RVA and ignore the rest rather than unpacking a fixed shape.
    table = sorted((info[0], name) for name, info in symbols.items()
                   if isinstance(info, (list, tuple)) and info and isinstance(info[0], int))
    pid = memcompare.find_pid(args.exe)

    def nearest(rva):
        lo, hi = 0, len(table)

        while lo < hi:
            mid = (lo + hi) // 2

            if table[mid][0] <= rva:
                lo = mid + 1
            else:
                hi = mid

        if not lo:
            return None, 0

        base, name = table[lo - 1]

        return name, rva - base

    hits = collections.Counter()

    for _ in range(args.samples):
        for tid in threads_of(pid):
            handle = k32.OpenThread(THREAD_ALL_ACCESS, False, tid)

            if not handle:
                continue

            ctx = CONTEXT64()
            ctx.ContextFlags = CONTEXT_FULL_AMD64

            if k32.SuspendThread(handle) != 0xFFFFFFFF:
                if k32.GetThreadContext(handle, ctypes.byref(ctx)):
                    rip = ctx.Rip

                    if target.base <= rip < target.base + target.size:
                        # Raw RVA only. The symbol table here is DATA globals from the PDB, not
                        # functions, so naming an instruction pointer from it produces confident
                        # nonsense (`__guard_flags +0xAF613`). Symbolize these with llvm-symbolizer,
                        # which reads the real function table.
                        hits["0x%X" % (rip - target.base)] += 1
                    else:
                        hits["(outside Frozen.exe -- system call or other module)"] += 1

                k32.ResumeThread(handle)

            k32.CloseHandle(handle)

        time.sleep(0.05)

    print("thread samples, most frequent first:")

    for where, count in hits.most_common(12):
        print("  %4d  %s" % (count, where))


if __name__ == "__main__":
    main()
