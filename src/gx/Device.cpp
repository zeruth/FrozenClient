#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Gx.hpp"
#include <storm/String.hpp>

CGxDevice* g_theGxDevicePtr = nullptr;

CGxDevice* GxDevCreate(EGxApi api, int32_t (*windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam), const CGxFormat& format) {
    CGxDevice* device;

    #if defined(WHOA_SYSTEM_WIN)
        if (api == GxApi_OpenGl) {
            device = CGxDevice::NewOpenGl();
        } else if (api == GxApi_D3d9) {
            device = CGxDevice::NewD3d();
        } else if (api == GxApi_D3d9Ex) {
            device = CGxDevice::NewD3d9Ex();
        } else {
            // Error
        }
    #endif

    #if defined(WHOA_SYSTEM_MAC)
        if (api == GxApi_OpenGl) {
            device = CGxDevice::NewOpenGl();
        } else if (api == GxApi_GLL) {
            device = CGxDevice::NewGLL();
        } else {
            // Error
        }
    #endif

    #if defined(WHOA_SYSTEM_ANDROID)
        // Every API request lands on OpenGL ES
        device = CGxDevice::NewGLES();
    #endif

    g_theGxDevicePtr = device;

    if (g_theGxDevicePtr->DeviceCreate(windowProc, format)) {
        return g_theGxDevicePtr;
    } else {
        if (g_theGxDevicePtr) {
            delete g_theGxDevicePtr;
        }

        return nullptr;
    }
}

int32_t GxDevExists() {
    return g_theGxDevicePtr != nullptr;
}

EGxApi GxDevApi() {
    return g_theGxDevicePtr->m_api;
}

void* GxDevWindow() {
    return g_theGxDevicePtr->DeviceWindow();
}

// ref: FUN_00616af0
int32_t GxMasterEnable(EGxMasterEnables state) {
    return g_theGxDevicePtr->MasterEnable(state);
}

int32_t GxScreenShot(const char* path) {
    if (!g_theGxDevicePtr) {
        return 0;
    }

    return g_theGxDevicePtr->ScreenShot(path);
}

static char s_screenshotFormat[16] = "jpeg";
static int32_t s_screenshotQuality = 60;

// ref: FUN_004a8480
void ScreenshotSetFormat(const char* format) {
    SStrCopy(s_screenshotFormat, format, sizeof(s_screenshotFormat));
}

// ref: FUN_004a84a0
// 1..10 on the CVar becomes a JPEG quality of 50..100
void ScreenshotSetQuality(int32_t quality) {
    if (quality < 11) {
        if (quality == 0) {
            quality = 1;
        }
    } else {
        quality = 10;
    }

    s_screenshotQuality = static_cast<int32_t>(static_cast<float>(quality) * 5.5f + 45.0f + 0.5f);
}
