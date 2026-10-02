#include "gx/CGxDevice.hpp"
#include "gx/Buffer.hpp"
#include <cmath>
#include <common/Time.hpp>
#include "gx/CGxMonitorMode.hpp"
#include "gx/Gx.hpp"
#include "gx/Shader.hpp"
#include "gx/texture/CGxTex.hpp"
#include "util/SFile.hpp"
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

#if defined(WHOA_SYSTEM_WIN)
    #include "gx/d3d/CGxDeviceD3d.hpp"
    #include "gx/win/Display.hpp"
#endif

#if defined(WHOA_SYSTEM_MAC)
    #include "gx/gll/CGxDeviceGLL.hpp"
    #include "gx/mac/Display.hpp"
    #include <ApplicationServices/ApplicationServices.h>
    #include <OpenGL/OpenGL.h>
#endif

#if defined(WHOA_SYSTEM_ANDROID)
    #include "gx/gles/CGxDeviceGLES.hpp"
    #include "util/android/OsAndroid.hpp"
    #include <android/native_window.h>
#endif

uint32_t CGxDevice::s_alphaRef[] = {
    0,      // GxBlend_Opaque
    224,    // GxBlend_AlphaKey
    1,      // GxBlend_Alpha
    1,      // GxBlend_Add
    1,      // GxBlend_Mod
    1,      // GxBlend_Mod2x
    1,      // GxBlend_ModAdd
    0,      // GxBlend_InvSrcAlphaAdd
    0,      // GxBlend_InvSrcAlphaOpaque
    0,      // GxBlend_SrcAlphaOpaque
    0,      // GxBlend_NoAlphaAdd
    0       // GxBlend_ConstantAlpha
};

C3Vector CGxDevice::s_pointScaleIdentity = { 1.0f, 0.0f, 0.0f };

uint32_t CGxDevice::s_primVtxAdjust[] = {
    0,      // GxPrim_Points
    0,      // GxPrim_Lines
    1,      // GxPrim_LineStrip
    0,      // GxPrim_Triangles
    2,      // GxPrim_TriangleStrip
    2,      // GxPrim_TriangleFan
};

uint32_t CGxDevice::s_primVtxDiv[] = {
    1,      // GxPrim_Points
    2,      // GxPrim_Lines
    1,      // GxPrim_LineStrip
    3,      // GxPrim_Triangles
    1,      // GxPrim_TriangleStrip
    1,      // GxPrim_TriangleFan
};

ShaderConstants CGxDevice::s_shadowConstants[2];

uint32_t CGxDevice::s_streamPoolSize[] = {
    0x2C0000,   // GxPoolTarget_Vertex
    0x40000     // GxPoolTarget_Index
};

uint32_t CGxDevice::s_texFormatBitDepth[] = {
    0,      // GxTex_Unknown
    32,     // GxTex_Abgr8888
    32,     // GxTex_Argb8888
    16,     // GxTex_Argb4444
    16,     // GxTex_Argb1555
    16,     // GxTex_Rgb565
    4,      // GxTex_Dxt1
    8,      // GxTex_Dxt3
    8,      // GxTex_Dxt5
    16,     // GxTex_Uv88
    32,     // GxTex_Gr1616F
    32,     // GxTex_R32F
    32      // GxTex_D24X8
};

uint32_t CGxDevice::s_texFormatBlockShift[] = {
    0,      // GxTex_Unknown
    0,      // GxTex_Abgr8888
    0,      // GxTex_Argb8888
    0,      // GxTex_Argb4444
    0,      // GxTex_Argb1555
    0,      // GxTex_Rgb565
    2,      // GxTex_Dxt1
    2,      // GxTex_Dxt3
    2,      // GxTex_Dxt5
    0,      // GxTex_Uv88
    0,      // GxTex_Gr1616F
    0,      // GxTex_R32F
    0       // GxTex_D24X8
};

// ref: FUN_006ac130
// Both tables are the reference's own, dumped from 0x00ad906c (the shifts) and 0x00ad9038 (the
// bytes) -- and the bytes one matches s_texFormatBytesPerBlock entry for entry, all thirteen,
// which is what says the two are indexed the same way.
//
// The clamps are only reached for a compressed format, and 4 is its block width: a mip narrower
// than one block still occupies one. The cube-map arm exists because a cube map is stored as its
// six faces side by side, so `width == height * 6`, and the row is that wide.
uint32_t CGxDevice::TexFormatStride(EGxTexFormat format, uint32_t width, uint32_t height) {
    if (format >= GxTex_Dxt1 && format <= GxTex_Dxt5) {
        if (width == height * 6) {
            if (height < 5) {
                height = 4;
            }

            return (height * 6 >> CGxDevice::s_texFormatBlockShift[format])
                 * CGxDevice::s_texFormatBytesPerBlock[format];
        }

        if (width < 5) {
            width = 4;
        }
    }

    return (width >> CGxDevice::s_texFormatBlockShift[format])
         * CGxDevice::s_texFormatBytesPerBlock[format];
}

uint32_t CGxDevice::s_texFormatBytesPerBlock[] = {
    0,      // GxTex_Unknown
    4,      // GxTex_Abgr8888
    4,      // GxTex_Argb8888
    2,      // GxTex_Argb4444
    2,      // GxTex_Argb1555
    2,      // GxTex_Rgb565
    8,      // GxTex_Dxt1
    16,     // GxTex_Dxt3
    16,     // GxTex_Dxt5
    2,      // GxTex_Uv88
    4,      // GxTex_Gr1616F
    4,      // GxTex_R32F
    4       // GxTex_D24X8
};

int32_t CGxDevice::AdapterFormats(EGxApi api, TSGrowableArray<CGxFormat>& adapterFormats) {
    adapterFormats.SetCount(0);
    adapterFormats.Reserve(256, 1);

#if defined(WHOA_SYSTEM_WIN)

    if (api == GxApi_OpenGl) {
        CGxDevice::OpenGlAdapterFormats(adapterFormats);
    } else if (api == GxApi_D3d9) {
        CGxDevice::D3dAdapterFormats(adapterFormats);
    } else if (api == GxApi_D3d9Ex) {
        CGxDevice::D3d9ExAdapterFormats(adapterFormats);
    }

#elif defined(WHOA_SYSTEM_MAC)

    if (api == GxApi_OpenGl) {
        CGxDevice::OpenGlAdapterFormats(adapterFormats);
    } else if (api == GxApi_GLL) {
        CGxDevice::GLLAdapterFormats(adapterFormats);
    }

#elif defined(WHOA_SYSTEM_ANDROID)

    // The activity window is the only mode there is
    auto window = OsAndroidGetWindow();

    if (window) {
        CGxFormat format = {};
        format.hwTnL = true;
        format.window = 1;
        format.maximize = 0;
        format.depthFormat = CGxFormat::Fmt_Ds248;
        format.size.x = ANativeWindow_getWidth(window);
        format.size.y = ANativeWindow_getHeight(window);
        format.multisampleCount = 1;
        format.colorFormat = CGxFormat::Fmt_Argb8888;
        format.refreshRate = 60;
        format.vsync = 1;

        *adapterFormats.New() = format;
    }

#elif defined(WHOA_SYSTEM_LINUX)

    // TODO

#endif

    return adapterFormats.Count() != 0;
}

// ref: FUN_0068a4c0
int32_t CGxDevice::AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes) {
#if defined(WHOA_SYSTEM_WIN)

    return CGxDevice::WinAdapterMonitorModes(monitorModes);

#elif defined(WHOA_SYSTEM_MAC)

    // TODO conditional check on s_cvGxApi
    auto api = GxApi_OpenGl; // avoiding GLLAdapterMonitorModes for now

    if (api == GxApi_OpenGl) {
        return CGxDevice::MacAdapterMonitorModes(monitorModes);
    }

    if (api == GxApi_GLL) {
        return CGxDevice::GLLAdapterMonitorModes(monitorModes);
    }

#elif defined(WHOA_SYSTEM_ANDROID)

    monitorModes.SetCount(0);

    auto window = OsAndroidGetWindow();

    if (!window) {
        return 0;
    }

    // Common sizes the surface can hold, then the surface itself
    static const C2iVector s_commonSizes[] = {
        { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 }, { 1280, 800 }, { 1280, 960 },
        { 1280, 1024 }, { 1366, 768 }, { 1440, 900 }, { 1600, 900 }, { 1600, 1200 }, { 1680, 1050 },
        { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 }, { 2560, 1600 },
    };

    C2iVector native = { ANativeWindow_getWidth(window), ANativeWindow_getHeight(window) };

    for (auto& size : s_commonSizes) {
        if (size.x <= native.x && size.y <= native.y && (size.x != native.x || size.y != native.y)) {
            auto mode = monitorModes.New();
            mode->size = size;
            mode->bpp = 32;
            mode->refreshRate = 60;
        }
    }

    auto mode = monitorModes.New();
    mode->size = native;
    mode->bpp = 32;
    mode->refreshRate = 60;

    return 1;

#else

    // TODO

    return 0;

#endif
}

#if defined(WHOA_SYSTEM_WIN)
void CGxDevice::D3dAdapterFormats(TSGrowableArray<CGxFormat>& adapterFormats) {
    // TODO
}

void CGxDevice::D3d9ExAdapterFormats(TSGrowableArray<CGxFormat>& adapterFormats) {
    // TODO
}
#endif

#if defined(WHOA_SYSTEM_MAC)
void CGxDevice::GLLAdapterFormats(TSGrowableArray<CGxFormat>& adapterFormats) {
    // TODO
}

int32_t CGxDevice::GLLAdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes) {
    // TODO
    return 0;
}
#endif

// ref: FUN_00684c20
// STUB, recorded: the reference writes to Logs\gx.log through Storm's SLog API (opened by
// FUN_00683670, written by FUN_00684ba0, capped at 2 MB), which frozen's Storm does not have.
void CGxDevice::Log(const char* format, ...) {
}

// ref: FUN_00684d10
void CGxDevice::Log(const CGxFormat& format) {
    static const char* s_formatNames[] = {
        "Rgb565", "ArgbX888", "Argb8888", "Argb2101010", "Ds160", "Ds24X", "Ds248", "Ds320"
    };

    if (format.window) {
        CGxDevice::Log("\tFormat: %d x %d Window, %s, multisample %d", format.size.x, format.size.y,
            s_formatNames[format.depthFormat], format.multisampleCount);
        return;
    }

    CGxDevice::Log("\tFormat %d x %d @ %d Fullscreen, %s, %s, multisample %d", format.size.x,
        format.size.y, format.refreshRate, s_formatNames[format.colorFormat],
        s_formatNames[format.depthFormat], format.multisampleCount);
}

