#ifndef UI_GAME_PLAYER_NAME_HPP
#define UI_GAME_PLAYER_NAME_HPP

#include <cstdint>

void PlayerNameRenderWorldText();

void PlayerNameUpdateWorldText();

class CGObject_C;

// The name plate a CGObject_C carries (+0xb0). Only its flags word (+0x18) is declared: the
// PlayerName.cpp name plate system is not ported, so nothing creates one and every object's is
// null, which each accessor below answers for the way the reference does.
struct PLAYERNAMEDESC {
    uint32_t m_unk00[6];
    uint32_t m_flags;   // +0x18: 0x1 needs rebuilding, 0x8 highlighted
};

// ref: FUN_007e5130
void PlayerNameInvalidate(void* desc);

// ref: FUN_007e5150
bool PlayerNameIsHighlighted(void* desc);

// ref: FUN_007e5420
// The scale a marker over the object keeps its size at: from its height above its position.
float PlayerNameGetMarkerScale(CGObject_C* object);

#endif
