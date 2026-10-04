// PlayerName.cpp (0x007e50a0..0x007e64d0): the name plate every unit and some game objects carry
// (CGObject_C +0xb0). The object updates it each frame; while its name shows, the object's model
// draws it through its draw callback, with the raid target icon over it. The plate also owns up to
// four world texts (WorldText.cpp), which it ages and hands to the world text pass.
#include "ui/game/PlayerName.hpp"
#include "console/CVar.hpp"
#include "console/Command.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/font/CGxStringBatch.hpp"
#include "gx/font/GxuFont.hpp"
#include "model/CM2Model.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/Util.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "ui/game/RaidTarget.hpp"
#include "ui/game/WorldText.hpp"
#include "util/CStatus.hpp"
#include <common/Handle.hpp>
#include <common/Time.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/Matrix.hpp>
#include <cmath>
#include <cstring>
#include <new>

// The world's floating markers are hidden while the UI is (0x00ac80a8, CGObject_C.cpp).
extern int32_t s_showQuestMarkers;

#include "world/CWFrustum.hpp"
#include "world/CWorldScene.hpp"

namespace {

// Every plate (0x00af46fc).
STORM_EXPLICIT_LIST(PLAYERNAMEDESC, m_link) s_playerNames;

// The UnitName cvars' categories (0x00d380a0), the frame counter (0x00d380a4), the name batch and
// font (0x00d380a8, 0x00d380ac) and the raid target icons (0x00d380b0).
uint32_t s_unitNameMask;
uint32_t s_playerNameFrame;
CGxStringBatch* s_playerNameBatch;
CGxFont* s_playerNameFont;
HTEXTURE s_raidTargetIcons;

// The raid icon's quad, a yard square standing on its point (0x00af46ac), and its corner of the
// icon sheet (0x00af46dc), a quarter of the sheet per icon.
const C3Vector s_raidIconCorners[4] = {
    { -0.5f, 1.0f, 0.0f },
    { 0.5f, 1.0f, 0.0f },
    { 0.5f, 0.0f, 0.0f },
    { -0.5f, 0.0f, 0.0f },
};

const C2Vector s_raidIconCoords[4] = {
    { 0.0f, 0.0f },
    { 0.25f, 0.0f },
    { 0.25f, 0.25f },
    { 0.0f, 0.25f },
};

// The UnitName cvars, the category each sets and its default (0x007e6150).
struct UnitNameCVar {
    const char* name;
    const char* value;
    uint32_t mask;
};

const UnitNameCVar s_unitNameCVars[] = {
    { "UnitNameOwn", "0", 0x1 },
    { "UnitNameNPC", "0", 0x2 },
    { "UnitNamePlayerGuild", "1", 0x4 },
    { "UnitNamePlayerPVPTitle", "1", 0x8 },
    { "UnitNameEnemyPlayerName", "1", 0x10 },
    { "UnitNameEnemyPetName", "1", 0x20 },
    { "UnitNameEnemyGuardianName", "0", 0x1000 },
    { "UnitNameEnemyTotemName", "0", 0x40 },
    { "UnitNameFriendlyPlayerName", "1", 0x80 },
    { "UnitNameFriendlyPetName", "1", 0x100 },
    { "UnitNameFriendlyGuardianName", "0", 0x2000 },
    { "UnitNameFriendlyTotemName", "0", 0x200 },
    { "UnitNameNonCombatCreatureName", "0", 0x400 },
};

CGObject_C* PlayerNameObject(const PLAYERNAMEDESC* desc, int32_t line) {
    return ClntObjMgrObjectPtr(desc->m_guid, TYPE_OBJECT, ".\\PlayerName.cpp", line);
}

// Whether the camera is looking out of this plate's object's eyes.
bool IsFirstPersonOf(const PLAYERNAMEDESC* desc) {
    auto camera = CGWorldFrame::GetActiveCamera();

    return camera && desc->m_guid == camera->GetTarget() && camera->IsFirstPerson();
}

// ref: FUN_007e50a0
void ClearWorldText(PLAYERNAMEDESC* desc, int32_t style) {
    for (int32_t i = 3; i >= 0; i--) {
        auto text = desc->m_texts[i];

        if (!text) {
            continue;
        }

        if (style != 11 && style != text->m_style) {
            continue;
        }

        WorldTextDestroy(text);
        desc->m_texts[i] = nullptr;
    }
}

// ref: FUN_007e5170
// The plate's texts age; one that has lived its time, or any while the camera is in the object's
// eyes, goes.
void UpdateTexts(PLAYERNAMEDESC* desc, uint32_t now) {
    auto object = PlayerNameObject(desc, 0x87);

    if (!object || (!(desc->m_flags & 0x4) && (!object->m_model || !object->m_model->IsDrawable(0, 0)))) {
        return;
    }

    for (int32_t i = 3; i >= 0; i--) {
        auto text = desc->m_texts[i];

        if (!text) {
            continue;
        }

        if (IsFirstPersonOf(desc) || !WorldTextUpdate(text, now)) {
            WorldTextDestroy(text);
            desc->m_texts[i] = nullptr;
        }
    }
}

// ref: FUN_007e5230
void AddTextIcons(PLAYERNAMEDESC* desc) {
    auto object = PlayerNameObject(desc, 0x9c);

    if (!object || (!(desc->m_flags & 0x4) && (!object->m_model || !object->m_model->IsDrawable(0, 0)))) {
        return;
    }

    for (int32_t i = 3; i >= 0; i--) {
        if (desc->m_texts[i]) {
            WorldTextAddIcon(desc->m_texts[i]);
        }
    }
}

// ref: FUN_007e52a0
// The raid icon fades as the camera closes in: opaque from eight yards out, a little over an
// eighth of the way at four and under.
uint8_t RaidIconAlpha(const C3Vector& position) {
    auto camera = CGWorldFrame::GetActiveCamera();

    if (!camera) {
        return 0xff;
    }

    const C3Vector& eye = camera->Position();
    float dx = position.x - eye.x;
    float dy = position.y - eye.y;
    float dz = position.z - eye.z;
    float distance = std::sqrt(dy * dy + dz * dz + dx * dx);

    if (!(8.0f > distance)) {
        return 0xff;
    }

    if (4.0f < distance) {
        return static_cast<uint8_t>(static_cast<int32_t>(std::lrint((distance - 4.0f) * 55.25f + 34.0f)));
    }

    return 0x22;
}

// ref: FUN_007e5340
// Whether the object wears raid target icon `index`: while the UI shows, and for a unit only while
// it lives or carries a nameplate.
bool ShowsRaidIcon(CGObject_C* object, uint32_t index) {
    bool show = true;

    if (object && object->IsA(TYPE_UNIT)) {
        auto unit = static_cast<CGUnit_C*>(object);
        void* namePlate = unit->m_namePlate;
        bool dead = unit->IsDead();
        show = !namePlate && !dead;
    }

    return s_showQuestMarkers && index < 8 && show;
}

// ref: FUN_007e54d0
void ReleaseDesc(PLAYERNAMEDESC* desc) {
    if (desc->m_string) {
        GxuFontDestroyString(desc->m_string);
    }

    for (int32_t i = 3; i >= 0; i--) {
        if (desc->m_texts[i]) {
            WorldTextDestroy(desc->m_texts[i]);
        }
    }

    s_playerNames.UnlinkNode(desc);
}

// ref: FUN_007e55f0
PLAYERNAMEDESC* AllocDesc() {
    auto memory = SMemAlloc(sizeof(PLAYERNAMEDESC), __FILE__, __LINE__, 0x0);
    auto desc = new (memory) PLAYERNAMEDESC();
    desc->m_frame = s_playerNameFrame;

    return desc;
}

// ref: FUN_007e5640
// The name, made again when it or its colour went out of date, stood on the object's head; then the
// raid target icon over it, a camera-facing quad.
void RenderName(PLAYERNAMEDESC* desc) {
    if (!desc->m_guid) {
        return;
    }

    auto object = PlayerNameObject(desc, 0xe5);

    if (!object) {
        return;
    }

    int32_t shown = object->Virtual0D0(s_unitNameMask);
    float height = PlayerNameGetMarkerScale(object) * 0.2f;
    C3Vector position = object->GetHeadPosition();

    if (!shown) {
        if (desc->m_string) {
            GxuFontDestroyString(desc->m_string);
        }
    } else {
        if (desc->m_flags & 0x2) {
            desc->m_flags &= ~0x2;
            object->Virtual078(reinterpret_cast<int32_t*>(&desc->m_color.value));

            if (desc->m_string && !(desc->m_flags & 0x1)) {
                GxuFontSetStringColor(desc->m_string, desc->m_color);
            }
        }

        bool build = !desc->m_string;

        if (!build && (desc->m_flags & 0x1)) {
            GxuFontDestroyString(desc->m_string);
            desc->m_nameHeight = 0.0f;
            desc->m_string = nullptr;
            build = true;
        }

        if (build) {
            char name[1024];
            memset(name, 0, sizeof(name));

            int32_t lines = object->GetNameText(s_unitNameMask, name, sizeof(name));
            desc->m_nameHeight = static_cast<float>(static_cast<uint32_t>(lines)) * height;

            if (s_playerNameFont && name[0]) {
                C3Vector origin = { 0.0f, 0.0f, 1.0f };
                GxuFontCreateString(s_playerNameFont, LanguageProcess(name), height, origin, 100000.0f, 100000.0f, 0.0f,
                                    desc->m_string, GxVJ_Bottom, GxHJ_Center, 0xc8, desc->m_color, 0.0f, 1.0f);
            }

            desc->m_flags &= ~0x1;
        }

        if (desc->m_string) {
            GxuFontAddToBatch(s_playerNameBatch, desc->m_string);
            position.z = desc->m_nameHeight + position.z;
            GxuFontSetStringPosition(desc->m_string, position);
            GxuFontRenderBatch(s_playerNameBatch);
        }
    }

    uint32_t index = static_cast<uint32_t>(RaidTargetGetIndex(desc->m_guid));

    if (!ShowsRaidIcon(object, index)) {
        return;
    }

    CGxTex* texture = TextureGetGxTex(s_raidTargetIcons, 0, nullptr);

    if (!texture) {
        return;
    }

    if (shown) {
        position.z = position.z + height;
    }

    C44Matrix view;
    GxXformView(view);

    GxRsPush();
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_DepthTest, 0);
    GxRsSet(GxRs_DepthWrite, 0);

