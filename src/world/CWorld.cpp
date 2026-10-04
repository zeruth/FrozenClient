#include "model/CM2Shared.hpp"
#include "world/map/CMapObj.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "gx/Texture.hpp"
#include "world/map/CMapLight.hpp"
#include "world/DayNightLight.hpp"
#include "world/LightZonePaths.hpp"
#include "world/map/Particulates.hpp"
#include "world/map/WaterRipples.hpp"
#include "world/map/DetailDoodad.hpp"
#include "util/Log.hpp"
#include "world/CWFrustum.hpp"
#include "world/WorldFacets.hpp"
#include "world/map/CMapCollide.hpp"
#include "console/Console.hpp"
#include "console/Command.hpp"
#include "world/Shadow.hpp"
#include "model/CM2Lighting.hpp"
#include <cstdlib>
#include "model/CM2Model.hpp"
#include "world/CWorld.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapDoodadDef.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/MapShadow.hpp"
#include "world/ShadowMap.hpp"
#include "world/map/LiquidMaterialSettings.hpp"
#include "world/map/CMapBaseObj.hpp"
#include "world/map/CMapEntity.hpp"
#include "world/CWorldScene.hpp"
#include <tempest/ColorConvert.hpp>
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/Shader.hpp"
#include "gx/shader/CShaderEffectManager.hpp"
#include "model/Model2.hpp"
#include "world/CWorldParam.hpp"
#include "world/Map.hpp"
#include "world/MapWeather.hpp"
#include "world/map/CMap.hpp"
#include "gx/LoadingScreen.hpp"
#include "async/AsyncFileRead.hpp"
#include "util/SFile.hpp"
#include "db/Db.hpp"
#include "client/Client.hpp"
#include <storm/Memory.hpp>
#include <common/Time.hpp>
#include <storm/String.hpp>
#include <cstdio>
#include <cmath>
#include <vector>

uint32_t CWorld::s_curTimeMs;
float CWorld::s_curTimeSec;
uint32_t CWorld::s_enables;
uint32_t CWorld::s_enables2;
uint32_t CWorld::s_m2PassMask;
float CWorld::s_farClip;
float CWorld::s_horizonFarClipScale = 1.0f;
float CWorld::s_horizonNearClipScale = 0.7f;
uint32_t CWorld::s_gameTimeFixed;
float CWorld::s_gameTimeSec;
CM2Scene* CWorld::s_m2Scene;
float CWorld::s_nearClip = 0.1f;
void (*CWorld::s_loadProgressCallback)(float) = nullptr;
float CWorld::s_prevFarClip;
uint32_t CWorld::s_tickTimeFixed;
uint32_t CWorld::s_tickTimeMs;
float CWorld::s_tickTimeSec;
float CWorld::s_textureScroll[8][4];
int32_t CWorld::s_terrainShadowLevel;
const float CWorld::s_textureScrollDir[8][2] = {
    { -1.0f,  0.0f },
    { -1.0f,  1.0f },
    {  0.0f,  1.0f },
    {  1.0f,  1.0f },
    {  1.0f,  0.0f },
    {  1.0f, -1.0f },
    {  0.0f, -1.0f },
    { -1.0f, -1.0f },
};
Weather* CWorld::s_weather;
Particulates* CWorld::s_particulates;
bool CWorld::s_cameraUnderLiquid = false;
C3Vector CWorld::s_cameraDir = { 1.0f, 0.0f, 0.0f };
C3Vector CWorld::s_cameraPos = { 0.0f, 0.0f, 0.0f };
C3Vector CWorld::s_targetPos;
CAaBox CWorld::s_nearBox;
CAaBox CWorld::s_farBox;
float CWorld::s_updateFarClip;
int32_t CWorld::s_prevWindowMinY;
int32_t CWorld::s_prevWindowMinX;
int32_t CWorld::s_prevWindowMaxY;
int32_t CWorld::s_prevWindowMaxX;
int32_t CWorld::s_reloadMap;
int32_t CWorld::s_mapDirty;
int32_t CWorld::s_groundEffectDensity;
float CWorld::s_groundEffectDistSq;
int32_t CWorld::s_textureCacheSize;
int32_t CWorld::s_textureCacheDirty = 1;
int32_t CWorld::s_updateCount;
float CWorld::s_frameTimes[30];
uint32_t CWorld::s_frameTimeIndex;
void (*CWorld::s_updateCallback)();


void CWorld::SetCameraUnderLiquid(bool under) {
    CWorld::s_cameraUnderLiquid = under;
}

bool CWorld::IsCameraUnderLiquid() {
    return CWorld::s_cameraUnderLiquid;
}

namespace {

float AdjustFarClip(float farClip, int32_t mapID) {
    float minFarClip = 183.33333f;
    float maxFarClip = 1583.3334f;

    if (mapID < 530 || mapID == 575 || mapID == 543) {
        if (!CWorldParam::cvar_farClipOverride || CWorldParam::cvar_farClipOverride->GetInt() < 1) {
            maxFarClip = 791.66669f;
        }
    } else if (false /* TODO OsGetPhysicalMemory() <= 1073741824 */) {
        maxFarClip = 791.66669f;
    }

    return std::min(std::max(farClip, minFarClip), maxFarClip);
}

}

// ref: FUN_00781a10
HWORLDOBJECT CWorld::AddObject(CM2Model* model, void* handler, void* handlerParam, uint64_t param64, uint32_t param32, uint32_t objFlags) {
    auto entity = CMap::AllocEntity(objFlags & 0x8 ? true : false);

    entity->m_model = model;
    entity->m_param64 = param64;
    entity->m_param32 = param32;

    entity->m_dirLightScale = 1.0f;
    entity->m_dirLightScaleTarget = 1.0f;

    entity->m_type |= CMapBaseObj::Type_200;
    entity->m_handler = nullptr;

    // The entity's state word, rearranged out of the caller's flags. The reference writes it as
    // one expression over a preserved mask; spelled out, the bits move like this:
    //
    //   caller 0x01  ->  0x0002   the group list takes it at the head rather than the tail
    //   caller 0x02  ->  0x0800   INVERTED: clear here means the entity casts a blob shadow,
    //                             so an object says "no shadow" by setting its own bit 1
    //   caller 0x04  ->  0x0400   keep animating even when nothing can see it
    //   caller 0x08  ->  0x2000
    //   caller 0x10  ->  0x8000
    //
    // Everything else the word carries is left alone.
    uint32_t state = ((((objFlags & 0x10) << 3 | (objFlags & 0x4)) << 7) | (objFlags & 0x1)) << 1;

    state |= ~(objFlags << 10) & 0x800;
    state |= (objFlags >> 3 & 0x1) << 13;

    entity->m_flags7c = state | (entity->m_flags7c & 0xffff13fd);

    entity->m_flags = 0x0;

    if (objFlags & 0x20) {
        entity->m_flags = 0x20000;
    }

    // It starts at the sun's ambient, until the floor under it says otherwise.
    const C3Vector& amb = CMap::s_outdoorLight->m_light.m_ambColor;

    auto channel = [](float v) {
        if (!(0.0f < v)) {
            return 0.0f;
        }

        return v < 1.0f ? v * 255.0f + 0.5f : 255.0f;
    };

    CImVector start;
    start.b = static_cast<uint8_t>(static_cast<int32_t>(channel(amb.z)));
    start.g = static_cast<uint8_t>(static_cast<int32_t>(channel(amb.y)));
    start.r = static_cast<uint8_t>(static_cast<int32_t>(channel(amb.x)));
    start.a = 0xFF;
    entity->m_ambientTarget = start;
    entity->m_ambient = start;

    if (entity->m_model) {
        if (!SStrCmpI("InvisibleStalker.m2", entity->m_model->m_shared->m_filePath, STORM_MAX_STR)) {
            entity->m_flags7c |= 0x4000;
        }

        entity->m_model->m_lightingCallback = &CWorld::LightingCallback;
        entity->m_model->m_lightingArg = entity;
        entity->m_model->m_refCount++;
    }

    entity->m_handler = handler;
    entity->m_handlerParam = handlerParam;

    return reinterpret_cast<HWORLDOBJECT>(entity);
}

// ref: FUN_00782350
void CWorld::SetObjectModel(HWORLDOBJECT object, CM2Model* model) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    if (entity->m_model) {
        entity->m_model->m_lightingCallback = nullptr;
        entity->m_model->m_lightingArg = nullptr;
        entity->m_model->Release();
    }

    entity->m_flags7c &= ~0x4000u;
    entity->m_model = model;

    if (model) {
        if (!SStrCmpI("InvisibleStalker.m2", model->m_shared->m_filePath, STORM_MAX_STR)) {
            entity->m_flags7c |= 0x4000;
        }

        entity->m_model->m_lightingCallback = &CWorld::LightingCallback;
        entity->m_model->m_lightingArg = entity;
        entity->m_model->m_refCount++;
    }
}

// ref: FUN_007826e0
void CWorld::RemoveObject(HWORLDOBJECT object) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    // FROZEN-ONLY guard: the reference rewrites the focus every update before anything reads it,
    // but frozen's world update can run without a camera target, and the plane must not read a
    // freed entity.
    if (CWorld::s_focusEntity == reinterpret_cast<CMapStaticEntity*>(entity)) {
        CWorld::s_focusEntity = nullptr;
    }

    for (auto link = entity->m_parentLinkList.Head(); link; ) {
        auto next = entity->m_parentLinkList.Next(link);
        CMap::FreeBaseObjLink(link);
        link = next;
    }

    if (entity->m_model && entity->m_model->m_attachParent) {
        entity->m_model->DetachFromParent();
    }

    CMap::FreeEntity(entity);
}

// ref: FUN_0077f2c0
void CWorld::SetObjectHandler(HWORLDOBJECT object, void* handler, void* handlerParam) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    entity->m_handler = handler;
    entity->m_handlerParam = handlerParam;
}

// ref: FUN_0077f1e0
int32_t CWorld::GetObjectFloor(HWORLDOBJECT object, uint32_t* fieldBC, float* height, uint32_t* a4) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    if (entity && (entity->m_flags7c & 0x20)) {
        *height = entity->m_liquidHeight;
        *fieldBC = entity->m_fieldBC;
        *a4 = 0;

        return 1;
    }

    return 0;
}

// ref: FUN_0077f220
// The two liquid bits of an entity standing in liquid (flags 0x100 and 0x200 under 0x20).
int32_t CWorld::GetObjectLiquidFlags(HWORLDOBJECT object, uint32_t* bit8, uint32_t* bit9) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    if (entity && (entity->m_flags7c & 0x20)) {
        *bit8 = (entity->m_flags7c >> 8) & 1;
        *bit9 = (entity->m_flags7c >> 9) & 1;

        return 1;
    }

    return 0;
}

// ref: FUN_007815c0
void CWorld::UpdateWindowAndMap(const C3Vector& targetPos) {
    CWorld::s_prevWindowMinX = CMap::s_chunkWindowMinX;
    CWorld::s_prevWindowMinY = CMap::s_chunkWindowMinY;
    CWorld::s_prevWindowMaxY = CMap::s_chunkWindowMaxY;
    CWorld::s_prevWindowMaxX = CMap::s_chunkWindowMaxX;

    CWorld::UpdateWindow(targetPos);
    CMap::Update(0);
}

