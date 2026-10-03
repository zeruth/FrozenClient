#include <cstdio>
#include <cstdlib>
#include "gx/Screen.hpp"
#include "gx/texture/TgaFile.hpp"
#include "event/Event.hpp"
#include "gx/Coordinate.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/Font.hpp"
#include "gx/Gx.hpp"
#include "gx/Transform.hpp"
#include "ui/FrameScript.hpp"
#include "util/Filesystem.hpp"
#include <ctime>
#include <storm/String.hpp>
#include <tempest/Matrix.hpp>

int32_t Screen::s_captureScreen = 0;
char Screen::s_capturePath[260] = { 0 };
float Screen::s_elapsedSec = 0.0f;
int32_t Screen::s_presentDisable = 0;
static HOBJECT s_stockObjects[SCRNSTOCKOBJECTS];
static float s_stockObjectHeights[SCRNSTOCKOBJECTS] = { 0.01953125f, 0.01953125f };
static STORM_EXPLICIT_LIST(CILayer, zorderlink) s_zOrderList;

int32_t OnIdle(const EVENT_DATA_IDLE* data, void* a2) {
    Screen::s_elapsedSec = data->elapsedSec + Screen::s_elapsedSec;

    return 1;
}

// FROZEN_AUTO_SCREENSHOT=<seconds>[,<seconds>...] captures the back buffer at each of those
// elapsed times and writes the usual Screenshots\\WoWScrnShot_*.tga.
//
// This exists because rendering bugs in this project are reported by eye and fixed blind. Capturing
// the SCREEN rectangle is not a substitute and has actively misled this work before -- it returns
// whatever window is in front, which one time was a different game entirely. This goes through
// ScreenshotRequest, so it captures THIS client's back buffer just before present, which is the
// only sound way to see what Frozen actually drew.
//
// Unset, it costs one getenv on the first frame and nothing after.
// Writes the frame the device captured on the last present the way the reference's TGA branch of
// ScreenshotRequest does (0x004a8570): the 32-bit capture taken as a top-down image, its alpha
// dropped, run-length encoded if that makes it smaller, and written with the TGA 2.0 footer.
static void ScreenshotWriteTga(const char* path) {
    uint32_t width;
    uint32_t height;
    const uint32_t* bits;
    GxCaptureGet(width, height, bits);

    if (!bits) {
        return;
    }

    TgaFile tga = {};

    if (tga.SetImage(bits, static_cast<uint16_t>(width), static_cast<uint16_t>(height), 32, 0, 1, 0)) {
        tga.RemoveAlphaChannel();
        tga.CompressRle();
        tga.Write(path);
    }

    tga.Close();
}

static void AutoScreenshotCheck() {
    static bool s_parsed = false;
    static float s_at[8];
    static uint32_t s_count = 0;
    static uint32_t s_fired = 0;

    if (!s_parsed) {
        s_parsed = true;

        const char* spec = getenv("FROZEN_AUTO_SCREENSHOT");

        if (spec) {
            while (*spec && s_count < 8) {
                s_at[s_count++] = static_cast<float>(atof(spec));

                while (*spec && *spec != ',') {
                    spec++;
                }

                if (*spec == ',') {
                    spec++;
                }
            }
        }
    }

    if (s_fired >= s_count) {
        return;
    }

    // Screen::s_elapsedSec is a PER-FRAME delta, not a running total: OnPaint zeroes it on the way
    // out and OnIdle accumulates into it between paints. So keep our own clock.
    static float s_total = 0.0f;

    s_total += Screen::s_elapsedSec;

    if (s_total >= s_at[s_fired]) {
        // Set the capture up DIRECTLY rather than going through ScreenshotRequest. That one also
        // signals script event 171, and reaching into the script layer from the paint callback is
        // what made the first version of this hook segfault at the glue screen.
        if (!OsDirectoryExists("Screenshots")) {
            OsCreateDirectory("Screenshots", 0);
        }

        SStrPrintf(Screen::s_capturePath, sizeof(Screen::s_capturePath),
                   "Screenshots/auto_%u.tga", s_fired);

        Screen::s_captureScreen = 1;

        s_fired++;
    }
}

