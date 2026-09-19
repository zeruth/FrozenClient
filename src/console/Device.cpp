#include "console/Device.hpp"
#include <cstdio>
#include "gx/CGxMonitorMode.hpp"
#include <cstdlib>
#include "gx/Gx.hpp"
#include "client/gui/OsGui.hpp"
#include <storm/String.hpp>
#include "client/Gui.hpp"
#include "console/CVar.hpp"
#include "console/Console.hpp"
#include "event/Input.hpp"
#include "gx/Adapter.hpp"
#include "gx/Device.hpp"
#include <storm/Array.hpp>
#include <cstring>

static CGxDevice* s_device;
static CVar* s_cvGxAspect;
static CVar* s_cvGxColorBits;
static CVar* s_cvGxCursor;
static CVar* s_cvGxDepthBits;
static CVar* s_cvGxFixLag;
static CVar* s_cvGxMaximize;
static CVar* s_cvGxMaxFPS;
static CVar* s_cvGxMaxFPSBk;
static CVar* s_cvGxMultisample;
static CVar* s_cvGxMultisampleQuality;
static CVar* s_cvGxOverride;
static CVar* s_cvGxRefresh;
static CVar* s_cvGxResolution;
static CVar* s_cvGxStereoConvergence;
static CVar* s_cvGxStereoEnabled;
static CVar* s_cvGxStereoSeparation;
static CVar* s_cvGxTripleBuffer;
static CVar* s_cvGxVSync;
static CVar* s_cvGxWidescreen;
static CVar* s_cvGxWindow;
static CVar* s_cvVideoOptionsVersion;
static CVar* s_cvFixedFunction;
static CVar* s_cvWindowResizeLock;
static DefaultSettings s_defaults;
static TSGrowableArray<CGxMonitorMode> s_gxMonitorModes;
static bool s_hwDetect;
static bool s_hwChanged;
static CGxFormat s_requestedFormat;
static bool s_requestedStereoEnabled;
static bool s_gxOverrideSet[9];      // gxOverride: which slots were given (reference DAT_00cabac8)
static int32_t s_gxOverrideValue[9]; // and their values (DAT_00cabb7c)  // gxStereoEnabled, the reference's DAT_00cabd0c beside the format

// ref: FUN_00769240
bool CVGxColorBitsCallback(CVar*, const char*, const char* value, void*) {
    int32_t bits = SStrToInt(value);

    if (bits == 16) {
        s_requestedFormat.colorFormat = CGxFormat::Fmt_Rgb565;
        ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

        return true;
    }

    if (bits == 24) {
        s_requestedFormat.colorFormat = CGxFormat::Fmt_ArgbX888;
        ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

        return true;
    }

    if (bits == 30) {
        s_requestedFormat.colorFormat = CGxFormat::Fmt_Argb2101010;
        ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

        return true;
    }

    ConsoleWrite("Color bits must be 16, 24, or 30", DEFAULT_COLOR);

    return false;
}