uint32_t CWorld::GetCurTimeMs() {
    return CWorld::s_curTimeMs;
}

float CWorld::GetCurTimeSec() {
    return CWorld::s_curTimeSec;
}

float CWorld::GetFarClip() {
    return CWorld::s_farClip;
}

// How far the world itself is drawn, which is NOT farclip.
//
// `farclip` is where fog ends and where the client stops bothering with doodads. The terrain,
// water and buildings behind that go out to `farclip * horizonFarclipScale` -- the CVar is
// registered with a default of 4.0 and, until now, was never read by anything: its callback is a
// bare TODO. So the camera's far plane was farclip, and every piece of distant geometry was
// frustum-culled the moment it passed it: a hard, sudden edge that took terrain and water with it,
// which is exactly what the user described and is not something fog can explain.
float CWorld::GetHorizonFarClip() {
    float scale = CWorldParam::cvar_horizonFarClip
        ? CWorldParam::cvar_horizonFarClip->GetFloat()
        : 4.0f;

    // A scale below 1 would pull the world IN of the fog, which cannot be what was meant.
    if (scale < 1.0f) {
        scale = 1.0f;
    }

    return CWorld::s_farClip * scale;
}

uint32_t CWorld::GetFixedPrecisionTime(float timeSec) {
    return static_cast<uint32_t>(timeSec * 1024.0f);
}

uint32_t CWorld::GetGameTimeFixed() {
    return CWorld::s_gameTimeFixed;
}

float CWorld::GetGameTimeSec() {
    return CWorld::s_gameTimeSec;
}

CM2Scene* CWorld::GetM2Scene() {
    return CWorld::s_m2Scene;
}

float CWorld::GetNearClip() {
    return CWorld::s_nearClip;
}

uint32_t CWorld::GetTickTimeFixed() {
    return CWorld::s_tickTimeFixed;
}

uint32_t CWorld::GetTickTimeMs() {
    return CWorld::s_tickTimeMs;
}

float CWorld::GetTickTimeSec() {
    return CWorld::s_tickTimeSec;
}

uint32_t CWorld::s_maxLod = 3;
uint32_t CWorld::s_waterRipples;
uint32_t CWorld::s_detailDoodadAlpha = 0x80;
// DAT_00adeebc, 1.0 in the image: characters take the plain ambient until a zone or the console
// says otherwise. Left at zero it blacked out every character.
float CWorld::s_characterAmbient = 1.0f;
CMapStaticEntity* CWorld::s_focusEntity = nullptr;
uint32_t CWorld::s_characterAmbientActive;
uint32_t CWorld::s_showSimpleDoodads;

// ref: FUN_0077f500
// The scene's projection callback: a batch flagged for projection is drawn onto whatever lies
// under it rather than as geometry, when projected textures are on or the model insists.
void CWorld::ProjectionCallback(const CAaBox& bounds, const CImVector& color, uint32_t shaded, void* context, uint32_t force) {
    (void)context;

    if (force || (CWorld::s_enables2 & Enables2::Enable_ProjectedTextures)) {
        DecalDrawBoundReceivers(bounds, color, 0x200122, shaded ? 2 : 0, 0.4f);
    }
}

static int32_t WorldToggle(uint32_t bit, const char* enabled, const char* disabled) {
    if (CWorld::s_enables & bit) {
        ConsoleWrite(disabled, DEFAULT_COLOR);
        CWorld::s_enables &= ~bit;
    } else {
        ConsoleWrite(enabled, DEFAULT_COLOR);
        CWorld::s_enables |= bit;
    }

    return 1;
}

// ref: FUN_0077f5b0
static int32_t ConsoleShowDetailDoodads(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_DetailDoodads, "Detail doodads enabled.", "Detail doodads disabled.");
}

// ref: FUN_0077f600
static int32_t ConsoleMaxLod(const char* command, const char* arguments) {
    uint32_t lod = 0;
    sscanf(arguments, "%d", &lod);

    if (lod > 3) {
        CWorld::s_maxLod = 3;
        return 1;
    }

    CWorld::s_maxLod = lod < 2 ? 2 : lod;

    return 1;
}

// ref: FUN_0077f650
static int32_t ConsoleShowCull(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_Culling, "Terrain culling enabled.", "Terrain culling disabled.");
}

// ref: FUN_0077f690
static int32_t ConsoleWaterRipples(const char* command, const char* arguments) {
    sscanf(arguments, "%d", &CWorld::s_waterRipples);

    return 1;
}

// ref: FUN_0077f6b0
static int32_t ConsoleWaterParticulates(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_Particulates, "Particulates enabled", "Particulates disabled");
}

// ref: FUN_0077f700
static int32_t ConsoleDetailDoodadAlpha(const char* command, const char* arguments) {
    uint32_t alpha = 0;
    sscanf(arguments, "%d", &alpha);

    if (alpha > 0xFF) {
        ConsoleWrite("Alpha ref range 0 - 255.", DEFAULT_COLOR);
        return 1;
    }

    CWorld::s_detailDoodadAlpha = alpha;

    return 1;
}

// ref: FUN_0077f750
// The multiplier is stored as 3x the argument; it is in force unless the argument is exactly 1.
static int32_t ConsoleCharacterAmbient(const char* command, const char* arguments) {
    float ambient = 0.0f;
    sscanf(arguments, "%f", &ambient);

    if (ambient < 0.0f || ambient > 1.0f) {
        ConsoleWrite("Ambient multiply range 0.0 - 1.0.", DEFAULT_COLOR);
        return 1;
    }

    CWorld::s_characterAmbient = ambient + ambient + ambient;
    CWorld::s_characterAmbientActive = ambient != 1.0f ? 1 : 0;

    return 1;
}

// ref: FUN_0077f7e0
static int32_t ConsoleShowShadow(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_Shadow, "Terrain shadow enabled.", "Terrain shadow disabled.");
}

// ref: FUN_0077f820
static int32_t ConsoleShowLowDetail(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_LowDetail, "Terrain low detail enabled.", "Terrain low detail disabled.");
}

// ref: FUN_0077f870
static int32_t ConsoleShowSimpleDoodads(const char* command, const char* arguments) {
    if (CWorld::s_showSimpleDoodads) {
        ConsoleWrite("Simple doodads disabled.", DEFAULT_COLOR);
        CWorld::s_showSimpleDoodads = 0;
    } else {
        ConsoleWrite("Simple doodads enabled.", DEFAULT_COLOR);
        CWorld::s_showSimpleDoodads = 1;
    }

    return 1;
}

void CWorld::Initialize() {
    CWorld::s_enables |=
          Enables::Enable_1
        | Enables::Enable_2
        | Enables::Enable_10
        | Enables::Enable_Culling
        | Enables::Enable_Shadow
        | Enables::Enable_100
        | Enables::Enable_200
        | Enables::Enable_800
        | Enables::Enable_ObjectFade
        | Enables::Enable_DetailDoodads
        | Enables::Enable_Liquid
        | Enables::Enable_Particulates
        | Enables::Enable_LowDetail;

    CWorld::s_gameTimeFixed = 0;
    CWorld::s_gameTimeSec = 0.0f;

    if (GxCaps().m_shaderTargets[GxSh_Pixel] > GxShPS_none) {
        CWorld::s_enables |= Enables::Enable_PixelShader;
    }

    if (GxCaps().m_shaderTargets[GxSh_Vertex] > GxShVS_none) {
        CWorld::s_enables2 |= Enables2::Enable_VertexShader;
    }

    // All three M2 passes on. The reference ORs a literal 7 here (0x780fb4); M2PASS_COUNT is 3,
    // so this is the same value said in terms of the enum rather than as a magic number.
    CWorld::s_m2PassMask |= (1 << M2PASS_COUNT) - 1;

    // TODO

    CWorld::s_m2Scene = M2CreateScene();

    // TODO

    uint32_t m2Flags = M2GetCacheFlags();
    CShaderEffect::InitShaderSystem(
        (m2Flags & 0x8) != 0,
        (CWorld::s_enables2 & Enables2::Enable_HwPcf) != 0
    );

    // The named effects the world draws through, in the order the reference reads them.
    CShaderEffectManager::LoadEffectFile("MapObj.wfx");
    CShaderEffectManager::LoadEffectFile("MapObjU.wfx");
    CShaderEffectManager::LoadEffectFile("Model2.wfx");
    CShaderEffectManager::LoadEffectFile("Particle.wfx");
    CShaderEffectManager::LoadEffectFile("ShadowMap.wfx");

    // TODO

    for (int32_t i = 0; i < 8; i++) {
        CWorld::s_textureScroll[i][0] = 0.0f;
        CWorld::s_textureScroll[i][1] = 0.0f;
        CWorld::s_textureScroll[i][2] = 0.0f;
        CWorld::s_textureScroll[i][3] = 1.0f;
    }

    CWorld::s_terrainShadowLevel = CWorldParam::cvar_shadowLevel ? CWorldParam::cvar_shadowLevel->GetInt() : 1;

    MapShadowInitialize();

    CWorldScene::Initialize();

    CMap::Initialize();

    // TODO

    CWorld::s_weather = STORM_NEW(Weather);
    CWorld::s_particulates = STORM_NEW(Particulates)(0.02777777798473835f, 30.0f, "Textures\\WaterPoop02.blp");

    // The projected-texture callback (0x00781340). The particle ground query installed beside it
    // (FUN_0077f540, 0x00781351) waits on the map segment query FUN_007a3b70.
    CWorld::s_m2Scene->SetProjectionCallback(reinterpret_cast<void*>(&CWorld::ProjectionCallback), nullptr);

    ConsoleCommandRegister("showDetailDoodads", ConsoleShowDetailDoodads, GRAPHICS, nullptr);
    ConsoleCommandRegister("maxLOD", ConsoleMaxLod, GRAPHICS, nullptr);
    ConsoleCommandRegister("showCull", ConsoleShowCull, GRAPHICS, nullptr);
    // "setShadow" (FUN_00780e20) sets the terrain shadow colour through FUN_00780660, not ported.
    ConsoleCommandRegister("waterRipples", ConsoleWaterRipples, GRAPHICS, nullptr);
    ConsoleCommandRegister("waterParticulates", ConsoleWaterParticulates, GRAPHICS, nullptr);
    ConsoleCommandRegister("showShadow", ConsoleShowShadow, GRAPHICS, nullptr);
    ConsoleCommandRegister("showLowDetail", ConsoleShowLowDetail, GRAPHICS, nullptr);
    ConsoleCommandRegister("showSimpleDoodads", ConsoleShowSimpleDoodads, GRAPHICS, nullptr);
    ConsoleCommandRegister("detailDoodadAlpha", ConsoleDetailDoodadAlpha, GRAPHICS, nullptr);
    ConsoleCommandRegister("characterAmbient", ConsoleCharacterAmbient, GRAPHICS, nullptr);

    CWorld::InitializeLightZones();
}

