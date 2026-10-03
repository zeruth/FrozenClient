#include <windowsx.h>
#include "event/Input.hpp"
#include "client/Gui.hpp"
#include "client/gui/OsGui.hpp"
#include <storm/Error.hpp>
#include <windows.h>

static RECT s_defaultWindowRect;
static int32_t s_savedResize;

// Where the mouse was last seen in normal mode (reference 0x00d413f8, 0x00d413fc, 0x00d413f4),
// so leaving relative mode can put it back.
static POINT s_savedMousePos;
static HWND s_savedMouseWindow;

// The point relative mode holds the cursor at (0x00d413ec, 0x00d413f0).
static POINT s_centerMousePos;

typedef BOOL(WINAPI* CURSORPOSFUNC)(LPPOINT);
typedef BOOL(WINAPI* SETCURSORPOSFUNC)(int, int);

// ref: FUN_00868c10
// The cursor in physical (unscaled) screen pixels where the system has it, else GetCursorPos.
static void GetPhysicalMouse(POINT* point) {
    static CURSORPOSFUNC s_get = nullptr;

    if (!s_get) {
        auto user32 = LoadLibraryA("user32.dll");

        if (user32) {
            s_get = reinterpret_cast<CURSORPOSFUNC>(GetProcAddress(user32, "GetPhysicalCursorPos"));

            if (!s_get) {
                s_get = reinterpret_cast<CURSORPOSFUNC>(GetProcAddress(user32, "GetCursorPos"));
            }

            FreeLibrary(user32);
        }

        if (!s_get) {
            return;
        }
    }

    s_get(point);
}

// ref: FUN_00868c70
static void SetPhysicalMouse(int32_t x, int32_t y) {
    static SETCURSORPOSFUNC s_set = nullptr;

    if (!s_set) {
        auto user32 = LoadLibraryA("user32.dll");

        if (user32) {
            s_set = reinterpret_cast<SETCURSORPOSFUNC>(GetProcAddress(user32, "SetPhysicalCursorPos"));

            if (!s_set) {
                s_set = reinterpret_cast<SETCURSORPOSFUNC>(GetProcAddress(user32, "SetCursorPos"));
            }

            FreeLibrary(user32);
        }

        if (!s_set) {
            return;
        }
    }

    s_set(x, y);
}

typedef BOOL(WINAPI* LOGICALTOPHYSICALFUNC)(HWND, LPPOINT);

static BOOL WINAPI LogicalToPhysicalIdentity(HWND, LPPOINT) {
    return TRUE;
}

// ref: FUN_00868ce0
static void LogicalToPhysical(HWND hwnd, POINT* point) {
    static LOGICALTOPHYSICALFUNC s_convert = nullptr;

    if (!s_convert) {
        auto user32 = LoadLibraryA("user32.dll");

        if (user32) {
            s_convert = reinterpret_cast<LOGICALTOPHYSICALFUNC>(GetProcAddress(user32, "LogicalToPhysicalPoint"));
            FreeLibrary(user32);
        }

        if (!s_convert) {
            s_convert = &LogicalToPhysicalIdentity;
        }
    }

    s_convert(hwnd, point);
}

// ref: FUN_00869db0
// Hold the cursor at the middle of the window. The reference halves the window rect's right and
// bottom edges -- the screen position of the middle only for a window at the screen's origin --
// and so does this.
void CenterMouse() {
    RECT rect;
    GetWindowRect(static_cast<HWND>(OsGuiGetWindow(0)), &rect);

    s_centerMousePos.y = rect.bottom / 2;
    s_centerMousePos.x = rect.right / 2;

    SetPhysicalMouse(s_centerMousePos.x, s_centerMousePos.y);
}

// ref: FUN_008695b0
// Put the cursor back where relative mode took it from.
void RestoreMouse() {
    POINT point = s_savedMousePos;
    ClientToScreen(s_savedMouseWindow, &point);
    LogicalToPhysical(s_savedMouseWindow, &point);
    SetPhysicalMouse(point.x, point.y);
}

// ref: FUN_00869600
void SaveMouse(POINT mousePos, HWND hwnd) {
    if (Input::s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        return;
    }

    s_savedMousePos = mousePos;
    s_savedMouseWindow = hwnd;
}

