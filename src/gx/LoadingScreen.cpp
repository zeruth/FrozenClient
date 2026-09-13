#include "gx/LoadingScreen.hpp"
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "event/Event.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/Gx.hpp"
#include "gx/RenderState.hpp"
#include "gx/Coordinate.hpp"
#include "gx/Screen.hpp"
#include "gx/Shader.hpp"
#include "gx/font/GxuFont.hpp"
#include "gx/font/TextBlock.hpp"
#include "util/CStatus.hpp"
#include "util/Filesystem.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/texture/CGxTex.hpp"
#include "net/Types.hpp"
#include "util/SFile.hpp"
#include <cstring>
#include <cstdio>
#include <common/DataStore.hpp>
#include <common/Handle.hpp>
#include <common/Time.hpp>
#include <storm/Region.hpp>
#include <storm/String.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Rect.hpp>
#include <tempest/Vector.hpp>

// Ported from the original's LoadingScreen.cpp

#define LOADING_SCREEN_LAYER_ZORDER     9.0f
#define LOADING_SCREEN_EVENT_PRIORITY   8.0f
#define LOADING_SCREEN_KEEP_ALIVE_MS    30000
#define LOADING_SCREEN_MIN_PAINT_MS     250
#define LOADING_SCREEN_NUM_BAR_TEXTURES 2
#define LOADING_SCREEN_FINISH_PROGRESS  0.99f
#define LOADING_SCREEN_TIP_HEIGHT       0.018f
#define LOADING_SCREEN_TIP_BOTTOM       0.1f
#define LOADING_SCREEN_TIP_LINE_SPACING 0.005f

struct LOADINGBARELEMENT {
    const char* texturePath;
    int32_t isFill;
    float centerX;
    float centerY;
    float width;
    float height;
};

// Loading bar geometry (0x9E2DFC in the original)
static const LOADINGBARELEMENT s_barElements[LOADING_SCREEN_NUM_BAR_TEXTURES] = {
    { "Interface\\Glues\\LoadingBar\\Loading-BarFill",   1, 0.5f, 0.075f, 0.525f, 0.025f },
    { "Interface\\Glues\\LoadingBar\\Loading-BarBorder", 0, 0.5f, 0.075f, 0.6f,   0.05f  },
};

static HLAYER s_layer = nullptr;
static HTEXTURE s_barTextures[LOADING_SCREEN_NUM_BAR_TEXTURES] = { nullptr, nullptr };
static HTEXTURE s_backgroundTexture = nullptr;
static CGxShader* s_vertexShaders[2] = { nullptr, nullptr };
static CGxShader* s_pixelShader = nullptr;
static HTEXTFONT s_tipFont = nullptr;
static CGxString* s_tipString = nullptr;
static CGxStringBatch* s_tipBatch = nullptr;
static int32_t (*s_playerReadyCallback)() = nullptr;
static int32_t s_finishPending = 0;
static int32_t s_widescreen = 0;
static int32_t s_mapID = -1;
static int32_t s_isLogin = 0;
static float s_progress[3] = { 0.0f, 0.0f, 0.0f };
static float s_displayProgress = 0.0f;
static uint64_t s_keepAliveTime = 0;
static uint64_t s_lastPaintTime = 0;
static const char* s_tip = nullptr;
static int32_t s_eventsRegistered = 0;
static int32_t s_sizeEventPending = 0;
static EVENT_DATA_SIZE s_sizeEvent;

static const EVENTID s_swallowedEvents[] = {
    EVENT_ID_CHAR,
    EVENT_ID_IME,
    EVENT_ID_KEYDOWN,
    EVENT_ID_KEYDOWN_REPEATING,
    EVENT_ID_MOUSEDOWN,
    EVENT_ID_MOUSEMOVE,
};

// Input events are consumed while the loading screen is up (0x8E5250 in the original)
static int32_t SwallowEventHandler(const void* data, void* param) {
    return 0;
}

// Window size changes are remembered and replayed once the loading screen is gone (0x407AB0)
static int32_t SizeEventHandler(const void* data, void* param) {
    s_sizeEvent = *static_cast<const EVENT_DATA_SIZE*>(data);
    s_sizeEventPending = 1;

    return 0;
}