#if defined(WHOA_SYSTEM_MAC)
int32_t CGxDevice::MacAdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes) {
    monitorModes.SetCount(0);

    // TODO this should use display global var, which is set somewhere in the Mac OpenGL init path
    auto displayModes = CGDisplayAvailableModes(CGMainDisplayID());

    if (displayModes) {
        auto count = CFArrayGetCount(displayModes);

        for (int32_t i = 0; i < count; i++) {
            auto displayMode = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(displayModes, i));

            if (displayMode) {
                ConvertDisplayMode(*monitorModes.New(), displayMode);
            }
        }
    }

    qsort(monitorModes.Ptr(), monitorModes.Count(), sizeof(CGxMonitorMode), CGxMonitorModeSort);

    return monitorModes.Count() != 0;
}
#endif

#if defined(WHOA_SYSTEM_WIN)
// ref: FUN_00689ef0
CGxDevice* CGxDevice::NewD3d() {
    auto m = SMemAlloc(sizeof(CGxDeviceD3d), __FILE__, __LINE__, 0x0);
    return new (m) CGxDeviceD3d();
}

CGxDevice* CGxDevice::NewD3d9Ex() {
    // TODO
    return nullptr;
}
#endif

#if defined(WHOA_SYSTEM_MAC)
CGxDevice* CGxDevice::NewGLL() {
    auto m = SMemAlloc(sizeof(CGxDeviceGLL), __FILE__, __LINE__, 0x0);
    return new (m) CGxDeviceGLL();
}
#endif

#if defined(WHOA_SYSTEM_ANDROID)
CGxDevice* CGxDevice::NewGLES() {
    auto m = SMemAlloc(sizeof(CGxDeviceGLES), __FILE__, __LINE__, 0x0);
    return new (m) CGxDeviceGLES();
}
#endif

CGxDevice* CGxDevice::NewOpenGl() {
    // TODO
    // auto m = SMemAlloc(sizeof(CGxDeviceOpenGl), __FILE__, __LINE__, 0x0);
    // return new (m) CGxDeviceOpenGl();

    return nullptr;
}

void CGxDevice::OpenGlAdapterFormats(TSGrowableArray<CGxFormat>& adapterFormats) {
#if defined(WHOA_SYSTEM_WIN)

    // TODO

#elif defined(WHOA_SYSTEM_MAC)

    uint32_t maxMultisampleCount = 1;

    // TODO this should use display global var, which is set somewhere in the Mac OpenGL init path
    auto displayMask = CGDisplayIDToOpenGLDisplayMask(CGMainDisplayID());

    CGLRendererInfoObj rend;
    GLint nrend;

    if (CGLQueryRendererInfo(displayMask, &rend, &nrend) == kCGLNoError) {
        for (int32_t i = 0; i < nrend; i++) {
            GLint accelerated;

            if (CGLDescribeRenderer(rend, i, kCGLRPAccelerated, &accelerated) == kCGLNoError) {
                if (accelerated) {
                    GLint maxSamples;

                    if (CGLDescribeRenderer(rend, i, kCGLRPMaxSamples, &maxSamples) == kCGLNoError) {
                        if (maxSamples > maxMultisampleCount) {
                            maxMultisampleCount = maxSamples;
                        }
                    }
                }
            }
        }
    }

    if (maxMultisampleCount > 16) {
        maxMultisampleCount = 16;
    }

    // TODO this should use display global var, which is set somewhere in the Mac OpenGL init path
    auto displayModes = CGDisplayAvailableModes(CGMainDisplayID());

    if (displayModes) {
        auto count = CFArrayGetCount(displayModes);

        for (int32_t i = 0; i < count; i++) {
            auto displayMode = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(displayModes, i));

            int32_t width = 0;
            auto widthValue = static_cast<CFNumberRef>(CFDictionaryGetValue(displayMode, kCGDisplayWidth));
            CFNumberGetValue(widthValue, kCFNumberIntType, &width);

            int32_t height = 0;
            auto heightValue = static_cast<CFNumberRef>(CFDictionaryGetValue(displayMode, kCGDisplayHeight));
            CFNumberGetValue(heightValue, kCFNumberIntType, &height);

            uint32_t bitsPerPixel = 0;
            auto bppValue = static_cast<CFNumberRef>(CFDictionaryGetValue(displayMode, kCGDisplayBitsPerPixel));
            CFNumberGetValue(bppValue, kCFNumberIntType, &bitsPerPixel);

            if (width >= 640 && height >= 480 && bitsPerPixel >= 16) {
                uint32_t refreshRate = 0;
                auto refreshRateValue = static_cast<CFNumberRef>(CFDictionaryGetValue(displayMode, kCGDisplayRefreshRate));
                CFNumberGetValue(refreshRateValue, kCFNumberIntType, &refreshRate);

                uint32_t multisampleCount = 1;

                while (multisampleCount <= maxMultisampleCount) {
                    CGxFormat format = {};

                    format.size = { width, height };
                    format.colorFormat = bitsPerPixel == 32 ? CGxFormat::Fmt_ArgbX888 : CGxFormat::Fmt_Rgb565;
                    format.depthFormat = CGxFormat::Fmt_Ds24X;
                    format.refreshRate = refreshRate;
                    format.multisampleCount = multisampleCount;

                    *adapterFormats.New() = format;

                    if (bitsPerPixel == 16) {
                        break;
                    }

                    // 1 -> 2 -> 4 -> 6 and so on
                    if (multisampleCount == 1) {
                        multisampleCount = 2;
                    } else {
                        multisampleCount += 2;
                    }
                }
            }
        }
    }

    qsort(adapterFormats.Ptr(), adapterFormats.Count(), sizeof(CGxFormat), AdapterFormatSort);

#endif
}

// ref: FUN_00682f40
uint32_t CGxDevice::PrimCalcCount(EGxPrim primType, uint32_t count) {
    auto div = CGxDevice::s_primVtxDiv[primType];
    if (div != 1) {
        count /= div;
    }

    return count - CGxDevice::s_primVtxAdjust[primType];
}

#if defined(WHOA_SYSTEM_WIN)
int32_t CGxDevice::WinAdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes) {
    monitorModes.SetCount(0);

    DISPLAY_DEVICE device;
    if (!FindDisplayDevice(&device, DISPLAY_DEVICE_PRIMARY_DEVICE)) {
        return 0;
    }

    DEVMODE deviceMode;
    deviceMode.dmSize = sizeof(DEVMODE);

    DWORD deviceNum = 0;
    while (EnumDisplaySettings(device.DeviceName, deviceNum++, &deviceMode)) {
        if (
            deviceMode.dmPelsWidth >= 640
            && deviceMode.dmPelsHeight >= 480
            && deviceMode.dmBitsPerPel >= 16
        ) {
            auto monitorMode = monitorModes.New();

            monitorMode->size.x = deviceMode.dmPelsWidth;
            monitorMode->size.y = deviceMode.dmPelsHeight;
            monitorMode->bpp = deviceMode.dmBitsPerPel;
            monitorMode->refreshRate = deviceMode.dmDisplayFrequency;
        }
    }

    qsort(monitorModes.Ptr(), monitorModes.Count(), sizeof(CGxMonitorMode), CGxMonitorModeSort);

    return monitorModes.Count() != 0;
}
#endif

CGxDevice::CGxDevice() {
    // TODO
    // - implement rest of constructor

    this->IRsInit();

    // Set default viewport
    this->m_viewport.x = { 0.0f, 1.0f };
    this->m_viewport.y = { 0.0f, 1.0f };
    this->m_viewport.z = { 0.0f, 1.0f };

    // Turn on all master enables
    this->m_appMasterEnables = 511;
    this->m_hwMasterEnables = 511;

    this->ShaderConstantsClear();

    this->m_gammaRamp.Build(1.0f);
    this->m_desktopGammaRamp.Build(1.0f);
}

// ref: FUN_00684070
// Truncated, as the reference's fistp under a chop control word is.
void CGxGammaRamp::Build(float gamma) {
    for (uint32_t i = 0; i < 256; i++) {
        auto value = static_cast<uint16_t>(static_cast<int32_t>(powf(static_cast<float>(i) * (1.0f / 255.0f), gamma) * 65535.0f));
        this->red[i] = value;
        this->green[i] = value;
        this->blue[i] = value;
    }
}

// ref: FUN_00684170
void CGxDevice::DeviceSetGamma(float gamma) {
    this->m_gammaRamp.Build(gamma);
}

// ref: FUN_00684190
void CGxDevice::DeviceSetGammaRamp(const CGxGammaRamp& ramp) {
    if (&this->m_gammaRamp != &ramp) {
        this->m_gammaRamp = ramp;
    }
}

// ref: FUN_006841b0
void CGxDevice::DeviceDesktopGammaRamp(CGxGammaRamp& ramp) {
    if (&ramp != &this->m_desktopGammaRamp) {
        ramp = this->m_desktopGammaRamp;
    }
}

// ref: FUN_00684fe0
// Override 8 is the largest point size, which also decides whether point sprites and point
// scaling are on.
void CGxDevice::DeviceOverride(int32_t which, uint32_t value) {
    CGxDevice::Log("CGxDevice::DeviceOverride(): %d set to %d", which, value);

    if (which == 8) {
        this->m_caps.m_pointSprites = value > 1;
        this->m_caps.m_pointScale = value > 1;
        this->m_caps.m_maxPointSize = static_cast<float>(value);
    }
}

// ref: FUN_006855c0
void CGxDevice::PrimBegin(EGxPrim primType) {
    this->m_primImmType = primType;
    this->m_primImmActive = 1;
    this->m_primImmDirty = 0;
    this->m_primImmIndices.SetCount(0);
    this->m_primImmPositions.SetCount(0);

    for (uint32_t tmu = 0; tmu < static_cast<uint32_t>(this->m_caps.m_numTmus); tmu++) {
        this->m_primImmTexCoords[tmu].SetCount(0);
    }

    this->m_primImmNormals.SetCount(0);
    this->m_primImmColors.SetCount(0);
}

