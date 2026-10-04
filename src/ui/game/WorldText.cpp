// WorldText.cpp (0x007e6590..0x007e7bb0): the text that floats up off units and objects -- combat
// text, damage and healing numbers, experience and honor -- and the PvP rank badge honor text
// carries. Each text is a WORLDTEXTSTRING owned by a name plate (PlayerName.cpp); this module ages
// it, finds where it lands on the screen, and draws the batch.
#include "ui/game/WorldText.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/Coordinate.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/font/CGxStringBatch.hpp"
#include "gx/font/GxuFont.hpp"
#include "gx/font/TextBlock.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "ui/game/ScreenLayout.hpp"
#include "util/CStatus.hpp"
#include <common/Handle.hpp>
#include <common/Time.hpp>
#include <storm/Array.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Rect.hpp>
#include <cmath>
#include <cstring>
#include <new>

namespace {

// 0x00af4748, with the colours its static initializer (0x009d04f0) fills in.
const WorldTextStyle s_worldTextStyles[11] = {
    { 2.0f, 150, 760, 1500, 0.0183333f, 0.0183333f, 0xffffffff },
    { 2.0f, 150, 90, 1500, 0.0183333f, 0.0183333f, 0xffffffff },
    { 0.0f, 150, 1000, 1500, 0.0f, 0.0275f, 0xffffffff },
    { 2.0f, 150, 1000, 1500, 0.0183333f, 0.0183333f, 0xffffffff },
    { 0.0f, 500, 2000, 4500, 0.0183333f, 0.0183333f, 0x8094008b },
    { 0.0f, 500, 2000, 4500, 0.0183333f, 0.0183333f, 0xffe0ca0a },
    { 2.0f, 150, 760, 1500, 0.0183333f, 0.0183333f, 0xff00ff00 },
    { 0.0f, 150, 1000, 1500, 0.0f, 0.0275f, 0xff00ff00 },
    { 6.0f, 150, 760, 1500, 0.0275f, 0.0275f, 0xffffffff },
    { 6.0f, 150, 760, 1500, 0.0275f, 0.0275f, 0xff00ff00 },
    { 2.0f, 150, 760, 1500, 0.0183333f, 0.0183333f, 0xffff9900 },
};

// The pop a critical (styles 2 and 7) makes as it appears (0x00a41618): from a tenth of its size to
// twice it over the first tenth of its life, back to its size over the next, then steady.
struct WorldTextPop {
    float start;
    float end;
    float from;
    float to;
};

const WorldTextPop s_worldTextPop[3] = {
    { 0.0f, 0.1f, 0.1f, 2.0f },
    { 0.1f, 0.2f, 2.0f, 1.0f },
    { 0.2f, 1.0f, 1.0f, 1.0f },
};

// The shadow's offset, in DDC (0x00af487c).
const C2Vector s_worldTextShadowOffset = { 0.002f, 0.002f };

// The badge quad, 32 pixels square (0x00af4884), and its texture coordinates (0x00af48b4).
const C3Vector s_worldTextIconCorners[4] = {
    { 0.0f, 1.0f, 0.0f },
    { 1.0f, 1.0f, 0.0f },
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f },
};

const C2Vector s_worldTextIconCoords[4] = {
    { 0.0f, 0.0f },
    { 1.0f, 0.0f },
    { 1.0f, 1.0f },
    { 0.0f, 1.0f },
};

const float WORLD_TEXT_ICON_SIZE = 32.0f;

// The badges (0x00af4708).
const char* const s_rankBadgePaths[15] = {
    "Interface\\PvPRankBadges\\PvPRank01.blp",
    "Interface\\PvPRankBadges\\PvPRank02.blp",
    "Interface\\PvPRankBadges\\PvPRank03.blp",
    "Interface\\PvPRankBadges\\PvPRank04.blp",
    "Interface\\PvPRankBadges\\PvPRank05.blp",
    "Interface\\PvPRankBadges\\PvPRank06.blp",
    "Interface\\PvPRankBadges\\PvPRank07.blp",
    "Interface\\PvPRankBadges\\PvPRank08.blp",
    "Interface\\PvPRankBadges\\PvPRank09.blp",
    "Interface\\PvPRankBadges\\PvPRank10.blp",
    "Interface\\PvPRankBadges\\PvPRank11.blp",
    "Interface\\PvPRankBadges\\PvPRank12.blp",
    "Interface\\PvPRankBadges\\PvPRank13.blp",
    "Interface\\PvPRankBadges\\PvPRank14.blp",
    "Interface\\PvPRankBadges\\PvPRank15.blp",
};