// Finishing is deferred to idle so the layer is not destroyed while layers are being painted
static int32_t IdleEventHandler(const void* data, void* param) {
    if (s_finishPending) {
        s_finishPending = 0;
        LoadingScreenFinish();
    }

    return 1;
}

// 0x407B00 in the original
static void RegisterEvents() {
    for (auto id : s_swallowedEvents) {
        EventRegisterEx(id, &SwallowEventHandler, nullptr, LOADING_SCREEN_EVENT_PRIORITY);
    }

    EventRegisterEx(EVENT_ID_SIZE, &SizeEventHandler, nullptr, LOADING_SCREEN_EVENT_PRIORITY);
    EventRegisterEx(EVENT_ID_IDLE, &IdleEventHandler, nullptr, LOADING_SCREEN_EVENT_PRIORITY);

    s_sizeEventPending = 0;
    s_eventsRegistered = 1;
}

// 0x407BD0 in the original
static void UnregisterEvents() {
    if (!s_eventsRegistered) {
        return;
    }

    // The flags select what an unregistration matches on (id, handler, param); with none set
    // it would strip every handler the client has
    for (auto id : s_swallowedEvents) {
        EventUnregister(id, &SwallowEventHandler);
    }

    EventUnregister(EVENT_ID_SIZE, &SizeEventHandler);
    EventUnregister(EVENT_ID_IDLE, &IdleEventHandler);

    s_eventsRegistered = 0;

    if (s_sizeEventPending) {
        // TODO the original re-posts the saved size event so the resize is applied
        s_sizeEventPending = 0;
    }
}

// Window aspect relative to 4:3, which is what the loading art is authored for
static float WindowAspect() {
    CRect windowSize;
    GxCapsWindowSize(windowSize);

    float width = windowSize.maxX - windowSize.minX;
    float height = windowSize.maxY - windowSize.minY;

    if (height <= 0.0f) {
        return 1.0f;
    }

    return (width / height) / (4.0f / 3.0f);
}

// Picks the loading art for the current map (0x409ED0 in the original). Widescreen variants are
// named with a "Wide" suffix before the extension and are used when the window is wider than 4:3.
static bool LoadBackgroundTexture() {
    CStatus status;
    s_widescreen = 0;
    s_backgroundTexture = nullptr;

    auto mapRec = g_mapDB.GetRecord(s_mapID);
    auto screenRec = mapRec ? g_loadingScreensDB.GetRecord(mapRec->m_loadingScreenID) : nullptr;

    if (screenRec && *screenRec->m_fileName) {
        if (screenRec->m_hasWideScreen && WindowAspect() > 1.0f + 0.001f) {
            char widePath[STORM_MAX_PATH];
            SStrCopy(widePath, screenRec->m_fileName, sizeof(widePath));

            char* extension = SStrChrR(widePath, '.');

            if (extension) {
                char extensionCopy[16];
                SStrCopy(extensionCopy, extension, sizeof(extensionCopy));
                SStrCopy(extension, "Wide", sizeof(widePath) - (extension - widePath));
                SStrPack(widePath, extensionCopy, sizeof(widePath));
            } else {
                SStrPack(widePath, "Wide", sizeof(widePath));
            }

            if (SFile::FileExists(widePath)) {

                // TODO trial client restrictions on streamed files
                s_backgroundTexture = TextureCreate(widePath, CGxTexFlags(), &status, 1);

                if (s_backgroundTexture) {
                    s_widescreen = 1;
                }
            }
        }

        if (!s_backgroundTexture && SFile::FileExists(screenRec->m_fileName)) {
            s_backgroundTexture = TextureCreate(screenRec->m_fileName, CGxTexFlags(), &status, 1);
        }
    }

    if (!s_backgroundTexture) {
        s_backgroundTexture = TextureCreate("Interface\\Glues\\loading", CGxTexFlags(), &status, 1);
    }

    return s_backgroundTexture != nullptr;
}

