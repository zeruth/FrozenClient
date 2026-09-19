#!/usr/bin/env python
"""Catch the client's first-chance access violation and print a symbolized stack.

Windows has no cdb on this machine, and the event log only records the faulting offset, which
symbolizes to a single frame and has twice pointed at the wrong culprit. This attaches as a real
debugger, and on the access violation reports:

  * the faulting instruction and the address it tried to touch (so a null-plus-offset deref is
    obvious, and tells you which member was null),
  * the registers,
  * a stack walk: every qword on the thread stack that lands inside the module's code section,
    symbolized through llvm-symbolizer. Frame-pointer omission makes this heuristic, so some rows
    are stale returns rather than live frames -- read it top-down and ignore what does not fit.

Usage (launch under the debugger):

    python tools/crashstack.py

    python tools/crashstack.py --attach 1234      # attach to a client that is already running
    python tools/crashstack.py --timeout 300      # give up after N seconds

The client is launched exactly the way the normal run does: build/dist/bin/Frozen.exe with the
reference install as its working directory.
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import msvcrt
import os
import subprocess
import sys

k32 = ctypes.WinDLL('kernel32', use_last_error=True)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))
EXE = os.path.join(REPO, 'build', 'dist', 'bin', 'Frozen.exe')
CWD = os.path.join(REPO, '.reference', 'WOTLK 3.3.5a - Windows', 'WoW_WOTLK_3.3.5a')
SYMBOLIZER = r'C:\Program Files\LLVM\bin\llvm-symbolizer.exe'

DEBUG_ONLY_THIS_PROCESS = 0x00000002
EXCEPTION_DEBUG_EVENT = 1
CREATE_PROCESS_DEBUG_EVENT = 3
EXIT_PROCESS_DEBUG_EVENT = 5
LOAD_DLL_DEBUG_EVENT = 6
OUTPUT_DEBUG_STRING_EVENT = 8
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
EXCEPTION_ACCESS_VIOLATION = 0xC0000005
STATUS_BREAKPOINT = 0x80000003
THREAD_ALL_ACCESS = 0x1FFFFF
CONTEXT_AMD64 = 0x100000
CONTEXT_FULL = CONTEXT_AMD64 | 0x1 | 0x2 | 0x8


class EXCEPTION_RECORD(ctypes.Structure):
    pass


EXCEPTION_RECORD._fields_ = [
    ('ExceptionCode', wt.DWORD),
    ('ExceptionFlags', wt.DWORD),
    ('ExceptionRecord', ctypes.POINTER(EXCEPTION_RECORD)),
    ('ExceptionAddress', ctypes.c_void_p),
    ('NumberParameters', wt.DWORD),
    ('__unused', wt.DWORD),
    ('ExceptionInformation', ctypes.c_ulonglong * 15),
]


class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [('ExceptionRecord', EXCEPTION_RECORD), ('dwFirstChance', wt.DWORD)]


class CREATE_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ('hFile', wt.HANDLE), ('hProcess', wt.HANDLE), ('hThread', wt.HANDLE),
        ('lpBaseOfImage', ctypes.c_void_p), ('dwDebugInfoFileOffset', wt.DWORD),
        ('nDebugInfoSize', wt.DWORD), ('lpThreadLocalBase', ctypes.c_void_p),
        ('lpStartAddress', ctypes.c_void_p), ('lpImageName', ctypes.c_void_p), ('fUnicode', wt.WORD),
    ]


class EXIT_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [('dwExitCode', wt.DWORD)]


class DEBUG_EVENT_UNION(ctypes.Union):
    _fields_ = [
        ('Exception', EXCEPTION_DEBUG_INFO),
        ('CreateProcessInfo', CREATE_PROCESS_DEBUG_INFO),
        ('ExitProcess', EXIT_PROCESS_DEBUG_INFO),
        ('__pad', ctypes.c_byte * 256),
    ]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [
        ('dwDebugEventCode', wt.DWORD),
        ('dwProcessId', wt.DWORD),
        ('dwThreadId', wt.DWORD),
        ('u', DEBUG_EVENT_UNION),
    ]


class CONTEXT(ctypes.Structure):
    _fields_ = [
        ('P1Home', ctypes.c_ulonglong), ('P2Home', ctypes.c_ulonglong),
        ('P3Home', ctypes.c_ulonglong), ('P4Home', ctypes.c_ulonglong),
        ('P5Home', ctypes.c_ulonglong), ('P6Home', ctypes.c_ulonglong),
        ('ContextFlags', wt.DWORD), ('MxCsr', wt.DWORD),
        ('SegCs', wt.WORD), ('SegDs', wt.WORD), ('SegEs', wt.WORD),
        ('SegFs', wt.WORD), ('SegGs', wt.WORD), ('SegSs', wt.WORD),
        ('EFlags', wt.DWORD),
        ('Dr0', ctypes.c_ulonglong), ('Dr1', ctypes.c_ulonglong), ('Dr2', ctypes.c_ulonglong),
        ('Dr3', ctypes.c_ulonglong), ('Dr6', ctypes.c_ulonglong), ('Dr7', ctypes.c_ulonglong),
        ('Rax', ctypes.c_ulonglong), ('Rcx', ctypes.c_ulonglong), ('Rdx', ctypes.c_ulonglong),
        ('Rbx', ctypes.c_ulonglong), ('Rsp', ctypes.c_ulonglong), ('Rbp', ctypes.c_ulonglong),
        ('Rsi', ctypes.c_ulonglong), ('Rdi', ctypes.c_ulonglong),
        ('R8', ctypes.c_ulonglong), ('R9', ctypes.c_ulonglong), ('R10', ctypes.c_ulonglong),
        ('R11', ctypes.c_ulonglong), ('R12', ctypes.c_ulonglong), ('R13', ctypes.c_ulonglong),
        ('R14', ctypes.c_ulonglong), ('R15', ctypes.c_ulonglong),
        ('Rip', ctypes.c_ulonglong),
        ('FltSave', ctypes.c_byte * 512),
        ('VectorRegister', ctypes.c_byte * 416), ('VectorControl', ctypes.c_ulonglong),
        ('DebugControl', ctypes.c_ulonglong), ('LastBranchToRip', ctypes.c_ulonglong),
        ('LastBranchFromRip', ctypes.c_ulonglong), ('LastExceptionToRip', ctypes.c_ulonglong),
        ('LastExceptionFromRip', ctypes.c_ulonglong),
    ]


def aligned_context():
    """CONTEXT must be 16-byte aligned for GetThreadContext."""
    size = ctypes.sizeof(CONTEXT)
    buf = ctypes.create_string_buffer(size + 16)
    addr = (ctypes.addressof(buf) + 15) & ~15
    ctx = CONTEXT.from_address(addr)
    ctx._buf = buf  # keep the backing store alive
    return ctx


def read_memory(process, address, size):
    """Read up to `size` bytes, a page at a time.

    ReadProcessMemory is all-or-nothing: one unreadable page anywhere in the range fails the whole
    call and returns nothing. Asking for 16K of stack in a single call therefore returned empty
    every time the range happened to run off the committed end, which made the stack walk look like
    it had found no frames when it had not been given any bytes to look at.
    """
    out = bytearray()
    chunk = 0x1000

    while len(out) < size:
        want = min(chunk, size - len(out))
        buf = ctypes.create_string_buffer(want)
        read = ctypes.c_size_t(0)
        ok = k32.ReadProcessMemory(
            process, ctypes.c_void_p(address + len(out)), buf, want, ctypes.byref(read))

        if not ok or read.value == 0:
            break

        out += buf.raw[:read.value]

    return bytes(out)


def module_text_range(exe):
    """(virtual size) of the image, from the PE headers, so stack scanning can filter by range."""
    with open(exe, 'rb') as f:
        data = f.read(0x400)
    pe = int.from_bytes(data[0x3C:0x40], 'little')
    size_of_image = int.from_bytes(data[pe + 0x18 + 0x38:pe + 0x18 + 0x3C], 'little')
    return size_of_image


def symbolize(exe, rvas):
    if not os.path.exists(SYMBOLIZER) or not rvas:
        return {}
    stdin = '\n'.join('0x%x' % r for r in rvas) + '\n'
    out = subprocess.run(
        [SYMBOLIZER, '--obj=' + exe, '--relative-address', '--demangle', '--functions=linkage'],
        input=stdin, capture_output=True, text=True,
    ).stdout
    # llvm-symbolizer prints "func\nfile:line:col\n\n" per address
    result = {}
    blocks = out.split('\n\n')
    for rva, block in zip(rvas, blocks):
        lines = [l for l in block.strip().splitlines() if l.strip()]
        if len(lines) >= 2:
            result[rva] = (lines[0], lines[1])
        elif lines:
            result[rva] = (lines[0], '')
    return result


def module_for(process, address):
    """Name and offset of the loaded module containing an address, for faults outside the exe."""
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    needed = wt.DWORD()
    arr = (ctypes.c_void_p * 1024)()

    if not psapi.EnumProcessModules(process, ctypes.byref(arr), ctypes.sizeof(arr), ctypes.byref(needed)):
        return None

    class MODULEINFO(ctypes.Structure):
        _fields_ = [('lpBaseOfDll', ctypes.c_void_p), ('SizeOfImage', wt.DWORD), ('EntryPoint', ctypes.c_void_p)]

    count = min(needed.value // ctypes.sizeof(ctypes.c_void_p), 1024)

    for i in range(count):
        mod = arr[i]
        info = MODULEINFO()

        if not psapi.GetModuleInformation(process, ctypes.c_void_p(mod), ctypes.byref(info), ctypes.sizeof(info)):
            continue

        start = info.lpBaseOfDll or 0

        if start <= address < start + info.SizeOfImage:
            buf = ctypes.create_unicode_buffer(260)
            psapi.GetModuleFileNameExW(process, ctypes.c_void_p(mod), buf, 260)
            return (os.path.basename(buf.value), address - start)

    return None


def report(process, thread_id, rec, exe, base, image_size):
    code = rec.ExceptionCode
    addr = rec.ExceptionAddress or 0
    print('\n=== first-chance exception 0x%08X at 0x%016X ===' % (code, addr))

    # A fault outside Frozen.exe symbolizes to nothing useful; name the module instead, which at
    # least says whether it is the graphics driver, the CRT, or something the client called into.
    if not (base <= addr < base + image_size):
        owner = module_for(process, addr)

        if owner:
            print('faulting module: %s +0x%x  (NOT Frozen.exe)' % (owner[0], owner[1]))
        else:
            print('faulting address is outside Frozen.exe and no module claims it')

    if code == EXCEPTION_ACCESS_VIOLATION and rec.NumberParameters >= 2:
        kind = {0: 'read', 1: 'write', 8: 'execute'}.get(rec.ExceptionInformation[0], '?')
        target = rec.ExceptionInformation[1]
        print('access violation: %s of 0x%016X' % (kind, target))
        if target < 0x10000:
            print('  -> near-null: a null pointer dereferenced at member offset 0x%X' % target)

    thread = k32.OpenThread(THREAD_ALL_ACCESS, False, thread_id)
    if not thread:
        print('could not open thread %d' % thread_id)
        return

    ctx = aligned_context()
    ctx.ContextFlags = CONTEXT_FULL
    if not k32.GetThreadContext(thread, ctypes.byref(ctx)):
        print('GetThreadContext failed: %d' % ctypes.get_last_error())
        k32.CloseHandle(thread)
        return

    lo, hi = base, base + image_size
    print('\nregisters:')
    for name in ('Rip', 'Rsp', 'Rbp', 'Rax', 'Rcx', 'Rdx', 'Rbx', 'Rsi', 'Rdi', 'R8', 'R9'):
        v = getattr(ctx, name)
        tag = '  <- in Frozen.exe +0x%x' % (v - base) if lo <= v < hi else ''
        print('  %-3s 0x%016X%s' % (name, v, tag))

    # Walk the stack heuristically: collect qwords that point into the image
    stack = read_memory(process, ctx.Rsp, 16384)
    rvas = [ctx.Rip - base] if lo <= ctx.Rip < hi else []
    offsets = [None]

    foreign = []

    for i in range(0, len(stack) - 8, 8):
        v = int.from_bytes(stack[i:i + 8], 'little')
        if lo <= v < hi:
            rvas.append(v - base)
            offsets.append(i)
        elif v > 0x10000:
            foreign.append((i, v))

    syms = symbolize(exe, rvas)
    print('\nstack (heuristic; faulting frame first):')
    seen = set()
    shown = 0
    for rva, off in zip(rvas, offsets):
        func, loc = syms.get(rva, ('?', ''))
        key = (func, loc)
        if key in seen:
            continue
        seen.add(key)
        where = 'rip' if off is None else 'rsp+0x%04x' % off
        print('  %-12s +0x%-8x %s' % (where, rva, func))
        shown += 1

    # When the faulting thread has no frames from our own image at all -- which is what a worker
    # thread looks like -- the only way to see who called in is to attribute the other return
    # addresses to whatever module owns them.
    if shown <= 1 and foreign:
        print('\nno frames from Frozen.exe on this thread; modules on the stack instead:')
        seen_mod = set()

        for off, v in foreign:
            owner = module_for(process, v)

            if not owner:
                continue

            if owner[0] in seen_mod:
                continue

            seen_mod.add(owner[0])
            print('  rsp+0x%04x  %s +0x%x' % (off, owner[0], owner[1]))

            if len(seen_mod) >= 12:
                break

    k32.CloseHandle(thread)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', default=EXE)
    ap.add_argument('--cwd', default=CWD)
    ap.add_argument('--attach', type=int, help='attach to this pid instead of launching')
    ap.add_argument('--timeout', type=int, default=0, help='stop waiting after N seconds (0 = forever)')
    ap.add_argument('--log', help="file for the client's own stdout/stderr (default: beside the exe)")
    ap.add_argument('--max-first-chance', type=int, default=20,
                    help='how many handled exceptions to list before suppressing the rest')
    opts = ap.parse_args()

    image_size = module_text_range(opts.exe)

    if opts.attach:
        if not k32.DebugActiveProcess(opts.attach):
            raise SystemExit('DebugActiveProcess(%d) failed: %d' % (opts.attach, ctypes.get_last_error()))
        print('attached to pid %d' % opts.attach)
    else:
        # Launch under the debugger, inheriting the normal working directory. The child's stdout
        # and stderr are redirected to files so the client's own diagnostics survive: without
        # inheritable handles they go nowhere, which silently lost a run's worth of output.
        out_path = opts.log or os.path.join(os.path.dirname(os.path.abspath(opts.exe)), 'crashstack-client.log')
        handle = msvcrt.get_osfhandle(os.open(out_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC))
        k32.SetHandleInformation(handle, 1, 1)  # HANDLE_FLAG_INHERIT

        si = ctypes.create_string_buffer(104)
        ctypes.memset(si, 0, 104)
        ctypes.c_uint32.from_address(ctypes.addressof(si)).value = 104
        # STARTUPINFOW: dwFlags at 60, hStdInput/hStdOutput/hStdError at 80/88/96 (x64)
        ctypes.c_uint32.from_address(ctypes.addressof(si) + 60).value = 0x00000100  # STARTF_USESTDHANDLES
        ctypes.c_uint64.from_address(ctypes.addressof(si) + 88).value = handle
        ctypes.c_uint64.from_address(ctypes.addressof(si) + 96).value = handle

        print('client output -> %s' % out_path)

        pi = ctypes.create_string_buffer(24)
        ok = k32.CreateProcessW(
            ctypes.c_wchar_p(opts.exe), None, None, None, True,
            DEBUG_ONLY_THIS_PROCESS, None, ctypes.c_wchar_p(opts.cwd),
            ctypes.byref(si), ctypes.byref(pi),
        )
        if not ok:
            raise SystemExit('CreateProcessW failed: %d' % ctypes.get_last_error())
        pid = ctypes.c_uint32.from_address(ctypes.addressof(pi) + 16).value
        print('launched %s under the debugger, pid %d' % (os.path.basename(opts.exe), pid))

    print('waiting for an access violation; the client runs normally until then')
    sys.stdout.flush()

    evt = DEBUG_EVENT()
    process = None
    base = 0
    deadline = None

    if opts.timeout:
        import time
        deadline = time.time() + opts.timeout

    benign = 0

    while True:
        if not k32.WaitForDebugEvent(ctypes.byref(evt), 1000):
            if deadline:
                import time
                if time.time() > deadline:
                    print('timed out with no crash')
                    return
            continue

        code = evt.dwDebugEventCode
        status = DBG_CONTINUE

        if code == CREATE_PROCESS_DEBUG_EVENT:
            process = evt.u.CreateProcessInfo.hProcess
            base = evt.u.CreateProcessInfo.lpBaseOfImage or 0
            print('image base 0x%016X, size 0x%x' % (base, image_size))
        elif code == EXIT_PROCESS_DEBUG_EVENT:
            exit_code = evt.u.ExitProcess.dwExitCode
            print('\nprocess exited with code %d (0x%X)' % (exit_code, exit_code & 0xFFFFFFFF))

            if benign:
                print('%d first-chance exception(s) were handled along the way; '
                      'none of them killed it' % benign)

            return
        elif code == EXCEPTION_DEBUG_EVENT:
            rec = evt.u.Exception.ExceptionRecord
            first = evt.u.Exception.dwFirstChance

            if rec.ExceptionCode == STATUS_BREAKPOINT:
                status = DBG_CONTINUE  # the initial loader breakpoint
            elif rec.ExceptionCode == EXCEPTION_ACCESS_VIOLATION:
                if process is None:
                    process = k32.OpenProcess(0x1F0FFF, False, evt.dwProcessId)

                # A first-chance access violation is not news on its own: this client takes one in
                # the C runtime on every single run and carries on regardless, and stopping there
                # reported a red herring twice while the fault that actually killed the process went
                # unseen. Only a SECOND-chance exception is fatal, because it means nothing in the
                # process was willing to handle it. So note the first-chance ones in one line each
                # and hand them to the app's own handler, and save the full report for the one that
                # ends the run.
                if first:
                    benign += 1
                    module = module_for(process, rec.ExceptionAddress) or 'unknown module'

                    if benign <= opts.max_first_chance:
                        print('  first-chance AV #%d at 0x%016X in %s (continuing)'
                              % (benign, rec.ExceptionAddress, module))
                    elif benign == opts.max_first_chance + 1:
                        print('  ... further first-chance exceptions suppressed')

                    status = DBG_EXCEPTION_NOT_HANDLED
                else:
                    print('\n*** this one is fatal: nothing in the process handled it ***')
                    report(process, evt.dwThreadId, rec, opts.exe, base, image_size)
                    print('\nletting the process take the exception now')
                    k32.ContinueDebugEvent(
                        evt.dwProcessId, evt.dwThreadId, DBG_EXCEPTION_NOT_HANDLED)
                    return
            else:
                status = DBG_EXCEPTION_NOT_HANDLED

        k32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, status)


if __name__ == '__main__':
    main()
