#ifndef CLIENT_GUI_OS_GUI_HPP
#define CLIENT_GUI_OS_GUI_HPP

#include <cstdint>

void* OsGuiGetWindow(int32_t type);

bool OsGuiIsModifierKeyDown(int32_t key);

int32_t OsGuiProcessMessage(void* message);

void OsGuiSetGxWindow(void* window);

// windowResizeLock CVar (reference DAT_00d41580)
void OsGuiSetWindowResizeLock(int32_t lock);

// windowResizeLock CVar (reference DAT_00d41580)
void OsGuiSetWindowResizeLock(int32_t lock);

// windowResizeLock CVar (reference DAT_00d41580)
void OsGuiSetWindowResizeLock(int32_t lock);

// windowResizeLock CVar (reference DAT_00d41580)
void OsGuiSetWindowResizeLock(int32_t lock);

#endif