// ref: FUN_006845b0
void CGxDevice::PrimEnd() {
    this->m_primImmActive = 0;

    if (!this->m_primImmPositions.Count()) {
        return;
    }

    GxPrimLockVertexPtrs(this->m_primImmPositions.Count(), this->m_primImmPositions.Ptr(), sizeof(C3Vector),
        this->m_primImmNormals.Ptr(), sizeof(C3Vector), this->m_primImmColors.Ptr(), sizeof(CImVector),
        nullptr, 0, this->m_primImmTexCoords[0].Ptr(), sizeof(C2Vector), this->m_primImmTexCoords[1].Ptr(),
        sizeof(C2Vector));

    GxPrimLockIndexPtr(this->m_primImmType, this->m_primImmIndices.Count(), this->m_primImmIndices.Ptr());
    GxPrimDrawLockedElements();
}

// ref: FUN_00685640
// Appends the vertex with the current texcoords, normal and colour. A batch that has reached the
// most primitives one 16-bit-indexed draw can carry is drawn and restarted, the dirty mask kept.
void CGxDevice::PrimVertex(const C3Vector& position) {
    this->m_primImmPosition = position;
    this->m_primImmDirty |= 0x1;

    *this->m_primImmIndices.New() = static_cast<uint16_t>(this->m_primImmPositions.Count());
    *this->m_primImmPositions.New() = this->m_primImmPosition;

    for (uint32_t tmu = 0; tmu < static_cast<uint32_t>(this->m_caps.m_numTmus); tmu++) {
        *this->m_primImmTexCoords[tmu].New() = this->m_primImmTexCoord[tmu];
    }

    *this->m_primImmNormals.New() = this->m_primImmNormal;
    *this->m_primImmColors.New() = this->m_primImmColor;

    auto prim = this->m_primImmType;
    uint32_t limit = CGxDevice::s_primVtxDiv[prim] == 1 ? 0x20000 : 0x20000 / CGxDevice::s_primVtxDiv[prim];

    if (CGxDevice::PrimCalcCount(prim, this->m_primImmIndices.Count()) == limit - CGxDevice::s_primVtxAdjust[prim]) {
        auto dirty = this->m_primImmDirty;
        this->PrimEnd();
        this->PrimBegin(this->m_primImmType);
        this->m_primImmDirty = dirty;
    }
}

// ref: FUN_00682fa0
void CGxDevice::PrimTexCoord(uint32_t tmu, const C2Vector& texCoord) {
    this->m_primImmTexCoord[tmu] = texCoord;
    this->m_primImmDirty |= 2 << tmu;
}

// ref: FUN_00682f70
void CGxDevice::PrimNormal(const C3Vector& normal) {
    this->m_primImmNormal = normal;
    this->m_primImmDirty |= 0x200;
}

// ref: FUN_00684590
void CGxDevice::PrimColor(const CImVector& color) {
    this->m_primImmColor = color;
    this->m_primImmDirty |= 0x400;
}

// ref: FUN_006877c0
void CGxDevice::QueryCreate(CGxQuery*& query, uint32_t type) {
    auto m = SMemAlloc(sizeof(CGxQuery), __FILE__, __LINE__, 0x0);
    query = m ? new (m) CGxQuery() : nullptr;

    if (query) {
        query->m_type = type;
    }

    this->m_queryList.LinkToTail(query);
}

// ref: FUN_006879f0
void CGxDevice::QueryDestroy(CGxQuery*& query) {
    query->m_link.Unlink();

    if (query) {
        query->~CGxQuery();
        SMemFree(query, __FILE__, __LINE__, 0x0);
    }

    query = nullptr;
}

// ref: FUN_006843b0
void CGxDevice::ICallbacksRestored() {
    for (uint32_t i = 0; i < this->m_restoredCallbacks.Count(); i++) {
        this->m_restoredCallbacks[i]();
    }
}

// ref: FUN_006843e0
void CGxDevice::ICallbacksTexturesLost() {
    for (uint32_t i = 0; i < this->m_texturesLostCallbacks.Count(); i++) {
        this->m_texturesLostCallbacks[i]();
    }
}

// ref: FUN_00684410
void CGxDevice::ICallbacks2() {
    for (uint32_t i = 0; i < this->m_callbacks2.Count(); i++) {
        this->m_callbacks2[i]();
    }
}


// ref: FUN_006853b0
void CGxDevice::CallbackAddRestored(GXDEVICECALLBACK callback) {
    *this->m_restoredCallbacks.New() = callback;
}

// ref: FUN_006853d0
void CGxDevice::CallbackRemoveRestored(GXDEVICECALLBACK callback) {
    uint32_t count = this->m_restoredCallbacks.Count();

    for (uint32_t i = 0; i < count; i++) {
        if (this->m_restoredCallbacks[i] == callback) {
            this->m_restoredCallbacks[i] = this->m_restoredCallbacks[count - 1];
            this->m_restoredCallbacks.SetCount(count - 1);
            return;
        }
    }
}

// ref: FUN_00685460
void CGxDevice::CallbackAddTexturesLost(GXDEVICECALLBACK callback) {
    *this->m_texturesLostCallbacks.New() = callback;
}

// ref: FUN_00685480
void CGxDevice::CallbackRemoveTexturesLost(GXDEVICECALLBACK callback) {
    uint32_t count = this->m_texturesLostCallbacks.Count();

    for (uint32_t i = 0; i < count; i++) {
        if (this->m_texturesLostCallbacks[i] == callback) {
            this->m_texturesLostCallbacks[i] = this->m_texturesLostCallbacks[count - 1];
            this->m_texturesLostCallbacks.SetCount(count - 1);
            return;
        }
    }
}

// ref: FUN_00685510
void CGxDevice::CallbackAdd2(GXDEVICECALLBACK callback) {
    *this->m_callbacks2.New() = callback;
}

// ref: FUN_00685530
void CGxDevice::CallbackRemove2(GXDEVICECALLBACK callback) {
    uint32_t count = this->m_callbacks2.Count();

    for (uint32_t i = 0; i < count; i++) {
        if (this->m_callbacks2[i] == callback) {
            this->m_callbacks2[i] = this->m_callbacks2[count - 1];
            this->m_callbacks2.SetCount(count - 1);
            return;
        }
    }
}

// ref: FUN_00687660
CGxBuf* CGxDevice::BufCreate(CGxPool* pool, uint32_t itemSize, uint32_t itemCount, uint32_t index) {
    auto m = SMemAlloc(sizeof(CGxBuf), __FILE__, __LINE__, 0x0);
    auto buf = new (m) CGxBuf(pool, itemSize, itemCount, index);

    pool->m_bufList.LinkToTail(buf);

    return buf;
}

// ref: FUN_00685e90
char* CGxBufScratch::Lock(uint32_t size) {
    if (this->m_data.Count() < size) {
        this->m_data.SetCount(size);
    }

    this->m_locked = 1;

    return reinterpret_cast<char*>(this->m_data.Ptr());
}

// ref: FUN_00682c40
void CGxBufScratch::Unlock() {
    this->m_locked = 0;
}

// ref: FUN_00683150
char* CGxDevice::BufLock(CGxBuf* buf) {
    buf->unk1E = 1;
    buf->unk1F = 0;

    this->m_bufLocked[buf->m_pool->m_target] = buf;

    return nullptr;
}

// ref: FUN_00684850
CGxBuf* CGxDevice::BufStream(EGxPoolTarget target, uint32_t itemSize, uint32_t itemCount) {
    CGxBuf* buf = this->m_streamBufs[target];
    CGxPool* pool = buf->m_pool;

    if (pool && pool->m_size < itemSize * itemCount) {
        this->PoolSizeSet(pool, itemSize * itemCount);
    }

    buf->m_itemSize = itemSize;
    buf->m_itemCount = itemCount;
    buf->m_size = itemSize * itemCount;
    buf->unk1C = 0;

    return buf;
}

// ref: FUN_00683180
int32_t CGxDevice::BufUnlock(CGxBuf* buf, uint32_t size) {
    this->m_bufLocked[buf->m_pool->m_target] = nullptr;

    return 1;
}

// ref: FUN_00683130
void CGxDevice::BufData(CGxBuf* buf, const void* data, size_t size, uintptr_t offset) {
    buf->unk1E = 1;
    buf->unk1F = 0;
}

const CGxCaps& CGxDevice::Caps() const {
    return this->m_caps;
}

// ref: FUN_00682cb0
int32_t CGxDevice::DeviceCreate(int32_t (*windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam), const CGxFormat& format) {
    this->m_windowProc = windowProc;

    return this->DeviceSetFormat(format);
}

void CGxDevice::DeviceCreatePools() {
    this->m_vertexPool = this->PoolCreate(
        GxPoolTarget_Vertex,
        GxPoolUsage_Stream,
        CGxDevice::s_streamPoolSize[GxPoolTarget_Vertex],
        GxPoolHintBit_Unk3,
        "Stream_vtx"
    );

    this->m_indexPool = this->PoolCreate(
        GxPoolTarget_Index,
        GxPoolUsage_Stream,
        CGxDevice::s_streamPoolSize[GxPoolTarget_Index],
        GxPoolHintBit_Unk3,
        "Stream_idx"
    );
}

// ref: FUN_00687900
void CGxDevice::DeviceCreateStreamBufs() {
    this->m_streamBufs[GxPoolTarget_Vertex] = this->BufCreate(this->m_vertexPool, 0, 0, 0);
    this->m_streamBufs[GxPoolTarget_Index] = this->BufCreate(this->m_indexPool, 0, 0, 0);
}

// `leal 0x174(%ecx), %eax; retl` -- the whole function. Identified behaviourally rather than by
// position: the reference's viewport setter calls it at 0x006a521d and then computes
// `(1.0 - viewport.y.h) * result[+0x8]`, which is IXformSetViewport's own
// `(1.0 - gxViewport.y.h) * windowRect.maxY`, and CRect is {minY, minX, maxY, maxX} so +0x8 is
// maxY. 15 callers, so linking it lifts the measured fidelity of every one of them that gets
// ported.
//
// Its neighbour FUN_00682d80 returns &this->[0x164], one CRect earlier, which by elimination is
// DeviceDefWindow -- frozen declares m_defWindowRect immediately before m_curWindowRect. Left
// untagged: that is adjacency, not proof, and a tag is a claim.
// ref: FUN_00682d70
const CRect& CGxDevice::DeviceCurWindow() {
    return this->m_curWindowRect;
}

float CGxDevice::s_aspectRatio = 0.0f;
uint32_t CGxDevice::s_nextFrameTime = 0;

