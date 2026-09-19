#include "client/gui/OsGui.hpp"

void* OsGuiGetWindow(int32_t type) {
    return nullptr;
}

bool OsGuiIsModifierKeyDown(int32_t key) {
    // TODO
    return false;
}

int32_t OsGuiProcessMessage(void* message) {
    return 0;
}

void OsGuiSetGxWindow(void* window) {
    // TODO
}

static int32_t s_windowResizeLock;

// ref: FUN_00869620
void OsGuiSetWindowResizeLock(int32_t lock) {
    s_windowResizeLock = lock;
}