    uint8_t alpha = RaidIconAlpha(position);

    CGxBuf* vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x18, 4);
    auto vertices = reinterpret_cast<uint8_t*>(g_theGxDevicePtr->BufLock(vertexBuf));

    float u = static_cast<float>(index & 3) * 0.25f;
    float v = static_cast<float>(index >> 2) * 0.25f;

    for (uint32_t i = 0; i < 4; i++, vertices += 0x18) {
        memcpy(vertices, &s_raidIconCorners[i], sizeof(C3Vector));

        vertices[0xc] = 0xff;
        vertices[0xd] = 0xff;
        vertices[0xe] = 0xff;
        vertices[0xf] = alpha;

        C2Vector coord = { s_raidIconCoords[i].x + u, s_raidIconCoords[i].y + v };
        memcpy(vertices + 0x10, &coord, sizeof(C2Vector));
    }

    g_theGxDevicePtr->BufUnlock(vertexBuf, 0);
    vertexBuf->unk1C = 1;
    GxPrimVertexPtr(vertexBuf, GxVBF_PCT);

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
    GxRsSet(GxRs_Texture0, texture);

    // Stood at the head in view space and turned back out, so the quad faces the camera.
    C44Matrix world;
    world.Translate(position * view);
    world *= view.Inverse(view.Determinant());
    GxXformSet(GxXform_World, world);

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_count = 6;
    batch.m_minIndex = 0;
    batch.m_maxIndex = 3;
    GxDraw(&batch, 1);

    GxXformPop(GxXform_World);
    GxRsPop();
}