// ref: FUN_006836d0
// The frame-rate cap, run by the backend just before each present. maxFPS applies while the window
// is active (intF64, reference +0xf64), the lower of maxFPS and maxFPSBk while it is not; 0 means
// no limit. Nothing is capped below 8 frames a second. A frame that is due more than a second out
// is not waited for. Frozen stored both CVars and read neither, so it had no cap at all.
void CGxDevice::ILimitFrameRate() {
    uint32_t maxFps = GxMaxFps() ? GxMaxFps() : 0xFFFFFFFF;
    uint32_t maxFpsBk = GxMaxFpsBk() ? GxMaxFpsBk() : 0xFFFFFFFF;

    if (!this->intF64 && maxFpsBk <= maxFps) {
        maxFps = maxFpsBk;
    }

    if (maxFps < 9) {
        maxFps = 8;
    } else if (maxFps == 0xFFFFFFFF) {
        return;
    }

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    if (CGxDevice::s_nextFrameTime - now < 1000) {
        OsSleep(CGxDevice::s_nextFrameTime - now);
        now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    }

    CGxDevice::s_nextFrameTime = now + 1000 / maxFps;
}

// ref: FUN_00682c50
// After a device (re)set, every light is off and every field of it is dirty, so the next sync
// sends all four in full.
void CGxDevice::ILightsInvalidate() {
    for (auto& light : this->m_lights) {
        light.m_enabled = 0;
        light.m_dirty = 0xFF;
        light.m_attenuationValid = 0xFF;
    }
}

// ref: FUN_006840f0
// Stores the format, raises a size below 320x240 to that minimum, and records the aspect the
// window sizing holds to.
int32_t CGxDevice::DeviceSetFormat(const CGxFormat& format) {
    memcpy(&this->m_format, &format, sizeof(this->m_format));

    if (this->m_format.size.x < 320 && this->m_format.size.y < 240) {
        this->m_format.size.x = 320;
        this->m_format.size.y = 240;
    }

    if (this->m_format.aspect) {
        CGxDevice::s_aspectRatio = static_cast<float>(this->m_format.size.x) / static_cast<float>(this->m_format.size.y);
    } else {
        CGxDevice::s_aspectRatio = 0.0f;
    }

    return 1;
}

void CGxDevice::DeviceSetCurWindow(const CRect& rect) {
    this->m_curWindowRect = rect;
}

// ref: FUN_00684360
void CGxDevice::DeviceSetDefWindow(CRect const& rect) {
    this->m_defWindowRect = rect;
    this->DeviceSetCurWindow(rect);
}

// ref: FUN_00682cd0
// Creating into a window someone else owns: the device only records the format.
int32_t CGxDevice::DeviceCreate(void* window, const CGxFormat& format) {
    this->m_format = format;

    return 1;
}

// ref: FUN_00682cf0
void CGxDevice::DeviceDestroy() {
    this->m_windowProc = nullptr;
}

// ref: FUN_00682d80
const CRect& CGxDevice::DeviceDefWindow() {
    return this->m_defWindowRect;
}

void CGxDevice::ICursorCreate(const CGxFormat& format) {
    // TODO
}

// ref: FUN_00682d40
int32_t CGxDevice::IDevIsWindowed() {
    return this->m_format.window;
}

// Identified 2026-09-23 field for field, which is worth recording because 641 reference functions
// call it and a wrong tag here would poison the fidelity of all of them. The reference indexes
// `m_appRenderStates + which * 0x18` -- 24 bytes, which is CGxAppRenderState's own size -- tests
// the dword at +0x14 as the dirty flag, compares the dword at +0x10 against the device's +0x18,
// and on a mismatch copies into a freshly grown entry: the state index, then four dwords from the
// entry's start (sizeof CGxStateBom), then the +0x10 dword. That is exactly m_which, m_value and
// m_stackDepth of CGxPushedRenderState, in that order, and exactly the two array growths below.
// The device's +0x18 is m_stackOffsets' count.
// ref: FUN_00685970
void CGxDevice::IRsDirty(EGxRenderState which) {
    auto rs = &this->m_appRenderStates[which];

    if (!rs->m_dirty) {
        auto ds = this->m_dirtyStates.New();
        *ds = which;

        rs->m_dirty = 1;
    }

    if (rs->m_stackDepth != this->m_stackOffsets.Count()) {
        auto ps = this->m_pushedStates.New();
        ps->m_which = which;
        ps->m_value = rs->m_value;
        ps->m_stackDepth = rs->m_stackDepth;

        rs->m_stackDepth = this->m_stackOffsets.Count();
    }
}

// The reference grows the dirty list with an inlined SetCount(count + 1) and then writes the new
// slot, rather than New(); same result, kept in the same shape.
void CGxDevice::IRsForceUpdate() {
    for (int32_t which = 0; which < GxRenderStates_Last; which++) {
        auto& rs = this->m_appRenderStates[which];
        auto& hs = this->m_hwRenderStates[which];

        uint32_t index = this->m_dirtyStates.Count();
        this->m_dirtyStates.SetCount(index + 1);
        this->m_dirtyStates[index] = static_cast<EGxRenderState>(which);

        rs.m_dirty = 1;

        // TODO manage this through operators
        hs.m_data.i[0] = ~rs.m_value.m_data.i[0];
        hs.m_data.i[1] = ~rs.m_value.m_data.i[1];
        hs.m_data.i[2] = ~rs.m_value.m_data.i[2];
        hs.filler = ~rs.m_value.filler;
    }
}

// Grows the dirty list through SetCount (FUN_00685180, out of line in the reference) and then
// writes the slot, as the reference does.
void CGxDevice::IRsForceUpdate(EGxRenderState which) {
    if (!this->m_context) {
        return;
    }

    uint32_t index = this->m_dirtyStates.Count();
    this->m_dirtyStates.SetCount(index + 1);
    this->m_dirtyStates[index] = which;

    auto& rs = this->m_appRenderStates[which];
    auto& hs = this->m_hwRenderStates[which];

    rs.m_dirty = 1;

    // TODO manage this through operators
    hs.m_data.i[0] = ~rs.m_value.m_data.i[0];
    hs.m_data.i[1] = ~rs.m_value.m_data.i[1];
    hs.m_data.i[2] = ~rs.m_value.m_data.i[2];
    hs.filler = ~rs.m_value.filler;
}

