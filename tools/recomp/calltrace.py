#!/usr/bin/env python3
"""Record which mapped functions a running client calls, in order, for a few frames.

Attaches as a debugger (software INT3 breakpoints only, no debug registers), plants a breakpoint
on every function in the chosen set, logs each hit as (time, thread, function), and detaches with
the original bytes restored. Works on both sides of the map:

    python tools/recomp/calltrace.py ref  --seconds 8      # the reference WoW.exe (32-bit, WOW64)
    python tools/recomp/calltrace.py whoa --seconds 8      # build/dist/bin/Whoa.exe (64-bit)

The set is every linked, non-stub function in tools/recomp/data/map.json (--spine limits it to the
world spine; --names a,b,c picks by whoa name), plus the frame marker CGWorldFrame::OnFrameRender,
which is always traced so hits can be cut into frames. Hot leaves are throttled: after --max-hits
hits an address stops being re-armed, so SMemAlloc cannot make a frame take a minute.

Output: tools/recomp/data/trace-<side>.jsonl, one hit per line; tracecompare.py diffs the two.

The process is found by full path, never by name (the user's own game is also a WoW.exe). Launch
and login are not this tool's job: tools/relog-reference.py brings the reference to the world,
WHOA_AUTO_LOGIN does it for whoa.
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import io
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import crashstack  # noqa: E402  (CONTEXT, aligned_context, DEBUG_EVENT, constants)

DATA = os.path.join(HERE, 'data')
MAP_JSON = os.path.join(DATA, 'map.json')
REF_JSONL = os.path.join(DATA, 'ref-functions.jsonl')
PDB_DUMP = os.path.join(DATA, 'whoa-pdb.txt')
REFERENCE_EXE = os.path.join(ROOT, '.reference', 'WOTLK 3.3.5a - Windows', 'WoW_WOTLK_3.3.5a', 'WoW.exe')
WHOA_EXE = os.path.join(ROOT, 'build', 'dist', 'bin', 'Whoa.exe')
REF_IMAGE_BASE = 0x400000  # Ghidra's addresses are relative to this preferred base

# The per-frame marker: CGWorldFrame::RenderWorld, the render-batch callback both clients run once
# per world frame (OnFrameRender only queues it, and in whoa is not itself observed by a breakpoint)
FRAME_REF = '004faf90'
FRAME_WHOA = 'CGWorldFrame::RenderWorld'

k32 = crashstack.k32
psapi = ctypes.WinDLL('psapi', use_last_error=True)

# A 64-bit debugger sees a WOW64 debuggee's int3 / trap as the WX86 codes, not the native ones
BREAKPOINT_CODES = {0x80000003, 0x4000001F}   # STATUS_BREAKPOINT, STATUS_WX86_BREAKPOINT
SINGLE_STEP_CODES = {0x80000004, 0x4000001E}  # STATUS_SINGLE_STEP, STATUS_WX86_SINGLE_STEP
LIST_MODULES_ALL = 0x03
TF = 0x100


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [
        ('ContextFlags', wt.DWORD),
        ('Dr0', wt.DWORD), ('Dr1', wt.DWORD), ('Dr2', wt.DWORD), ('Dr3', wt.DWORD), ('Dr6', wt.DWORD), ('Dr7', wt.DWORD),
        ('FloatSave', ctypes.c_byte * 112),
        ('SegGs', wt.DWORD), ('SegFs', wt.DWORD), ('SegEs', wt.DWORD), ('SegDs', wt.DWORD),
        ('Edi', wt.DWORD), ('Esi', wt.DWORD), ('Ebx', wt.DWORD), ('Edx', wt.DWORD), ('Ecx', wt.DWORD), ('Eax', wt.DWORD),
        ('Ebp', wt.DWORD), ('Eip', wt.DWORD), ('SegCs', wt.DWORD), ('EFlags', wt.DWORD), ('Esp', wt.DWORD), ('SegSs', wt.DWORD),
        ('ExtendedRegisters', ctypes.c_byte * 512),
    ]


WOW64_CONTEXT_CONTROL_INTEGER = 0x10000 | 0x1 | 0x2


def find_pid(exe):
    target = os.path.normcase(os.path.abspath(exe))
    listing = subprocess.run(
        ['powershell', '-NoProfile', '-c',
         "Get-CimInstance Win32_Process -Filter \"Name='%s'\" | Select-Object ProcessId, ExecutablePath | ConvertTo-Csv -NoTypeInformation" % os.path.basename(exe)],
        capture_output=True, text=True).stdout
    for line in listing.splitlines()[1:]:
        parts = line.strip().strip('"').split('","')
        if len(parts) == 2 and os.path.normcase(os.path.abspath(parts[1])) == target:
            return int(parts[0])
    return None


def pe_sections(exe):
    """[(VirtualAddress, VirtualSize)] in section order, for PDB segment:offset -> RVA."""
    with open(exe, 'rb') as f:
        data = f.read(0x1000)
    pe = int.from_bytes(data[0x3C:0x40], 'little')
    nsec = int.from_bytes(data[pe + 6:pe + 8], 'little')
    opt = int.from_bytes(data[pe + 20:pe + 22], 'little')
    sec = pe + 24 + opt
    out = []
    for i in range(nsec):
        s = data[sec + i * 40: sec + (i + 1) * 40]
        out.append((int.from_bytes(s[12:16], 'little'), int.from_bytes(s[8:12], 'little')))
    return out


def whoa_rvas(names):
    """whoa name -> RVA from the PDB dump + PE section table."""
    sections = pe_sections(WHOA_EXE)
    want = set(names)
    out = {}
    owners = {}
    pending = None
    proc = re.compile(r'^\s*\d+ \| S_[GL]PROC32 \[size = \d+\] `(.+)`$')
    addr = re.compile(r'addr = (\d+):(\d+), code size = (\d+)')
    with io.open(PDB_DUMP, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = proc.match(line)
            if m:
                pending = m.group(1).replace("`anonymous namespace'::", '')
                continue
            if pending:
                m = addr.search(line)
                if m:
                    seg, off = int(m.group(1)), int(m.group(2))
                    if 1 <= seg <= len(sections):
                        rva = sections[seg - 1][0] + off
                        owners.setdefault(rva, set()).add(pending)
                        if pending in want and pending not in out:
                            out[pending] = rva
                    pending = None
    # /OPT:ICF folds identical bodies onto one address (every empty destructor, every `return
    # this`): a hit there says nothing about which function ran, so those are not traced
    folded = [n for n, rva in out.items() if len(owners.get(rva, ())) > 1]
    for n in folded:
        del out[n]
    if folded:
        print('skipping %d whoa functions on ICF-folded addresses (e.g. %s)' % (len(folded), ', '.join(sorted(folded)[:4])))
    return out


def choose(side, opts):
    """{address-key: label} to trace. Keys are reference addresses (hex) or whoa names."""
    m = json.load(io.open(MAP_JSON, encoding='utf-8'))
    picked = {}
    spine = None
    if opts.spine:
        # reuse recomp's reachability: everything the report calls the world spine
        sys.path.insert(0, HERE)
        import recomp
        refs = recomp.load_reference()
        spine = recomp.spine(refs)
    names = set(opts.names.split(',')) if opts.names else None
    # Hot leaves (allocators, string ops, render-state setters) fire thousands of times a frame;
    # every hit is two debugger round trips, and a client stalled for seconds gets dropped by the
    # server. They are left out unless asked for: their callers are what the comparison is about.
    callers = {}
    if not opts.hot:
        with io.open(REF_JSONL, encoding='utf-8') as f:
            for line in f:
                r = json.loads(line)
                callers[r['addr'].lower()] = r['callers']
    for a, e in m.items():
        if e['status'] == 'stub':
            continue
        if spine is not None and a not in spine:
            continue
        if not opts.hot and callers.get(a, 0) > opts.max_callers:
            continue
        if names is not None and e['whoa'] not in names:
            continue
        if side == 'ref':
            picked[a] = e['whoa']
        else:
            picked[e['whoa']] = a
    return picked


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('side', choices=['ref', 'whoa'])
    ap.add_argument('--seconds', type=float, default=20.0, help='hard stop')
    ap.add_argument('--frames', type=int, default=5, help='stop after this many complete frames (0 = run to --seconds)')
    ap.add_argument('--max-hits', type=int, default=0, help='per address (0 = unlimited); throttled addresses stop being re-armed')
    ap.add_argument('--max-total', type=int, default=400000, help='hard cap on logged hits')
    ap.add_argument('--spine', action='store_true')
    ap.add_argument('--hot', action='store_true', help='also trace hot leaves (SMemAlloc, RsSet ...): slow, may stall the client')
    ap.add_argument('--max-callers', type=int, default=100, help='without --hot, skip functions the reference calls from more sites than this')
    ap.add_argument('--names', help='comma-separated whoa names to trace instead of the whole map')
    ap.add_argument('--pid', type=int)
    ap.add_argument('--out')
    opts = ap.parse_args()

    exe = REFERENCE_EXE if opts.side == 'ref' else WHOA_EXE
    pid = opts.pid or find_pid(exe)
    if not pid:
        sys.exit('%s is not running (by full path)' % exe)
    picked = choose(opts.side, opts)
    out_path = opts.out or os.path.join(DATA, 'trace-%s.jsonl' % opts.side)

    k32.DebugSetProcessKillOnExit(False)
    if not k32.DebugActiveProcess(pid):
        sys.exit('DebugActiveProcess(%d) failed: %d (run as the same user; is another debugger attached?)' % (pid, ctypes.get_last_error()))

    evt = crashstack.DEBUG_EVENT()
    process = None
    base = 0
    bps = {}        # address -> (original byte, label, key)
    hits = {}       # address -> count
    pending = {}    # thread id -> address to re-arm after the single step
    log = io.open(out_path, 'w', encoding='utf-8')
    wow64 = opts.side == 'ref'
    seq = 0
    t0 = None
    deadline = None
    planted = False

    def read_byte(addr):
        buf = ctypes.c_ubyte(0)
        n = ctypes.c_size_t(0)
        k32.ReadProcessMemory(process, ctypes.c_void_p(addr), ctypes.byref(buf), 1, ctypes.byref(n))
        return buf.value if n.value == 1 else None

    def write_byte(addr, value):
        buf = ctypes.c_ubyte(value)
        n = ctypes.c_size_t(0)
        old = wt.DWORD(0)
        k32.VirtualProtectEx(process, ctypes.c_void_p(addr), 1, 0x40, ctypes.byref(old))  # PAGE_EXECUTE_READWRITE
        ok = k32.WriteProcessMemory(process, ctypes.c_void_p(addr), ctypes.byref(buf), 1, ctypes.byref(n))
        k32.VirtualProtectEx(process, ctypes.c_void_p(addr), 1, old.value, ctypes.byref(old))
        k32.FlushInstructionCache(process, ctypes.c_void_p(addr), 1)
        return bool(ok) and n.value == 1

    def get_ctx(thread):
        if wow64:
            ctx = WOW64_CONTEXT()
            ctx.ContextFlags = WOW64_CONTEXT_CONTROL_INTEGER
            if not k32.Wow64GetThreadContext(thread, ctypes.byref(ctx)):
                return None
            return ctx
        ctx = crashstack.aligned_context()
        ctx.ContextFlags = crashstack.CONTEXT_FULL
        if not k32.GetThreadContext(thread, ctypes.byref(ctx)):
            return None
        return ctx

    def set_ctx(thread, ctx):
        if wow64:
            return k32.Wow64SetThreadContext(thread, ctypes.byref(ctx))
        return k32.SetThreadContext(thread, ctypes.byref(ctx))

    def pc(ctx):
        return ctx.Eip if wow64 else ctx.Rip

    def set_pc(ctx, v):
        if wow64:
            ctx.Eip = v
        else:
            ctx.Rip = v

    def plant_all():
        if opts.side == 'ref':
            targets = {base + int(a, 16) - REF_IMAGE_BASE: (label, a) for a, label in picked.items()}
            targets[base + int(FRAME_REF, 16) - REF_IMAGE_BASE] = (FRAME_WHOA, FRAME_REF)
        else:
            rvas = whoa_rvas(list(picked) + [FRAME_WHOA])
            targets = {}
            for name, rva in sorted(rvas.items(), key=lambda kv: kv[0] != FRAME_WHOA):
                targets.setdefault(base + rva, (name, picked.get(name, '')))  # the marker wins a folded address
            if FRAME_WHOA not in rvas:
                print('WARNING: %s not found in the PDB dump; frames cannot be cut' % FRAME_WHOA)
        ok = 0
        for addr, (label, key) in targets.items():
            orig = read_byte(addr)
            if orig is None:
                continue
            if write_byte(addr, 0xCC):
                bps[addr] = (orig, label, key)
                ok += 1
        print('planted %d/%d breakpoints (frame marker %s)' % (ok, len(targets), FRAME_WHOA))
        sys.stdout.flush()

    def restore_all():
        for addr, (orig, label, key) in bps.items():
            write_byte(addr, orig)
        bps.clear()

    frame_addr = None
    try:
        while True:
            if not k32.WaitForDebugEvent(ctypes.byref(evt), 500):
                if deadline and time.time() > deadline:
                    break
                continue
            code = evt.dwDebugEventCode
            status = crashstack.DBG_CONTINUE
            if code == crashstack.CREATE_PROCESS_DEBUG_EVENT:
                process = evt.u.CreateProcessInfo.hProcess
                base = evt.u.CreateProcessInfo.lpBaseOfImage or 0
                print('attached to pid %d, image base 0x%X' % (pid, base))
            elif code == crashstack.EXIT_PROCESS_DEBUG_EVENT:
                print('process exited')
                bps.clear()
                break
            elif code == crashstack.EXCEPTION_DEBUG_EVENT:
                rec = evt.u.Exception.ExceptionRecord
                xcode = rec.ExceptionCode
                addr = rec.ExceptionAddress or 0
                tid = evt.dwThreadId
                if xcode in BREAKPOINT_CODES and not planted:
                    # the attach break-in: the process is stopped, plant now
                    plant_all()
                    planted = True
                    t0 = time.time()
                    deadline = t0 + opts.seconds
                    if opts.side == 'ref':
                        frame_addr = base + int(FRAME_REF, 16) - REF_IMAGE_BASE
                    else:
                        frame_addr = next((a for a, b in bps.items() if b[1] == FRAME_WHOA), None)
                elif xcode in BREAKPOINT_CODES and addr in bps:
                    orig, label, key = bps[addr]
                    hits[addr] = hits.get(addr, 0) + 1
                    seq += 1
                    log.write('{"n":%d,"t":%.4f,"tid":%d,"key":"%s","name":"%s"}\n' % (seq, time.time() - t0, tid, key, label.replace('"', '\\"')))
                    thread = k32.OpenThread(crashstack.THREAD_ALL_ACCESS, False, tid)
                    ctx = get_ctx(thread)
                    write_byte(addr, orig)
                    if ctx is not None:
                        set_pc(ctx, addr)
                        if addr == frame_addr or not opts.max_hits or hits[addr] < opts.max_hits:
                            ctx.EFlags |= TF
                            pending[tid] = addr
                        else:
                            del bps[addr]  # throttled: stays restored
                        set_ctx(thread, ctx)
                    k32.CloseHandle(thread)
                    # enough frames (the marker hit that opens frame N+1 closes frame N), or too much
                    if (opts.frames and addr == frame_addr and hits[addr] > opts.frames) or seq >= opts.max_total:
                        deadline = 1.0  # any positive value in the past ends the loop
                elif xcode in SINGLE_STEP_CODES and tid in pending:
                    re_addr = pending.pop(tid)
                    if re_addr in bps:
                        write_byte(re_addr, 0xCC)
                elif xcode in BREAKPOINT_CODES | SINGLE_STEP_CODES:
                    pass  # somebody else's int3 / step
                else:
                    status = crashstack.DBG_EXCEPTION_NOT_HANDLED
            k32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, status)
            if deadline and time.time() > deadline:
                break
        # Time is up. Restore every breakpoint first, then keep pumping until no thread still has
        # the trap flag pending: a single-step exception with no debugger attached kills the
        # process, which is how the first version of this took whoa down on detach.
        if process:
            restore_all()
            drain_until = time.time() + 2.0
            while pending and time.time() < drain_until:
                if not k32.WaitForDebugEvent(ctypes.byref(evt), 200):
                    continue
                code = evt.dwDebugEventCode
                status = crashstack.DBG_CONTINUE
                if code == crashstack.EXCEPTION_DEBUG_EVENT:
                    xcode = evt.u.Exception.ExceptionRecord.ExceptionCode
                    if xcode in SINGLE_STEP_CODES:
                        pending.pop(evt.dwThreadId, None)
                    elif xcode not in BREAKPOINT_CODES:
                        status = crashstack.DBG_EXCEPTION_NOT_HANDLED
                elif code == crashstack.EXIT_PROCESS_DEBUG_EVENT:
                    pending.clear()
                k32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, status)
            if pending:
                print('WARNING: %d thread(s) still mid-step at detach' % len(pending))
    finally:
        k32.DebugActiveProcessStop(pid)
        log.close()
    if frame_addr is not None:
        print('frame marker at 0x%X: %d hits' % (frame_addr, hits.get(frame_addr, 0)))
    else:
        print('WARNING: frame marker was not planted')
    frames = sum(1 for a, c in hits.items() if a == frame_addr)
    print('%d hits over %d frames from %d functions -> %s' % (seq, hits.get(frame_addr, 0), len([a for a in hits if a != frame_addr]), os.path.relpath(out_path, ROOT)))


if __name__ == '__main__':
    main()