int32_t ConvertButton(uint32_t message, uintptr_t wparam, MOUSEBUTTON* button) {
    switch (message) {
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP: {
        *button = MOUSE_BUTTON_LEFT;
        return 1;
    }

    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP: {
        *button = MOUSE_BUTTON_RIGHT;
        return 1;
    }

    case WM_NCMBUTTONDOWN:
    case WM_NCMBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: {
        *button = MOUSE_BUTTON_MIDDLE;
        return 1;
    }

    case WM_NCXBUTTONDOWN:
    case WM_NCXBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP: {
        switch (GET_XBUTTON_WPARAM(wparam)) {
        case XBUTTON1: {
            *button = MOUSE_BUTTON_XBUTTON1;
            return 1;
        }
        case XBUTTON2: {
            *button = MOUSE_BUTTON_XBUTTON2;
            return 1;
        }
        default: {
            *button = MOUSE_BUTTON_NONE;
            return 0;
        }
        }
    }

    default: {
        *button = MOUSE_BUTTON_NONE;
        return 0;
    }
    }
}

int32_t ConvertKeyCode(uint32_t vkey, KEY* key) {
    if (vkey >= VK_F1 && vkey <= VK_F12) {
        *key = static_cast<KEY>(KEY_F1 + (vkey - VK_F1));
        return 1;
    }

    if (vkey >= 0x30 && vkey <= 0x39) {
        *key = static_cast<KEY>(KEY_0 + (vkey - 0x30));
        return 1;
    }

    switch (vkey) {
    case VK_BACK: {
        *key = KEY_BACKSPACE;
        return 1;
    }

    case VK_TAB: {
        *key = KEY_TAB;
        return 1;
    }

    case VK_RETURN: {
        *key = KEY_ENTER;
        return 1;
    }

    case VK_PAUSE: {
        *key = KEY_PAUSE;
        return 1;
    }

    case VK_CAPITAL: {
        *key = KEY_CAPSLOCK;
        return 1;
    }

    case VK_ESCAPE: {
        *key = KEY_ESCAPE;
        return 1;
    }

    case VK_SPACE: {
        *key = KEY_SPACE;
        return 1;
    }

    case VK_PRIOR: {
        *key = KEY_PAGEUP;
        return 1;
    }

    case VK_NEXT: {
        *key = KEY_PAGEDOWN;
        return 1;
    }

    case VK_END: {
        *key = KEY_END;
        return 1;
    }

    case VK_HOME: {
        *key = KEY_HOME;
        return 1;
    }

    case VK_LEFT: {
        *key = KEY_LEFT;
        return 1;
    }

    case VK_UP: {
        *key = KEY_UP;
        return 1;
    }

    case VK_RIGHT: {
        *key = KEY_RIGHT;
        return 1;
    }

    case VK_DOWN: {
        *key = KEY_DOWN;
        return 1;
    }

    case VK_SNAPSHOT: {
        *key = KEY_PRINTSCREEN;
        return 1;
    }

    case VK_INSERT: {
        *key = KEY_INSERT;
        return 1;
    }

    case VK_DELETE: {
        *key = KEY_DELETE;
        return 1;
    }

    case VK_NUMPAD0:
    case VK_NUMPAD1:
    case VK_NUMPAD2:
    case VK_NUMPAD3:
    case VK_NUMPAD4:
    case VK_NUMPAD5:
    case VK_NUMPAD6:
    case VK_NUMPAD7:
    case VK_NUMPAD8:
    case VK_NUMPAD9: {
        *key = static_cast<KEY>(KEY_NUMPAD0 + (vkey - VK_NUMPAD0));
        return 1;
    }

    case VK_MULTIPLY: {
        *key = KEY_NUMPAD_MULTIPLY;
        return 1;
    }

    case VK_ADD: {
        *key = KEY_NUMPAD_PLUS;
        return 1;
    }

    case VK_SUBTRACT: {
        *key = KEY_NUMPAD_MINUS;
        return 1;
    }

    case VK_DECIMAL: {
        *key = KEY_NUMPAD_DECIMAL;
        return 1;
    }

    case VK_DIVIDE: {
        *key = KEY_NUMPAD_DIVIDE;
        return 1;
    }

    case VK_NUMLOCK: {
        *key = KEY_NUMLOCK;
        return 1;
    }

    case VK_SCROLL: {
        *key = KEY_SCROLLLOCK;
        return 1;
    }

    case VK_LSHIFT: {
        *key = KEY_LSHIFT;
        return 1;
    }

    case VK_RSHIFT: {
        *key = KEY_RSHIFT;
        return 1;
    }

    case VK_LCONTROL: {
        *key = KEY_LCONTROL;
        return 1;
    }

    case VK_RCONTROL: {
        *key = KEY_RCONTROL;
        return 1;
    }

    case VK_LMENU: {
        *key = KEY_LALT;
        return 1;
    }

    case VK_RMENU: {
        *key = KEY_RALT;
        return 1;
    }

    default: {
        auto character = MapVirtualKey(vkey, MAPVK_VK_TO_CHAR);
        *key = static_cast<KEY>(character);
        if (character && character <= 0xFF) {
            return 1;
        } else {
            return 0;
        }
    }
    }
}