// Audited against the reference's own IRsInit on 2026-09-23, end to end, because every draw in the
// client starts from this table and a wrong entry here is the kind of defect that never looks like
// a defect. **Read, not run** -- this is not a `verified` claim.
//
// The reference is at 0x00686120 and identifies itself: it sizes both arrays to 0x56 = 86 =
// GxRenderStates_Last, then memsets 0x810 bytes over the app array and 0x560 over the hardware one.
// 0x810 / 86 = 24 = sizeof(CGxAppRenderState) and 0x560 / 86 = 16 = sizeof(CGxStateBom), which is a
// third and fourth confirmation of the two strides the D3D state-sync ports rest on. A store at
// 0xOFF through the pointer at +0x28f4 therefore sets render state OFF / 24.
//
// 69 states decoded and every one agrees with the lines below, including the values worth getting
// wrong: FogColor 0xff808080, ColorWrite 0xf, Culling 1, DepthWrite 1, DepthFunc 0, Multisample 1,
// ScissorTest 0, every texture and shader slot null, and the Unk70..Unk76 run counting 1 through 7.
// PointScaleAttenuation is a three-dword copy from the global at 0x00ad8bb4, which reads
// {1.0, 0.0, 0.0} out of .data and matches s_pointScaleIdentity byte for byte.
//
// Sixteen states looked unset by the reference on a first pass and are not: the compiler routes
// float defaults through a scratch slot at -0x4(%ebp) and schedules the reload BEFORE the store
// that fills it, so a naive scan attributes the previous value. FogStart, FogEnd, PointScale,
// PointScaleMin, PointScaleMax and the Texture8..15 slots are all set there.
// ref: FUN_00686120
void CGxDevice::IRsInit() {
    this->m_appRenderStates.SetCount(GxRenderStates_Last);
    this->m_hwRenderStates.SetCount(GxRenderStates_Last);

    memset(this->m_appRenderStates.m_data, 0, sizeof(CGxAppRenderState) * GxRenderStates_Last);
    memset(this->m_hwRenderStates.m_data, 0, sizeof(CGxStateBom) * GxRenderStates_Last);

    this->m_appRenderStates[GxRs_PolygonOffset].m_value     = 0;
    this->m_appRenderStates[GxRs_MatDiffuse].m_value        = 0xFFFFFFFF;
    this->m_appRenderStates[GxRs_MatEmissive].m_value       = 0;
    this->m_appRenderStates[GxRs_MatSpecular].m_value       = 0;
    this->m_appRenderStates[GxRs_MatSpecularExp].m_value    = 0.0f;
    this->m_appRenderStates[GxRs_NormalizeNormals].m_value  = 0;
    this->m_appRenderStates[GxRs_BlendingMode].m_value      = GxBlend_Opaque;
    this->m_appRenderStates[GxRs_FogStart].m_value          = 1.0f;
    this->m_appRenderStates[GxRs_FogEnd].m_value            = 0.0f;
    this->m_appRenderStates[GxRs_FogColor].m_value          = 0xFF808080;
    this->m_appRenderStates[GxRs_Lighting].m_value          = 1;
    this->m_appRenderStates[GxRs_Fog].m_value               = 1;
    this->m_appRenderStates[GxRs_DepthTest].m_value         = 1;
    this->m_appRenderStates[GxRs_DepthFunc].m_value         = 0;
    this->m_appRenderStates[GxRs_DepthWrite].m_value        = 1;
    this->m_appRenderStates[GxRs_ColorWrite].m_value        = 15;
    this->m_appRenderStates[GxRs_Culling].m_value           = 1;
    this->m_appRenderStates[GxRs_ClipPlaneMask].m_value     = 0;
    // Confirmed against the reference's own IRsInit at 0x00686208, which writes these defaults
    // through the array pointer at +0x28f4 with a 24-byte stride: 0x1c8 / 24 = 19 = this state,
    // and it takes the register holding 1. So the reference defaults it to 1 as well, and its
    // GxRsSet(0x13, 1) inside the world-render push is defensive rather than a change. frozen does
    // not antialias the UI by mistake.
    //
    // Four of its neighbours in the same run decode consistently and confirm the stride:
    // 0x180 / 24 = 16 = ColorWrite takes 0xf, 0x198 = 17 = Culling takes 1, 0x1b0 = 18 =
    // ClipPlaneMask takes 0 and 0x1e0 = 20 = ScissorTest takes 0 -- all matching the lines here.
    this->m_appRenderStates[GxRs_Multisample].m_value       = 1;
    this->m_appRenderStates[GxRs_ScissorTest].m_value       = 0;

    this->m_appRenderStates[GxRs_Texture0].m_value          = nullptr;
    this->m_appRenderStates[GxRs_ColorOp0].m_value          = 0;
    this->m_appRenderStates[GxRs_AlphaOp0].m_value          = 0;
    this->m_appRenderStates[GxRs_TexGen0].m_value           = 0;
    this->m_appRenderStates[GxRs_Unk61].m_value             = 0;
    this->m_appRenderStates[GxRs_TexCoord0].m_value            = 0;

    this->m_appRenderStates[GxRs_Texture1].m_value          = nullptr;
    this->m_appRenderStates[GxRs_ColorOp1].m_value          = 0;
    this->m_appRenderStates[GxRs_AlphaOp1].m_value          = 0;
    this->m_appRenderStates[GxRs_TexGen1].m_value           = 0;
    this->m_appRenderStates[GxRs_Unk62].m_value             = 0;
    this->m_appRenderStates[GxRs_TexCoord1].m_value            = 1;

    this->m_appRenderStates[GxRs_Texture2].m_value          = nullptr;
    this->m_appRenderStates[GxRs_ColorOp2].m_value          = 0;
    this->m_appRenderStates[GxRs_AlphaOp2].m_value          = 0;
    this->m_appRenderStates[GxRs_TexGen2].m_value           = 0;
    this->m_appRenderStates[GxRs_Unk63].m_value             = 0;
    this->m_appRenderStates[GxRs_TexCoord2].m_value            = 2;

    this->m_appRenderStates[GxRs_Texture3].m_value          = nullptr;
    this->m_appRenderStates[GxRs_ColorOp3].m_value          = 0;
    this->m_appRenderStates[GxRs_AlphaOp3].m_value          = 0;
    this->m_appRenderStates[GxRs_TexGen3].m_value           = 0;
    this->m_appRenderStates[GxRs_Unk64].m_value             = 0;
    this->m_appRenderStates[GxRs_TexCoord3].m_value            = 3;

    this->m_appRenderStates[GxRs_Texture4].m_value          = nullptr;
    this->m_appRenderStates[GxRs_ColorOp4].m_value          = 0;
    this->m_appRenderStates[GxRs_AlphaOp4].m_value          = 0;
    this->m_appRenderStates[GxRs_TexGen4].m_value           = 0;
    this->m_appRenderStates[GxRs_Unk65].m_value             = 0;
    this->m_appRenderStates[GxRs_TexCoord4].m_value            = 4;

    this->m_appRenderStates[GxRs_Texture5].m_value          = nullptr;
    this->m_appRenderStates[GxRs_ColorOp5].m_value          = 0;
    this->m_appRenderStates[GxRs_AlphaOp5].m_value          = 0;
    this->m_appRenderStates[GxRs_TexGen5].m_value           = 0;
    this->m_appRenderStates[GxRs_Unk66].m_value             = 0;
    this->m_appRenderStates[GxRs_TexCoord5].m_value            = 5;

    this->m_appRenderStates[GxRs_Texture6].m_value          = nullptr;
    this->m_appRenderStates[GxRs_ColorOp6].m_value          = 0;
    this->m_appRenderStates[GxRs_AlphaOp6].m_value          = 0;
    this->m_appRenderStates[GxRs_TexGen6].m_value           = 0;
    this->m_appRenderStates[GxRs_Unk67].m_value             = 0;
    this->m_appRenderStates[GxRs_TexCoord6].m_value            = 6;

    this->m_appRenderStates[GxRs_Texture7].m_value          = nullptr;
    this->m_appRenderStates[GxRs_ColorOp7].m_value          = 0;
    this->m_appRenderStates[GxRs_AlphaOp7].m_value          = 0;
    this->m_appRenderStates[GxRs_TexGen7].m_value           = 0;
    this->m_appRenderStates[GxRs_Unk68].m_value             = 0;
    this->m_appRenderStates[GxRs_TexCoord7].m_value            = 7;

    this->m_appRenderStates[GxRs_Texture8].m_value          = nullptr;
    this->m_appRenderStates[GxRs_Texture9].m_value          = nullptr;
    this->m_appRenderStates[GxRs_Texture10].m_value         = nullptr;
    this->m_appRenderStates[GxRs_Texture11].m_value         = nullptr;
    this->m_appRenderStates[GxRs_Texture12].m_value         = nullptr;
    this->m_appRenderStates[GxRs_Texture13].m_value         = nullptr;
    this->m_appRenderStates[GxRs_Texture14].m_value         = nullptr;
    this->m_appRenderStates[GxRs_Texture15].m_value         = nullptr;

    this->m_appRenderStates[GxRs_PixelShader].m_value       = nullptr;
    this->m_appRenderStates[GxRs_VertexShader].m_value      = nullptr;

    this->m_appRenderStates[GxRs_PointScale].m_value        = 1.0f;
    this->m_appRenderStates[GxRs_PointScaleAttenuation].m_value = CGxDevice::s_pointScaleIdentity;
    this->m_appRenderStates[GxRs_PointScaleMin].m_value     = 0.0f;
    this->m_appRenderStates[GxRs_PointScaleMax].m_value     = 1.0f;
    this->m_appRenderStates[GxRs_PointSprite].m_value       = 0;

    this->m_appRenderStates[GxRs_BlendFactor].m_value            = 0.0f;
    this->m_appRenderStates[GxRs_ColorMaterial].m_value     = 0;
}

void CGxDevice::IRsSync(int32_t force) {
    if (force) {
        this->IRsForceUpdate();
    }

    for (int32_t i = this->m_dirtyStates.Count() - 1; i >= 0; i--) {
        auto ds = this->m_dirtyStates[i];
        auto rs = &this->m_appRenderStates[ds];
        auto hs = &this->m_hwRenderStates[ds];

        if (rs->m_dirty && rs->m_value != *hs) {
            this->IRsSendToHw(ds);
        }

        *hs = rs->m_value;
        rs->m_dirty = 0;
    }

    this->m_dirtyStates.SetCount(0);
}

void CGxDevice::IShaderLoad(CGxShader* shaders[], EGxShTarget target, const char* a4, const char* a5, int32_t permutations) {
    int32_t profile = this->m_caps.m_shaderTargets[target];

    if (!profile) {
        return;
    }

    char path[260];
    SFile* file;

    while (true) {
        sprintf(path, "%s\\%s\\%s.bls", a4, g_gxShaderProfileNames[target][profile], a5);
        SFile::Open(path, &file);

        // Opened shader file
        if (file) {
            break;
        }

        // Fallback
        if (target == GxSh_Vertex) {
            // vs_3_0 -> vs_2_0
            // vs_2_0 -> vs_1_1

            if (profile == 3) {
                profile = 2;
            } else if (profile == 2) {
                profile = 1;
            } else {
                break;
            }
        } else if (target == GxSh_Pixel) {
            // nvts3 -> nvts
            // nvts2 -> nvts
            // ps_3_0 -> ps_2_0
            // ps_2_0 -> ps_1_4
            // ps_1_4 -> ps_1_1

            if (profile == 9 || profile == 10) {
                profile = 8;
            } else if (profile == 4) {
                profile = 3;
            } else if (profile == 3) {
                profile = 2;
            } else if (profile == 2) {
                profile = 1;
            } else {
                break;
            }
        }
    }

    if (!file) {
        return;
    }

    uint32_t signature;
    uint32_t version;
    uint32_t permutationCount;

    if (file) {
        SFile::Read(file, &signature, 4, nullptr, nullptr, nullptr);

        if (signature == 'GXSH') {
            SFile::Read(file, &version, 4, nullptr, nullptr, nullptr);

            if (version == 0x10003) {
                SFile::Read(file, &permutationCount, 4, nullptr, nullptr, nullptr);

                // TODO
                // assert(permutationCount == permutations);

                if (permutations > 0) {
                    for (int32_t p = 0; p < permutations; p++) {
                        shaders[p]->Load(file);

                        shaders[p]->loaded = 0;
                        shaders[p]->int34 = 0;
                    }
                }
            }
        }

        SFile::Close(file);
    }
}

// ref: FUN_00684900
void CGxDevice::ITexMarkAsUpdated(CGxTex* texId) {
    if (!texId->m_needsUpdate) {
        return;
    }

    texId->m_updateRect = {
        static_cast<int32_t>(texId->m_height),
        static_cast<int32_t>(texId->m_width),
        0,
        0
    };

    texId->m_needsUpdate = 0;
}

void CGxDevice::ITexWHDStartEnd(CGxTex* texId, uint32_t& width, uint32_t& height, uint32_t& baseMip, uint32_t& mipCount) {
    width = texId->m_width;
    height = texId->m_height;

    if ((texId->m_flags.m_filter == GxTex_Nearest || texId->m_flags.m_filter == GxTex_Linear || texId->m_flags.m_generateMipMaps) && !texId->m_flags.m_forceMipTracking) {
        baseMip = 0;
        mipCount = 1;

        return;
    }

    mipCount = 1;

    if (texId->m_format == GxTex_Dxt1 || texId->m_format == GxTex_Dxt3 || texId->m_format == GxTex_Dxt5) {
        uint32_t shortEdge = std::min(texId->m_width, texId->m_height);

        while (shortEdge > 4) {
            shortEdge /= 2;
            mipCount++;
        }
    } else {
        uint32_t longEdge = std::max(texId->m_width, texId->m_height);

        while (longEdge > 1) {
            longEdge /= 2;
            mipCount++;
        }
    }

    baseMip = std::min(this->m_baseMipLevel, mipCount - 1);

    width >>= baseMip;
    height >>= baseMip;

    if (texId->m_flags.m_forceMipTracking) {
        mipCount = baseMip + 1;
    }
}

// A negative value only reads the flag; anything else stores (value > 0) and answers it.
// ref: FUN_00682e20
int32_t CGxDevice::ContextFlag(int32_t value) {
    if (value >= 0) {
        this->m_context = value > 0;
        return value > 0;
    }

    return this->m_context;
}

