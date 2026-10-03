#ifndef UI_GAME_PORTRAIT_BUTTON_HPP
#define UI_GAME_PORTRAIT_BUTTON_HPP

#include "util/GUID.hpp"
#include <cstdint>

// Whether the back buffer keeps the alpha it is cleared to, which is what lets a portrait be
// rendered with its circular mask in place (reference DAT_00c5cdfc).
extern int32_t g_portraitAlphaSupported;

int32_t PortraitTestBackBufferAlpha();

void PortraitButtonInvalidateAll();

// ref: FUN_00618110 (reached through FUN_00512b50)
// A unit's portrait (flag 2) or model (flag 1) changed: the cached portrait is marked stale and
// UNIT_PORTRAIT_UPDATE / UNIT_MODEL_CHANGED are queued for it.
void PortraitRefresh(const WOWGUID& guid, uint32_t flags);

#endif