void CWorld::LoadMap(const char* mapName, const C3Vector& position, int32_t mapID) {
    CWorld::s_farClip = AdjustFarClip(CWorldParam::cvar_farClip->GetFloat(), mapID);
    CWorld::s_nearClip = 0.2f;
    CWorld::s_prevFarClip = CWorld::s_farClip;

    // TODO

    // The reference (FUN_00781430) takes the far clip from the map, remembers it as the value the
    // map was updated with, builds the chunk window from the spawn position and seeds the
    // previous window with it, and only then loads the map, so the load-time update streams the
    // tiles round the spawn. The rest of that function (the streaming-trial hook and the liquid
    // setup FUN_008a1720 / FUN_008a1730 / FUN_008a1f50) is not ported yet.
    CWorld::s_updateFarClip = CWorld::s_farClip;
    CWorld::UpdateWindow(position);
    CWorld::s_prevWindowMinX = CMap::s_chunkWindowMinX;
    CWorld::s_prevWindowMinY = CMap::s_chunkWindowMinY;
    CWorld::s_prevWindowMaxY = CMap::s_chunkWindowMaxY;
    CWorld::s_prevWindowMaxX = CMap::s_chunkWindowMaxX;

    CMap::Load(mapName, mapID);

    // The liquid bank, after the map has settled what the device may do (FUN_00781430 tail): shader
    // materials need both the vertex and the pixel shader enable, specular water the map's
    // specular permission (0x00ce04a0, which CMap::LoadSettings derives from the same two words),
    // and the bank is emptied so the next lookup builds against the new settings.
    Liquid::SetShaderMaterials(
        (CWorld::s_enables2 & CWorld::Enables2::Enable_VertexShader)
        && (CWorld::s_enables & CWorld::Enables::Enable_PixelShader));
    Liquid::SetSpecular(
        (CWorld::s_enables & CWorld::Enables::Enable_8000000)
        && (CWorld::s_enables & CWorld::Enables::Enable_PixelShader));
    Liquid::ReleaseMaterials();

    // TODO the terrain, map objects, and doodads around the position are loaded here, and the
    // original reports their progress through the callback as they come in

    if (CWorld::s_loadProgressCallback) {
        CWorld::s_loadProgressCallback(1.0f);
    }
}

int32_t CWorld::OnTick(const EVENT_DATA_TICK* data, void* param) {
    CWorld::SetUpdateTime(data->tickTimeSec, data->curTimeMs);

    return 1;
}

void CWorld::SetFarClip(float farClip) {
    farClip = AdjustFarClip(farClip, CMap::s_mapID);

    if (CWorld::s_farClip == farClip) {
        return;
    }

    CWorld::s_prevFarClip = CWorld::s_farClip;
    CWorld::s_farClip = farClip;

    // TODO CMapRenderChunk::DirtyPools();

    CWorld::s_nearClip = 0.2f;

    CMapObj::s_farClipDirty = 1;
    CWorld::s_textureCacheDirty = 1;
}

// ref: FUN_00780cd0
// A model in the world takes the sun when nothing placed it; a placed one is fogged by the frame's
// fog and asks its map object for its lights and its side of the water.
void CWorld::LightingCallback(CM2Model* model, CM2Lighting* lighting, void* arg) {
    lighting->m_flags |= 0x10;

    auto block = DayNightGetBlock();
    const float k = 1.0f / 255.0f;

    if (!arg) {
        C3Vector ambient = { block->ambient.r * k, block->ambient.g * k, block->ambient.b * k };
        lighting->AddAmbient(ambient);

        C3Vector diffuse = { block->diffuse.r * k, block->diffuse.g * k, block->diffuse.b * k };
        lighting->AddDiffuse(diffuse, block->direction);
    } else {
        C3Vector fog = { block->fogColor.r * k, block->fogColor.g * k, block->fogColor.b * k };
        lighting->SetFog(fog, block->fogStart, block->fogEnd, block->fogRate);

        auto obj = static_cast<CMapBaseObj*>(arg);
        obj->SelectLights(lighting);
        obj->SelectUnderwater(lighting);

        if (obj->m_flags & 0x2) {
            lighting->m_flags |= 0x8;
            return;
        }
    }

    lighting->m_flags &= ~0x8u;
}

// ref: FUN_0077eff0
void CWorld::RenderMap(const C3Vector& cameraPos, float dt) {
    CMap::Render(cameraPos, dt);
}

// ref: FUN_0077f8f0
void CWorld::SetUpdateCallback(void (*callback)()) {
    CWorld::s_updateCallback = callback;
}

// The loading screen's progress, as CMap::Update reports it while a far-clip jump has the
// loading screen up: FUN_0040af40 takes the progress alone, and the map's callback slot passes
// an argument after it that the reference's cdecl call simply leaves on the stack.
static void ReportLoadProgress(float progress, void* arg) {
    LoadingScreenSetProgress3(progress);
}

void CWorld::SetLoadProgressCallback(void (*callback)(float)) {
    CWorld::s_loadProgressCallback = callback;
}

void CWorld::SetUpdateTime(float tickTimeSec, uint32_t curTimeMs) {
    auto tickTimeFixed = CWorld::GetFixedPrecisionTime(tickTimeSec);

    CWorld::s_curTimeMs = curTimeMs;
    CWorld::s_curTimeSec = static_cast<float>(curTimeMs) * 0.001f;

    CWorld::s_gameTimeFixed += tickTimeFixed;
    CWorld::s_gameTimeSec += tickTimeSec;

    CWorld::s_tickTimeFixed = tickTimeFixed;
    CWorld::s_tickTimeMs = static_cast<uint32_t>(tickTimeSec * 1000.0f);
    CWorld::s_tickTimeSec = tickTimeSec;
}

// ref: FUN_00780860
// Everything the map streams against, from the target position: the two update boxes, the inner
// chunk rectangle (the target's chunk, widened by one chunk per 33 yards of far clip, snapped to
// even chunks) and the loaded window two chunks beyond it, widened to at least eight chunks and
// clamped to the map. The reference also rebuilds the camera matrix and frustum corners here
// (FUN_006bf370 / FUN_006bf6d0 into the device's transform stack), which is not ported yet.
void CWorld::UpdateWindow(const C3Vector& targetPos) {
    CWorld::s_targetPos = targetPos;

    // TODO camera facing -> matrix (FUN_006bf370), frustum corners (FUN_006bf6d0), device
    // transform (FUN_00407f80 on g_theGxDevicePtr's stack)

    const float NEAR_EXTENT = 150.0f;   // DAT_009f989c
    CWorld::s_nearBox.b = { targetPos.x - NEAR_EXTENT, targetPos.y - NEAR_EXTENT, targetPos.z - NEAR_EXTENT };
    CWorld::s_nearBox.t = { targetPos.x + NEAR_EXTENT, targetPos.y + NEAR_EXTENT, targetPos.z + NEAR_EXTENT };

    float farClip = CWorld::s_farClip;
    CWorld::s_farBox.b = { targetPos.x - farClip, targetPos.y - farClip, targetPos.z - farClip };
    CWorld::s_farBox.t = { targetPos.x + farClip, targetPos.y + farClip, targetPos.z + farClip };

    // One chunk of margin per chunk of far clip (the multiply is by -0.03 and the result truncated)
    int32_t margin = 1 - static_cast<int32_t>(farClip * -0.029999999329447746f);

    const float MAP_HALF_EXTENT = 17066.666015625f;
    const float CHUNKS_PER_UNIT = 0.029999999329447746f;
    int32_t chunkY = static_cast<int32_t>(roundf(-(targetPos.x - MAP_HALF_EXTENT) * CHUNKS_PER_UNIT - 0.5f));
    int32_t chunkX = static_cast<int32_t>(roundf(-(targetPos.y - MAP_HALF_EXTENT) * CHUNKS_PER_UNIT - 0.5f));

    if (SFile::IsStreamingTrial()) {
        // TODO FUN_00420a50(chunkX, chunkY): streaming trial bookkeeping
    }

    int32_t hi = (chunkX + margin) & ~1;
    CMap::s_chunkInnerMinX = (chunkX - margin) & ~1;
    CMap::s_chunkInnerMaxX = hi + 1;
    CMap::s_chunkWindowMinX = CMap::s_chunkInnerMinX - 2;
    CMap::s_chunkWindowMaxX = hi + 3;

    hi = (chunkY + margin) & ~1;
    CMap::s_chunkInnerMinY = (chunkY - margin) & ~1;
    CMap::s_chunkInnerMaxY = hi + 1;
    CMap::s_chunkWindowMinY = CMap::s_chunkInnerMinY - 2;
    CMap::s_chunkWindowMaxY = hi + 3;

    if (CMap::s_chunkWindowMaxX - chunkX < 8) {
        int32_t widen = 8 - (CMap::s_chunkWindowMaxX - chunkX);
        CMap::s_chunkWindowMinY = (CMap::s_chunkWindowMinY - widen) & ~1;
        CMap::s_chunkWindowMinX = (CMap::s_chunkWindowMinX - widen) & ~1;
        CMap::s_chunkWindowMaxX = ((CMap::s_chunkWindowMaxX + widen) & ~1) + 1;
        CMap::s_chunkWindowMaxY = ((CMap::s_chunkWindowMaxY + widen) & ~1) + 1;
    }

    if (CMap::s_chunkInnerMinX < 0) CMap::s_chunkInnerMinX = 0;
    if (CMap::s_chunkInnerMaxX > 0x3FF) CMap::s_chunkInnerMaxX = 0x3FF;
    if (CMap::s_chunkInnerMinY < 0) CMap::s_chunkInnerMinY = 0;
    if (CMap::s_chunkInnerMaxY > 0x3FF) CMap::s_chunkInnerMaxY = 0x3FF;
    if (CMap::s_chunkWindowMinX < 0) CMap::s_chunkWindowMinX = 0;
    if (CMap::s_chunkWindowMaxX > 0x3FF) CMap::s_chunkWindowMaxX = 0x3FF;
    if (CMap::s_chunkWindowMinY < 0) CMap::s_chunkWindowMinY = 0;
    if (CMap::s_chunkWindowMaxY > 0x3FF) CMap::s_chunkWindowMaxY = 0x3FF;
}

// ref: FUN_0077f900
// When the base mip level or the far clip changed (s_textureCacheDirty) and gxTextureCacheSize leaves
// the choice to the client, size the device's texture cache from them: 64 MB, or 128 MB past a far
// clip of 727, at full detail; 16 MB, or 32 MB past 177, at the reduced base mip.
//
// DIVERGED in the one call that uses the size: the reference hands it to the device through its
// vtable slot 0xf4, which on the Direct3D 9 device is an empty `ret 4` (0x00632050). Frozen's D3D9
// device has nothing to receive it, so the choice is made and dropped as the reference's own device
// drops it.
void CWorld::UpdateTextureCacheSize() {
    if (!CWorld::s_textureCacheDirty || CWorld::s_textureCacheSize) {
        return;
    }

    CWorld::s_textureCacheDirty = 0;

    uint32_t baseMip = g_theGxDevicePtr->DeviceBaseMipLevel();
    int32_t farClip = static_cast<int32_t>(CWorld::s_farClip);

    int32_t megabytes;

    if (baseMip == 0) {
        megabytes = farClip <= 727 ? 64 : 128;
    } else {
        megabytes = farClip <= 177 ? 16 : 32;
    }

    // The size the reference passes to the device's slot 0xf4; see above.
    uint32_t size = static_cast<uint32_t>(megabytes) << 20;
    (void)size;
}