// ref: FUN_00683100
int32_t CGxDevice::MasterEnable(EGxMasterEnables state) {
    return ((1 << state) & this->m_appMasterEnables) != 0;
}

// ref: FUN_00685eb0
void CGxDevice::MasterEnableSet(EGxMasterEnables state, int32_t enable) {
    this->m_appMasterEnables = ((enable & 1) << state) | (this->m_appMasterEnables & ~(1 << state));

    switch (state) {
        case GxMasterEnable_Lighting:
            this->IRsForceUpdate(GxRs_Lighting);
            break;

        case GxMasterEnable_Fog:
            this->IRsForceUpdate(GxRs_Fog);
            break;

        case GxMasterEnable_DepthTest:
            this->IRsForceUpdate(GxRs_DepthTest);
            break;

        case GxMasterEnable_DepthWrite:
            this->IRsForceUpdate(GxRs_DepthWrite);
            break;

        case GxMasterEnable_ColorWrite:
            this->IRsForceUpdate(GxRs_ColorWrite);
            break;

        case GxMasterEnable_Culling:
            this->IRsForceUpdate(GxRs_Culling);
            break;

        default:
            break;
    }
}

// ref: FUN_00682f10
void CGxDevice::PrimIndexPtr(CGxBuf* buf) {
    if (buf->unk1E || this->m_primIndexBuf != buf) {
        buf->unk1E = 0;
        this->m_primIndexDirty = 1;
        this->m_primIndexBuf = buf;
    }
}

// Stores one user clip plane and marks it dirty only when it actually changed, which is the
// whole point: the sync in the D3D backend walks the dirty mask, so an unchanged plane costs
// nothing.
//
// The reference compares all four components with the fcom/fnstsw/testb $0x44 idiom. With mask
// 0x44 the tested bits are C3 (equal) and C2 (unordered), and `jp` is taken only when BOTH are
// clear -- an ordered, not-equal compare -- which is the branch into the store. The last of the
// four inverts to `jnp` to fall out when every component matched. Spelled here as a plain
// inequality, which is the same thing for ordered values and does not silently invert if a NaN
// ever reaches it.
// Copy an application light into a device slot, one field group at a time, marking each with its
// own dirty bit so a backend can send only what moved. Every comparison is an inequality against
// what is already stored, so setting a light to the value it already holds costs nothing.
//
// The position group is the odd one. It compares four things -- the three components AND the
// derived w -- so that a light flipping between point and directional is caught even when its
// xyz happens to be unchanged. The w it compares against is `(flags >> 1) & 1` read as a float,
// which is the whole of how the point/directional choice crosses into the device.
//
// The write order below is the reference's, not a tidied one: attenuation first, then position,
// then diffuse, then ambient, then specular. It costs nothing to keep and makes the call-order
// check mean something.
// ref: FUN_00684620
void CGxLightState::Set(const CGxLight& light) {
    if (light.m_attenuation.x != this->m_attenuation.x) {
        this->m_dirty |= 0x20;
        this->m_attenuation.x = light.m_attenuation.x;
    }

    if (light.m_attenuation.y != this->m_attenuation.y) {
        this->m_dirty |= 0x40;
        this->m_attenuation.y = light.m_attenuation.y;
    }

    if (light.m_attenuation.z != this->m_attenuation.z) {
        this->m_dirty |= 0x80;
        this->m_attenuation.z = light.m_attenuation.z;
    }

    float w = static_cast<float>((light.m_flags >> 1) & 0x1);

    if (light.m_posOrDir.x != this->m_posOrDir.x || light.m_posOrDir.y != this->m_posOrDir.y
            || light.m_posOrDir.z != this->m_posOrDir.z || w != this->m_posOrDir.w) {
        this->m_dirty |= 0x2;
        this->m_posOrDir.x = light.m_posOrDir.x;
        this->m_posOrDir.y = light.m_posOrDir.y;
        this->m_posOrDir.z = light.m_posOrDir.z;

        if (light.m_flags & 0x2) {
            this->m_attenuationValid |= 0xe0;
            this->m_posOrDir.w = 1.0f;
        } else {
            this->m_attenuationValid &= 0xff1f;
            this->m_posOrDir.w = 0.0f;
        }
    }

    if (light.m_diffuse.x != this->m_diffuse.x || light.m_diffuse.y != this->m_diffuse.y
            || light.m_diffuse.z != this->m_diffuse.z) {
        this->m_diffuse = light.m_diffuse;
        this->m_dirty |= 0x8;
    }

    if (light.m_ambient.x != this->m_ambient.x || light.m_ambient.y != this->m_ambient.y
            || light.m_ambient.z != this->m_ambient.z) {
        this->m_ambient = light.m_ambient;
        this->m_dirty |= 0x4;
    }

    if (light.m_specular.x != this->m_specular.x || light.m_specular.y != this->m_specular.y
            || light.m_specular.z != this->m_specular.z) {
        this->m_specular = light.m_specular;
        this->m_dirty |= 0x10;
    }
}

// The public half of the above. The origin shift applies only to a POINT light with a non-zero
// origin -- a directional light has no position to shift -- and it happens AFTER the store, on the
// value already in the slot, so the comparison inside Set sees the unshifted position. That means
// two calls with the same light and the same non-zero origin do not settle: the second shifts the
// stored value again only if Set rewrote it, which it will, because the first call left the slot
// holding position-minus-origin while the light still holds position. Both current callers pass a
// zero origin so the branch never runs; reproduced as found rather than corrected.
// ref: FUN_006847d0
void CGxDevice::LightSet(uint32_t index, const CGxLight& light, const C3Vector& origin) {
    CGxLightState& state = this->m_lights[index];

    state.Set(light);

    if ((light.m_flags & 0x2) && (origin.x != 0.0f || origin.y != 0.0f || origin.z != 0.0f)) {
        state.m_dirty |= 0x2;
        state.m_posOrDir.x -= origin.x;
        state.m_posOrDir.y -= origin.y;
        state.m_posOrDir.z -= origin.z;
    }
}

// ref: FUN_00682fd0
void CGxDevice::LightGet(uint32_t index, CGxLight& light) {
    const CGxLightState& state = this->m_lights[index];

    // The enabled bit first, and it gates everything else.
    light.m_flags = (light.m_flags & ~1u) | (static_cast<uint32_t>(state.m_enabled) & 1u);

    if (!(light.m_flags & 1u)) {
        return;
    }

    // A non-zero w is a point light; that is the only place the distinction is kept.
    if (state.m_posOrDir.w != 0.0f) {
        light.m_flags |= 2u;
    } else {
        light.m_flags &= ~2u;
    }

    light.m_posOrDir.x = state.m_posOrDir.x;
    light.m_posOrDir.y = state.m_posOrDir.y;
    light.m_posOrDir.z = state.m_posOrDir.z;
    light.m_ambient = state.m_ambient;
    light.m_diffuse = state.m_diffuse;
    light.m_specular = state.m_specular;
    light.m_attenuation = state.m_attenuation;
}

// ref: FUN_00683080
void CGxDevice::LightEnable(uint32_t index, int32_t enable) {
    CGxLightState& state = this->m_lights[index];

    if (state.m_enabled != enable) {
        state.m_dirty |= 0x1;
        state.m_enabled = enable;
    }
}

// ref: FUN_00684440
void CGxDevice::ClipPlaneSet(uint32_t index, const C4Plane* plane) {
    C4Plane* dst = &this->m_clipPlanes[index];

    if (plane->n.x != dst->n.x || plane->n.y != dst->n.y || plane->n.z != dst->n.z || plane->d != dst->d) {
        this->m_clipPlaneDirty |= 1 << index;

        dst->n.x = plane->n.x;
        dst->n.y = plane->n.y;
        dst->n.z = plane->n.z;
        dst->d = plane->d;
    }
}

// Stores the scissor rectangle in normalised coordinates and marks it dirty. Unlike ClipPlaneSet
// this does NOT compare first -- the reference marks dirty unconditionally -- so a caller setting
// the same rectangle every frame re-sends it. Reproduced rather than improved.
// ref: FUN_00682e70
void CGxDevice::ScissorSet(const CRect* rect) {
    this->m_scissorDirty = 1;

    this->m_scissorRect.minY = rect->minY;
    this->m_scissorRect.minX = rect->minX;
    this->m_scissorRect.maxY = rect->maxY;
    this->m_scissorRect.maxX = rect->maxX;
}

// ref: FUN_006844c0
void CGxDevice::PrimVertexFormat(CGxBuf* buf, CGxVertexAttrib* attribs, uint32_t count) {
    for (int32_t i = 0; i < count; i++) {
        int32_t attrib = attribs->attrib;

        int32_t dirty = buf->unk1E
            || this->m_primVertexFormatBuf[attrib] != buf
            || this->m_primVertexFormatAttrib[attrib].bufSize != attribs->bufSize
            || this->m_primVertexFormatAttrib[attrib].type != attribs->type
            || this->m_primVertexFormatAttrib[attrib].offset != attribs->offset;

        if (dirty) {
            this->m_primVertexDirty |= 1 << attrib;
        }

        this->m_primVertexFormatBuf[attrib] = buf;

        this->m_primVertexFormatAttrib[attrib].attrib = attribs->attrib;
        this->m_primVertexFormatAttrib[attrib].type = attribs->type;
        this->m_primVertexFormatAttrib[attrib].offset = attribs->offset;
        this->m_primVertexFormatAttrib[attrib].bufSize = attribs->bufSize;

        attribs++;
    }

    buf->unk1E = 0;
    this->m_primVertexFormat = GxVertexBufferFormats_Last;
}

// ref: FUN_00682eb0
void CGxDevice::PrimVertexMask(uint32_t mask) {
    this->m_primVertexDirty |= mask ^ this->m_primVertexMask;
    this->m_primVertexMask = mask;
    this->m_primVertexFormat = GxVertexBufferFormats_Last;
}

// ref: FUN_00682ee0
void CGxDevice::PrimVertexPtr(CGxBuf* buf, EGxVertexBufferFormat format) {
    this->m_primVertexFormat = format;
    this->m_primVertexBuf = buf;
    this->m_primVertexSize = Buffer::s_vertexBufDesc[format].size;
}

