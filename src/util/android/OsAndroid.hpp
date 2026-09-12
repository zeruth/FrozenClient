#ifndef UTIL_ANDROID_OS_ANDROID_HPP
#define UTIL_ANDROID_OS_ANDROID_HPP

struct android_app;
struct ANativeWindow;

// The native activity state and its window, shared between the entry point, the event pump, and
// the graphics device

android_app* OsAndroidGetApp();

ANativeWindow* OsAndroidGetWindow();

void OsAndroidSetApp(android_app* app);

void OsAndroidSetWindow(ANativeWindow* window);

#endif
