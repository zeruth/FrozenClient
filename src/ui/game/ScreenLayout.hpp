#ifndef UI_GAME_SCREEN_LAYOUT_HPP
#define UI_GAME_SCREEN_LAYOUT_HPP

#include <cstdint>

class CRect;

// The screen rects things floating over the world have already taken this frame, per list, so the
// next one can be moved clear of them (ScriptEvents.cpp, 0x00615050..0x00615cd0). The rects are in
// DDC with minY holding the TOP edge and maxY the bottom one, the way every caller fills them.
// World text uses list 1; the world frame empties the lists around its render.

// ref: FUN_00615890
void ScreenLayoutClear(uint32_t list);

// ref: FUN_00615cd0
// Move `rect` to the nearest place on the screen that list `list` has free, and take it.
void ScreenLayoutPlace(uint32_t list, CRect& rect);

#endif