// ref: FUN_007695e0
bool CVGxCursorCallback(CVar*, const char*, const char* value, void*) {
    s_requestedFormat.cursor = SStrToInt(value) != 0;
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_007692d0
bool CVGxDepthBitsCallback(CVar*, const char*, const char* value, void*) {
    int32_t bits = SStrToInt(value);

    if (bits == 16) {
        s_requestedFormat.depthFormat = CGxFormat::Fmt_Ds160;
        ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

        return true;
    }

    if (bits == 24) {
        s_requestedFormat.depthFormat = CGxFormat::Fmt_Ds24X;
        ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

        return true;
    }

    if (bits == 32) {
        s_requestedFormat.depthFormat = CGxFormat::Fmt_Ds320;
        ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

        return true;
    }

    ConsoleWrite("Depth bits must be 16, 24, or 32", DEFAULT_COLOR);

    return false;
}

// ref: FUN_007696a0
bool CVGxFixLagCallback(CVar*, const char*, const char* value, void*) {
    s_requestedFormat.fixLag = SStrToInt(value) != 0;
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_007695b0
bool CVGxMaximizeCallback(CVar*, const char*, const char* value, void*) {
    s_requestedFormat.maximize = SStrToInt(value) != 0;
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_0076a580
bool CVGxRefreshCallback(CVar*, const char*, const char* value, void*) {
    uint32_t refresh = SStrToUnsigned(value);

    TSGrowableArray<CGxMonitorMode> modes;
    GxAdapterMonitorModes(modes);

    uint32_t i = 0;
    while (i < modes.Count()) {
        if (modes[i].refreshRate == refresh) {
            break;
        }

        i++;
    }

    if (i == modes.Count()) {
        ConsoleWrite("Unsupported refresh rate", DEFAULT_COLOR);

        return false;
    }

    s_requestedFormat.refreshRate = refresh;
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_0076a220
bool CVGxResolutionCallback(CVar*, const char*, const char* value, void*) {
    int32_t width = -1;
    int32_t height = -1;
    char separator;
    sscanf(value, "%d%c%d", &width, &separator, &height);

    // Windowed: any size goes
    if (s_requestedFormat.window) {
        s_requestedFormat.size.x = width;
        s_requestedFormat.size.y = height;
        ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

        return true;
    }

    TSGrowableArray<C2iVector> resolutions;

    if (s_cvGxWidescreen->GetInt()) {
        TSGrowableArray<CGxMonitorMode> modes;
        GxAdapterMonitorModes(modes);

        C2iVector last = { 0, 0 };

        for (uint32_t i = 0; i < modes.Count(); i++) {
            const C2iVector& size = modes[i].size;

            if (static_cast<float>(size.x) / static_cast<float>(size.y) >= 1.248f
                && size.x >= 640
                && size.y >= 480
                && (size.x != last.x || size.y != last.y)) {
                last = size;
                resolutions.Add(1, &last);
            }
        }
    }

    if (resolutions.Count() == 0) {
        static const C2iVector defaults[] = {
            { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 960 }, { 1280, 1024 }, { 1600, 1200 }
        };

        for (const C2iVector& size : defaults) {
            C2iVector v = size;
            resolutions.Add(1, &v);
        }
    }

    uint32_t i = 0;
    while (i < resolutions.Count()) {
        if (width == resolutions[i].x && height == resolutions[i].y) {
            break;
        }

        i++;
    }

    if (i != resolutions.Count()) {
        s_requestedFormat.size.x = width;
        s_requestedFormat.size.y = height;
        ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

        return true;
    }

    char message[256];
    SStrCopy(message, "invalid resolution, must be one of ", sizeof(message));

    for (uint32_t j = 0; j < resolutions.Count(); j++) {
        if (j != 0) {
            SStrPack(message, ", ", sizeof(message));
        }

        if (SStrLen(message) > 100) {
            ConsoleWrite(message, DEFAULT_COLOR);
            message[0] = '\0';
        }

        char resolution[32];
        SStrPrintf(resolution, sizeof(resolution), "%dx%d", resolutions[j].x, resolutions[j].y);
        SStrPack(message, resolution, sizeof(message));
    }

    ConsoleWrite(message, DEFAULT_COLOR);

    return false;
}

bool CVGxStereoConvergenceCallback(CVar*, const char*, const char*, void*) {
    // TODO
    return true;
}

// ref: FUN_00769c00
bool CVGxStereoEnabledCallback(CVar*, const char*, const char* value, void*) {
    s_requestedFormat.stereoEnabled = SStrToInt(value) == 1;
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

bool CVGxStereoSeparationCallback(CVar*, const char*, const char*, void*) {
    // TODO
    return true;
}

// ref: FUN_00769360
bool CVGxTripleBufferCallback(CVar*, const char*, const char* value, void*) {
    int32_t tripleBuffer = SStrToInt(value);

    if (tripleBuffer != 0 && tripleBuffer != 1) {
        ConsoleWrite("TripleBuffer must be 0 or 1", DEFAULT_COLOR);

        return false;
    }

    s_requestedFormat.backBufferCount = (tripleBuffer != 0) + 1;
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_00769580
bool CVGxAspectCallback(CVar*, const char*, const char* value, void*) {
    s_requestedFormat.aspect = SStrToInt(value) != 0;
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_00769610
bool CVGxMultisampleCallback(CVar*, const char*, const char* value, void*) {
    s_requestedFormat.multisampleCount = SStrToInt(value);

    if (s_requestedFormat.multisampleCount < 2) {
        s_requestedFormat.multisampleCount = 1;
    } else if (s_requestedFormat.multisampleCount > 15) {
        s_requestedFormat.multisampleCount = 16;
    }

    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_00769650
bool CVGxMultisampleQualityCallback(CVar*, const char*, const char* value, void*) {
    float quality = SStrToFloat(value);
    float clamped = 0.0f;

    if (quality >= 0.0f && quality < 1.0f) {
        clamped = quality;
    } else if (quality >= 1.0f) {
        clamped = 1.0f;
    }

    s_requestedFormat.multisampleQuality = clamped;
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_007696d0
// "slot=value" pairs separated by spaces, commas or semicolons; slot 0 is remapped through the
// reference's table.
static void CVGxOverrideParse(const char* text) {
    char slotText[256];
    char valueText[256];

    while (*text) {
        SStrTokenize(&text, slotText, sizeof(slotText), " ,", nullptr);
        SStrTokenize(&text, valueText, sizeof(valueText), " ;", nullptr);

        if (!slotText[0] || !valueText[0]) {
            continue;
        }

        uint32_t slot = atol(slotText);
        int32_t value = atol(valueText);

        if (slot >= 9) {
            continue;
        }

        if (slot == 0) {
            switch (value) {
                case 0:
                case 1:
                case 2:
                    value = 1;
                    break;
                case 3:
                    value = 2;
                    break;
                case 4:
                    value = 3;
                    break;
                case 5:
                    value = 7;
                    break;
                case 6:
                    value = 8;
                    break;
                case 7:
                    value = 9;
                    break;
                case 8:
                    value = 10;
                    break;
                case 10:
                    value = 12;
                    break;
                case 11:
                    value = 13;
                    break;
                default:
                    break;
            }
        }

        s_gxOverrideSet[slot] = true;
        s_gxOverrideValue[slot] = value;
    }
}

// ref: FUN_00769810
bool CVGxOverrideCallback(CVar*, const char*, const char* value, void*) {
    CVGxOverrideParse(value);

    return true;
}

// ref: FUN_00769830
bool CVGxMaxFPSCallback(CVar*, const char*, const char* value, void*) {
    int32_t maxFps = SStrToInt(value);

    if (static_cast<uint32_t>(maxFps - 1) < 7) {
        maxFps = 8;
    }

    GxMaxFpsSet(maxFps);

    return true;
}

// ref: FUN_00769860
bool CVGxMaxFPSBkCallback(CVar*, const char*, const char* value, void*) {
    int32_t maxFps = SStrToInt(value);

    if (static_cast<uint32_t>(maxFps - 1) < 7) {
        maxFps = 8;
    }

    GxMaxFpsBkSet(maxFps);

    return true;
}

// The reference's callback is the folded "return true" at FUN_008a1420.
bool CVVideoOptionsVersionCallback(CVar*, const char*, const char*, void*) {
    return true;
}

// ref: FUN_00769520
bool CVGxVSyncCallback(CVar*, const char*, const char* value, void*) {
    s_requestedFormat.vsync = SStrToInt(value);
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_00769550
bool CVGxWindowCallback(CVar*, const char*, const char* value, void*) {
    s_requestedFormat.window = SStrToInt(value) != 0;
    ConsoleWrite("set pending gxRestart", DEFAULT_COLOR);

    return true;
}

// ref: FUN_00769890
bool CVWindowResizeLockCallback(CVar*, const char*, const char* value, void*) {
    OsGuiSetWindowResizeLock(SStrToInt(value));

    return true;
}

void RegisterGxCVars() {
    auto& format = s_defaults.format;

    // TODO CURRENT_LANGUAGE check?
    auto v1 = true;

    s_cvGxWidescreen = CVar::Register(
        "widescreen",
        "Allow widescreen support",
        0x0,
        "1",
        nullptr,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxWindow = CVar::Register(
        "gxWindow",
        "toggle fullscreen/window",
        0x1 | 0x2,
        v1 ? "1" : "0",
        &CVGxWindowCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxMaximize = CVar::Register(
        "gxMaximize",
        "maximize game window",
        0x1 | 0x2,
        v1 ? "1" : "0",
        &CVGxMaximizeCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    char colorBits[260];
    SStrPrintf(colorBits, sizeof(colorBits), "%s", CGxFormat::formatToBitsString[format.colorFormat]);
    s_cvGxColorBits = CVar::Register(
        "gxColorBits",
        "color bits",
        0x1 | 0x2,
        colorBits,
        &CVGxColorBitsCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    char depthBits[260];
    SStrPrintf(depthBits, sizeof(depthBits), "%s", CGxFormat::formatToBitsString[format.depthFormat]);
    s_cvGxDepthBits = CVar::Register(
        "gxDepthBits",
        "depth bits",
        0x1 | 0x2,
        depthBits,
        &CVGxDepthBitsCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    char resolution[260];
    SStrPrintf(resolution, 260, "%dx%d", format.size.x, format.size.y);
    s_cvGxResolution = CVar::Register(
        "gxResolution",
        "resolution",
        0x1 | 0x2,
        resolution,
        &CVGxResolutionCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxRefresh = CVar::Register(
        "gxRefresh",
        "refresh rate",
        0x1 | 0x2,
        "75",
        &CVGxRefreshCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxTripleBuffer = CVar::Register(
        "gxTripleBuffer",
        "triple buffer",
        0x1 | 0x2,
        "0",
        &CVGxTripleBufferCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    // TODO s_cvGxApi

    s_cvGxVSync = CVar::Register(
        "gxVSync",
        "vsync on or off",
        0x1 | 0x2,
        "1",
        &CVGxVSyncCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxAspect = CVar::Register(
        "gxAspect",
        "constrain window aspect",
        0x1 | 0x2,
        "1",
        &CVGxAspectCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxCursor = CVar::Register(
        "gxCursor",
        "toggle hardware cursor",
        0x1 | 0x2,
        "1",
        &CVGxCursorCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    char multisample[260];
    SStrPrintf(multisample, sizeof(multisample), "%d", 1); // TODO the reference formats the device's current multisample count
    s_cvGxMultisample = CVar::Register(
        "gxMultisample",
        "multisample",
        0x1 | 0x2,
        multisample,
        &CVGxMultisampleCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxMultisampleQuality = CVar::Register(
        "gxMultisampleQuality",
        "multisample quality",
        0x1 | 0x2,
        "0.0",
        &CVGxMultisampleQualityCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    char fixLag[260];
    SStrPrintf(fixLag, sizeof(fixLag), "%d", 0); // TODO value from s_hardware
    s_cvGxFixLag = CVar::Register(
        "gxFixLag",
        "prevent cursor lag",
        0x1 | 0x2,
        fixLag,
        &CVGxFixLagCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxStereoEnabled = CVar::Register(
        "gxStereoEnabled",
        "Enable stereoscopic rendering",
        0x1,
        "0",
        &CVGxStereoEnabledCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxOverride = CVar::Register(
        "gxOverride",
        "gx overrides",
        0x1,
        "",
        &CVGxOverrideCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxMaxFPS = CVar::Register(
        "maxFPS",
        "Set FPS limit",
        0x1,
        "200",
        &CVGxMaxFPSCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxMaxFPSBk = CVar::Register(
        "maxFPSBk",
        "Set background FPS limit",
        0x1,
        "30",
        &CVGxMaxFPSBkCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvVideoOptionsVersion = CVar::Register(
        "videoOptionsVersion",
        "Video options version",
        0x1 | 0x2,
        "0",
        &CVVideoOptionsVersionCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvWindowResizeLock = CVar::Register(
        "windowResizeLock",
        "prevent resizing in windowed mode",
        0x1,
        "0",
        &CVWindowResizeLockCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvFixedFunction = CVar::Register(
        "fixedFunction",
        "Force fixed function rendering",
        0x1 | 0x2,
        "0",
        nullptr,
        GRAPHICS,
        false,
        nullptr,
        false
    );
}

// ref: FUN_007698b0
void UpdateGxCVars() {
    s_cvGxColorBits->Update();
    s_cvGxDepthBits->Update();
    s_cvGxWindow->Update();
    s_cvGxResolution->Update();
    s_cvGxRefresh->Update();
    s_cvGxTripleBuffer->Update();

    // TODO s_cvGxApi->Update();

    s_cvGxVSync->Update();
    s_cvGxAspect->Update();
    s_cvGxMaximize->Update();
    s_cvGxCursor->Update();
    s_cvGxMultisample->Update();
    s_cvGxMultisampleQuality->Update();
    s_cvGxFixLag->Update();
}

// ref: FUN_00769950
void SetGxCVars(const CGxFormat& format) {
    char value[1024];

    s_cvGxColorBits->Set(CGxFormat::formatToBitsString[format.colorFormat], true, false, false, true);

    s_cvGxDepthBits->Set(CGxFormat::formatToBitsString[format.depthFormat], true, false, false, true);

    SStrPrintf(value, sizeof(value), "%d", format.window);
    s_cvGxWindow->Set(value, true, false, false, true);

    SStrPrintf(value, sizeof(value), "%dx%d", format.size.x, format.size.y);
    s_cvGxResolution->Set(value, true, false, false, true);

    SStrPrintf(value, sizeof(value), "%d", format.refreshRate);
    s_cvGxRefresh->Set(value, true, false, false, true);

    s_cvGxTripleBuffer->Set(format.backBufferCount < 2 ? "0" : "1", true, false, false, true);

    SStrPrintf(value, sizeof(value), "%d", format.vsync);
    s_cvGxVSync->Set(value, true, false, false, true);

    SStrPrintf(value, sizeof(value), "%d", format.aspect);
    s_cvGxAspect->Set(value, true, false, false, true);

    SStrPrintf(value, sizeof(value), "%d", format.maximize);
    s_cvGxMaximize->Set(value, true, false, false, true);

    SStrPrintf(value, sizeof(value), "%d", format.cursor);
    s_cvGxCursor->Set(value, true, false, false, true);

    SStrPrintf(value, sizeof(value), "%d", format.multisampleCount);
    s_cvGxMultisample->Set(value, true, false, false, true);

    SStrPrintf(value, sizeof(value), "%f", format.multisampleQuality);
    s_cvGxMultisampleQuality->Set(value, true, false, false, true);

    SStrPrintf(value, sizeof(value), "%d", format.fixLag);
    s_cvGxFixLag->Set(value, true, false, false, true);

    UpdateGxCVars();
}

void ConsoleDeviceStereoInitialize() {
    s_cvGxStereoConvergence = CVar::Register(
        "gxStereoConvergence",
        "Set stereoscopic rendering convergence depth",
        0x1,
        "1",
        &CVGxStereoConvergenceCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_cvGxStereoSeparation = CVar::Register(
        "gxStereoSeparation",
        "Set stereoscopic rendering separation percentage",
        0x1,
        "25",
        &CVGxStereoSeparationCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    // TODO stereo changed device callback
}

void ConsoleDeviceInitialize(const char* title) {
    // TODO

    // TODO proper logic
    s_hwDetect = true;

    // TODO ConsoleAccessSetEnabled(CmdLineGetBool(35));
    ConsoleAccessSetEnabled(1);

    // TODO

    RegisterGxCVars();

    // TODO ConsoleCommandRegister("gxRestart", &CCGxRestart, 1, nullptr);

    GxAdapterMonitorModes(s_gxMonitorModes);

    // TODO

    // The saved resolution, when it parses
    s_requestedFormat.size.x = 1024;
    s_requestedFormat.size.y = 768;

    if (s_cvGxResolution && s_cvGxResolution->GetString()) {
        int32_t width = 0;
        int32_t height = 0;
        char sep = 0;

        if (sscanf(s_cvGxResolution->GetString(), "%d%c%d", &width, &sep, &height) == 3 && width > 0 && height > 0) {
            s_requestedFormat.size.x = width;
            s_requestedFormat.size.y = height;
        }
    }
    s_requestedFormat.colorFormat = CGxFormat::Fmt_Argb8888;
    s_requestedFormat.depthFormat = CGxFormat::Fmt_Ds248;

    if (s_hwDetect || s_hwChanged) {
        // TODO Sub76B3F0(&UnkCABAF0, &UnkCABB38);
        // TODO s_cvFixedFunction->Set("0", 1, 0, 0, 1);
        // TODO memcpy(&s_requestedFormat, &s_defaults.format, sizeof(s_requestedFormat));

        s_requestedFormat.window = s_cvGxWindow->GetInt() != 0;
        s_requestedFormat.maximize = s_cvGxMaximize->GetInt() != 0;

        // TODO temporary override
        s_requestedFormat.maximize = 0;

        SetGxCVars(s_requestedFormat);
    }

    // TODO

    // TODO s_requestedFormat.hwTnL = !CmdLineGetBool(CMD_SW_TNL);
    s_requestedFormat.hwTnL = true;

    // TODO

    CGxFormat format;
    memcpy(&format, &s_requestedFormat, sizeof(s_requestedFormat));

    // TODO proper api selection
    EGxApi api = GxApi_OpenGl;
#if defined(WHOA_SYSTEM_WIN)
    api = GxApi_D3d9;
#elif defined(WHOA_SYSTEM_MAC)
    api = GxApi_GLL;
#endif

    s_device = GxDevCreate(api, OsWindowProc, format);

    // TODO

    auto gxWindow = GxDevWindow();
    OsGuiSetGxWindow(gxWindow);

    // TODO

    ConsoleDeviceStereoInitialize();

    // TODO
}

int32_t ConsoleDeviceExists() {
    return s_device != nullptr;
}