// One badge to draw this frame (SWorldTextIcon, 0x18 bytes): where, how big, its colour, its
// texture.
struct SWorldTextIcon {
    C3Vector position;      // +0x00, pixels
    float size;             // +0x0c
    CImVector color;        // +0x10
    HTEXTURE texture;       // +0x14
};

// The world text batch and font (0x00d380c8, 0x00d380cc), the badges (0x00d380d0) and this frame's
// badges (0x00d38120).
CGxStringBatch* s_worldTextBatch;
HTEXTFONT s_worldTextFont;
HTEXTURE s_rankBadges[15];
TSGrowableArray<SWorldTextIcon> s_worldTextIcons;

// The height the damage font is generated at, in DDC (0x00a41658).
const float WORLD_TEXT_FONT_HEIGHT = 0.0183333f;

// ref: FUN_007e6c60
WORLDTEXTSTRING* WorldTextAlloc() {
    auto memory = SMemAlloc(sizeof(WORLDTEXTSTRING), __FILE__, __LINE__, 0x0);

    return new (memory) WORLDTEXTSTRING();
}

// ref: FUN_007e6ac0
// A projection in pixels over the device's viewport.
void WorldTextSetupPixelProjection() {
    float minX, maxX, minY, maxY, minZ, maxZ;
    GxXformViewport(minX, maxX, minY, maxY, minZ, maxZ);

    float offsetX = 0.0f;
    float offsetY = 0.0f;

    if (!g_theGxDevicePtr->Caps().m_pixelCenterOnEdge) {
        offsetX = -0.5f;
        offsetY = 0.5f;
    }

    CRect window = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_theGxDevicePtr->CapsWindowSize(window);

    float width = static_cast<float>(static_cast<int32_t>(std::lrint(window.maxX - window.minX)));
    float height = static_cast<float>(static_cast<int32_t>(std::lrint(window.maxY - window.minY)));

    C44Matrix projection;
    GxuXformCreateOrtho(
        std::floor(width * minX) + offsetX,
        std::floor(width * maxX) + offsetX,
        std::floor(height * minY) + offsetY,
        std::floor(height * maxY) + offsetY,
        -1.0f,
        1.0f,
        projection
    );

    GxXformSetProjection(projection);
}

} // namespace

WORLDTEXTSTRING::~WORLDTEXTSTRING() {
    if (this->m_string) {
        GxuFontDestroyString(this->m_string);
    }
}

// ref: FUN_007e66f0
// The string made again at the current height, its size kept for the placement.
void WORLDTEXTSTRING::BuildString() {
    if (this->m_string) {
        GxuFontDestroyString(this->m_string);
    }

    this->m_string = nullptr;

    CGxFont* face = TextBlockGetFontPtr(s_worldTextFont);
    float height = DDCToNDCHeight(this->m_height);

    C2Vector shadow = { 0.0f, 0.0f };
    DDCToNDC(s_worldTextShadowOffset.x, s_worldTextShadowOffset.y, &shadow.x, &shadow.y);

    float width;
    GxuFontGetTextExtent(face, this->m_text, SStrLen(this->m_text), height, &width, shadow.x, 1.0f, 0.0f, 0);
    float textHeight = GxuFontGetWrappedTextHeight(face, this->m_text, height, width, shadow, 1.0f, 0.0f, 0);

    C3Vector position = { 0.0f, 0.0f, 1.0f };
    GxuFontCreateString(face, this->m_text, height, position, width, textHeight, 0.0f, this->m_string,
                        GxVJ_Bottom, GxHJ_Left, 0, this->m_color, 0.0f, 1.0f);
    GxuFontAddToBatch(s_worldTextBatch, this->m_string);

    this->m_halfWidth = width * 0.5f;
    this->m_halfHeight = 0.5f * textHeight;
}