// ref: FUN_007831a0
// The reference's frame update, of which the map part is ported: the previous chunk window is
// kept, the new one built from the target, a window that no longer overlaps flags a full reload,
// a far-clip jump of more than ten yards goes behind a loading screen, and CMap::Update runs.
void CWorld::Update(const C3Vector& cameraPos, const C3Vector& cameraTarget, const C3Vector& targetPos) {
    CWorld::s_cameraPos = cameraPos;

    C3Vector d = { cameraTarget.x - cameraPos.x, cameraTarget.y - cameraPos.y, cameraTarget.z - cameraPos.z };
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);

    if (len > 1e-4f) {
        CWorld::s_cameraDir = { d.x / len, d.y / len, d.z / len };
    }

    CWorld::UpdateTextureCacheSize();

    CWorld::s_frameTimes[CWorld::s_frameTimeIndex] = CWorld::s_tickTimeSec;
    CWorld::s_frameTimeIndex++;

    if (CWorld::s_frameTimeIndex == 30) {
        CWorld::s_frameTimeIndex = 0;
    }

    CWorld::s_prevWindowMinX = CMap::s_chunkWindowMinX;
    CWorld::s_prevWindowMinY = CMap::s_chunkWindowMinY;
    CWorld::s_prevWindowMaxY = CMap::s_chunkWindowMaxY;
    CWorld::s_prevWindowMaxX = CMap::s_chunkWindowMaxX;

    CWorld::UpdateWindow(targetPos);

    int32_t maxX = CWorld::s_prevWindowMaxX <= CMap::s_chunkWindowMaxX ? CWorld::s_prevWindowMaxX : CMap::s_chunkWindowMaxX;
    int32_t maxY = CWorld::s_prevWindowMaxY <= CMap::s_chunkWindowMaxY ? CWorld::s_prevWindowMaxY : CMap::s_chunkWindowMaxY;
    int32_t minX = CWorld::s_prevWindowMinX <= CMap::s_chunkWindowMinX ? CMap::s_chunkWindowMinX : CWorld::s_prevWindowMinX;
    int32_t minY = CWorld::s_prevWindowMinY <= CMap::s_chunkWindowMinY ? CMap::s_chunkWindowMinY : CWorld::s_prevWindowMinY;

    if (minY < maxY && minX < maxX) {
        CWorld::s_reloadMap = 0;
    } else {
        CWorld::s_reloadMap = 1;
        CWorld::s_mapDirty = 1;
    }

    // The shadow cascades were drawn over what is about to be unloaded (0x0078326d).
    g_shadowMapCascadesStale = CWorld::s_reloadMap;

    CWorld::s_updateCount++;

    if (CWorld::s_updateCallback) {
        CWorld::s_updateCallback();
    }

    // The texture scrolls step by the frame's tick (DAT_00cd76a0).
    float scrollStep = CWorld::s_tickTimeSec;

    for (int32_t i = 0; i < 8; i++) {
        float x = CWorld::s_textureScrollDir[i][0] * scrollStep + CWorld::s_textureScroll[i][0];
        CWorld::s_textureScroll[i][0] = x;
        float y = CWorld::s_textureScrollDir[i][1] * scrollStep + CWorld::s_textureScroll[i][1];
        CWorld::s_textureScroll[i][1] = y;

        if (64.0f <= x) {
            CWorld::s_textureScroll[i][0] = 0.0f;
        }
        if (64.0f <= y) {
            CWorld::s_textureScroll[i][1] = 0.0f;
        }
    }

    CWorldScene::UpdateCamera(cameraPos, cameraTarget);

    float farClipDelta = CWorld::s_farClip - CWorld::s_updateFarClip;
    bool smallChange = farClipDelta <= 10.0f;
    bool bigChange = 10.0f < farClipDelta;
    CWorld::s_updateFarClip = CWorld::s_farClip;

    if (bigChange) {
        CMap::s_loadProgressCallback = &ReportLoadProgress;
        CMap::s_loadProgressArg = nullptr;
        LoadingScreenStart(CMap::s_mapID, 1);
        CMap::s_loading = 1;
    }

    CMap::Update(smallChange);

    if (bigChange) {
        CMap::s_loading = 0;
        CMap::s_loadProgressCallback = nullptr;
        CMap::s_loadProgressArg = nullptr;
        AsyncFileReadSetProgressCallback(nullptr, nullptr);
        LoadingScreenFinish();
    }

    // Which building the camera is standing in, which is what CMap::Render's indoor branch runs
    // off. Safe to call now that that branch has somewhere to go: if the portal walk exposes
    // nothing it falls back to the outdoor traversal rather than to the unported teardown.
    CWorldScene::UpdateCameraDef();

    CWorld::UpdateDayNight(CWorld::s_forceDayNight | CWorld::s_reloadMap, &cameraPos);
    CWorld::s_frameFogEnd = DayNightGetBlock()->fogEnd;

    if (!g_theGxDevicePtr->MasterEnable(GxMasterEnable_Fog)) {
        CWorld::s_frameFogEnd = 1e10f;
    }

    CWorld::s_forceDayNight = 0;

    if ((CWorld::s_enables & CWorld::Enable_Particulates) && CWorldScene::s_cameraLiquidType != 0) {
        CWorld::s_particulates->Update();
    }

    CWorld::s_weather->Update();

    // The characters' ambient multiplier follows the zone the player stands in (0x007833fa ..
    // 0x007834df): AreaTable's ambient multiplier m, taken from the parent zone unless the area
    // carries flag 0x2000, gives a target of 2m + 1, eased toward at the frame's tick and snapped
    // within a hundredth or after a second. The console's characterAmbient holds it while active.
    auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

    if (player && !CWorld::s_characterAmbientActive && player->m_worldObject) {
        uint32_t areaID = 0;
        CWorld::GetEntityAreaID(reinterpret_cast<CMapStaticEntity*>(player->m_worldObject), &areaID);

        auto area = g_areaTableDB.GetRecord(areaID);

        if (area && !(area->m_flags & 0x2000) && area->m_parentAreaID) {
            area = g_areaTableDB.GetRecord(area->m_parentAreaID);
        }

        if (area) {
            float target = area->m_ambientMultiplier * 2.0f + 1.0f;
            float delta = target - CWorld::s_characterAmbient;
            float dt = CWorld::s_tickTimeSec;

            if (std::fabs(delta) < 0.01f || dt >= 1.0f) {
                CWorld::s_characterAmbient = target;
            } else {
                CWorld::s_characterAmbient += delta * dt;
            }
        }
    }

    // The world scene learns which side of the liquid the camera is on (0x007834e5).
    CWorld::s_m2Scene->m_cameraLiquidType = CWorldScene::s_cameraLiquidType;
}

// ref: FUN_0077f030
// The weather's draw, which CGWorldFrame::OnWorldRender calls on each side of the liquid pass
void CWorld::RenderWeather() {
    CWorld::s_weather->Render();
}

const C3Vector& CWorld::GetCameraPos() {
    return CWorld::s_cameraPos;
}

const C3Vector& CWorld::GetCameraDir() {
    return CWorld::s_cameraDir;
}

// ref: FUN_0077f490
void CWorld::SetNearClip(float nearClip) {
    CWorld::s_nearClip = nearClip;
}

// ref: FUN_0077f4a0
void CWorld::SetHorizonFarClipScale(float scale) {
    CWorld::s_horizonFarClipScale = scale;
}

// ref: FUN_0077f4b0
void CWorld::SetHorizonNearClipScale(float scale) {
    CWorld::s_horizonNearClipScale = scale;
}

// The distance bands (DAT_00adf350, see WorldDetailBands)
static WorldDetailBands s_detailBands = {
    { 1.0f, 4.0f, 15.0f, 100.0f, 100000.0f },
    { 30.0f, 100.0f, 200.0f, 750.0f, 1250.0f },
    { 1.0f, 4.0f, 15.0f, 100.0f, 100000.0f },
    { 5.0f, 10.0f, 15.0f, 20.0f, 50.0f },
    { 30.0f, 100.0f, 200.0f, 750.0f, 1250.0f },
    { 900.0f, 10000.0f, 40000.0f, 562500.0f, 1562500.0f },
    { 25.0f, 90.0f, 185.0f, 730.0f, 1200.0f },
    { 625.0f, 8100.0f, 34225.0f, 532900.0f, 1440000.0f },
};

const WorldDetailBands& CWorld::GetDetailBands() {
    return s_detailBands;
}

// ref: FUN_00781610
// The fixed-function fog states from the current light: start and end only on a device
// without vertex shaders (the shaders fog for themselves), the colour always
void CWorld::SetupFogRenderStates() {
    if (GxCaps().m_shaderTargets[GxSh_Vertex] == 0) {
        auto fogBlock = DayNightGetBlock();
        g_theGxDevicePtr->RsSet(GxRs_FogStart, fogBlock->fogStart < 0.0f ? 0.0f : fogBlock->fogStart);
        g_theGxDevicePtr->RsSet(GxRs_FogEnd, fogBlock->fogEnd);
    }

    C3Vector fog = DNColorVector(DayNightGetBlock()->finalFogColor);
    CImVector color;
    color.b = CM2Lighting::FogColorByte(fog.z);
    color.g = CM2Lighting::FogColorByte(fog.y);
    color.r = CM2Lighting::FogColorByte(fog.x);
    color.a = 0xFF;
    g_theGxDevicePtr->RsSet(GxRs_FogColor, color.value);
}

// ref: FUN_0078f570
void CWorld::SetEnvironmentDetail(float detail) {
    for (int32_t i = 0; i < 5; i++) {
        s_detailBands.nearDist[i] = s_detailBands.defaultNear[i];

        float farDist = s_detailBands.defaultFar[i];
        if (i != 0 && i != 4) {
            farDist *= detail;
        }

        s_detailBands.farDist[i] = farDist;
        s_detailBands.fadeStart[i] = farDist - s_detailBands.fadeWidth[i];
        s_detailBands.farDistSq[i] = farDist * farDist;
        s_detailBands.fadeStartSq[i] = s_detailBands.fadeStart[i] * s_detailBands.fadeStart[i];
    }
}