bool ProcessMouseEvent(MOUSEBUTTON button, uint32_t message, HWND hwnd, OSINPUT id) {
    POINT mousePos;

    // ref: FUN_008697e0: in relative mode a button reports where the cursor was held from.
    if (Input::s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        mousePos = s_savedMousePos;
    } else {
        GetCursorPos(&mousePos);
        ScreenToClient(hwnd, &mousePos);
    }

    OsQueuePut(id, button, mousePos.x, mousePos.y, 0);

    return message == WM_XBUTTONDOWN
        || message == WM_XBUTTONUP
        || message == WM_NCXBUTTONDOWN
        || message == WM_NCXBUTTONUP;
}

int32_t HandleMouseDown(uint32_t message, uintptr_t wparam, bool* xbutton, HWND hwnd) {
    MOUSEBUTTON button;
    if (!ConvertButton(message, wparam, &button)) {
        return 0;
    }

    if (Input::s_osButtonState == 0) {
        SetCapture(hwnd);
    }

    Input::s_osButtonState |= button;

    auto xb = ProcessMouseEvent(button, message, hwnd, OS_INPUT_MOUSE_DOWN);

    if (xbutton) {
        *xbutton = xb;
    }

    return 1;
}

int32_t HandleMouseUp(uint32_t message, uintptr_t wparam, bool* xbutton, HWND hwnd) {
    MOUSEBUTTON button;
    if (!ConvertButton(message, wparam, &button)) {
        return 0;
    }

    Input::s_osButtonState &= ~button;

    if (Input::s_osButtonState == 0) {
        // TODO
        ReleaseCapture();
        // TODO
    }

    auto xb = ProcessMouseEvent(button, message, hwnd, OS_INPUT_MOUSE_UP);

    if (xbutton) {
        *xbutton = xb;
    }

    return 1;
}

int32_t OsInputGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    *id = static_cast<OSINPUT>(-1);

    if (s_savedResize) {
        auto hwnd = static_cast<HWND>(OsGuiGetWindow(0));

        if (!IsIconic(hwnd)) {
            s_savedResize = 0;

            RECT windowRect;
            GetWindowRect(hwnd, &windowRect);

            auto style = GetWindowLong(hwnd, GWL_STYLE);
            RECT clientArea = { 0, 0, 0, 0 };
            AdjustWindowRectEx(&clientArea, style, false, 0);

            auto width = windowRect.right - clientArea.right - (windowRect.left - clientArea.left);
            auto height = windowRect.bottom - clientArea.bottom - (windowRect.top - clientArea.top);

            if (s_defaultWindowRect.right != width || s_defaultWindowRect.bottom != height) {
                s_defaultWindowRect.left = 0;
                s_defaultWindowRect.top = 0;
                s_defaultWindowRect.right = width;
                s_defaultWindowRect.bottom = height;

                *id = OS_INPUT_SIZE;
                *param0 = width;
                *param1 = height;
                *param2 = 0;
                *param3 = 0;

                return 1;
            }
        }
    }

    if (Input::s_queueTail != Input::s_queueHead) {
        OsQueueGet(id, param0, param1, param2, param3);
        return 1;
    }

    // TODO Sub8714B0(dwordB1C220);

    while (true) {
        MSG msg;
        auto peekResult = PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);

        if (Input::s_queueTail != Input::s_queueHead) {
            break;
        }

        if (!peekResult) {
            return 0;
        }

        if (!GetMessage(&msg, nullptr, 0, 0)) {
            *id = OS_INPUT_SHUTDOWN;
            break;
        }

        if (OsGuiProcessMessage(&msg)) {
            break;
        }

        if (Input::s_queueTail != Input::s_queueHead) {
            break;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (Input::s_queueTail != Input::s_queueHead) {
            break;
        }
    }

    OsQueueGet(id, param0, param1, param2, param3);
    return 1;
}

// ref: FUN_0086a0d0
// The mouse in the game window's client coordinates; outside relative mode it is also saved.
void OsInputGetMousePosition(int32_t* x, int32_t* y) {
    auto hwnd = static_cast<HWND>(OsGuiGetWindow(0));

    POINT mousePos;
    GetCursorPos(&mousePos);
    ScreenToClient(hwnd, &mousePos);

    if (Input::s_osMouseMode != OS_MOUSE_MODE_RELATIVE) {
        SaveMouse(mousePos, hwnd);
    }

    if (x) {
        *x = mousePos.x;
    }

    if (y) {
        *y = mousePos.y;
    }
}