// ref: FUN_007e6850
// The text fades in over its first moments and out over its last; its shadow follows.
void WORLDTEXTSTRING::UpdateFade(uint32_t elapsed) {
    const WorldTextStyle& style = s_worldTextStyles[this->m_style];

    float t = static_cast<float>(elapsed);
    float duration = static_cast<float>(style.duration);

    uint8_t alpha = 0xff;
    uint32_t shadow = 0x7f;

    if (elapsed < style.fadeInTime) {
        // The reference measures the fade-in against the whole life, not the fade-in time.
        int64_t a = std::llrint(255.0f * (t / duration));

        if (static_cast<uint32_t>(a) < 0x100) {
            alpha = static_cast<uint8_t>(a);
        }

        shadow = static_cast<uint32_t>(std::llrint((t / duration) * 127.0f));

        if (shadow > 0x7f) {
            shadow = 0x7f;
        }
    } else if (style.fadeOutTime <= elapsed) {
        uint32_t span = style.duration - style.fadeOutTime;
        float f = 0.0f;

        if (span != 0) {
            f = static_cast<float>(elapsed - style.fadeOutTime) / static_cast<float>(span);
        }

        float a = f * 255.0f;
        a = a < 0.0f ? 0.0f : (255.0f < a ? 255.0f : a);
        alpha = static_cast<uint8_t>(static_cast<int32_t>(std::lrint(255.0f - a)));

        float s = f * 127.0f;
        s = s < 0.0f ? 0.0f : (127.0f < s ? 127.0f : s);
        shadow = static_cast<uint32_t>(static_cast<int32_t>(std::lrint(255.0f - s))) & 0xff;
    }

    this->m_color.a = alpha;

    CImVector shadowColor;
    shadowColor.value = shadow << 24;

    GxuFontSetStringColor(this->m_string, this->m_color);
    GxuFontAddShadow(this->m_string, shadowColor, s_worldTextShadowOffset);
}

// ref: FUN_007e6590
float WORLDTEXTSTRING::GetRise(uint32_t elapsed) const {
    const WorldTextStyle& style = s_worldTextStyles[this->m_style];

    return (static_cast<float>(elapsed) / static_cast<float>(style.duration)) * style.riseDistance;
}

// ref: FUN_007e6cc0
// The height the style gives the text at this point of its life; the string is made again when it
// changes.
void WORLDTEXTSTRING::UpdateHeight(uint32_t elapsed) {
    const WorldTextStyle& style = s_worldTextStyles[this->m_style];
    float t = static_cast<float>(elapsed) / static_cast<float>(style.duration);

    float height;

    if (this->m_style == 2 || this->m_style == 7) {
        float scale = 1.0f;

        for (uint32_t i = 0; i < 3; i++) {
            const WorldTextPop& pop = s_worldTextPop[i];

            if (pop.start <= t && t < pop.end) {
                scale = (pop.to - pop.from) * ((t - pop.start) / (pop.end - pop.start)) + pop.from;
                break;
            }
        }

        height = scale * style.endHeight;
    } else {
        height = (style.endHeight - style.startHeight) * t + style.startHeight;
    }

    height = 0.001f < height ? height : 0.001f;

    if (this->m_height == height) {
        return;
    }

    this->m_height = height;
    this->BuildString();
}

