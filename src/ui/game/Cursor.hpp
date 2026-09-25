#ifndef UI_GAME_CURSOR_HPP
#define UI_GAME_CURSOR_HPP

#include <cstdint>

// The game cursor (the reference's Cursor.cpp): which cursor is showing, and the 32x32 image it
// is drawn from. Not the cursor's contents -- what is being dragged lives on CGGameUI.

int32_t CopyCursorImage(uint32_t* dest, const uint32_t* const* image, int32_t width, int32_t height);

int32_t GetCursorMode();

void SetCursorMode(int32_t mode);

#endif
