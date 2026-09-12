#ifndef GX_LOADING_SCREEN_HPP
#define GX_LOADING_SCREEN_HPP

#include <cstdint>

// The loading screen is a full screen layer showing the map's loading art, a progress bar, and a
// game tip. While it is up, keyboard and mouse events are swallowed and a keep alive is sent to
// the server every 30 seconds.

bool LoadingScreenDrawing();

void LoadingScreenFinish();

void LoadingScreenInitialize();

void LoadingScreenSetProgress(float progress);

void LoadingScreenSetProgress2(float progress);

void LoadingScreenSetProgress3(float progress);

void LoadingScreenSetPlayerReadyCallback(int32_t (*callback)());

void LoadingScreenSetTip(const char* tip);

void LoadingScreenStart(int32_t mapID, int32_t isLogin);

void LoadingScreenUpdate(int32_t force);

#endif