// ref: FUN_00688290
// Takes the caller's pointer and nulls it. The buffer owns nothing of its own -- it is a range
// inside its pool's API buffer -- so this is bookkeeping: detach, unlink, free.
void CGxDevice::BufDestroy(CGxBuf*& buf) {
    if (buf) {
        if (buf->m_pool) {
            buf->m_pool->BufRemove(buf);
        }

        buf->~CGxBuf();
        SMemFree(buf, __FILE__, __LINE__, 0x0);
    }

    buf = nullptr;
}

void CGxDevice::PoolDestroy(CGxPool* pool) {
    if (!pool) {
        return;
    }

    // Its buffers first. They are linked into the pool rather than owned by their callers, so
    // dropping the pool without them would leave the records behind.
    while (auto buf = pool->m_bufList.Head()) {
        this->BufDestroy(buf);
    }

    this->IPoolRelease(pool);

    // The CPU shadow, when the backend keeps one (the GLES device does, through PoolSizeSet).
    if (pool->m_mem) {
        SMemFree(pool->m_mem, __FILE__, __LINE__, 0x0);
        pool->m_mem = nullptr;
    }

    // ~TSLinkedNode unlinks it from m_poolList, so the walk in IReleaseD3dPools cannot reach it
    // again. Unlink is idempotent, which is why nothing needs to check first.
    pool->~CGxPool();
    SMemFree(pool, __FILE__, __LINE__, 0x0);
}

CGxPool* CGxDevice::PoolCreate(EGxPoolTarget target, EGxPoolUsage usage, uint32_t size, EGxPoolHintBits hint, const char* name) {
    auto m = SMemAlloc(sizeof(CGxPool), __FILE__, __LINE__, 0x0);
    auto pool = new (m) CGxPool(target, usage, size, hint, name);

    this->m_poolList.LinkToTail(pool);

    return pool;
}

// `leal 0xa44(%eax,%eax,2), %eax; movl (%ecx,%eax,4), %ecx; movl %ecx, (%edx)` -- the index is
// scaled by three and then by four, so the stride is twelve and the base is 0xa44 * 4 = 0x2910.
// That is m_textureTarget, and the field read is its first, m_texture. `retl $0x8` matches the two
// stack arguments.
//
// This is the third independent confirmation of that layout, and the cleanest: IStateSyncScissorRect
// and IXformSetViewport both test +0x2918 and +0x2924, which are m_apiSpecific of entries 0 and 1
// against this base, and device create zeroes exactly the six dwords from +0x2910 to +0x2924. The
// divergence recorded on those two -- frozen tests m_texture because it never stores a surface in
// m_apiSpecific -- rests on this.
// ref: FUN_00682d50
void CGxDevice::RenderTargetGet(EGxBuffer buffer, CGxTex*& gxTex) {
    gxTex = this->m_textureTarget[buffer].m_texture;
}

void CGxDevice::RenderTargetSet(EGxBuffer buffer, CGxTex* gxTex, uint32_t plane) {
    if (!this->m_context) {
        return;
    }

    this->m_textureTarget[buffer].m_texture = gxTex;
    this->m_textureTarget[buffer].m_plane = plane;

    this->IRenderTargetSet(buffer, gxTex, plane);
}

int32_t CGxDevice::RenderTargetDump(CGxTex* gxTex, const char* path) {
    return this->IRenderTargetDump(gxTex, path);
}

int32_t CGxDevice::ScreenShot(const char* path) {
    return this->IScreenShot(path);
}

void CGxDevice::RsGet(EGxRenderState which, int32_t& value) {
    value = static_cast<int32_t>(this->m_appRenderStates[which].m_value);
}

void CGxDevice::RsSet(EGxRenderState which, int32_t value) {
    if (!this->m_context) {
        return;
    }

    if (this->m_appRenderStates[which].m_value != value) {
        this->IRsDirty(which);
        this->m_appRenderStates[which].m_value = value;
    }
}

// The float-valued render states go through here rather than through the int32_t overload above:
// CGxStateBom stores either in the same union, but comparing the bit patterns as integers is not
// the same test as comparing them as floats, and the reference compares as floats.
void CGxDevice::RsSet(EGxRenderState which, float value) {
    if (!this->m_context) {
        return;
    }

    if (this->m_appRenderStates[which].m_value != value) {
        this->IRsDirty(which);
        this->m_appRenderStates[which].m_value = value;
    }
}

void CGxDevice::RsSet(EGxRenderState which, uint32_t value) {
    if (!this->m_context) {
        return;
    }

    if (this->m_appRenderStates[which].m_value != value) {
        this->IRsDirty(which);
        this->m_appRenderStates[which].m_value = value;
    }
}

void CGxDevice::RsSet(EGxRenderState which, void* value) {
    if (!this->m_context) {
        return;
    }

    if (this->m_appRenderStates[which].m_value != value) {
        this->IRsDirty(which);
        this->m_appRenderStates[which].m_value = value;

        if (value) {
            if (which >= GxRs_Texture0 && which <= GxRs_Texture15) {
                CGxTex* texture = static_cast<CGxTex*>(value);

                if (texture->m_flags.m_renderTarget && texture->m_needsUpdate) {
                    this->ITexMarkAsUpdated(texture);
                }
            }
        }
    }
}

void CGxDevice::RsSetAlphaRef() {
    if (!this->m_context) {
        return;
    }

    int32_t blendingMode;
    this->RsGet(GxRs_BlendingMode, blendingMode);

    this->RsSet(GxRs_AlphaRef, CGxDevice::s_alphaRef[blendingMode]);
}

// ref: FUN_00685fb0
// The reference (FUN_00685fb0) makes exactly these calls in exactly this order -- call-order
// fidelity is 1.0 -- but the recomp report still does not count it faithful, and the reason is
// structural rather than a defect. Its branch ratio is 0.214, because the reference inlines the
// same growable-array shrink three times:
//
//     if (growQuantum == 0) growQuantum = PowerOfTwoFloor(n);   // 00590830 / 005d0040
//     if (n % growQuantum)  n += growQuantum - n % growQuantum;
//     array.Reserve(n);
//
// once per array, where frozen writes SetCount and lets TSGrowableArray do it. Same calls, a third
// of the branches. Do not "fix" this by unrolling it here.
//
// The three Reserve helpers the reference calls are told apart only by their index scaling
// (4-byte, 4-byte and 0x18-byte elements); one of them, 00408490, was linked to CGxDevice::RsPush
// until 2026-09-23, which held this function's measured recall at 0% and made it look as though
// RsPop called RsPush. See overrides.json.
void CGxDevice::RsPop() {
    auto topOfStack = this->m_stackOffsets[this->m_stackOffsets.Count() - 1];

    if (this->m_pushedStates.Count() > topOfStack) {
        auto bottomOfStack = this->m_pushedStates.Count() - 1;
        auto stackSize = this->m_pushedStates.Count() - topOfStack;

        for (uint32_t stackOffset = 0; stackOffset < stackSize; stackOffset++) {
            auto ps = &this->m_pushedStates[bottomOfStack - stackOffset];
            auto rs = &this->m_appRenderStates[ps->m_which];

            if (!rs->m_dirty) {
                auto ds = this->m_dirtyStates.New();
                *ds = ps->m_which;

                rs->m_dirty = 1;
            }

            rs->m_value = ps->m_value;
            rs->m_stackDepth = ps->m_stackDepth;
        }
    }

    this->m_pushedStates.SetCount(topOfStack);
    this->m_stackOffsets.SetCount(this->m_stackOffsets.Count() - 1);
}

void CGxDevice::RsPush() {
    auto offset = this->m_stackOffsets.New();
    *offset = this->m_pushedStates.Count();
}

// The tail every backend's ScenePresent already calls, and it was empty. Three statements in the
// reference, and the frame counter is the one that matters: the texture priority path reads it to
// decide which textures have gone cold.
//
// Clearing intF5C here looks alarming next to CGxDeviceD3d::Draw, which bails while that flag is
// set, but it is what the reference does and it is inert in frozen: nothing here ever sets the flag
// to anything but zero. It stops being inert the day device-lost handling lands, and at that point
// the reference's behaviour -- re-raise it each frame while the device is still lost -- is the
// behaviour to match.
// ref: FUN_00682e50
void CGxDevice::ScenePresent() {
    this->m_frameCount++;
    this->int2934 = 0;
    this->intF5C = 0;
}

// ref: FUN_006833a0
void CGxDevice::ShaderConstantsClear() {
    for (int32_t i = 0; i < 256; i++) {
        CGxDevice::s_shadowConstants[0].constants[i] = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()
        };

        CGxDevice::s_shadowConstants[1].constants[i] = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()
        };
    }

    CGxDevice::s_shadowConstants[0].unk1 = 0;
    CGxDevice::s_shadowConstants[0].unk2 = 255;

    CGxDevice::s_shadowConstants[1].unk1 = 0;
    CGxDevice::s_shadowConstants[1].unk2 = 255;
}

char* CGxDevice::ShaderConstantsLock(EGxShTarget target) {
    return target == GxSh_Vertex
        ? reinterpret_cast<char*>(&CGxDevice::s_shadowConstants[1].constants)
        : reinterpret_cast<char*>(&CGxDevice::s_shadowConstants[0].constants);
}

void CGxDevice::ShaderConstantsSet(EGxShTarget target, uint32_t index, const float* constants, uint32_t count) {
    STORM_ASSERT((index + count - 1) <= 255);

    if (!count) {
        return;
    }

    ShaderConstants* dst;

    if (target == GxSh_Vertex) {
        dst = &CGxDevice::s_shadowConstants[1];
    } else if (target == GxSh_Pixel) {
        dst = &CGxDevice::s_shadowConstants[0];
    } else {
        STORM_ASSERT(false);
    }

    const float* c = constants;

    for (int32_t i = index; i < index + count; i++, c += 4) {
        int32_t dirty = 0;

        if (dst->constants[i].x != c[0]) {
            dirty = 1;
            dst->constants[i].x = c[0];
        }

        if (dst->constants[i].y != c[1]) {
            dirty = 1;
            dst->constants[i].y = c[1];
        }

        if (dst->constants[i].z != c[2]) {
            dirty = 1;
            dst->constants[i].z = c[2];
        }

        if (dst->constants[i].w != c[3]) {
            dirty = 1;
            dst->constants[i].w = c[3];
        }

        if (dirty) {
            if (dst->unk2 > i) {
                dst->unk2 = i;
            }

            if (dst->unk1 < i) {
                dst->unk1 = i;
            }
        }
    }
}

