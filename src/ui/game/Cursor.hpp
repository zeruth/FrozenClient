#ifndef UI_GAME_CURSOR_HPP
#define UI_GAME_CURSOR_HPP

#include <cstdint>

// The game cursor (the reference's Cursor.cpp): which cursor is showing, and the 32x32 image it
// is drawn from. Not the cursor's contents -- what is being dragged lives on CGGameUI.

int32_t CopyCursorImage(uint32_t* dest, const uint32_t* const* image, int32_t width, int32_t height);

int32_t GetCursorMode();

void SetCursorMode(int32_t mode);

struct MipBits;

// The named cursors, loaded once from Interface\Cursor\<name>.blp.
void CursorInitialize();

// Push the current cursor image to the device.
void CursorUpdate();

// The index of a named cursor, or 0.
int32_t CursorGetIndex(const char* name);

void CursorSet(int32_t index);

void CursorReset();

// Show the image at `path` as the cursor. True when it was already showing or loaded.
bool CursorSetCustom(const char* path);

// Show an item's icon under the Item cursor, from a texture path.
void CursorSetItemTexture(const char* path);

void CursorClearItem();

void CursorDestroyItemImage();

// Copy the first level of `image` into the device cursor and show it.
void CursorSetImage(MipBits* image);

#endif