// ref: FUN_00780240
void CWorld::UpdateObject(HWORLDOBJECT object, const C44Matrix& matrix, const CAaBox& box,
                          const CAaSphere& sphere, const C3Vector& collisionCenter,
                          int32_t noRelink, uint32_t param) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    C3Vector position = { matrix.d0, matrix.d1, matrix.d2 };
    float scale = sqrtf(matrix.a0 * matrix.a0 + matrix.a1 * matrix.a1 + matrix.a2 * matrix.a2);
    C3Vector collision = collisionCenter * matrix;

    CAaSphere placedSphere = { position, 0.0f };

    if (0.001f < sphere.r) {
        placedSphere.r = scale * sphere.r;
        placedSphere.c = sphere.c * matrix;
    }

    CAaBox placedBox = { position, position };

    if (box.b.x < box.t.x && box.b.y < box.t.y && box.b.z < box.t.z) {
        placedBox = TransformBox(box, matrix);
    }

    auto distSq = [](const C3Vector& a, const C3Vector& b) {
        float x = a.x - b.x;
        float y = a.y - b.y;
        float z = a.z - b.z;

        return z * z + y * y + x * x;
    };

    float moved = distSq(entity->m_position, position);
    float collisionMoved = distSq(entity->m_collisionCenter, collision);
    float bottomMoved = distSq(entity->m_bounds.b, placedBox.b);
    float topMoved = distSq(entity->m_bounds.t, placedBox.t);
    float sphereMoved = distSq(entity->m_sphere.c, placedSphere.c);
    float shrunk = entity->m_sphere.r - placedSphere.r;

    entity->m_position = position;
    entity->m_collisionCenter = collision;
    entity->m_scale = scale;
    entity->m_bounds = placedBox;
    entity->m_sphere = placedSphere;
    entity->m_updateParam = param;

    if (noRelink) {
        return;
    }

    if (9.999999974752427e-07f < moved || 9.999999747378752e-05f < collisionMoved
        || 9.999999747378752e-05f < bottomMoved || 9.999999747378752e-05f < topMoved
        || 9.999999747378752e-05f < sphereMoved || 9.999999747378752e-05f < shrunk) {
        CMap::UpdateEntity(entity);
    }
}

// ref: FUN_00990560
// WMOAreaTable by (WMO id, name set, WMOGroupID). The reference binary-searches a copy sorted on
// those three; a linear walk of the table finds the same record.
static const WMOAreaTableRec* FindWMOArea(int32_t wmoID, int32_t nameSet, int32_t wmoGroupID) {
    for (uint32_t i = 0; i < g_wmoAreaTableDB.GetNumRecords(); i++) {
        const WMOAreaTableRec* rec = g_wmoAreaTableDB.GetRecordByIndex(i);

        if (rec && rec->m_wmoID == wmoID && rec->m_nameSetID == nameSet && rec->m_wmoGroupID == wmoGroupID) {
            return rec;
        }
    }

    return nullptr;
}

// ref: FUN_00782560
int32_t CWorld::GetEntityAreaID(CMapStaticEntity* entity, uint32_t* areaID) {
    // A global-WMO map is one area, Map.dbc's own.
    if (CMap::s_globalMapObj) {
        auto rec = g_mapDB.GetRecord(CMap::s_mapID);

        // Frozen-only null check: the reference reads the row unguarded.
        if (!rec || !rec->m_areaTableID) {
            return 0;
        }

        *areaID = static_cast<uint32_t>(rec->m_areaTableID);

        return 1;
    }

    if (!entity) {
        return 0;
    }

    auto link = entity->m_parentLinkList.Head();

    if (link) {
        if (link->ref && (link->ref->m_type & CMapBaseObj::Type_Chunk)) {
            *areaID = static_cast<CMapChunk*>(link->ref)->m_areaId;
            return 1;
        }

        CMapObjDef* def = nullptr;

        for (; link; link = entity->m_parentLinkList.Next(link)) {
            auto defGroupLink = link->ref ? link->ref->m_parentLinkList.Head() : nullptr;
            def = defGroupLink ? static_cast<CMapObjDef*>(defGroupLink->ref) : nullptr;

            if (def && !(def->m_flags & 0x400)) {
                break;
            }
        }

        if (link && def && def->m_mapObj && def->m_mapObj->m_mohd) {
            auto defGroup = static_cast<CMapObjDefGroup*>(link->ref);
            CMapObjGroup* group = def->m_mapObj->GetGroup(defGroup->m_groupIndex, 0);

            if (group) {
                const WMOAreaTableRec* rec = FindWMOArea(def->m_mapObj->m_mohd->wmoID, def->m_nameSet, group->m_groupID);

                if (rec && rec->m_areaTableID) {
                    *areaID = rec->m_areaTableID;
                    return 1;
                }
            }
        }
    }

    *areaID = CMap::GetChunkAreaID(entity->m_position);
    return 1;
}

namespace {

// The building the entity stands in: the first of its placements whose group is indoors (no MOGP
// flag 0x8), or any group when `anyGroup`, unless the placement passes indoor tests on (def
// flag 0x400). A chunk link means it is outdoors.
template <class F>
int32_t WalkEntityBuildings(CMapStaticEntity* entity, F visit) {
    for (auto link = entity->m_parentLinkList.Head(); link; link = entity->m_parentLinkList.Next(link)) {
        auto ref = link->ref;

        if (!ref || (ref->m_type & CMapBaseObj::Type_Chunk)) {
            return 0;
        }

        auto defGroup = static_cast<CMapObjDefGroup*>(ref);
        auto defLink = defGroup->m_parentLinkList.Head();
        auto def = defLink ? static_cast<CMapObjDef*>(defLink->ref) : nullptr;

        if (!def || !def->m_mapObj) {
            continue;
        }

        auto group = def->m_mapObj->GetGroup(defGroup->m_groupIndex, 0);

        if (group) {
            int32_t result = visit(def, group);

            if (result >= 0) {
                return result;
            }
        }
    }

    return 0;
}

} // namespace

// ref: FUN_0077f090 (with FUN_007a1480)
// Whether the entity is inside a building: in a group not marked outdoors.
int32_t CWorld::IsEntityIndoors(CMapStaticEntity* entity) {
    if (!entity) {
        return 0;
    }

    return WalkEntityBuildings(entity, [](CMapObjDef* def, CMapObjGroup* group) -> int32_t {
        if (!(group->m_flags & 0x8)) {
            return 1;
        }

        if (!(def->m_flags & 0x400)) {
            return 0;
        }

        return -1;
    });
}

// ref: FUN_0077f1b0 (with FUN_007a1640)
// The WMOAreaTable rows of the building the entity is in: its group's and the building's own.
int32_t CWorld::GetEntityWMOAreas(CMapStaticEntity* entity, const WMOAreaTableRec** groupArea,
                                  const WMOAreaTableRec** rootArea, uint32_t* groupID) {
    if (!entity) {
        return 0;
    }

    return WalkEntityBuildings(entity, [&](CMapObjDef* def, CMapObjGroup* group) -> int32_t {
        if ((group->m_flags & 0x8) && (def->m_flags & 0x400)) {
            return -1;
        }

        int32_t wmoID = def->m_mapObj->m_mohd ? def->m_mapObj->m_mohd->wmoID : 0;

        *groupID = group->m_groupID;
        *groupArea = FindWMOArea(wmoID, def->m_nameSet, group->m_groupID);
        *rootArea = FindWMOArea(wmoID, def->m_nameSet, -1);

        return *groupArea && *rootArea ? 1 : 0;
    });
}

// ref: FUN_0078f1f0
// Whether the entity's area is a snowy one (AreaTable flag 0x1): the building's area indoors,
// the ground's outdoors; an area without its own flags (0x2 clear) takes its parent's.
int32_t CWorld::IsEntityInSnow(CMapStaticEntity* entity) {
    const AreaTableRec* area = nullptr;

    if (!CWorld::IsEntityIndoors(entity)) {
        uint32_t areaID = 0;

        if (!CWorld::GetEntityAreaID(entity, &areaID)) {
            return 0;
        }

        area = g_areaTableDB.GetRecord(static_cast<int32_t>(areaID));
    } else {
        const WMOAreaTableRec* groupArea = nullptr;
        const WMOAreaTableRec* rootArea = nullptr;
        uint32_t groupID = 0;

        if (!CWorld::GetEntityWMOAreas(entity, &groupArea, &rootArea, &groupID)) {
            return 0;
        }

        if (groupArea) {
            area = g_areaTableDB.GetRecord(groupArea->m_areaTableID);
        }

        if (!area) {
            if (!rootArea) {
                return 0;
            }

            area = g_areaTableDB.GetRecord(rootArea->m_areaTableID);
        }
    }

    if (!area) {
        return 0;
    }

    auto parent = g_areaTableDB.GetRecord(area->m_parentAreaID);
    uint32_t flags = area->m_flags;

    if (!(flags & 0x2) && parent) {
        flags = parent->m_flags;
    }

    return flags & 0x1;
}

// ref: FUN_009905c0
const LiquidTypeRec* CWorld::GetAreaLiquidType(uint32_t areaID, uint32_t liquidType) {
    if (liquidType == 0) {
        return nullptr;
    }

    if (areaID && liquidType < 0x15) {
        uint32_t index = (liquidType - 1) & 0x3;
        const AreaTableRec* area = g_areaTableDB.GetRecord(areaID);

        if (area) {
            if (area->m_liquidTypeID[index] == 0 && area->m_parentAreaID != 0) {
                area = g_areaTableDB.GetRecord(area->m_parentAreaID);
            }

            if (area && area->m_liquidTypeID[index]) {
                return g_liquidTypeDB.GetRecord(area->m_liquidTypeID[index]);
            }
        }
    }

    return g_liquidTypeDB.GetRecord(liquidType);
}

// ref: FUN_0077f310
int32_t WorldQuerySegment(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* t, uint32_t flags, void* result) {
    return CMap::QuerySegment(start, end, hit, t, flags, result) ? 1 : 0;
}

// ref: FUN_00780710
// A new density asks the detail doodads to rebuild.
void CWorld::SetGroundEffectDensity(int32_t density) {
    if (CWorld::s_groundEffectDensity != density) {
        CWorld::s_groundEffectDensity = density;
        DetailDoodad::s_rebuild = 1;
    }
}

// ref: FUN_00780730
void CWorld::SetGroundEffectDist(float dist) {
    if (dist != DetailDoodad::s_fadeDistance) {
        DetailDoodad::s_fadeDistance = dist;
        DetailDoodad::s_rebuild = 1;
        CWorld::s_groundEffectDistSq = dist * dist;
    }
}

// ref: FUN_00782740
// The hit records are in each owner's own space; every face is taken out to the world by its
// record's placement, its normal turned by the placement's rotation and normalised (rsqrtss when
// CMap::s_useSse is set, which is the only difference between the reference's two copies of the
// loop).
void CWorld::AddHitFacets(CFacetList& list, uint32_t owner0, uint32_t owner1) {
    uint32_t first = list.facets.Count();

    for (uint32_t r = 0; r < CMapObjGroup::s_hitRecordCount; r++) {
        const CMapObjHitRecord& record = CMapObjGroup::s_hitRecords[r];
        const uint16_t* tri = record.indices;

        for (uint32_t f = 0; f < record.faceCount; f++, tri += 3) {
            M2CollisionTriangle* facet = list.facets.New();

            facet->plane.n = { 0.0f, 0.0f, 1.0f };
            facet->plane.d = 0.0f;

            const C3Vector& v0 = record.vertices[tri[0]];
            const C3Vector& v1 = record.vertices[tri[1]];
            const C3Vector& v2 = record.vertices[tri[2]];
            const C44Matrix& m = *record.placement;

            float nx = (v1.y - v0.y) * (v2.z - v0.z) - (v1.z - v0.z) * (v2.y - v0.y);
            float ny = (v1.z - v0.z) * (v2.x - v0.x) - (v2.z - v0.z) * (v1.x - v0.x);
            float nz = (v2.y - v0.y) * (v1.x - v0.x) - (v1.y - v0.y) * (v2.x - v0.x);

            facet->plane.n.x = m.a0 * nx + m.b0 * ny + m.c0 * nz;
            facet->plane.n.y = m.a1 * nx + m.b1 * ny + m.c1 * nz;
            facet->plane.n.z = m.c2 * nz + m.b2 * ny + m.a2 * nx;

            facet->vertices[0] = v0 * m;
            facet->vertices[1] = v1 * m;
            facet->vertices[2] = v2 * m;

            C3Vector& n = facet->plane.n;
            float lengthSq = n.x * n.x + n.y * n.y + n.z * n.z;

            if (lengthSq == 0.0f) {
                SysMsgPrintf(SYSMSG_ERROR, "Found degenerate triangle -- data needs to be fixed\n");
                n = { 0.0f, 0.0f, 1.0f };
            } else {
                float inv = CMap::s_useSse ? FacetRsqrt(lengthSq) : 1.0f / sqrtf(lengthSq);
                n = { inv * n.x, inv * n.y, inv * n.z };
            }

            const C3Vector& p0 = facet->vertices[0];
            facet->plane.d = -(p0.x * n.x + p0.y * n.y + p0.z * n.z);
        }
    }

    uint32_t count = list.facets.Count();

    if (count != first) {
        list.owners.SetCount(count);

        for (uint32_t i = first; i < count; i++) {
            list.owners[i] = { owner0, owner1 };
        }
    }
}

