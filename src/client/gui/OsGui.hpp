#ifndef CLIENT_GUI_OS_GUI_HPP
#define CLIENT_GUI_OS_GUI_HPP

#include <cstdint>

void* OsGuiGetWindow(int32_t type);

bool OsGuiIsModifierKeyDown(int32_t key);

int32_t OsGuiProcessMessage(void* message);

void OsGuiSetGxWindow(void* window);

// windowResizeLock CVar (reference DAT_00d41580)
void OsGuiSetWindowResizeLock(int32_t lock);

// mouseSpeed CVar: the OS pointer speed setting (reference DAT_00d41548), reported in tenths
float OsGuiGetMouseSpeed();

void OsGuiSetMouseSpeed(float speed);

#endif
