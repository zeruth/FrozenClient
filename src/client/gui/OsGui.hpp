#ifndef CLIENT_GUI_OS_GUI_HPP
#define CLIENT_GUI_OS_GUI_HPP

#include <cstdint>

void* OsGuiGetWindow(int32_t type);

bool OsGuiIsModifierKeyDown(int32_t key);

int32_t OsGuiProcessMessage(void* message);

// Shows a message: type 0 OK, 1 OK/Cancel, 2 Yes/No, 3 Yes/No/Cancel. Answers 0 for OK or
// Yes, 1 for No, 2 for anything else.
int32_t OsGuiMessageBox(void* parent, int32_t type, const char* text, const char* caption);

void OsGuiSetGxWindow(void* window);

void OsGuiSetWindowTitle(void* window, const char* title);

// windowResizeLock CVar (reference DAT_00d41580)
void OsGuiSetWindowResizeLock(int32_t lock);

// mouseSpeed CVar: the OS pointer speed setting (reference DAT_00d41548), reported in tenths
float OsGuiGetMouseSpeed();

void OsGuiSetMouseSpeed(float speed);

#endif
