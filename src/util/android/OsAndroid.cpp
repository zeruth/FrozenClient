#include "util/android/OsAndroid.hpp"

static android_app* s_app = nullptr;
static ANativeWindow* s_window = nullptr;

android_app* OsAndroidGetApp() {
    return s_app;
}

ANativeWindow* OsAndroidGetWindow() {
    return s_window;
}

void OsAndroidSetApp(android_app* app) {
    s_app = app;
}

static uint32_t s_windowGeneration = 0;

void OsAndroidSetWindow(ANativeWindow* window) {
    s_window = window;

    if (window) {
        s_windowGeneration++;
    }
}

uint32_t OsAndroidGetWindowGeneration() {
    return s_windowGeneration;
}

static int32_t s_presentX = 0;
static int32_t s_presentY = 0;
static int32_t s_presentWidth = 0;
static int32_t s_presentHeight = 0;
static int32_t s_renderWidth = 0;
static int32_t s_renderHeight = 0;

void OsAndroidSetPresentRect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t renderWidth, int32_t renderHeight) {
    s_presentX = x;
    s_presentY = y;
    s_presentWidth = width;
    s_presentHeight = height;
    s_renderWidth = renderWidth;
    s_renderHeight = renderHeight;
}

void OsAndroidMapTouch(int32_t& x, int32_t& y) {
    if (s_presentWidth <= 0 || s_presentHeight <= 0 || s_renderWidth <= 0 || s_renderHeight <= 0) {
        return;
    }

    int64_t mappedX = (static_cast<int64_t>(x - s_presentX) * s_renderWidth) / s_presentWidth;
    int64_t mappedY = (static_cast<int64_t>(y - s_presentY) * s_renderHeight) / s_presentHeight;

    if (mappedX < 0) {
        mappedX = 0;
    } else if (mappedX >= s_renderWidth) {
        mappedX = s_renderWidth - 1;
    }

    if (mappedY < 0) {
        mappedY = 0;
    } else if (mappedY >= s_renderHeight) {
        mappedY = s_renderHeight - 1;
    }

    x = static_cast<int32_t>(mappedX);
    y = static_cast<int32_t>(mappedY);
}