// ref: FUN_007e6090
void PlayerNameDrawCallback(CM2Model* model, CM2Lighting* lighting, void* arg) {
    (void)model;
    (void)lighting;

    auto desc = static_cast<PLAYERNAMEDESC*>(arg);

    if (!desc) {
        return;
    }

    auto object = PlayerNameObject(desc, 0x66);

    if (object && object->m_model->IsDrawable(0, 0)) {
        RenderName(desc);
    }
}

// ref: FUN_007e5c30
// A text into the plate's first free slot, started a little under the object's head -- or under
// the top of the view, when the head is above it -- and in the camera's transport's space when it
// rides one.
void AddText(PLAYERNAMEDESC* desc, int32_t style, const char* text, const CImVector* color, const int32_t* icon) {
    uint32_t slot = 0;

    for (; slot < 4; slot++) {
        if (!desc->m_texts[slot]) {
            break;
        }
    }

    if (slot == 4) {
        return;
    }

    auto object = PlayerNameObject(desc, 0x176);

    if (!object) {
        return;
    }

    C3Vector position = object->GetHeadPosition();
    position.z = position.z - 0.333333f;

    C3Vector corners[8] = {};
    CWorldScene::GetFrustumCorners(corners);

    if (corners[1].x == corners[0].x && corners[1].y == corners[0].y && corners[1].z == corners[0].z) {
        return;
    }

    // The far face pulled a tenth of the way in, and the first side face's near edge likewise.
    corners[5] = corners[5] * 0.9f + corners[4] * 0.1f;
    corners[6] = corners[6] * 0.9f + corners[7] * 0.1f;
    corners[1] = corners[1] * 0.9f + corners[0] * 0.1f;
    corners[2] = corners[2] * 0.9f + corners[3] * 0.1f;

    CWFrustum frustum(corners);
    const C4Plane& plane = frustum.planes[0];

    float limit = (-1.0f / plane.n.z) * (plane.n.y * position.y + plane.n.x * position.x + plane.d);

    if (limit < position.z) {
        position.z = limit;
        position.z = position.z - WorldTextGetRiseDistance(style);
    }

    auto camera = CGWorldFrame::GetActiveCamera();

    if (camera) {
        auto transport = ClntObjMgrObjectPtr(camera->m_relativeTo, TYPE_GAMEOBJECT, ".\\PlayerName.cpp", 0x193);

        if (transport) {
            C44Matrix matrix;
            transport->GetWorldMatrix(matrix);
            position = position * matrix.AffineInverse();
        }
    }

    desc->m_texts[slot] = WorldTextCreate(style, position, text, color, icon);
}

