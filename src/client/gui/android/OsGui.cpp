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