// Combines the progress values into the displayed fraction (0x4079D0 in the original)
static void CalcProgress() {
    float primary = 0.0f;
    float scale = 1.0f;

    if (s_progress[0] != 0.0f) {
        primary = s_progress[0];

        if (s_isLogin) {
            primary = s_progress[0] * 0.5f;
            scale = 0.5f;
        }
    }

    s_displayProgress = scale * s_progress[2] + s_progress[1] * 0.0f + primary;

    if (s_displayProgress < 0.0f) {
        s_displayProgress = 0.0f;
    } else if (s_displayProgress > 1.0f) {
        s_displayProgress = 1.0f;
    }

    // TODO trial client progress scaling
}

// Sends CMSG_KEEP_ALIVE every 30 seconds while loading (0x408520 in the original)
static void KeepAlive(uint64_t now) {
    if (now < s_keepAliveTime) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_KEEP_ALIVE));
    msg.Finalize();

    ClientServices::Send(&msg);

    s_keepAliveTime = now + LOADING_SCREEN_KEEP_ALIVE_MS;
}

static void DrawTexturedQuad(HTEXTURE texture, float left, float right, float bottom, float top) {
    auto gxTex = TextureGetGxTex(texture, 1, nullptr);

    if (!gxTex) {
        return;
    }

    static const uint16_t indices[] = { 0, 1, 2, 3 };

    C3Vector position[4] = {
        { left,  top,    0.0f },
        { right, top,    0.0f },
        { left,  bottom, 0.0f },
        { right, bottom, 0.0f },
    };

    static const C2Vector texCoords[4] = {
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 1.0f },
        { 1.0f, 1.0f },
    };

    static const CImVector white = { 0xFF, 0xFF, 0xFF, 0xFF };

    GxRsSet(GxRs_Texture0, gxTex);

    GxPrimLockVertexPtrs(4, position, sizeof(C3Vector), nullptr, 0, &white, 0, nullptr, 0, texCoords, sizeof(C2Vector), nullptr, 0);
    GxDrawLockedElements(GxPrim_TriangleStrip, 4, indices);
    GxPrimUnlockVertexPtrs();
}

// Renders the game tip into a font batch drawn above the loading bar (part of 0x40AB70)
static void DestroyTip() {
    if (s_tipBatch) {
        GxuFontDestroyBatch(s_tipBatch);
        s_tipBatch = nullptr;
    }

    if (s_tipString) {
        GxuFontDestroyString(s_tipString);
    }

    if (s_tipFont) {
        HandleClose(s_tipFont);
        s_tipFont = nullptr;
    }
}

static void CreateTip() {
    char fontFile[STORM_MAX_PATH];
    OsBuildFontFilePath("FRIZQT__.TTF", fontFile, sizeof(fontFile));

    if (!*fontFile) {
        return;
    }

    // The tip block is 515 units of a 1024 wide line, corrected for the aspect ratio
    float blockWidth = 515.0f / (CoordinateGetAspectCompensation() * 1024.0f);
    float fontSize = NDCToDDCHeight(blockWidth);

    s_tipFont = TextBlockGenerateFont(fontFile, 0x1, fontSize);

    auto face = s_tipFont ? TextBlockGetFontPtr(s_tipFont) : nullptr;

    if (!face) {
        return;
    }

    // Copy the tip and drop trailing line breaks
    char text[1024];
    SStrCopy(text, s_tip, sizeof(text));

    for (auto end = text + SStrLen(text); end > text && (end[-1] == '\r' || end[-1] == '\n'); end--) {
        end[-1] = '\0';
    }

    // Centered above the loading bar, moved up with the letterbox on narrow windows
    float ratio = WindowAspect();
    float letterbox = ratio < 1.0f ? (1.0f - ratio) * 0.5f : 0.0f;

    C3Vector position = { (1.0f - blockWidth) * 0.5f, letterbox + LOADING_SCREEN_TIP_BOTTOM, 1.0f };
    CImVector color = { 0xC8, 0xC8, 0xC8, 0xD7 };

    GxuFontCreateString(
        face,
        text,
        LOADING_SCREEN_TIP_HEIGHT,
        position,
        blockWidth,
        1.0f,
        LOADING_SCREEN_TIP_LINE_SPACING,
        s_tipString,
        GxVJ_Bottom,
        GxHJ_Left,
        0x0,
        color,
        0.0f,
        1.0f
    );

    if (!s_tipString) {
        return;
    }

    CImVector shadowColor = { 0x00, 0x00, 0x00, 0xFF };
    C2Vector shadowOffset = { 0.001f, -0.001f };
    GxuFontAddShadow(s_tipString, shadowColor, shadowOffset);

    s_tipBatch = GxuFontCreateBatch(false, false);
    GxuFontAddToBatch(s_tipBatch, s_tipString);

}

