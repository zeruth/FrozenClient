#include "event/Input.hpp"
#include "console/CVar.hpp"
#include "util/android/OsAndroid.hpp"
#include <android_native_app_glue.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_window.h>

// Window size last reported to the client
static int32_t s_windowWidth = 0;
static int32_t s_windowHeight = 0;

static void QueueWindowSize() {
    auto window = OsAndroidGetWindow();

    if (!window) {
        return;
    }

    auto width = ANativeWindow_getWidth(window);
    auto height = ANativeWindow_getHeight(window);

    if (width == s_windowWidth && height == s_windowHeight) {
        return;
    }

    s_windowWidth = width;
    s_windowHeight = height;

    OsQueuePut(OS_INPUT_SIZE, width, height, 0, 0);
}

static int32_t ConvertKeyCode(int32_t keyCode, KEY* key) {
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        *key = static_cast<KEY>(KEY_A + (keyCode - AKEYCODE_A));
        return 1;
    }

    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        *key = static_cast<KEY>(KEY_0 + (keyCode - AKEYCODE_0));
        return 1;
    }

    switch (keyCode) {
    case AKEYCODE_BACK:
    case AKEYCODE_ESCAPE:
        *key = KEY_ESCAPE;
        return 1;

    case AKEYCODE_ENTER:
    case AKEYCODE_NUMPAD_ENTER:
        *key = KEY_ENTER;
        return 1;

    case AKEYCODE_DEL:
        *key = KEY_BACKSPACE;
        return 1;

    case AKEYCODE_SPACE:
        *key = KEY_SPACE;
        return 1;

    default:
        return 0;
    }
}

// Runs the activity's pending commands and input; a negative timeout waits for the next one
void OsAndroidPollEvents(int32_t timeoutMs) {
    auto app = OsAndroidGetApp();

    if (!app) {
        return;
    }

    int32_t events;
    android_poll_source* source;

    while (ALooper_pollOnce(timeoutMs, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
        if (source) {
            source->process(app, source);
        }

        // Only the first iteration may block
        timeoutMs = 0;

        if (app->destroyRequested) {
            break;
        }
    }
}

// Activity lifecycle, translated into the window events the client expects (the Windows window
// procedure's WM_SIZE, WM_ACTIVATE, and WM_CLOSE)
void OsAndroidOnAppCommand(android_app* app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        OsAndroidSetWindow(app->window);
        QueueWindowSize();
        break;

    case APP_CMD_TERM_WINDOW:
        OsAndroidSetWindow(nullptr);
        break;

    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        QueueWindowSize();
        break;

    case APP_CMD_GAINED_FOCUS:
        OsQueuePut(OS_INPUT_FOCUS, 1, 0, 0, 0);
        break;

    case APP_CMD_LOST_FOCUS:
        OsQueuePut(OS_INPUT_FOCUS, 0, 0, 0, 0);
        break;

    case APP_CMD_PAUSE:
    case APP_CMD_STOP:
        // The activity may be killed without ever returning, so settings are written here
        if (CVar::m_initialized) {
            CVar::Save();
        }
        break;

    case APP_CMD_DESTROY:
        OsQueuePut(OS_INPUT_CLOSE, 0, 0, 0, 0);
        break;

    default:
        break;
    }
}

// Touches drive the left mouse button; keys map onto the client's key codes
int32_t OsAndroidOnInputEvent(android_app* app, AInputEvent* event) {
    auto type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        auto action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        auto x = static_cast<int32_t>(AMotionEvent_getX(event, 0));
        auto y = static_cast<int32_t>(AMotionEvent_getY(event, 0));

        // Surface pixels to render pixels (the render target may be letterboxed on the surface)
        OsAndroidMapTouch(x, y);

        switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            // Move the cursor under the finger first so hover state matches the press
            OsQueuePut(OS_INPUT_MOUSE_MOVE, 0, x, y, 0);
            OsQueuePut(OS_INPUT_MOUSE_DOWN, MOUSE_BUTTON_LEFT, x, y, 0);
            return 1;

        case AMOTION_EVENT_ACTION_MOVE:
        case AMOTION_EVENT_ACTION_HOVER_MOVE:
            OsQueuePut(OS_INPUT_MOUSE_MOVE, 0, x, y, 0);
            return 1;

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            OsQueuePut(OS_INPUT_MOUSE_UP, MOUSE_BUTTON_LEFT, x, y, 0);
            return 1;

        default:
            return 0;
        }
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        auto keyCode = AKeyEvent_getKeyCode(event);
        auto keyAction = AKeyEvent_getAction(event);
        auto repeat = AKeyEvent_getRepeatCount(event);

        KEY key;

        if (!ConvertKeyCode(keyCode, &key)) {
            return 0;
        }

        if (keyAction == AKEY_EVENT_ACTION_DOWN) {
            OsQueuePut(OS_INPUT_KEY_DOWN, key, repeat, 0, 0);

            // TODO proper text input; letters, digits, and space produce their characters
            if (key >= KEY_A && key <= KEY_A + 25) {
                OsQueuePut(OS_INPUT_CHAR, 'a' + (key - KEY_A), repeat, 0, 0);
            } else if (key >= KEY_0 && key <= KEY_0 + 9) {
                OsQueuePut(OS_INPUT_CHAR, '0' + (key - KEY_0), repeat, 0, 0);
            } else if (key == KEY_SPACE) {
                OsQueuePut(OS_INPUT_CHAR, ' ', repeat, 0, 0);
            }
        } else if (keyAction == AKEY_EVENT_ACTION_UP) {
            OsQueuePut(OS_INPUT_KEY_UP, key, repeat, 0, 0);
        }

        return 1;
    }

    return 0;
}

int32_t OsInputGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    *id = static_cast<OSINPUT>(-1);

    if (Input::s_queueTail == Input::s_queueHead) {
        // Nothing queued: give the activity a chance to deliver something
        OsAndroidPollEvents(0);
    }

    if (Input::s_queueTail == Input::s_queueHead) {
        return 0;
    }

    OsQueueGet(id, param0, param1, param2, param3);

    return 1;
}

void OsInputSetMouseMode(OS_MOUSE_MODE mode) {
    // TODO relative mode for camera dragging
}

int32_t OsWindowProc(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam) {
    return 0;
}
