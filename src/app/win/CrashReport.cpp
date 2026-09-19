#include "app/win/CrashReport.hpp"

#include <cstdint>
#include <cstdio>
#include <windows.h>

// Report a fatal exception from inside the process.
//
// The crash this exists for is timing-sensitive: attaching a debugger before it fires makes it stop
// happening, and Windows Error Reporting only yields a fault offset, with no registers and no
// stack. Configuring WER to write a full local dump needs administrator rights this account does
// not have. An in-process handler has neither problem: it costs nothing until the process is
// already dying, so it cannot perturb the timing that produces the fault.
//
// It deliberately does not use DbgHelp. StackWalk64 wants symbols loaded and can allocate, and the
// heap is exactly what may be broken by the time this runs. Scanning the thread stack for values
// that land inside our own image is cruder but allocates nothing, and the addresses it prints
// symbolize offline with llvm-symbolizer, which is how every other crash here has been read.

namespace {

uintptr_t s_imageBase = 0;
uintptr_t s_imageEnd = 0;

void DescribeModule() {
    auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));

    if (!module) {
        return;
    }

    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);

    s_imageBase = module;
    s_imageEnd = module + nt->OptionalHeader.SizeOfImage;
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info) {
    FILE* out = fopen("Logs\\crash.log", "w");

    if (!out) {
        out = stderr;
    }

    DescribeModule();

    auto record = info->ExceptionRecord;
    auto context = info->ContextRecord;
    auto address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);

    fprintf(out, "exception 0x%08lX at 0x%016llX\n",
            record->ExceptionCode, static_cast<unsigned long long>(address));

    if (s_imageBase && address >= s_imageBase && address < s_imageEnd) {
        fprintf(out, "  in Frozen.exe +0x%llX\n",
                static_cast<unsigned long long>(address - s_imageBase));
    } else {
        fprintf(out, "  OUTSIDE Frozen.exe\n");
    }

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const char* kind = record->ExceptionInformation[0] == 1 ? "write"
                         : record->ExceptionInformation[0] == 8 ? "execute" : "read";
        fprintf(out, "  %s of 0x%016llX\n", kind,
                static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }

#ifdef _M_X64
    fprintf(out, "\nregisters:\n");
    fprintf(out, "  rip %016llX rsp %016llX rbp %016llX\n",
            context->Rip, context->Rsp, context->Rbp);
    fprintf(out, "  rax %016llX rcx %016llX rdx %016llX rbx %016llX\n",
            context->Rax, context->Rcx, context->Rdx, context->Rbx);
    fprintf(out, "  rsi %016llX rdi %016llX r8  %016llX r9  %016llX\n",
            context->Rsi, context->Rdi, context->R8, context->R9);
    fprintf(out, "  r10 %016llX r11 %016llX r12 %016llX r13 %016llX\n",
            context->R10, context->R11, context->R12, context->R13);
    fprintf(out, "  r14 %016llX r15 %016llX\n", context->R14, context->R15);

    // Every qword on the stack that points into our own code, newest first. Frame-pointer omission
    // makes this a guess rather than a call chain, so read it top down and discard what does not
    // fit; stale return addresses from earlier calls are expected.
    fprintf(out, "\nstack candidates (symbolize the +0x offsets against Frozen.exe):\n");

    auto stack = reinterpret_cast<const uintptr_t*>(context->Rsp);
    int32_t shown = 0;

    for (int32_t i = 0; i < 4096 && shown < 48; i++) {
        uintptr_t value = 0;

        // The stack may be the thing that is damaged, so never fault while reporting a fault.
        if (IsBadReadPtr(&stack[i], sizeof(uintptr_t))) {
            break;
        }

        value = stack[i];

        if (s_imageBase && value >= s_imageBase && value < s_imageEnd) {
            fprintf(out, "  rsp+0x%04X  +0x%llX\n",
                    static_cast<unsigned>(i * sizeof(uintptr_t)),
                    static_cast<unsigned long long>(value - s_imageBase));
            shown++;
        }
    }
#endif

    fflush(out);

    if (out != stderr) {
        fclose(out);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void CrashReportInstall() {
    DescribeModule();
    SetUnhandledExceptionFilter(&OnUnhandledException);
}