// Draws the loading bar (0x4090C0 in the original)
static void DrawBar() {
    for (int32_t i = 0; i < LOADING_SCREEN_NUM_BAR_TEXTURES; ++i) {
        if (!s_barTextures[i]) {
            continue;
        }

        const LOADINGBARELEMENT& element = s_barElements[i];

        float left = element.centerX - element.width * 0.5f;
        float bottom = element.centerY - element.height * 0.5f;
        float right = element.centerX + element.width * 0.5f;
        float top = element.centerY + element.height * 0.5f;

        if (element.isFill) {
            right = left + element.width * s_displayProgress;
        }

        DrawTexturedQuad(s_barTextures[i], left, right, bottom, top);
    }
}

// Layer paint callback (0x40A270 in the original)
static void Paint(void* param, const RECTF* rect, const RECTF* visible, float elapsedSec) {
    if (!g_theGxDevicePtr) {
        return;
    }

    // Once loading is nearly complete, the screen comes down as soon as the player's model is in
    if (s_displayProgress > LOADING_SCREEN_FINISH_PROGRESS && s_playerReadyCallback && s_playerReadyCallback()) {
        s_finishPending = 1;
        return;
    }

    if (!s_backgroundTexture && s_mapID != -1 && !LoadBackgroundTexture()) {
        s_mapID = -1;
    }

    float savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ;
    GxXformViewport(savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ);

    GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

    CImVector black = { 0x00, 0x00, 0x00, 0xFF };
    GxSceneClear(0x1, black);

    // Letterbox so the art keeps its aspect ratio

    float textureAspect = 1.0f;

    if (s_widescreen) {
        auto gxTex = TextureGetGxTex(s_backgroundTexture, 1, nullptr);

        if (gxTex && gxTex->m_height) {
            textureAspect = (static_cast<float>(gxTex->m_width) / static_cast<float>(gxTex->m_height)) / (4.0f / 3.0f);
        }
    }

    float ratio = WindowAspect() / textureAspect;
    float minX = 0.0f;
    float maxX = 1.0f;
    float minY = 0.0f;
    float maxY = 1.0f;

    if (ratio > 1.0f) {
        float width = 1.0f / ratio;
        minX = (1.0f - width) * 0.5f;
        maxX = minX + width;
    } else if (ratio < 1.0f) {
        minY = (1.0f - ratio) * 0.5f;
        maxY = minY + ratio;
    }

    GxXformSetViewport(minX, maxX, minY, maxY, 0.0f, 1.0f);

    C44Matrix projection;
    GxuXformCreateOrtho(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 500.0f, projection);
    GxXformSetProjection(projection);

    C44Matrix view;
    GxXformSetView(view);

    GxRsPush();

    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_DepthTest, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);

    // The device draws through the UI shaders, like the frame renderer
    auto vertexShader = g_theGxDevicePtr->StereoEnabled() ? s_vertexShaders[1] : s_vertexShaders[0];

    if (vertexShader && vertexShader->Valid()) {
        GxRsSet(GxRs_VertexShader, vertexShader);

        C44Matrix viewProj;
        GxXformViewProjNativeTranspose(viewProj);
        GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<float*>(&viewProj), 4);
    }

    if (s_pixelShader && s_pixelShader->Valid()) {
        GxRsSet(GxRs_PixelShader, s_pixelShader);
    }

    if (s_backgroundTexture) {
        DrawTexturedQuad(s_backgroundTexture, 0.0f, 1.0f, 0.0f, 1.0f);
    }

    // TODO dynamic elements and the tip text

    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_AlphaRef, CGxDevice::s_alphaRef[GxBlend_Alpha]);

    DrawBar();

    if (s_tipBatch) {
        GxuFontRenderBatch(s_tipBatch);
    }

    GxRsPop();

    GxXformSetViewport(savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ);
}