int32_t OnPaint(const void* a1, void* a2) {
    AutoScreenshotCheck();

    // TODO
    // if (!g_theGxDevicePtr || !g_theGxDevicePtr->CapsHasContext(-1) || !g_theGxDevicePtr->CapsIsWindowVisible(-1)) {
    //     // TODO
    //     // - sound engine logic
    //
    //     return 1;
    // }

    CSRgn rgn;
    SRgnCreate(&rgn.m_handle, 0);

    RECTF baseRect = { 0.0f, 0.0f, 1.0f, 1.0f };
    SRgnCombineRectf(rgn.m_handle, &baseRect, nullptr, 2);

    // Walk the layer list backward (highest z-order to lowest) to establish visibility rects
    for (auto layer = s_zOrderList.Tail(); layer; layer = layer->zorderlink.Prev()) {
        SRgnGetBoundingRectf(rgn.m_handle, &layer->visible);

        layer->visible.left = std::max(layer->visible.left, layer->rect.left);
        layer->visible.bottom = std::max(layer->visible.bottom, layer->rect.bottom);
        layer->visible.right = std::min(layer->visible.right, layer->rect.right);
        layer->visible.top = std::min(layer->visible.top, layer->rect.top);

        if (!(layer->flags & 0x1)) {
            SRgnCombineRectf(rgn.m_handle, &layer->rect, nullptr, 4);
        }
    }

    SRgnDelete(rgn.m_handle);

    // Save viewport
    float minX, maxX, minY, maxY, minZ, maxZ;
    GxXformViewport(minX, maxX, minY, maxY, minZ, maxZ);

    // Walk the layer list forward (lowest z-order to highest) to paint visible layers
    for (auto layer = s_zOrderList.Head(); layer; layer = layer->zorderlink.Next()) {
        if (layer->visible.right > layer->visible.left && layer->visible.top > layer->visible.bottom) {
            if (layer->flags & 0x4) {
                GxXformSetViewport(
                    0.0f,
                    1.0f,
                    0.0f,
                    1.0f,
                    0.0f,
                    1.0f
                );
            } else {
                GxXformSetViewport(
                    layer->visible.left,
                    layer->visible.right,
                    layer->visible.bottom,
                    layer->visible.top,
                    0.0f,
                    1.0f
                );
            }

            if (layer->flags & 0x2) {
                C44Matrix identity;
                GxXformSetView(identity);

                C44Matrix orthoProj;
                GxuXformCreateOrtho(
                    layer->visible.left,
                    layer->visible.right,
                    layer->visible.bottom,
                    layer->visible.top,
                    0.0f,
                    500.0f,
                    orthoProj
                );
                GxXformSetProjection(orthoProj);
            }

            layer->paintfunc(
                layer->param,
                &layer->rect,
                &layer->visible,
                Screen::s_elapsedSec
            );
        }
    }

    // Restore viewport
    GxXformSetViewport(minX, maxX, minY, maxY, minZ, maxZ);

    GxuFontUpdate();

    if (!Screen::s_presentDisable) {
        if (Screen::s_captureScreen) {
            // Grab the finished frame before it is presented, then present it as usual. Capturing
            // here rather than inside the Lua binding is what makes the image a whole frame: at the
            // moment the binding runs, the interface is still being drawn on top of the world.
            GxCaptureRequest();
            Screen::s_captureScreen = 0;

            GxSub682A00();

            if (Screen::s_capturePath[0]) {
                ScreenshotWriteTga(Screen::s_capturePath);
            }

            return 1;
        }

        GxSub682A00();
    }

    Screen::s_elapsedSec = 0.0f;

    return 1;
}

void ILayerInitialize() {
    EventRegister(EVENT_ID_IDLE, reinterpret_cast<EVENTHANDLERFUNC>(OnIdle));
    EventRegister(EVENT_ID_PAINT, &OnPaint);
}

