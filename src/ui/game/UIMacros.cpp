#include "ui/game/UIMacros.hpp"

// The macro in each of the interface's 72 macro slots. Written by the macro load and create paths,
// which are not ported, so every slot is empty.
static int32_t s_macroSlots[72];                // ref: DAT_00beae20

// The slot holding the macro, or -1.
// ref: FUN_00564ab0
uint32_t MacroGetSlotByID(int32_t macroID) {
    for (uint32_t i = 0; i < 72; i++) {
        if (s_macroSlots[i] == macroID) {
            return i;
        }
    }

    return 0xFFFFFFFF;
}
