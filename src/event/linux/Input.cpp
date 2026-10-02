#include "event/Input.hpp"

int32_t OsInputGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    // TODO
    return 0;
}

// Only the Windows software cursor asks; there is no pointer to report here.
void OsInputGetMousePosition(int32_t* x, int32_t* y) {
    if (x) {
        *x = -1;
    }

    if (y) {
        *y = -1;
    }
}

void OsInputSetMouseMode(OS_MOUSE_MODE mode) {
    // TODO
}

int32_t OsWindowProc(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam) {
    return 0;
}