void IStockInitialize() {
    GxuFontInitialize();

    char fontFile[STORM_MAX_PATH];
    OsBuildFontFilePath("FRIZQT__.TTF", fontFile, sizeof(fontFile));

    if (*fontFile) {
        ScrnSetStockFont(STOCK_SYSFONT, fontFile);
    } else {
        // TODO
        // SErrSetLastError(0x57u);
    }

    if (*fontFile) {
        ScrnSetStockFont(STOCK_PERFFONT, fontFile);
    } else {
        // TODO
        // SErrSetLastError(0x57u);
    }
}

// Shared because the reference binds Screenshot in BOTH its glue and game tables -- a player on
// the login screen can take one just as well as a player in the world. Frozen had the body in the
// game binding only, so the glue's was a stub.
void ScreenshotRequest() {
    // Screenshots/WoWScrnShot_MMDDYY_HHMMSS.tga, the name the reference writes. The interface calls
    // this from a binding, and FrameXML has no way to name the file, so the name is made here.
    if (!OsDirectoryExists("Screenshots")) {
        OsCreateDirectory("Screenshots", 0);
    }

    time_t now = time(nullptr);
    struct tm parts;

    #if defined(WHOA_SYSTEM_WIN)
        localtime_s(&parts, &now);
    #else
        localtime_r(&now, &parts);
    #endif

    char path[260];

    SStrPrintf(path, sizeof(path), "Screenshots\\WoWScrnShot_%02d%02d%02d_%02d%02d%02d.tga",
               parts.tm_mon + 1, parts.tm_mday, parts.tm_year % 100,
               parts.tm_hour, parts.tm_min, parts.tm_sec);

    // Hand the name to the layer code and let it capture just before the frame is presented. The
    // interface is still drawing at this point, so grabbing the back buffer here would catch a
    // half-composed frame.
    SStrCopy(Screen::s_capturePath, path, sizeof(Screen::s_capturePath));
    Screen::s_captureScreen = 1;

    // Index 171 from g_scriptEvents. The capture has not happened yet, so this reports that one was
    // requested; there is no path by which the layer code can report back a failure today.
    FrameScript_SignalEvent(171, nullptr);
}

void ScrnInitialize(int32_t a1) {
    ILayerInitialize();

    // TODO
    // consoleInitialized = a1;

    IStockInitialize();
}

void ScrnLayerCreate(const RECTF* rect, float zOrder, uint32_t flags, void* param, void (*paintFunc)(void*, const RECTF*, const RECTF*, float), HLAYER* layerPtr) {
    static RECTF defaultrect = { 0.0f, 0.0f, 1.0f, 1.0f };
    const RECTF* r = rect ? rect : &defaultrect;

    auto m = SMemAlloc(sizeof(CILayer), __FILE__, __LINE__, 0x0);
    auto layer = new (m) CILayer();

    layer->rect.left = r->left;
    layer->rect.bottom = r->bottom;
    layer->rect.right = r->right;
    layer->rect.top = r->top;

    layer->zorder = zOrder;
    layer->flags = flags;
    layer->param = param;
    layer->paintfunc = paintFunc;

    auto node = s_zOrderList.Head();

    while (node && zOrder < node->zorder) {
        node = node->zorderlink.Next();
    }

    s_zOrderList.LinkNode(layer, 1, node);

    *layerPtr = HandleCreate(layer);
}

void ScrnLayerSetRect(HLAYER layer, const RECTF* rect) {
    static_cast<CILayer*>(HandleDereference(layer))->rect = *rect;
}

void ScrnSetStockFont(SCRNSTOCK stockID, const char* fontTexturePath) {
    if (s_stockObjects[stockID]) {
        HandleClose(s_stockObjects[stockID]);
    }

    float fontHeight = NDCToDDCHeight(s_stockObjectHeights[stockID]);
    HTEXTFONT font = TextBlockGenerateFont(fontTexturePath, 0, fontHeight);
    s_stockObjects[stockID] = font;
}
