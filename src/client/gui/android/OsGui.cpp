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

// The pointer speed the OS reports, in the 1..20 steps Windows uses. TODO the reference reads the
// initial value from the system at startup; 10 is the Windows default (1.0 as a CVar).
static int32_t s_mouseSpeed = 10;

// ref: FUN_00869630
float OsGuiGetMouseSpeed() {
    return s_mouseSpeed * 0.1f;
}

// ref: FUN_0086a070
// TODO the reference's float-to-int (FUN_0088b9c0) may carry a scale; and on Windows it pushes the
// value to SystemParametersInfo(SPI_SETMOUSESPEED) when its OS-mouse flag (DAT_00b1c220) is set.
void OsGuiSetMouseSpeed(float speed) {
    s_mouseSpeed = static_cast<int32_t>(speed);
}