// ref: FUN_007e70d0
// Age the text and put it where its point in the world lands on the screen, clear of the other
// texts there. False once it has lived its time or its point is off the side of the view.
bool WORLDTEXTSTRING::Update(uint32_t now) {
    const WorldTextStyle& style = s_worldTextStyles[this->m_style];

    if (static_cast<int32_t>(now - style.duration - this->m_startTime) >= 0) {
        return false;
    }

    uint32_t elapsed = now - this->m_startTime;

    this->UpdateHeight(elapsed);

    if (!this->m_string) {
        return true;
    }

    this->UpdateFade(elapsed);

    C4Vector point = { this->m_position.x, this->m_position.y, this->m_position.z, 1.0f };

    // A text made on a transport rides it.
    auto camera = CGWorldFrame::GetActiveCamera();

    if (camera) {
        auto transport = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(camera->m_relativeTo, TYPE_GAMEOBJECT, ".\\WorldText.cpp", 0xf6));

        if (transport) {
            C44Matrix matrix;
            transport->GetWorldMatrix(matrix);

            C4Vector moved;
            TransformVector4(&moved, point, matrix);
            point = moved;
        }
    }

    point.z = this->GetRise(elapsed) + point.z;

    auto frame = CGWorldFrame::s_currentWorldFrame;
    CRect frameRect = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (!frame || !frame->GetRect(&frameRect)) {
        return false;
    }

    C3Vector screen = { 0.0f, 0.0f, 0.0f };
    uint32_t onScreen = 0;
    C3Vector world = { point.x, point.y, point.z };
    frame->GetScreenCoordinates(world, &screen, &onScreen);

    if (!(onScreen & 0x1) || !(onScreen & 0x4)) {
        return false;
    }

    screen.x = screen.x + frameRect.minX;
    screen.y = screen.y + frameRect.minY;

    // Kept on the screen by its half size. The reference measures the half size in NDC against
    // the DDC point, and so does this.
    float halfWidth = this->m_halfWidth;
    float halfHeight = this->m_halfHeight;

    float x = halfWidth < screen.x ? screen.x : halfWidth;

    if (NDCToDDCWidth(1.0f) - halfWidth <= x) {
        x = NDCToDDCWidth(1.0f) - halfWidth;
    } else if (halfWidth < screen.x) {
        x = screen.x;
    } else {
        x = halfWidth;
    }

    float y = halfHeight < screen.y ? screen.y : halfHeight;

    if (NDCToDDCHeight(1.0f) - halfHeight <= y) {
        y = NDCToDDCHeight(1.0f) - halfHeight;
    } else if (halfHeight < screen.y) {
        y = screen.y;
    } else {
        y = halfHeight;
    }

    CRect rect;
    rect.minY = this->m_halfHeight + y;
    rect.minX = x - this->m_halfWidth;
    rect.maxY = y - this->m_halfHeight;
    rect.maxX = x + this->m_halfWidth;

    ScreenLayoutPlace(1, rect);

    this->m_screenPosition.x = DDCToNDCWidth((rect.maxX + rect.minX) * 0.5f) - this->m_halfWidth;
    this->m_screenPosition.y = DDCToNDCHeight((rect.maxY + rect.minY) * 0.5f);
    this->m_screenPosition.z = point.z;

    GxuFontSetStringPosition(this->m_string, this->m_screenPosition);

    return true;
}

// ref: FUN_007e7390
// An honor text's rank badge, beside it in pixels.
void WORLDTEXTSTRING::AddIcon() {
    if (this->m_icon <= 4) {
        return;
    }

    uint32_t rank = static_cast<uint32_t>(this->m_icon - 5);

    if (rank >= 15) {
        return;
    }

    CRect window = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_theGxDevicePtr->CapsWindowSize(window);

    float width = window.maxX - window.minX;
    float height = window.maxY - window.minY;

    auto icon = s_worldTextIcons.New();

    float size = this->m_halfHeight * height;
    icon->size = size + size;
    icon->texture = s_rankBadges[rank];
    icon->position = this->m_screenPosition;
    icon->position.x = icon->position.x * width - 40.0f;
    icon->position.y = height * icon->position.y - (icon->size - WORLD_TEXT_ICON_SIZE) * 0.5f;
    icon->color = this->m_color;
}

// ref: FUN_007e7bb0
void WorldTextInitialize() {
    auto fontName = FrameScript_GetText("DAMAGE_TEXT_FONT", -1, GENDER_NOT_APPLICABLE);
    s_worldTextFont = TextBlockGenerateFont(fontName, 0, DDCToNDCHeight(WORLD_TEXT_FONT_HEIGHT));
    s_worldTextBatch = GxuFontCreateBatch(false, false);

    CStatus status;

    for (uint32_t i = 0; i < 15; i++) {
        s_rankBadges[i] = TextureCreate(s_rankBadgePaths[i], CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 0);
    }
}

// ref: FUN_007e6a30
void WorldTextShutdown() {
    if (s_worldTextFont) {
        HandleClose(s_worldTextFont);
    }

    GxuFontDestroyBatch(s_worldTextBatch);

    for (uint32_t i = 0; i < 15; i++) {
        if (s_rankBadges[i]) {
            HandleClose(s_rankBadges[i]);
            s_rankBadges[i] = nullptr;
        }
    }
}

// ref: FUN_007e65e0
float WorldTextGetRiseDistance(int32_t style) {
    return s_worldTextStyles[style].riseDistance;
}

