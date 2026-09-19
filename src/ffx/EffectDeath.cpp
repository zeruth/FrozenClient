#include "ffx/EffectDeath.hpp"
#include "console/CVar.hpp"
#include "console/Console.hpp"
#include <cstdlib>

// ref: FUN_008c02a0
bool FFXDeathCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (atol(value)) {
        ConsoleWrite("enabled", DEFAULT_COLOR);

        return true;
    }

    ConsoleWrite("disabled", DEFAULT_COLOR);

    return true;
}

EffectDeath::EffectDeath() {
    // TODO

    CVar::Register(
        "ffxDeath",
        "full screen death effect",
        0x1,
        "1",
        &FFXDeathCallback,
        GRAPHICS
    );
}
