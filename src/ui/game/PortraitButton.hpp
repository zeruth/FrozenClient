#ifndef UI_GAME_PORTRAIT_BUTTON_HPP
#define UI_GAME_PORTRAIT_BUTTON_HPP

#include <cstdint>

// Whether the back buffer keeps the alpha it is cleared to, which is what lets a portrait be
// rendered with its circular mask in place (reference DAT_00c5cdfc).
extern int32_t g_portraitAlphaSupported;

int32_t PortraitTestBackBufferAlpha();

void PortraitButtonInvalidateAll();

#endif
