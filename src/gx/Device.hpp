#ifndef GX_DEVICE_HPP
#define GX_DEVICE_HPP

#include "gx/CGxDevice.hpp"
#include "gx/Types.hpp"
#include <cstdint>

class CGxFormat;

extern CGxDevice* g_theGxDevicePtr;

CGxDevice* GxDevCreate(EGxApi api, int32_t (*windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam), const CGxFormat& format);

int32_t GxDevExists();

EGxApi GxDevApi(void);

void* GxDevWindow();

int32_t GxMasterEnable(EGxMasterEnables state);

void GxLogOpen();

void GxLogClose();

void GxCaptureRequest();

void GxCaptureGet(uint32_t& width, uint32_t& height, const uint32_t*& bits);

// screenshotFormat / screenshotQuality CVars (reference DAT_00ac1b88 / DAT_00ac1b98)
void ScreenshotSetFormat(const char* format);

void ScreenshotSetQuality(int32_t quality);

#endif
