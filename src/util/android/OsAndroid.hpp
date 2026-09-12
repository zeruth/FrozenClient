#ifndef UTIL_ANDROID_OS_ANDROID_HPP
#define UTIL_ANDROID_OS_ANDROID_HPP

#include <cstdint>

struct android_app;
struct ANativeWindow;

// The native activity state and its window, shared between the entry point, the event pump, and
// the graphics device

android_app* OsAndroidGetApp();

ANativeWindow* OsAndroidGetWindow();

void OsAndroidSetApp(android_app* app);

void OsAndroidSetWindow(ANativeWindow* window);

// The graphics device renders at the configured resolution and presents it letterboxed on the
// surface; touches arrive in surface pixels and are mapped into render pixels through this rect
// (x, y from the top left of the surface)
void OsAndroidSetPresentRect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t renderWidth, int32_t renderHeight);
void OsAndroidMapTouch(int32_t& x, int32_t& y);

#endif