void OsInputSetMouseMode(OS_MOUSE_MODE mode) {
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(mode < OS_MOUSE_MODES);
    STORM_VALIDATE_END_VOID;

    if (Input::s_osMouseMode == mode) {
        return;
    }

    if (mode == OS_MOUSE_MODE_NORMAL) {
        Input::s_osMouseMode = mode;
        RestoreMouse();
    } else if (mode == OS_MOUSE_MODE_RELATIVE) {
        Input::s_osMouseMode = mode;
        CenterMouse();
    }
}

int32_t OsWindowProc(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam) {
    auto hwnd = static_cast<HWND>(window);

    // TODO

    switch (message) {
    // TODO handle remaining message types

    case WM_SIZE:
    case WM_DISPLAYCHANGE: {
        s_savedResize = lparam;
        break;
    }

    case WM_ACTIVATE: {
        auto isMinimized = IsIconic(hwnd);
        auto isActive = wparam != WA_INACTIVE;
        Input::s_windowFocused = isActive && !isMinimized;

        // TODO capture

        // TODO mouse speed

        OsQueuePut(OS_INPUT_FOCUS, Input::s_windowFocused != 0, 0, 0, 0);

        break;
    }

    case WM_CLOSE: {
        OsQueuePut(OS_INPUT_CLOSE, 0, 0, 0, 0);
        return 0;
    }

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: {
        auto keyDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;

        if (wparam == VK_SHIFT) {
            // TODO
        } else if (wparam == VK_CONTROL) {
            // TODO
        } else if (wparam == VK_MENU) {
            // TODO
        }

        KEY key;
        if (ConvertKeyCode(wparam, &key)) {
            OsQueuePut(keyDown ? OS_INPUT_KEY_DOWN : OS_INPUT_KEY_UP, key, LOWORD(lparam), 0, 0);

            // Alt + F4
            if (key == KEY_F4 && OsGuiIsModifierKeyDown(2)) {
                break;
            }

            return 0;
        }

        break;
    }

    case WM_CHAR: {
        if (wparam < 32) {
            break;
        }

        uint32_t character = wparam;

        if (wparam >= 128) {
            // TODO
        }

        OsQueuePut(OS_INPUT_CHAR, character, LOWORD(lparam), 0, 0);

        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN: {
        bool xbutton;
        if (HandleMouseDown(message, wparam, &xbutton, hwnd)) {
            // Normally, a processed button down message should return 0
            // In the case of xbuttons, a processed button down message should return 1
            // See: https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-xbuttondown
            return xbutton ? 1 : 0;
        }

        break;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP: {
        if (message == WM_LBUTTONUP) {
            // TODO
        }

        bool xbutton;
        if (HandleMouseUp(message, wparam, &xbutton, hwnd)) {
            // Normally, a processed button down message should return 0
            // In the case of xbuttons, a processed button down message should return 1
            // See: https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-xbuttondown
            return xbutton ? 1 : 0;
        }

        break;
    }

    case WM_MOUSEWHEEL: {

        // The wheel delta arrives in multiples of WHEEL_DELTA (120); one notch is one step. The
        // position is in screen coordinates and must be converted to client
        int32_t notches = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
        POINT wheelPos = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };

        // In relative mode the wheel, like the buttons, reports the held-from position.
        if (Input::s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
            wheelPos = s_savedMousePos;
            ClientToScreen(hwnd, &wheelPos);
        }

        ScreenToClient(hwnd, &wheelPos);
        OsQueuePut(OS_INPUT_MOUSE_WHEEL, notches, wheelPos.x, wheelPos.y, 0);
        break;
    }

    case WM_MOUSEMOVE: {
        // TODO

        // ref: FUN_0086a210 (WM_MOUSEMOVE): relative mode reports how far the cursor got from the
        // middle and puts it back there.
        if (Input::s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
            POINT mousePos;
            GetPhysicalMouse(&mousePos);

            if (mousePos.x != s_centerMousePos.x || mousePos.y != s_centerMousePos.y) {
                OsQueuePut(OS_INPUT_MOUSE_MOVE_RELATIVE, 0, mousePos.x - s_centerMousePos.x,
                           mousePos.y - s_centerMousePos.y, 0);
                CenterMouse();
            }
        } else {
            POINT mousePos;
            GetCursorPos(&mousePos);
            ScreenToClient(hwnd, &mousePos);
            OsQueuePut(OS_INPUT_MOUSE_MOVE, 0, mousePos.x, mousePos.y, 0);
            SaveMouse(mousePos, hwnd);
        }

        break;
    }

    default:
        break;
    }

    // TODO

    return DefWindowProc(static_cast<HWND>(window), message, wparam, lparam);
}