// ref: FUN_0077f330
int32_t WorldQueryFrustumFacets(const CWFrustum& frustum, CFacetList& list, uint32_t flags, uint32_t* hitFlags) {
    return CMap::QueryFrustumFacets(frustum, list, flags, hitFlags) ? 1 : 0;
}

// ref: FUN_0077f9d0
void CWorld::RenderParticulates() {
    if ((CWorld::s_enables & CWorld::Enable_Particulates) && CWorldScene::s_cameraLiquidType != 0) {
        CWorld::s_particulates->Render();
    }
}

// ref: FUN_0077f2e0
void CWorld::UpdateObjectLighting(HWORLDOBJECT object) {
    CMap::UpdateEntityLighting(reinterpret_cast<CMapEntity*>(object));
}

// ref: FUN_0077f9a0
void CWorld::SetBarrierPoint(const C3Vector& position) {
    CWorldScene::s_barriers.moverPos = position;
}

// ref: FUN_0077f980
void CWorld::RenderBarriers(float dt) {
    CWorldScene::RenderBarriers(CWorldScene::s_cameraPos, dt);
}

// ref: FUN_0077f400
void CWorld::AddRipple(const C3Vector& position, float angle, float radius, float alphaPeak, float life, float radiusRate, int32_t kind, int32_t reserved) {
    WaterRipples::Add(position, angle, radius, alphaPeak, life, radiusRate, kind, reserved);
}

static STORM_EXPLICIT_LIST(SWModelFadeout, m_link) s_fadeouts;      // DAT_00adf1a8
static STORM_EXPLICIT_LIST(SWModelFadeout, m_link) s_freeFadeouts;  // DAT_00adf1b4

// ref: FUN_00783100
static SWModelFadeout* AllocFadeout() {
    void* memory = SMemAlloc(sizeof(SWModelFadeout), ".?AUSWModelFadeout@@", -2, 0x8);

    return memory ? new (memory) SWModelFadeout() : nullptr;
}

// The handler a fading object's entity is given: the scene tells it whether it was seen, and it
// keeps the model drawing and animating only while it is.
// ref: FUN_007823d0
static int32_t FadeoutEntityHandler(void* param, int32_t event, uint32_t guidLow, uint32_t guidHigh, uint32_t param32) {
    auto entity = static_cast<CMapEntity*>(param);

    if (!entity) {
        return 0;
    }

    CM2Model* model = entity->m_model;

    if (!(event & 1)) {
        if (!model->m_attachParent) {
            model->m_flag8 = 0;
            model->m_flag10000 = 0;
        } else {
            model->m_flag80 = 0;
            model->m_flag20000 = 0;
        }

        model->SetAnimating(0);

        model = entity->m_model;

        if (!model->m_attachParent) {
            model->m_flag10000 = 0;
        } else {
            model->m_flag20000 = 0;
        }

        return 1;
    }

    model->SetAnimating(1);
    model = entity->m_model;

    if (model->m_attachParent) {
        model->m_flag80 = 1;
        model->m_flag20000 = 1;
    } else {
        model->m_flag8 = 1;
        model->m_flag10000 = 1;
    }

    return 1;
}

static void RetireFadeout(SWModelFadeout* fadeout) {
    CWorld::RemoveObject(reinterpret_cast<HWORLDOBJECT>(fadeout->entity));
    fadeout->entity = nullptr;
    fadeout->m_link.Unlink();
    s_freeFadeouts.LinkToTail(fadeout);
}

// ref: FUN_00783630
void CWorld::FadeOutObject(HWORLDOBJECT object, float alpha, WOWGUID transport) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    if (!entity) {
        return;
    }

    if (!entity->m_model || !entity->m_model->IsDrawable(0, 0) || alpha < 0.01f || (entity->m_flags7c & 0x4)) {
        CWorld::RemoveObject(object);
        return;
    }

    SWModelFadeout* fadeout = s_freeFadeouts.Head();

    if (!fadeout) {
        fadeout = AllocFadeout();
    }

    fadeout->entity = entity;
    fadeout->startMs = static_cast<uint32_t>(OsGetAsyncTimeMs());
    fadeout->alpha = alpha;
    fadeout->transport = 0;

    if (transport) {
        // TODO the reference looks the transport up as a game object (ClntObjMgrObjectPtr, type
        // 0x20) and stores the model's matrix times the inverse of the transport's world matrix,
        // which it gets through game-object vtable slot +0xc4. That slot is not identified in
        // frozen, so a fade on a transport is held in world space, as it is when the transport
        // has already gone.
    }

    fadeout->m_link.Unlink();
    s_fadeouts.LinkToTail(fadeout);

    entity->m_handler = reinterpret_cast<void*>(&FadeoutEntityHandler);
    entity->m_handlerParam = entity;
}

// ref: FUN_00782f20
void CWorld::UpdateFadeouts() {
    int32_t now = static_cast<int32_t>(OsGetAsyncTimeMs());

    for (auto fadeout = s_fadeouts.Head(); fadeout; ) {
        auto next = s_fadeouts.Next(fadeout);
        int32_t elapsed = now - static_cast<int32_t>(fadeout->startMs);
        CM2Model* model = fadeout->entity->m_model;

        if (elapsed > 2000 || !model->IsLoadedWithTextures()) {
            RetireFadeout(fadeout);
            fadeout = next;
            continue;
        }

        if (fadeout->transport) {
            // TODO model->matrixB4 = fadeout->relative * (the transport's world matrix), with
            // m_flag8000 set; see FadeOutObject. Never reached while the transport is not stored.
        }

        float t = (1.0f - static_cast<float>(elapsed) * 0.0005f) * fadeout->alpha;

        if (t < 0.0f) {
            model->m_baseAlpha = 0.0f;
        } else if (t > 1.0f) {
            model->m_baseAlpha = 1.0f;
        } else {
            model->m_baseAlpha = (3.0f - (t + t)) * t * t;
        }

        fadeout = next;
    }
}

// ref: FUN_00782e40
void CWorld::ClearFadeouts() {
    for (auto fadeout = s_fadeouts.Head(); fadeout; ) {
        auto next = s_fadeouts.Next(fadeout);
        RetireFadeout(fadeout);
        fadeout = next;
    }
}

// ref: FUN_00783780
void CWorld::FreeFadeoutPool() {
    for (auto fadeout = s_freeFadeouts.Head(); fadeout; fadeout = s_freeFadeouts.Head()) {
        fadeout->m_link.Unlink();
        fadeout->~SWModelFadeout();
        SMemFree(fadeout, ".?AUSWModelFadeout@@", -2, 0);
    }
}

int32_t CWorld::s_forceDayNight;
float CWorld::s_frameFogEnd;

namespace {

const float CHUNK_SIZE = 33.33333206176758f;       // DAT_00a3e554

// One light zone (0x30 bytes): an outline on a map that forces a light on near and inside it.
struct LightZone {
    int32_t mapID;          // +0x00
    int32_t unknown04;      // +0x04
    int32_t lightID;        // +0x08
    float* points;          // +0x0c, (x, y) pairs
    int32_t count;          // +0x10
    float offsetX;          // +0x14
    float offsetY;          // +0x18
    const char* path;       // +0x1c, an SVG path of L commands
    float minX;             // +0x20
    float minY;             // +0x24
    float maxX;             // +0x28
    float maxY;             // +0x2c
};

// The table as the reference's static constructor (0x009cdbc0) leaves it: every zone on map 571,
// in tile units shifted by the same origin, with its bounds emptied for the parser to grow.
LightZone s_lightZones[11] = {
    { 571, -1, 914, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[0], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 825, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[1], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 959, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[2], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 862, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[3], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1847, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[4], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1703, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[5], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1796, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[6], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1777, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[7], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1792, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[8], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1589, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[9], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1740, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[10], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
};

// The terrain shadow's colour, as an 8x8 texture of one colour (DAT_00cd7554..DAT_00cd7878).
HTEXTURE s_shadowModTexture;
uint32_t s_shadowModTexels[64];
uint32_t s_shadowModColor;

// ref: FUN_0077f4c0
void ShadowModCallback(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mip, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Latch) {
        stride = w * 4;
        texels = userArg;
    }
}

// ref: FUN_007f9bf0
// The squared distance from `p` to the segment a..b.
float SegmentDistanceSq(const float* a, const float* b, const float* p) {
    float px = p[0] - a[0];
    float py = p[1] - a[1];
    float ex = b[0] - a[0];
    float ey = b[1] - a[1];
    float t = (ex * px + ey * py) / (ex * ex + ey * ey);

    if (t < 0.0f) {
        return py * py + px * px;
    }

    if (1.0f < t) {
        return (p[1] - b[1]) * (p[1] - b[1]) + (p[0] - b[0]) * (p[0] - b[0]);
    }

    float dx = p[0] - (ex * t + a[0]);
    float dy = p[1] - (t * ey + a[1]);

    return dy * dy + dx * dx;
}

// ref: FUN_007f9c90
// Whether `p` is inside the polygon, and its distance to the nearest edge.
bool PolygonContains(int32_t count, const float* points, const float* p, float* distance) {
    float best = 3.4028235e38f;
    bool inside = false;

    for (int32_t i = 0, j = count - 1; i < count; j = i++) {
        const float* a = &points[i * 2];
        const float* b = &points[j * 2];

        float d = SegmentDistanceSq(a, b, p);

        if (d < best) {
            best = d;
        }

        if (((a[1] < p[1] && p[1] <= b[1]) || (b[1] < p[1] && p[1] <= a[1]))
            && (b[0] - a[0]) * ((p[1] - a[1]) / (b[1] - a[1])) + a[0] < p[0]) {
            inside = !inside;
        }
    }

    *distance = sqrtf(best);

    return inside;
}

}

