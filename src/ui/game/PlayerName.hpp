#ifndef UI_GAME_PLAYER_NAME_HPP
#define UI_GAME_PLAYER_NAME_HPP

#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CGObject_C;
class CGxString;
struct WORLDTEXTSTRING;

// The name over an object's head and the world text floating off it (PLAYERNAMEDESC, 0x34 bytes,
// held at CGObject_C +0xb0). The name draws from the model's draw callback, so it is depth-tested
// against the world; the texts draw with the world text after it.
struct PLAYERNAMEDESC {
    TSLink<PLAYERNAMEDESC> m_link;          // +0x00
    CGxString* m_string = nullptr;          // +0x08
    CImVector m_color = { 0, 0, 0, 0 };     // +0x0c
    WOWGUID m_guid = 0;                     // +0x10
    // +0x18: 0x1 the name needs making again, 0x2 its colour, 0x4 kept without a drawable model,
    // 0x8 the name is shown (which the quest marker's highlight follows).
    uint32_t m_flags = 0x3;
    uint32_t m_frame = 0;                   // +0x1c, the frame it was last updated in
    WORLDTEXTSTRING* m_texts[4] = {};       // +0x20
    float m_nameHeight = 0.0f;              // +0x30, how far the name raises what sits over it
};

// ref: FUN_007e64d0
void PlayerNameInitialize();

// ref: FUN_007e53a0
void PlayerNameShutdown();

// ref: FUN_007e6150
void PlayerNameRegisterCVars();

// ref: FUN_007e5f60
// A name plate for the object, or null when it has no model.
PLAYERNAMEDESC* PlayerNameCreate(const WOWGUID& guid);

// ref: FUN_007e5fd0
// A name plate for the object whether or not it has a drawable model.
PLAYERNAMEDESC* PlayerNameCreateAlways(const WOWGUID& guid);

// ref: FUN_007e6320
void PlayerNameDestroy(PLAYERNAMEDESC* desc);

// ref: FUN_007e6390
// The object's per-frame turn: whether its name shows, and the model's callback that draws it.
void PlayerNameUpdate(PLAYERNAMEDESC* desc);

// ref: FUN_007e6030
// Float `text` off the object in world text style `style` (see WorldTextStyle).
void PlayerNameAddWorldText(PLAYERNAMEDESC* desc, int32_t style, const char* text, const CImVector* color, const int32_t* icon);

// ref: FUN_007e5100
// Drop the object's texts of `style`, or all of them for 11.
void PlayerNameClearWorldText(PLAYERNAMEDESC* desc, int32_t style);

// ref: FUN_007e5120
void PlayerNameNewFrame();

// ref: FUN_007e5580
// Texts of objects nothing updated this frame are dropped.
void PlayerNameExpireWorldText();

// ref: FUN_007e5550
void PlayerNameInvalidateAllReactions();

// ref: FUN_007e6480
void PlayerNameUpdateWorldText();

// ref: FUN_007e5140
void PlayerNameRenderWorldText();

// ref: FUN_007e5130
void PlayerNameInvalidate(PLAYERNAMEDESC* desc);

// ref: FUN_007e50f0
void PlayerNameInvalidateReaction(PLAYERNAMEDESC* desc);

// ref: FUN_007e5150
bool PlayerNameIsHighlighted(PLAYERNAMEDESC* desc);

// ref: FUN_007e5420
// The scale a marker over the object keeps its size at: from its height above its position.
float PlayerNameGetMarkerScale(CGObject_C* object);

#endif
