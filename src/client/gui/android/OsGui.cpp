#include "client/gui/OsGui.hpp"
#include "util/android/OsAndroid.hpp"

void* OsGuiGetWindow(int32_t type) {
    return OsAndroidGetWindow();
}

bool OsGuiIsModifierKeyDown(int32_t key) {
    // TODO
    return false;
}

int32_t OsGuiProcessMessage(void* message) {
    return 0;
}

void OsGuiSetGxWindow(void* window) {
    // The window comes from the activity, nothing to do
}

static int32_t s_windowResizeLock;

// ref: FUN_00869620
void OsGuiSetWindowResizeLock(int32_t lock) {
    s_windowResizeLock = lock;
}