// ref: FUN_007e6dc0
WORLDTEXTSTRING* WorldTextCreate(int32_t style, const C3Vector& position, const char* text, const CImVector* color, const int32_t* icon) {
    if (!text || !*text) {
        return nullptr;
    }

    auto string = WorldTextAlloc();

    if (color) {
        string->m_color = *color;
    } else {
        string->m_color.value = s_worldTextStyles[style].color;
    }

    string->m_position = position;
    string->m_style = style;
    string->m_startTime = static_cast<uint32_t>(OsGetAsyncTimeMs());
    SStrCopy(string->m_text, text, sizeof(string->m_text));

    if (style == 5 && icon) {
        string->m_icon = *icon;
    }

    return string;
}

// ref: FUN_007e6a90
void WorldTextDestroy(WORLDTEXTSTRING* text) {
    text->~WORLDTEXTSTRING();
    SMemFree(text, __FILE__, __LINE__, 0x0);
}

// ref: FUN_007e7450
bool WorldTextUpdate(WORLDTEXTSTRING* text, uint32_t now) {
    if (!text) {
        return false;
    }

    return text->Update(now);
}

// ref: FUN_007e7470
void WorldTextAddIcon(WORLDTEXTSTRING* text) {
    if (text) {
        text->AddIcon();
    }
}

// ref: FUN_007e7490
// The texts, then this frame's badges as pixel quads over them.
void WorldTextRender() {
    GxuFontRenderBatch(s_worldTextBatch);

    if (s_worldTextIcons.Count() == 0) {
        return;
    }

    C44Matrix savedProjection;
    GxXformProjection(savedProjection);

    C44Matrix savedView;
    GxXformView(savedView);

    GxRsPush();
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();

    GxXformSetView(C44Matrix());
    WorldTextSetupPixelProjection();

    GxRsSet(GxRs_DepthTest, 0);
    GxRsSet(GxRs_DepthWrite, 0);

    CGxBuf* vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x14, 4);
    auto vertices = reinterpret_cast<uint8_t*>(g_theGxDevicePtr->BufLock(vertexBuf));

    for (uint32_t i = 0; i < 4; i++, vertices += 0x14) {
        C3Vector corner = {
            s_worldTextIconCorners[i].x * WORLD_TEXT_ICON_SIZE,
            s_worldTextIconCorners[i].y * WORLD_TEXT_ICON_SIZE,
            s_worldTextIconCorners[i].z * WORLD_TEXT_ICON_SIZE,
        };

        memcpy(vertices, &corner, sizeof(corner));
        memcpy(vertices + 0xc, &s_worldTextIconCoords[i], sizeof(C2Vector));
    }

    g_theGxDevicePtr->BufUnlock(vertexBuf, 0);
    vertexBuf->unk1C = 1;
    GxPrimVertexPtr(vertexBuf, GxVBF_PT);

    CGxBuf* indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, 6);
    auto indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(indexBuf));
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 3;
    indices[3] = 3;
    indices[4] = 1;
    indices[5] = 2;
    g_theGxDevicePtr->BufUnlock(indexBuf, 0);
    indexBuf->unk1C = 1;
    GxPrimIndexPtr(indexBuf);

    GxXformPush(GxXform_World);

    for (uint32_t i = 0; i < s_worldTextIcons.Count(); i++) {
        const SWorldTextIcon& icon = s_worldTextIcons[i];

        // DIVERGED-LOOKING BUT FAITHFUL: the reference draws the badge only when its texture has
        // NO device texture (0x007e79c8 jumps past the draw when TextureGetGxTex returns one), and
        // then with no texture bound. Reproduced as it is.
        if (TextureGetGxTex(icon.texture, 0, nullptr)) {
            continue;
        }

        CImVector diffuse;
        diffuse.value = 0x00ffffff | (static_cast<uint32_t>(icon.color.a) << 24);
        GxRsSet(GxRs_MatDiffuse, diffuse.value);
        GxRsSet(GxRs_Texture0, static_cast<CGxTex*>(nullptr));

        C44Matrix world;
        world.Translate(icon.position);
        GxXformSet(GxXform_World, world);

        CGxBatch batch;
        batch.m_primType = GxPrim_Triangles;
        batch.m_start = 0;
        batch.m_count = 6;
        batch.m_minIndex = 0;
        batch.m_maxIndex = 3;
        GxDraw(&batch, 1);
    }

    s_worldTextIcons.SetCount(0);

    GxXformPop(GxXform_World);
    GxRsPop();

    GxXformSetView(savedView);
    GxXformSetProjection(savedProjection);
}
