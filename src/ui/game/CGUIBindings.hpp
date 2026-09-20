#ifndef UI_GAME_C_G_U_I_BINDINGS_HPP
#define UI_GAME_C_G_U_I_BINDINGS_HPP

#include "ui/FrameXML.hpp"

#include <common/string/RCString.hpp>
#include <cstdint>
#include <storm/array/TSGrowableArray.hpp>

class CStatus;

// One <ModifiedClick action="..." default="..."/> out of Bindings.xml.
struct MODIFIED_CLICK {
    RCString action;

    // What Bindings.xml declared, kept so a reset can go back to it.
    RCString defaultModifier;

    // What is in force now. Starts as the default; SetModifiedClick replaces it.
    RCString modifier;
};

// The modifier expression a string like "SHIFT", "CTRL-BUTTON1" or "ALT-SHIFT" parses to. The bit
// for each modifier is the same number as its KEY constant -- KEY_LSHIFT is 0 and bit 0 is left
// shift -- which is not a coincidence: the reference indexes its key query with the bit position.
enum MODIFIER_FLAG {
    MODIFIER_LSHIFT = 0x01,
    MODIFIER_RSHIFT = 0x02,
    MODIFIER_SHIFT  = 0x03,
    MODIFIER_LCTRL  = 0x04,
    MODIFIER_RCTRL  = 0x08,
    MODIFIER_CTRL   = 0x0c,
    MODIFIER_LALT   = 0x10,
    MODIFIER_RALT   = 0x20,
    MODIFIER_ALT    = 0x30,
};

class CGUIBindings {
    public:
        // Static variables
        // The declared modified clicks, in the order Bindings.xml lists them, which is the order
        // GetModifiedClickAction indexes.
        static TSGrowableArray<MODIFIED_CLICK> s_modifiedClicks;

        // Static functions
        static void LoadModifiedClicks(const char* filePath, CStatus* status);
        static MODIFIED_CLICK* GetModifiedClick(const char* action);
        static uint32_t ParseModifierFlags(const char** text);
        static bool ModifierFlagsHeld(uint32_t flags);
        static bool IsModifiedClick(const char* action);
        static bool AnyModifierHeld();
        static bool ParseBinding(const char* text, uint32_t* flags, uint32_t* button);
};

#endif