// ref: FUN_0077ed40
void CWorld::InitializeLightZones() {
    for (auto& zone : s_lightZones) {
        const char* p = zone.path;

        if (p) {
            for (const char* c = p; *c != 'z'; c++) {
                if (*c == 'M' || *c == 'L') {
                    zone.count++;
                }
            }

            float* out = static_cast<float*>(SMemAlloc(zone.count * 0xc, __FILE__, __LINE__, 0));
            zone.points = out;

            while (*p != 'z') {
                if (*p == 'M' || *p == 'L') {
                    const char* x = p + 2;

                    while (*p != ',') {
                        p++;
                    }

                    const char* y = p + 1;
                    p = y;

                    while (*p != ' ') {
                        p++;
                    }

                    out[0] = static_cast<float>(atof(x));
                    out[1] = static_cast<float>(atof(y));
                    out += 2;
                }

                p++;
            }
        }

        float* pt = zone.points;

        for (int32_t i = 0; i < zone.count; i++, pt += 2) {
            float x = zone.offsetX + pt[0];
            float y = (zone.offsetY + pt[1]) * 0.0009765625f;
            pt[0] = x * CHUNK_SIZE;
            pt[1] = y * 34133.332f;

            if (pt[0] < zone.minX) {
                zone.minX = pt[0];
            }

            if (pt[1] < zone.minY) {
                zone.minY = pt[1];
            }

            if (zone.maxX < pt[0]) {
                zone.maxX = pt[0];
            }

            if (zone.maxY < pt[1]) {
                zone.maxY = pt[1];
            }
        }

        zone.minX -= 50.0f;
        zone.minY -= 50.0f;
        zone.maxX += 50.0f;
        zone.maxY += 50.0f;
    }
}

// ref: FUN_00780660
void CWorld::SetShadowColor(const CImVector& color) {
    if (s_shadowModColor != color.value) {
        for (auto& texel : s_shadowModTexels) {
            texel = color.value;
        }

        if (!s_shadowModTexture) {
            CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);
            s_shadowModTexture = TextureCreate(static_cast<EGxTexTarget>(0), 8, 8, 0, static_cast<EGxTexFormat>(3), static_cast<EGxTexFormat>(2), flags, s_shadowModTexels, ShadowModCallback, "CWorld::shadowMod", 0);
        }

        auto gxTex = TextureGetGxTex(s_shadowModTexture, 1, nullptr);

        if (gxTex) {
            GxTexUpdate(gxTex, 0, 0, 8, 8, 1);
        }
    }

    s_shadowModColor = color.value;
}

// ref: FUN_0077eed0
void CWorld::UpdateLightZones(const C3Vector& cameraPos) {
    if (!DayNightGetBlock()) {
        return;
    }

    float p[2] = { -(cameraPos.y - 17066.666f), -(cameraPos.x - 17066.666f) };

    for (auto& zone : s_lightZones) {
        if (zone.mapID != CMap::s_mapID || !zone.points) {
            continue;
        }

        if (zone.minX <= p[0] && zone.minY <= p[1] && p[0] <= zone.maxX && p[1] <= zone.maxY) {
            float distance = 3.4028235e38f;

            if (PolygonContains(zone.count, zone.points, p, &distance)) {
                distance = -distance;
            }

            if (distance - 50.0f < 0.0f) {
                DayNightAddForcedLight(zone.lightID, -(distance - 50.0f));
            }
        }
    }
}

// ref: FUN_0077fb90
int32_t CWorld::QueryMapObjFog(SMOFog* fog, CMapObjDef** def, uint8_t* inside, TSGrowableArray<uint32_t>** groups, float* distance) {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (player && (player->Player()->flags & 0x10)) {
        return 0;
    }

    *distance = 3.4028235e38f;

    return CMap::QueryCameraFog(fog, def, inside, groups, distance);
}

// ref: FUN_007816f0
void CWorld::UpdateDayNight(int32_t force, const C3Vector* cameraPos) {
    auto block = DayNightGetBlock();
    float darken = 0.0f;

    int32_t fogMode = 1;
    int32_t vertexTarget = g_theGxDevicePtr->Caps().m_shaderTargets[GxSh_Vertex];

    if (vertexTarget == 0 || vertexTarget == 1) {
        fogMode = 0;
    }

    if (CMap::s_mapID < 0x212) {
        fogMode = 0;
    }

    DayNightSetFogMode(fogMode);

    for (int32_t i = 0; i < 5; i++) {
        block->forcedLight[i] = nullptr;
        block->forcedLightDepth[i] = 0.0f;
    }

    block->forcedLightCount = 0;

    if (cameraPos) {
        CWorld::UpdateLightZones(*cameraPos);
    }

    if (force) {
        if (cameraPos) {
            block->cameraPos = *cameraPos;
        }

        DayNightResetLightFade(1);
        DayNightClearFadeFlag(1);
    } else {
        darken = DayNightGetBodies()->sunGlare.darken * 0.35f;

        if (darken == 0.0f) {
            DayNightClearFadeFlag(1);
        }
    }

    DayNightUpdateCamera();
    DayNightUpdateClouds();
    DayNightUpdateStars();
    DayNightUpdateFog();

    CWorld::SetShadowColor(block->shadowColor);

    // A glare in view dims the outdoor colours.
    uint32_t keep = static_cast<uint32_t>(lrintf((1.0f - darken) * 255.0f)) & 0xFF;

    auto scale = [keep](CImVector& c) {
        uint32_t r = (c.r * keep + 0xFF) >> 8;
        uint32_t g = (c.g * keep + 0xFF) >> 8;
        uint32_t b = (c.b * keep + 0xFF) >> 8;
        c.value = (c.value & 0xFF000000) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    };

    scale(block->ambient);
    scale(block->diffuse);

    if (CMap::s_outdoorLight) {
        CM2Light& light = CMap::s_outdoorLight->m_light;
        light.SetDirection(block->direction);
        light.m_ambColor = { block->ambient.r / 255.0f, block->ambient.g / 255.0f, block->ambient.b / 255.0f };
        light.m_dirColor = { block->diffuse.r / 255.0f, block->diffuse.g / 255.0f, block->diffuse.b / 255.0f };
        const CImVector& spec = block->info.color[9];
        light.m_specColor = { spec.r / 255.0f, spec.g / 255.0f, spec.b / 255.0f };
    }

    DayNightSetFogMode(0);
}

float CWorld::GetHorizonDistance() {
    return CWorld::s_horizonFarClipScale * CWorld::s_farClip;
}

// ref: FUN_00783910
bool CWorld::QueryFacets(const CAaBox& sweep, const CAaBox& box, CFacetList& list, uint32_t flags, uint32_t* hitFlags) {
    list.facets.SetCount(0);
    list.owners.SetCount(0);

    if (hitFlags) {
        *hitFlags = 0;
    }

    if (!(flags & 0x4000)) {
        return MapQueryBoxFacets(sweep, box, list, flags, hitFlags);
    }

    // The walkable-only form gathers into a list of its own (0x00cd8500) and keeps what faces up.
    static CFacetList s_walkable;
    s_walkable.facets.SetCount(0);
    s_walkable.owners.SetCount(0);

    if (!MapQueryBoxFacets(sweep, box, s_walkable, flags, hitFlags)) {
        return false;
    }

    for (uint32_t i = 0; i < s_walkable.facets.Count(); i++) {
        // 0x00a37f0c: cos 50 degrees.
        if (0.6427876353263855f <= s_walkable.facets[i].plane.n.z) {
            *list.facets.New() = s_walkable.facets[i];
            *list.owners.New() = s_walkable.owners[i];
        }
    }

    return true;
}

// ref: FUN_00783a40
bool CWorld::QueryFacets(const CAaBox& box, CFacetList& list, uint32_t flags, uint32_t* hitFlags) {
    return CWorld::QueryFacets(box, box, list, flags, hitFlags);
}

// ------------------------------------------------------------------------------------------------
// The world's dynamic objects: map objects a game object places and moves (World.cpp).
// ------------------------------------------------------------------------------------------------

// ref: FUN_00783500
// A game object's map object: a building (a .wmo name) or a prop (anything else), turned by
// `facing`, owned by `owner` (the def keeps the guid), and placed at once when `place` asks
// (the position first moved onto the cell grid). A building also joins the map's own list.
CMapBaseObj* CWorld::AddDynamicObject(const char* name, C3Vector& position, float facing, int32_t wait, int32_t place,
                                      WOWGUID owner, int32_t extraSetCount, const uint16_t* extraSets,
                                      const float* radius, uint32_t id) {
    auto dot = SStrChrR(name, '.');

    if (dot && !SStrCmpI(dot, ".wmo", STORM_MAX_STR)) {
        auto def = CMap::CreateDynamicMapObjDef(name, position, facing, wait, extraSetCount, extraSets, radius, id);
        def->m_ownerGUID = owner;

        auto link = CMap::AllocBaseObjLink(def);
        link->ref = nullptr;
        CMap::s_mapObjDefLinkList.LinkToTail(link);

        if (place) {
            CMap::SnapDynamicPosition(def, position, facing);
            CMap::PlaceDynamicMapObjDef(def, position, facing, 0.0f, 0.0f);
        }

        def->m_linkCount++;

        return def;
    }

    auto def = CMap::CreateDynamicDoodadDef(name, position, facing, wait);

    if (!def) {
        return nullptr;
    }

    def->m_flags7c |= 0x2010;
    def->m_ownerGUID = owner;

    if (place) {
        CMap::SnapDynamicPosition(def, position, facing);
        CMap::PlaceDynamicDoodadDef(def, position, facing);
    }

    def->m_linkCount++;

    return def;
}

// ref: FUN_00782680
// A game object lets its map object go: the links it holds are freed, and the building or prop is
// released once nothing else links it.
void CWorld::RemoveDynamicObject(CMapBaseObj* object) {
    while (auto link = object->m_parentLinkList.Head()) {
        CMap::FreeBaseObjLink(link);
    }

    object->m_linkCount--;

    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        CMap::ReleaseDynamicMapObjDef(static_cast<CMapObjDef*>(object));
        return;
    }

    if (object->m_type & CMapBaseObj::Type_DoodadDef) {
        CMap::ReleaseDoodadDef(static_cast<CMapDoodadDef*>(object));
    }
}

// ref: FUN_00783a60
// A dynamic object shown or hidden (flag 0x20). A building hidden lets go of the entities its
// groups held, which go back to the map's own placement.
void CWorld::SetDynamicObjectShown(CMapBaseObj* object, int32_t shown) {
    if (shown) {
        object->m_flags &= 0xFFFFFFDF;
    } else {
        object->m_flags |= 0x20;
    }

    if (!(object->m_type & CMapBaseObj::Type_MapObjDef)) {
        return;
    }

    auto def = static_cast<CMapObjDef*>(object);

    for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
        auto defGroup = static_cast<CMapObjDefGroup*>(link->owner);

        while (auto entityLink = defGroup->m_entityLinkList.Head()) {
            auto entity = static_cast<CMapEntity*>(entityLink->owner);
            CMap::FreeBaseObjLink(entityLink);
            CMap::UpdateEntity(entity);
        }
    }
}