// ref: FUN_007e60e0
// A UnitName cvar sets or clears its category; a change makes every name again.
bool UnitNameCVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    (void)var;
    (void)oldValue;

    uint32_t mask = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    uint32_t categories = SStrToInt(value) ? s_unitNameMask | mask : s_unitNameMask & ~mask;

    if (categories == s_unitNameMask) {
        s_unitNameMask = categories;

        return true;
    }

    s_unitNameMask = categories;

    for (auto desc = s_playerNames.Head(); desc; desc = s_playerNames.Next(desc)) {
        desc->m_flags |= 0x1;
    }

    return true;
}

} // namespace

// ref: FUN_007e64d0
void PlayerNameInitialize() {
    PlayerNameShutdown();

    const char* fontName = nullptr;

    if (FrameScript_GetVariable("UNIT_NAME_FONT", &fontName)) {
        GxuFontCreateFont(fontName, 0.99f, s_playerNameFont, 0x4);
    }

    s_playerNameBatch = GxuFontCreateBatch(true, true);

    CStatus status;
    s_raidTargetIcons = TextureCreate("Interface\\TargetingFrame\\UI-RaidTargetingIcons", CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 0);
}

// ref: FUN_007e53a0
void PlayerNameShutdown() {
    if (s_playerNameFont) {
        GxuFontDestroyFont(s_playerNameFont);
    }

    s_playerNameFont = nullptr;

    GxuFontDestroyBatch(s_playerNameBatch);
    s_playerNameBatch = nullptr;

    // The plate allocator lets go of every plate.
    while (auto desc = s_playerNames.Head()) {
        ReleaseDesc(desc);
        desc->~PLAYERNAMEDESC();
        SMemFree(desc, __FILE__, __LINE__, 0x0);
    }

    ConsoleCommandUnregister("PlayerNames");

    if (s_raidTargetIcons) {
        HandleClose(s_raidTargetIcons);
        s_raidTargetIcons = nullptr;
    }
}

// ref: FUN_007e6150
void PlayerNameRegisterCVars() {
    for (auto& cvar : s_unitNameCVars) {
        CVar::Register(cvar.name, nullptr, 0x10, cvar.value, &UnitNameCVarCallback, GAME, false,
                       reinterpret_cast<void*>(static_cast<uintptr_t>(cvar.mask)), false);
    }
}

// ref: FUN_007e5f60
PLAYERNAMEDESC* PlayerNameCreate(const WOWGUID& guid) {
    if (!guid) {
        return nullptr;
    }

    auto object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, ".\\PlayerName.cpp", 0x1ef);

    if (!object || !object->m_model) {
        return nullptr;
    }

    auto desc = AllocDesc();
    desc->m_guid = guid;
    s_playerNames.LinkToTail(desc);

    return desc;
}

// ref: FUN_007e5fd0
PLAYERNAMEDESC* PlayerNameCreateAlways(const WOWGUID& guid) {
    if (!guid) {
        return nullptr;
    }

    auto object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, ".\\PlayerName.cpp", 0x201);

    if (!object) {
        return nullptr;
    }

    auto desc = AllocDesc();
    desc->m_guid = guid;
    desc->m_flags |= 0x4;
    s_playerNames.LinkToTail(desc);

    return desc;
}

// ref: FUN_007e6320
void PlayerNameDestroy(PLAYERNAMEDESC* desc) {
    if (!desc) {
        return;
    }

    auto object = PlayerNameObject(desc, 0x211);

    if (object && object->m_model) {
        object->m_model->m_flag20 = 1;
        object->m_model->m_drawCallback = nullptr;
        object->m_model->m_drawCallbackArg = nullptr;
    }

    ReleaseDesc(desc);
    desc->~PLAYERNAMEDESC();
    SMemFree(desc, __FILE__, __LINE__, 0x0);
}

