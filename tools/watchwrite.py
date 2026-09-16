#!/usr/bin/env python3
"""Catch whatever writes a given address in the reference client, and say where it came from.

Five explanations for the light-colour gap were proposed and disproved by reading decompiled code
and reasoning about it. Reading more static code is not converging. This asks the process instead:
put a hardware write watchpoint on the address, let the client run, and report the instruction that
stores to it together with the stack around it.

The reference is a 32-bit process, so its debug registers live in a WOW64_CONTEXT and are read and
written through Wow64GetThreadContext / Wow64SetThreadContext rather than the 64-bit pair.

Usage:
    python tools/watchwrite.py --addr 0x00d38bd4 [--size 4] [--hits 3]
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import memcompare

k32 = ctypes.WinDLL("kernel32", use_last_error=True)

DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
EXCEPTION_DEBUG_EVENT = 1
CREATE_PROCESS_DEBUG_EVENT = 3
CREATE_THREAD_DEBUG_EVENT = 2
EXIT_PROCESS_DEBUG_EVENT = 5
EXCEPTION_SINGLE_STEP = 0x80000004
STATUS_BREAKPOINT = 0x80000003

WOW64_CONTEXT_i386 = 0x00010000
WOW64_CONTEXT_CONTROL = WOW64_CONTEXT_i386 | 0x1
WOW64_CONTEXT_INTEGER = WOW64_CONTEXT_i386 | 0x2
WOW64_CONTEXT_DEBUG_REGISTERS = WOW64_CONTEXT_i386 | 0x10
WOW64_CONTEXT_FULL = WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_INTEGER | (WOW64_CONTEXT_i386 | 0x4)

THREAD_ALL_ACCESS = 0x1FFFFF


class WOW64_FLOATING_SAVE_AREA(ctypes.Structure):
    _fields_ = [
        ("ControlWord", wt.DWORD), ("StatusWord", wt.DWORD), ("TagWord", wt.DWORD),
        ("ErrorOffset", wt.DWORD), ("ErrorSelector", wt.DWORD), ("DataOffset", wt.DWORD),
        ("DataSelector", wt.DWORD), ("RegisterArea", ctypes.c_byte * 80), ("Cr0NpxState", wt.DWORD),
    ]


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [
        ("ContextFlags", wt.DWORD),
        ("Dr0", wt.DWORD), ("Dr1", wt.DWORD), ("Dr2", wt.DWORD),
        ("Dr3", wt.DWORD), ("Dr6", wt.DWORD), ("Dr7", wt.DWORD),
        ("FloatSave", WOW64_FLOATING_SAVE_AREA),
        ("SegGs", wt.DWORD), ("SegFs", wt.DWORD), ("SegEs", wt.DWORD), ("SegDs", wt.DWORD),
        ("Edi", wt.DWORD), ("Esi", wt.DWORD), ("Ebx", wt.DWORD), ("Edx", wt.DWORD),
        ("Ecx", wt.DWORD), ("Eax", wt.DWORD), ("Ebp", wt.DWORD), ("Eip", wt.DWORD),
        ("SegCs", wt.DWORD), ("EFlags", wt.DWORD), ("Esp", wt.DWORD), ("SegSs", wt.DWORD),
        ("ExtendedRegisters", ctypes.c_byte * 512),
    ]


class EXCEPTION_RECORD32(ctypes.Structure):
    pass


EXCEPTION_RECORD32._fields_ = [
    ("ExceptionCode", wt.DWORD), ("ExceptionFlags", wt.DWORD),
    ("ExceptionRecord", ctypes.POINTER(EXCEPTION_RECORD32)),
    ("ExceptionAddress", ctypes.c_void_p), ("NumberParameters", wt.DWORD),
    ("__unused", wt.DWORD), ("ExceptionInformation", ctypes.c_ulonglong * 15),
]


class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("ExceptionRecord", EXCEPTION_RECORD32), ("dwFirstChance", wt.DWORD)]


class CREATE_THREAD_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("hThread", wt.HANDLE), ("lpThreadLocalBase", ctypes.c_void_p),
                ("lpStartAddress", ctypes.c_void_p)]


class DEBUG_EVENT_UNION(ctypes.Union):
    _fields_ = [("Exception", EXCEPTION_DEBUG_INFO),
                ("CreateThread", CREATE_THREAD_DEBUG_INFO),
                ("__pad", ctypes.c_byte * 256)]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [("dwDebugEventCode", wt.DWORD), ("dwProcessId", wt.DWORD),
                ("dwThreadId", wt.DWORD), ("u", DEBUG_EVENT_UNION)]


def arm(thread, address, size):
    """Set debug register 0 as a WRITE watchpoint covering `size` bytes at `address`."""
    ctx = WOW64_CONTEXT()
    ctx.ContextFlags = WOW64_CONTEXT_DEBUG_REGISTERS

    if not k32.Wow64GetThreadContext(wt.HANDLE(thread), ctypes.byref(ctx)):
        return False

    ctx.Dr0 = address
    ctx.Dr6 = 0

    # DR7: slot 0 local enable (bit 0); type bits 16-17 = 01 (write); length bits 18-19 by size.
    length = {1: 0b00, 2: 0b01, 4: 0b11, 8: 0b10}[size]
    ctx.Dr7 = (ctx.Dr7 & ~0xF0000) | 0x1 | (0b01 << 16) | (length << 18)
    ctx.ContextFlags = WOW64_CONTEXT_DEBUG_REGISTERS

    return bool(k32.Wow64SetThreadContext(wt.HANDLE(thread), ctypes.byref(ctx)))


def report(process, thread, base, size, index):
    ctx = WOW64_CONTEXT()
    ctx.ContextFlags = WOW64_CONTEXT_FULL | WOW64_CONTEXT_DEBUG_REGISTERS

    if not k32.Wow64GetThreadContext(wt.HANDLE(thread), ctypes.byref(ctx)):
        print("  could not read the thread context")
        return

    print("\n=== write #%d ===" % index)
    print("  eip 0x%08X   (module +0x%X)" % (ctx.Eip, ctx.Eip - base + 0x400000 - 0x400000))
    print("  image-relative: 0x%08X" % (ctx.Eip - base + 0x400000))
    print("  eax %08X ecx %08X edx %08X ebx %08X" % (ctx.Eax, ctx.Ecx, ctx.Edx, ctx.Ebx))
    print("  esp %08X ebp %08X esi %08X edi %08X" % (ctx.Esp, ctx.Ebp, ctx.Esi, ctx.Edi))

    # Return addresses on the stack that land inside the image, newest first.
    buf = (ctypes.c_char * 512)()
    got = ctypes.c_size_t(0)
    k32.ReadProcessMemory(wt.HANDLE(process), ctypes.c_void_p(ctx.Esp), buf,
                          ctypes.c_size_t(512), ctypes.byref(got))
    raw = bytes(buf[:got.value])
    lo, hi = base, base + size
    seen = []

    for i in range(0, len(raw) - 4, 4):
        v = int.from_bytes(raw[i:i + 4], "little")

        if lo <= v < hi:
            rel = v - base + 0x400000
            if rel not in seen:
                seen.append(rel)
                print("    esp+0x%03X  -> 0x%08X" % (i, rel))
            if len(seen) >= 10:
                break


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", required=True, help="absolute address in the reference, e.g. 0x00d38bd4")
    ap.add_argument("--size", type=int, default=4, choices=(1, 2, 4, 8))
    ap.add_argument("--hits", type=int, default=3)
    ap.add_argument("--seconds", type=int, default=300,
                    help="how long to wait overall; the light record is not rewritten every frame")
    ap.add_argument("--exe", default=memcompare.REFERENCE_EXE)
    args = ap.parse_args()

    pid = memcompare.find_pid(args.exe)

    if not pid:
        sys.exit("that client is not running")

    target = memcompare.attach("reference", args.exe)
    live = target.base + int(args.addr, 16) - 0x400000
    print("watching writes to 0x%08X (image 0x%s) in pid %d" % (live, args.addr.lstrip("0x"), pid))

    if not k32.DebugActiveProcess(pid):
        sys.exit("could not attach: %d" % ctypes.get_last_error())

    k32.DebugSetProcessKillOnExit(False)

    evt = DEBUG_EVENT()
    threads = {}
    hits = 0
    process = None

    try:
        import time
        deadline = time.time() + args.seconds

        while hits < args.hits:
            if not k32.WaitForDebugEvent(ctypes.byref(evt), 1000):
                # A quiet second is normal: keep waiting until the overall deadline, because the
                # block is only rewritten when the light actually changes.
                if time.time() > deadline:
                    print("no write seen in %d seconds" % args.seconds)
                    break

                continue

            code = evt.dwDebugEventCode
            status = DBG_CONTINUE

            if code == CREATE_PROCESS_DEBUG_EVENT:
                process = k32.OpenProcess(0x1F0FFF, False, evt.dwProcessId)
            elif code == CREATE_THREAD_DEBUG_EVENT:
                handle = evt.u.CreateThread.hThread
                threads[evt.dwThreadId] = handle
                arm(handle, live, args.size)
            elif code == EXIT_PROCESS_DEBUG_EVENT:
                print("the client exited")
                break
            elif code == EXCEPTION_DEBUG_EVENT:
                rec = evt.u.Exception.ExceptionRecord

                if rec.ExceptionCode == STATUS_BREAKPOINT:
                    # The attach breakpoint: every thread exists by now, so arm them all.
                    if process is None:
                        process = k32.OpenProcess(0x1F0FFF, False, evt.dwProcessId)

                    armed = 0

                    for tid, handle in list(threads.items()):
                        armed += bool(arm(handle, live, args.size))

                    print("armed %d thread(s)" % armed)
                elif rec.ExceptionCode == EXCEPTION_SINGLE_STEP:
                    handle = threads.get(evt.dwThreadId)

                    if handle:
                        hits += 1
                        report(process, handle, target.base, target.size, hits)
                else:
                    status = DBG_EXCEPTION_NOT_HANDLED

            k32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, status)
    finally:
        k32.DebugActiveProcessStop(pid)
        print("\ndetached")


if __name__ == "__main__":
    main()
