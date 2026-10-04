#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include "gx/font/FreeType.hpp"
#include <common/Time.hpp>

int main(int argc, char* argv[]) {
    // Global initialization
    FreeTypeInitialize();

    // The client starts the OS clock long before anything else (InitializeGlobal); creating a
    // graphics device probes the CPU, which reads it, so a test that builds a device would
    // otherwise depend on some earlier test having started it.
    OsTimeStartup(OS_TIMING_BEST_AVAILABLE);

    int result = Catch::Session().run( argc, argv );

    // Global cleanup
    // TODO

    return result;
}
