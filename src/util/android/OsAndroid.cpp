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

void OsAndroidSetWindow(ANativeWindow* window) {
    s_window = window;
}