// Creates the layer and draws the first frame (0x40A990 in the original)
static void Begin(int32_t isLogin) {
    CStatus status;
    s_progress[0] = 0.0f;
    s_progress[1] = 0.0f;
    s_progress[2] = 0.0f;
    s_isLogin = isLogin;

    if (!s_pixelShader && g_theGxDevicePtr) {
        g_theGxDevicePtr->ShaderCreate(s_vertexShaders, GxSh_Vertex, "Shaders\\Vertex", "UI", 2);
        g_theGxDevicePtr->ShaderCreate(&s_pixelShader, GxSh_Pixel, "Shaders\\Pixel", "UI", 1);
    }

    if (!s_layer) {
        for (int32_t i = 0; i < LOADING_SCREEN_NUM_BAR_TEXTURES; ++i) {
            if (!s_barTextures[i]) {

                s_barTextures[i] = TextureCreate(s_barElements[i].texturePath, CGxTexFlags(), &status, 0);
            }
        }

        RECTF rect = { 0.0f, 0.0f, 1.0f, 1.0f };
        ScrnLayerCreate(&rect, LOADING_SCREEN_LAYER_ZORDER, 0x6, nullptr, &Paint, &s_layer);

        RegisterEvents();
    }

    s_keepAliveTime = OsGetAsyncTimeMs() + LOADING_SCREEN_KEEP_ALIVE_MS;

    LoadingScreenUpdate(0);
}

bool LoadingScreenDrawing() {
    return s_layer != nullptr;
}

// 0x409550 in the original
void LoadingScreenFinish() {
    if (s_layer) {
        HandleClose(s_layer);
        s_layer = nullptr;
    }

    for (int32_t i = 0; i < LOADING_SCREEN_NUM_BAR_TEXTURES; ++i) {
        if (s_barTextures[i]) {
            HandleClose(s_barTextures[i]);
            s_barTextures[i] = nullptr;
        }
    }

    if (s_backgroundTexture) {
        HandleClose(s_backgroundTexture);
        s_backgroundTexture = nullptr;
    }

    UnregisterEvents();

    DestroyTip();

    // TODO dynamic elements

    s_tip = nullptr;
    s_mapID = -1;
    s_widescreen = 0;
}

void LoadingScreenInitialize() {
    // TODO
}

// 0x40AF90 in the original
void LoadingScreenSetProgress(float progress) {
    if (progress < s_progress[0]) {
        return;
    }

    s_progress[0] = progress;

    LoadingScreenUpdate(progress == 1.0f);
}

// 0x40AEF0 in the original
void LoadingScreenSetProgress2(float progress) {
    if (progress < s_progress[1]) {
        return;
    }

    s_progress[1] = progress;

    LoadingScreenUpdate(progress == 1.0f);
}

void LoadingScreenSetProgress3(float progress) {
    if (progress < s_progress[2]) {
        return;
    }

    s_progress[2] = progress;

    LoadingScreenUpdate(progress == 1.0f);
}

void LoadingScreenSetPlayerReadyCallback(int32_t (*callback)()) {
    s_playerReadyCallback = callback;
}

// 0x407E30 in the original
void LoadingScreenSetTip(const char* tip) {
    s_tip = tip;
}

// 0x40AB70 in the original
void LoadingScreenStart(int32_t mapID, int32_t isLogin) {
    DestroyTip();

    if (s_tip && *s_tip) {
        CreateTip();
    }

    s_mapID = mapID;

    Begin(isLogin);
}

// Repaints the loading screen outside the normal frame loop, at most every 250 ms unless forced
// (0x40A920 in the original)
void LoadingScreenUpdate(int32_t force) {
    uint64_t now = OsGetAsyncTimeMs();

    if (!force && now - s_lastPaintTime < LOADING_SCREEN_MIN_PAINT_MS) {
        return;
    }

    s_lastPaintTime = now;

    // TODO sound engine update

    KeepAlive(now);
    CalcProgress();

    if (!s_layer) {
        return;
    }

    Paint(nullptr, nullptr, nullptr, 0.0f);
    GxScenePresent(0);
}