// ref: FUN_007801a0
// Whether the dynamic object collides (flag 0x100 set while it does not).
void CWorld::SetDynamicObjectCollides(CMapBaseObj* object, int32_t collides) {
    if (collides) {
        object->m_flags &= 0xFFFFFEFF;
    } else {
        object->m_flags |= 0x100;
    }
}

// ref: FUN_0077fe00
// The 0x1000 flag a transport's building carries (an elevator's, set by the transport type).
void CWorld::SetDynamicObjectFlag1000(CMapBaseObj* object, int32_t set) {
    if (set) {
        object->m_flags |= 0x1000;
    } else {
        object->m_flags &= 0xFFFFEFFF;
    }
}

// ref: FUN_00780190
bool CWorld::DynamicObjectIsMapObj(CMapBaseObj* object) {
    return (object->m_type & CMapBaseObj::Type_MapObjDef) != 0;
}

// ref: FUN_007b42f0
// A group's doodads are in: the group has made them (flag 0x8) and each with a model is placed.
static bool DefGroupDoodadsPlaced(CMapObjDefGroup* defGroup) {
    if (!(defGroup->m_flags & 0x8)) {
        return false;
    }

    for (auto link = defGroup->m_doodadDefLinkList.Head(); link; link = defGroup->m_doodadDefLinkList.Next(link)) {
        auto doodad = static_cast<CMapDoodadDef*>(link->owner);

        if (doodad->m_model && !(doodad->m_flags & 0x80)) {
            // A streaming client pushes the model's read (FUN_00825150).
            return false;
        }
    }

    return true;
}

// ref: FUN_0077fd10
// Whether a dynamic object has arrived: a building set up, its groups' doodads placed
// (FUN_007b4760) and every group of its root in (FUN_007af740); a prop with its model loaded.
bool CWorld::DynamicObjectIsLoaded(CMapBaseObj* object) {
    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        auto def = static_cast<CMapObjDef*>(object);

        if (!(def->m_flags & 0x80)) {
            return false;
        }

        if (!(def->m_flags & 0x4000)) {
            for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
                if (!DefGroupDoodadsPlaced(static_cast<CMapObjDefGroup*>(link->owner))) {
                    return false;
                }
            }

            def->m_flags |= 0x4000;
        }

        auto mapObj = def->m_mapObj;

        if (!mapObj->m_rootLoaded) {
            return false;
        }

        for (uint32_t i = 0; i < mapObj->m_groupCount; i++) {
            auto group = mapObj->GetGroup(i, 1);

            if (!group || !(group->m_state & 0x1)) {
                return false;
            }
        }

        return true;
    }

    if (object->m_type & CMapBaseObj::Type_DoodadDef) {
        auto doodad = static_cast<CMapDoodadDef*>(object);
        return !doodad->m_model || doodad->m_model->IsLoaded(0, 0);
    }

    return false;
}

// ref: FUN_0077fdd0
void CWorld::SetDynamicObjectPlacement(CMapBaseObj* object, const C44Matrix& placement) {
    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        CMap::SetMapObjDefPlacement(static_cast<CMapObjDef*>(object), placement);
        return;
    }

    if (object->m_type & CMapBaseObj::Type_DoodadDef) {
        CMap::SetDoodadDefPlacement(static_cast<CMapDoodadDef*>(object), placement);
    }
}

// ref: FUN_0077fd60
// A dynamic object moved (snapped to the cell grid first when asked): a building by three angles,
// a prop by its facing.
void CWorld::SetDynamicObjectPosition(CMapBaseObj* object, C3Vector& position, float rotZ, float rotY, float rotX, int32_t snap) {
    if (snap) {
        CMap::SnapDynamicPosition(object, position, rotZ);
    }

    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        CMap::PlaceDynamicMapObjDef(static_cast<CMapObjDef*>(object), position, rotZ, rotY, rotX);
        return;
    }

    if (object->m_type & CMapBaseObj::Type_DoodadDef) {
        CMap::PlaceDynamicDoodadDef(static_cast<CMapDoodadDef*>(object), position, rotZ);
    }
}

// ref: FUN_00780130
// A building's root bounds, or a prop's model's box.
void CWorld::GetDynamicObjectBounds(CMapBaseObj* object, CAaBox& bounds) {
    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        bounds = static_cast<CMapObjDef*>(object)->m_mapObj->m_bounds;
        return;
    }

    if (object->m_type & CMapBaseObj::Type_DoodadDef) {
        static_cast<CMapDoodadDef*>(object)->m_model->GetBoundingBox(bounds);
    }
}

// ref: FUN_0077fe20
void CWorld::SetDynamicObjectFlag2000(CMapBaseObj* object, int32_t set) {
    if (set) {
        object->m_flags |= 0x2000;
    } else {
        object->m_flags &= ~0x2000u;
    }
}

// ref: FUN_0077fe80
void CWorld::SetDynamicObjectEmittersPaused(CMapBaseObj* object, int32_t paused, uint16_t set) {
    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        CMap::SetDoodadSetEmittersPaused(static_cast<CMapObjDef*>(object), paused, set);
    }
}

// ref: FUN_0077fea0
void CWorld::SetDynamicObjectDoodadSetShown(CMapBaseObj* object, int32_t shown, uint16_t set) {
    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        CMap::SetDoodadSetShown(static_cast<CMapObjDef*>(object), shown, set);
    }
}

// ref: FUN_0077fe40
// Both must be loaded buildings.
int32_t CWorld::MoveDynamicObjectDoodadSet(CMapBaseObj* from, uint16_t set, CMapBaseObj* to, uint16_t newSet) {
    if (!(from->m_type & CMapBaseObj::Type_MapObjDef) || !(from->m_flags & 0x80)
        || !(to->m_type & CMapBaseObj::Type_MapObjDef) || !(to->m_flags & 0x80)) {
        return 0;
    }

    return CMap::MoveDoodadSet(static_cast<CMapObjDef*>(from), set, static_cast<CMapObjDef*>(to), newSet);
}

// ref: FUN_0077ff60
// ref: FUN_007b46a0
void CWorld::SetDynamicObjectAnimEvent(CMapBaseObj* object, M2AnimEventCallback callback, WOWGUID owner, uint16_t set) {
    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        auto def = static_cast<CMapObjDef*>(object);

        for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
            CMap::SetGroupDoodadAnimEvent(static_cast<CMapObjDefGroup*>(link->owner), callback, owner, set);
        }

        return;
    }

    if (object->m_type & CMapBaseObj::Type_DoodadDef) {
        auto doodad = static_cast<CMapDoodadDef*>(object);

        if (doodad->m_model) {
            doodad->m_model->SetAnimEventCallback(callback, owner);
        }
    }
}

// ref: FUN_007819c0
// The game object owning the dynamic building a world entity stands in: its first parent is one
// of the building's groups, whose own first parent is the building.
WOWGUID CWorld::GetObjectBuildingOwner(HWORLDOBJECT object) {
    auto entity = reinterpret_cast<CMapBaseObj*>(object);
    auto link = entity->m_parentLinkList.Head();

    if (!link || !link->ref || !(link->ref->m_type & CMapBaseObj::Type_MapObjDefGroup)) {
        return 0;
    }

    auto defLink = link->ref->m_parentLinkList.Head();

    // The reference reads through the group's first parent unguarded; a group always has its
    // building there.
    if (!defLink || !defLink->ref) {
        return 0;
    }

    return static_cast<CMapObjDef*>(defLink->ref)->m_ownerGUID;
}

// ref: FUN_0077f2f0
// An entity taken out of the scene (state bit 0x4, which CMap::BucketEntities passes over) or put
// back.
void CWorld::SetObjectHidden(HWORLDOBJECT object, int32_t hidden) {
    auto entity = reinterpret_cast<CMapStaticEntity*>(object);
    entity->m_flags7c ^= (static_cast<uint32_t>(hidden) * 4 ^ entity->m_flags7c) & 0x4;
}

// ref: FUN_0077fec0
// ref: FUN_007b45f0
// The dynamic object plays `sequence`: a prop's model does; a building's doodads of `set` do.
void CWorld::SetDynamicObjectSequence(CMapBaseObj* object, uint32_t sequence, uint32_t a3, uint16_t set) {
    (void)a3;

    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        auto def = static_cast<CMapObjDef*>(object);

        for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
            CMap::SetGroupDoodadSequence(static_cast<CMapObjDefGroup*>(link->owner), sequence, set);
        }

        return;
    }

    if (object->m_type & CMapBaseObj::Type_DoodadDef) {
        auto doodad = static_cast<CMapDoodadDef*>(object);

        if (doodad->m_model) {
            doodad->m_model->SetBoneSequence(-1, sequence, -1, 0, 1.0f, 1, 1);
        }
    }
}

// ref: FUN_0077ffb0
// Whether a point in the object's own space is aboard it: inside a building's convex volume, or
// inside a prop's collision box (its top raised by 1.64 + 1/72 yards, room for a passenger's
// height). An object of neither kind holds everything; a prop not yet loaded holds nothing.
bool CWorld::DynamicObjectContains(CMapBaseObj* object, const C3Vector& position) {
    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        return static_cast<CMapObjDef*>(object)->m_mapObj->PointInConvexVolume(position);
    }

    if (!(object->m_type & CMapBaseObj::Type_DoodadDef)) {
        return true;
    }

    auto model = static_cast<CMapDoodadDef*>(object)->m_model;

    if (!model || !model->IsLoaded(0, 0)) {
        return false;
    }

    if (!model->m_shared->m_m2DataLoaded) {
        model->WaitForLoad(nullptr);
    }

    const CAaBox& box = model->m_shared->m_data->collisionBounds.extent;

    const C4Plane planes[6] = {
        { {  1.0f,  0.0f,  0.0f }, -box.t.x },
        { {  0.0f,  1.0f,  0.0f }, -box.t.y },
        { {  0.0f,  0.0f,  1.0f }, -box.t.z - 1.6404099f - 0.013888889f },
        { { -1.0f,  0.0f,  0.0f },  box.b.x },
        { {  0.0f, -1.0f,  0.0f },  box.b.y },
        { {  0.0f,  0.0f, -1.0f },  box.b.z },
    };

    for (const auto& plane : planes) {
        if (0.0f < plane.n.y * position.y + plane.n.z * position.z + plane.n.x * position.x + plane.d) {
            return false;
        }
    }

    return true;
}

// ref: FUN_0077ff10
// ref: FUN_007b4640
// The dynamic object's sequence-done hook: a prop's model's own; a building's doodads' of `set`.
void CWorld::SetDynamicObjectSequenceDone(CMapBaseObj* object, M2SequenceDoneCallback callback, WOWGUID owner, uint16_t set) {
    if (object->m_type & CMapBaseObj::Type_MapObjDef) {
        auto def = static_cast<CMapObjDef*>(object);

        for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
            CMap::SetGroupDoodadSequenceDone(static_cast<CMapObjDefGroup*>(link->owner), callback, owner, set);
        }

        return;
    }

    if (object->m_type & CMapBaseObj::Type_DoodadDef) {
        auto doodad = static_cast<CMapDoodadDef*>(object);

        if (doodad->m_model) {
            doodad->m_model->SetSequenceDoneCallback(callback, owner);
        }
    }
}
