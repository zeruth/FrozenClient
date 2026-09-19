#include "app/win/CrashReport.hpp"
#include "client/Client.hpp"
#include <cstdio>
#include <windows.h>

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    // Before anything else, so a fault during startup is reported too.
    CrashReportInstall();

    // TODO

    CommonMain();

    // TODO

    // Leave without running static destructors.
    //
    // The client keeps its caches and lists in namespace-scope Storm containers (the component
    // texture cache, the console command and cvar hashes, the font and shader caches, the async
    // queues, the connection list, ...). Their nodes live in Storm/ObjectAlloc heaps, and static
    // destruction order across translation units is unspecified, so on the way out a container's
    // destructor can walk nodes whose heap has already gone -- which faulted on exit, each fix
    // only exposing the next container in the chain.
    //
    // Everything that must persist (cvars, logs) is written by CommonMain's own shutdown before it
    // returns, so there is nothing left to release: the process is about to die and the OS
    // reclaims the memory. Flush the C streams, since ExitProcess will not.
    fflush(nullptr);
    ExitProcess(0);

    return 0;
}