// ref: FUN_007e6390
void PlayerNameUpdate(PLAYERNAMEDESC* desc) {
    if (!desc) {
        return;
    }

    auto object = PlayerNameObject(desc, 0x21e);

    if (!object) {
        return;
    }

    auto model = object->m_model;

    if (!model) {
        return;
    }

    desc->m_frame = s_playerNameFrame;

    uint32_t index = static_cast<uint32_t>(RaidTargetGetIndex(desc->m_guid));

    if (!object->Virtual0D0(s_unitNameMask) && !ShowsRaidIcon(object, index)) {
        model->m_flag20 = 1;
        model->m_drawCallback = nullptr;
        model->m_drawCallbackArg = nullptr;

        if (desc->m_flags & 0x8) {
            desc->m_flags &= ~0x8;
            object->UpdateQuestMarkerSequence();
        }

        return;
    }

    if (!(desc->m_flags & 0x8)) {
        desc->m_flags |= 0x8;
        object->UpdateQuestMarkerSequence();
    }

    model->m_flag20 = 0;
    model->m_drawCallback = &PlayerNameDrawCallback;
    model->m_drawCallbackArg = desc;
}

// ref: FUN_007e6030
void PlayerNameAddWorldText(PLAYERNAMEDESC* desc, int32_t style, const char* text, const CImVector* color, const int32_t* icon) {
    if (!desc) {
        return;
    }

    if (IsFirstPersonOf(desc)) {
        return;
    }

    if (ClntObjMgrGetPlayerType() == PLAYER_NORMAL) {
        AddText(desc, style, text, color, icon);
    }
}

// ref: FUN_007e5100
void PlayerNameClearWorldText(PLAYERNAMEDESC* desc, int32_t style) {
    if (desc) {
        ClearWorldText(desc, style);
    }
}

// ref: FUN_007e5120
void PlayerNameNewFrame() {
    s_playerNameFrame++;
}

// ref: FUN_007e5580
void PlayerNameExpireWorldText() {
    for (auto desc = s_playerNames.Head(); desc; desc = s_playerNames.Next(desc)) {
        if (desc->m_frame == s_playerNameFrame || (desc->m_flags & 0x4)) {
            continue;
        }

        for (int32_t i = 3; i >= 0; i--) {
            if (desc->m_texts[i]) {
                WorldTextDestroy(desc->m_texts[i]);
                desc->m_texts[i] = nullptr;
            }
        }
    }
}

// ref: FUN_007e5550
void PlayerNameInvalidateAllReactions() {
    for (auto desc = s_playerNames.Head(); desc; desc = s_playerNames.Next(desc)) {
        desc->m_flags |= 0x2;
    }
}

// ref: FUN_007e6480
void PlayerNameUpdateWorldText() {
    auto now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    for (auto desc = s_playerNames.Head(); desc; desc = s_playerNames.Next(desc)) {
        UpdateTexts(desc, now);
        AddTextIcons(desc);
    }
}

// ref: FUN_007e5140
void PlayerNameRenderWorldText() {
    WorldTextRender();
}

// ref: FUN_007e5130
void PlayerNameInvalidate(PLAYERNAMEDESC* desc) {
    if (desc) {
        desc->m_flags |= 0x1;
    }
}

// ref: FUN_007e50f0
// The name's colour is out of date (the unit's reaction to the player changed).
void PlayerNameInvalidateReaction(PLAYERNAMEDESC* desc) {
    if (desc) {
        desc->m_flags |= 0x2;
    }
}

// ref: FUN_007e5150
bool PlayerNameIsHighlighted(PLAYERNAMEDESC* desc) {
    return desc && (desc->m_flags & 0x8);
}

// ref: FUN_007e5420
float PlayerNameGetMarkerScale(CGObject_C* object) {
    float height;

    if (object->IsA(TYPE_UNIT)) {
        height = static_cast<CGUnit_C*>(object)->GetModelHeight();
    } else if (object->m_model && object->m_model->IsLoaded(0, 0) && object->m_model->HasAttachment(0x12)) {
        C3Vector attach = object->m_model->GetAttachmentWorldPosition(0x12);
        C3Vector position = object->m_model->GetPosition();
        height = attach.z - position.z;
    } else {
        height = object->m_height * object->m_scale * 1.25f;
    }

    if (4.0f < height) {
        return height * 0.25f * 1.5f;
    }

    return 1.0f;
}