void CGxDevice::ShaderConstantsUnlock(EGxShTarget target, uint32_t index, uint32_t count) {
    if (target == GxSh_Pixel) {
        ShaderConstants& dst = CGxDevice::s_shadowConstants[0];
        dst.unk2 = std::min(dst.unk2, index);
        dst.unk1 = std::max(dst.unk1, index + count - 1);
    } else {
        ShaderConstants& dst = CGxDevice::s_shadowConstants[1];
        dst.unk2 = std::min(dst.unk2, index);
        dst.unk1 = std::max(dst.unk1, index + count - 1);
    }
}

// The other half of ShaderCreate, which until now had none: shaders were created, reference
// counted and never released by anything. Every liquid material loads a pair on first use and the
// reference drops that pair in the material's destructor -- there was simply no call to port it
// to, so the materials could not have destructors and the shaders leaked for the life of the
// process.
//
// Refcounted like the creation side: ShaderCreate bumps the count on every request, whether it
// made the shader or found it already in the list, so a matching drop has to reach zero before the
// entry goes. The slot is cleared either way -- the caller has given up its reference, and leaving
// a pointer behind that the next load would skip over is how a freed shader gets bound.
// ref: FUN_00687820
// Drops one reference; the last one unlinks the shader from its target's table and frees it.
// Callers check for null, as the reference's do.
void CGxDevice::ShaderDestroy(CGxShader** shader) {
    auto s = *shader;

    if (--s->refCount == 0) {
        this->m_shaderList[s->target].Delete(s);
    }

    *shader = nullptr;
}

// ref: FUN_006897c0
void CGxDevice::ShaderCreate(CGxShader* shaders[], EGxShTarget target, const char* a4, const char* a5, int32_t permutations) {
    auto shaderList = &this->m_shaderList[target];

    if (permutations == 0) {
        return;
    }

    if (permutations == 1) {
        auto shader = shaderList->Ptr(a5);

        if (shader) {
            shaders[0] = shader;
            shader->refCount++;
            return;
        }

        shader = shaderList->New(a5, 0, 0);
        shaders[0] = shader;

        shader->refCount++;
        shader->target = target;

        this->IShaderLoad(shaders, target, a4, a5, permutations);

        return;
    }

    memset(shaders, 0, permutations * sizeof(void*));

    char key[256];

    int32_t p = 0;

    while (p < permutations) {
        sprintf(key, "%s:%d", a5, p);

        auto shader = shaderList->Ptr(key);
        shaders[p] = shader;

        if (!shader) {
            break;
        }

        p++;
    }

    // Fully loaded
    if (p == permutations) {
        for (p = 0; p < permutations; p++) {
            shaders[p]->refCount++;
        }

        return;
    }

    for (p = 0; p < permutations; p++) {
        sprintf(key, "%s:%d", a5, p);

        auto shader = shaderList->New(key, 0, 0);
        shaders[p] = shader;

        shader->refCount++;
        shader->target = target;
    }

    this->IShaderLoad(shaders, target, a4, a5, permutations);
}

// ref: FUN_00685c60
int32_t CGxDevice::TexCreate(EGxTexTarget target, uint32_t width, uint32_t height, uint32_t depth, EGxTexFormat format, EGxTexFormat dataFormat, CGxTexFlags flags, void* userArg, void (*userFunc)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), const char* name, CGxTex*& texId) {
    auto m = SMemAlloc(sizeof(CGxTex), __FILE__, __LINE__, 0);
    auto tex = new (m) CGxTex(
        target,
        width,
        height,
        depth,
        format,
        dataFormat,
        flags,
        userArg,
        userFunc,
        name
    );

    texId = tex;

    this->m_texList.LinkToHead(tex);

    return 1;
}

// ref: FUN_00687980
// Unlinks the texture from the device list and frees it. A backend releases its own object first
// and then calls this.
void CGxDevice::TexDestroy(CGxTex* texId) {
    texId->m_link.Unlink();

    if (texId) {
        delete texId;
    }
}

// ref: FUN_006848a0
void CGxDevice::TexMarkForUpdate(CGxTex* texId, const CiRect& updateRect, int32_t immediate) {
    texId->m_needsUpdate = 1;

    // If the bounds of the updateRect are invalid, default to { 0, 0, height, width }
    if (updateRect.minY >= updateRect.maxY || updateRect.minX >= updateRect.maxX) {
        texId->m_updateRect = {
            0,
            0,
            static_cast<int32_t>(texId->m_height),
            static_cast<int32_t>(texId->m_width)
        };
    } else {
        texId->m_updateRect = updateRect;
    }

    if (immediate) {
        this->ITexMarkAsUpdated(texId);
    }
}

void CGxDevice::TexSetWrap(CGxTex* texId, EGxTexWrapMode wrapU, EGxTexWrapMode wrapV) {
    if (texId->m_flags.m_wrapU == wrapU && texId->m_flags.m_wrapV == wrapV) {
        return;
    }

    texId->m_flags.m_wrapU = wrapU;
    texId->m_flags.m_wrapV = wrapV;
    texId->m_needsFlagUpdate = 1;

    for (int32_t rs = GxRs_Texture0; rs <= GxRs_Texture15; rs++) {
        this->IRsForceUpdate(static_cast<EGxRenderState>(rs));
    }
}

// A negative value only reads the flag; anything else stores (value > 0) and answers it.
// ref: FUN_00682dc0
int32_t CGxDevice::WindowVisibleFlag(int32_t value) {
    if (value >= 0) {
        this->m_windowVisible = value > 0;
        return value > 0;
    }

    return this->m_windowVisible;
}

// Empty in the release build: every caller reaches the shared three-byte return at 0x00653a10.
void CGxDevice::ValidateDraw(CGxBatch* batch, int32_t count) {
}

void CGxDevice::XformPop(EGxXform xf) {
    this->m_xforms[xf].Pop();
}

void CGxDevice::XformProjection(C44Matrix& matrix) {
    matrix = this->m_projection;
}

void CGxDevice::XformProjNative(C44Matrix& matrix) {
    matrix = this->m_projNative;

    if (this->m_api == GxApi_OpenGl) {
        matrix.c0 *= -1.0f;
        matrix.c1 *= -1.0f;
        matrix.c2 *= -1.0f;
        matrix.c3 *= -1.0f;
    }
}

// ref: FUN_0057c3a0
void CGxDevice::XformPush(EGxXform xf) {
    this->m_xforms[xf].Push();
}

// Push, then load the new top: one call in the reference, with both halves inlined.
// ref: FUN_00616a30
void CGxDevice::XformPush(EGxXform xf, const C44Matrix& matrix) {
    auto& stack = this->m_xforms[xf];

    stack.Push();
    stack.Top() = matrix;
}

// The reference indexes `device + 0x1008 + xf * 0x118`, and CGxMatrixStack is byte-for-byte
// that block: m_level at +0, m_dirty at +4, m_mtx[4] from +8 (0x100 bytes), m_flags[4] at +0x108,
// 0x118 in total. Top() sets m_dirty and clears F_Identity, which is the reference's
// `movb $0x1, 0x4(%eax)` and `andl $-0x2, (%ecx)`.
//
// Worth having the address: several functions INLINE this rather than call it -- FUN_0081f620 is
// one -- and the inlined form is unrecognisable until the 0x118 stride is worked out.
// ref: FUN_0057c450
void CGxDevice::XformSet(EGxXform xf, const C44Matrix& matrix) {
    this->m_xforms[xf].Top() = matrix;
}

void CGxDevice::XformSetProjection(const C44Matrix& matrix) {
    this->m_projection = matrix;
}

// ref: FUN_00689050
// A texgen built from the camera (modes 1 and 2) has to be rebuilt when the view moves, so every
// such stage is forced back through ISetTexGen. The reference walks TexGen0 to TexGen7 inclusive
// and tests (mode - 1) as unsigned, so mode 0 does not qualify. This used to stop one stage short
// and compare signed, which also forced every mode-0 stage.
void CGxDevice::XformSetView(const C44Matrix& matrix) {
    this->m_xforms[GxXform_View].Top() = matrix;

    for (int32_t i = GxRs_TexGen0; i <= GxRs_TexGen7; i++) {
        if (static_cast<uint32_t>(this->m_appRenderStates[i].m_value) - 1 <= 1) {
            this->IRsForceUpdate(static_cast<EGxRenderState>(i));
        }
    }
}

void CGxDevice::XformSetViewport(float minX, float maxX, float minY, float maxY, float minZ, float maxZ) {
    if (
        minX == this->m_viewport.x.l
        && maxX == this->m_viewport.x.h
        && minY == this->m_viewport.y.l
        && maxY == this->m_viewport.y.h
        && minZ == this->m_viewport.z.l
        && maxZ == this->m_viewport.z.h
    ) {
        return;
    }

    this->intF6C = 1;

    this->m_viewport.x.l = minX;
    this->m_viewport.x.h = maxX;
    this->m_viewport.y.l = minY;
    this->m_viewport.y.h = maxY;
    this->m_viewport.z.l = minZ;
    this->m_viewport.z.h = maxZ;
}

void CGxDevice::XformView(C44Matrix& matrix) {
    matrix = this->m_xforms[GxXform_View].m_mtx[this->m_xforms[GxXform_View].m_level];
}

// The sibling of XformView. Not a reference function of its own -- the reference reaches
// into the stack inline wherever it wants the world matrix -- but frozen already had the
// view accessor and having only one of the pair is how the second caller open-codes it.
void CGxDevice::XformWorld(C44Matrix& matrix) {
    matrix = this->m_xforms[GxXform_World].m_mtx[this->m_xforms[GxXform_World].m_level];
}

void CGxDevice::XformViewport(float& minX, float& maxX, float& minY, float& maxY, float& minZ, float& maxZ) {
    minX = this->m_viewport.x.l;
    maxX = this->m_viewport.x.h;
    minY = this->m_viewport.y.l;
    maxY = this->m_viewport.y.h;
    minZ = this->m_viewport.z.l;
    maxZ = this->m_viewport.z.h;
}

// ref: FUN_006908b0
bool GxExtensionListContains(const char* list, const char* extension) {
    if (!list || !extension) {
        return false;
    }

    int32_t length = static_cast<int32_t>(SStrLen(extension));

    while (*list) {
        if (SStrCmpI(list, extension, length) == 0) {
            return true;
        }

        // Step to the start of the next word: past this one, then past the spaces after it.
        while (*list && *list != ' ') {
            list++;
        }

        while (*list == ' ') {
            list++;
        }
    }

    return false;
}
