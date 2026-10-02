#include "model/CM2Scene.hpp"
#include "util/OsSystem.hpp"
#include "world/CWFrustum.hpp"
#include "world/WorldFacets.hpp"
#include "model/CM2Model.hpp"
#include <tempest/Intersect.hpp>
#include <tempest/Ray.hpp>
#include <algorithm>
#include <cmath>
#include "world/map/CMap.hpp"
#include <cstdio>
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "world/ShadowMap.hpp"
#include "world/MapShadow.hpp"
#include "world/ParticleFx.hpp"
#include "world/map/VBBList.hpp"
#include "world/map/CChunkLiquid.hpp"
#include "world/map/LiquidSurface.hpp"
#include "world/map/LiquidMaterialSettings.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapAreaLow.hpp"
#include "world/map/MapLowDetail.hpp"
#include "world/map/MapOcclusion.hpp"
#include "world/map/CMapAreaMed.hpp"
#include "world/map/CMapCacheLight.hpp"
#include "world/map/CMapChunk.hpp"
#include "db/Db.hpp"
#include "world/map/CMapDoodadDef.hpp"
#include "world/map/CMapEntity.hpp"
#include "world/map/CMapLight.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapRenderChunk.hpp"
#include "world/map/DetailDoodad.hpp"
#include "sound/SI2.hpp"
#include "async/AsyncFile.hpp"
#include "async/AsyncFileRead.hpp"
#include "async/CAsyncObject.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/Texture.hpp"
#include "gx/shader/CGxShader.hpp"
#include "gx/texture/CGxTex.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Transform.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "model/CM2Lighting.hpp"
#include "model/CM2Model.hpp"
#include "util/CStatus.hpp"
#include "world/CWorldScene.hpp"
#include "util/SFile.hpp"
#include "world/CWorld.hpp"
#include "util/Log.hpp"
#include <cstdlib>
#include <cstring>
#include <common/ObjectAlloc.hpp>
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <new>

uint32_t* CMap::s_lightHeap;
uint32_t* CMap::s_cacheLightHeap;
uint32_t* CMap::s_mapObjGroupHeap;
uint32_t* CMap::s_mapObjHeap;
uint32_t* CMap::s_baseObjLinkHeap;
uint32_t* CMap::s_areaHeap;
uint32_t* CMap::s_areaMedHeap;
uint32_t* CMap::s_areaLowHeap;
uint32_t* CMap::s_chunkHeap;
uint32_t* CMap::s_doodadDefHeap;
uint32_t* CMap::s_entityHeap;
uint32_t* CMap::s_mapObjDefGroupHeap;
uint32_t* CMap::s_mapObjDefHeap;
uint32_t* CMap::s_chunkLiquidHeap;

STORM_EXPLICIT_LIST(CMapArea, m_lameAssLink) CMap::s_areaList;
STORM_EXPLICIT_LIST(CMapChunk, m_lameAssLink) CMap::s_chunkList;
STORM_EXPLICIT_LIST(CChunkLiquid, m_link) CMap::s_chunkLiquidList;
STORM_EXPLICIT_LIST(CMapObjDefGroup, m_lameAssLink) CMap::s_mapObjDefGroupList;
STORM_EXPLICIT_LIST(CMapBaseObj, m_lameAssLink) CMap::s_entityList;
STORM_EXPLICIT_LIST(CMapStaticEntity, m_rowLink) CMap::s_pendingEntityList;
STORM_EXPLICIT_LIST(CMapLight, m_lameAssLink) CMap::s_lightList;
STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) CMap::s_renderChunkFreeList;
STORM_EXPLICIT_LIST(CMapObj, m_link) CMap::s_mapObjLoadList;
STORM_EXPLICIT_LIST(CMapObjGroup, m_link) CMap::s_mapObjGroupLoadList;

uint8_t CMap::s_chunkVerticesWorldSpace;
int32_t CMap::s_terrainVertexFormat;
uint8_t CMap::s_terrainSpecular;

CMapArea* CMap::s_areaGrid[64 * 64];
STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) CMap::s_areaLinkList;
TSGrowableArray<int32_t> CMap::s_cellList;
int32_t CMap::s_cellListCount;

int32_t CMap::s_chunkWindowMinY;
int32_t CMap::s_chunkWindowMinX;
int32_t CMap::s_chunkWindowMaxY;
int32_t CMap::s_chunkWindowMaxX;
int32_t CMap::s_chunkInnerMinY;
int32_t CMap::s_chunkInnerMinX;
int32_t CMap::s_chunkInnerMaxY;
int32_t CMap::s_chunkInnerMaxX;

uint32_t CMap::s_wdtVersion;
uint32_t CMap::s_wdtHeader[8];
uint32_t CMap::s_areaInfo[64 * 64][2];

int32_t CMap::s_loading;
int32_t CMap::s_streamingMode;
void (*CMap::s_loadProgressCallback)(float progress, void* arg);
void* CMap::s_loadProgressArg;

CGxShader* CMap::s_terrainVertexShaders[0x80];
CGxShader* CMap::s_terrain3PixelShaders[0x60];
CGxShader* CMap::s_terrain2PixelShaders[0x20];
CGxShader* CMap::s_terrainPixelShaders[3];
CGxShader* CMap::s_terrainEnvPixelShader[1];
CGxShader* CMap::s_terrain1PixelShaders[0x20];
CGxShader* CMap::s_terrain1wPixelShaders[8];
CGxShader* CMap::s_terrainShadowMapPixelShader[1];
CMapLowDetail CMap::s_lowDetail;
CGxPool* CMap::s_lowDetailIndexPool;
CGxBuf* CMap::s_lowDetailIndexBuf;
int32_t CMap::s_terrainShadersDirty;

STORM_EXPLICIT_LIST(CMapChunk, m_frameLink) CMap::s_frameChunkList;

uint8_t CMap::s_terrainShaders;
uint8_t CMap::s_shaderVertexMode;
TSGrowableArray<CMapChunkBufBlock> CMap::s_bufBlocks;
STORM_EXPLICIT_LIST(CMapChunkBufEntry, link) CMap::s_freeBufEntryList;
STORM_EXPLICIT_LIST(CMapChunkBufBlock, freeLink) CMap::s_freeBufBlockList;
STORM_EXPLICIT_LIST(CMapChunkBufBlock, link) CMap::s_bufBlockList;
STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) CMap::s_activeRenderChunkList;
CGxPool* CMap::s_renderChunkVertexPool;
CGxPool* CMap::s_renderChunkIndexPool;
uint32_t CMap::s_renderChunkPoolVertices;
uint32_t CMap::s_renderChunkPoolIndices;
uint16_t CMap::s_chunkVertexCount = 145;
uint16_t CMap::s_chunkIndexCount = 768;
CGxBatch CMap::s_chunkBatch;

static const float CHUNK_SIZE = 33.33333206176758f;       // DAT_00a3e554
static const float MAP_HALF_EXTENT = 17066.666015625f;    // DAT_009e2acc
static const float TILE_SIZE = 533.3333129882812f;        // DAT_00a0b5e4

int32_t CMap::s_mapID = -1;
char CMap::s_mapName[256];
char CMap::s_mapPath[256];
char CMap::s_wdtFilename[256];

void CMap::Initialize() {
    // TODO

    CMap::MapMemInitialize();

    // TODO
}

// ref: FUN_007bfce0
// Names the map's files, tears the previous map down, reads the WDT and starts the tiles round
// the target loading. The light block (FUN_007d9bd0 / FUN_007d9d50 / FUN_007da100), the map
// object cache flush (FUN_007b0040), FUN_0079fa10, the WDL low-detail load (FUN_007cc310), the
// .tex cache open (FUN_007bd540), the per-map day/night setup (FUN_007f2790) and the final
// AsyncFileReadWaitAll (FUN_004bae10) are not ported yet.
void CMap::Load(const char* mapName, int32_t mapID) {
    auto nameOfs = SStrCopy(CMap::s_mapPath, "World\\Maps\\");
    SStrCopy(&CMap::s_mapPath[nameOfs], mapName);

    SStrCopy(CMap::s_mapName, mapName);

    SStrPrintf(CMap::s_wdtFilename, sizeof(CMap::s_wdtFilename), "%s\\%s.wdt", CMap::s_mapPath, CMap::s_mapName);

    // TODO DAT_00ce04a8 = FUN_007d9bd0(1, 0); CM2Light::SetLightType(0); FUN_007d9d50; FUN_007da100

    CMap::UnloadAll();

    // TODO FUN_007b0040(1); FUN_0079fa10()

    CMap::s_mapID = mapID;
    // TODO DAT_00cf08f0 = 1 (map loaded), DAT_00cf08f4 = 0 (global WMO)
    CMap::s_loading = 1;

    CMap::s_streamingMode = SFile::IsStreamingMode() | SFile::IsStreamingTrial();
    bool waitAll = CMap::s_streamingMode == 0;

    CMap::s_lowDetail.Load(CMap::s_mapPath, CMap::s_mapName);
    CMap::LoadWdt();
    // TODO FUN_007bd540(): the .tex cache
    // TODO FUN_007f2790(mapID)

    CMap::Update(0);

    if (waitAll) {
        AsyncFileReadWaitAll();
    }

    if (CMap::s_loadProgressCallback) {
        CMap::s_loadProgressCallback(1.0f, CMap::s_loadProgressArg);
    }

    CMap::s_loading = 0;
    // TODO DAT_00cdfff4 = 0; DAT_00cd7678 = 1 (day/night: force a full update)
}

// ref: FUN_007bf8b0
// The WDT: version, header, the 64x64 tile table, and, when the header says the map is one
// global WMO, that WMO's placement (not ported yet: it needs the map obj def creation from
// MapLoad.cpp). Then the terrain shader level from the header and the settings pass.
void CMap::LoadWdt() {
    SFile* file = nullptr;
    SFile::Open(CMap::s_wdtFilename, &file);

    if (!file) {
        SErrDisplayAppFatal("CMap::LoadWdt() failed %s\n", CMap::s_wdtFilename);
        return;
    }

    uint32_t chunkHeader[2] = { 0, 0 };

    SFile::Read(file, chunkHeader, 8, nullptr, nullptr, nullptr);
    SFile::Read(file, &CMap::s_wdtVersion, 4, nullptr, nullptr, nullptr);
    SFile::Read(file, chunkHeader, 8, nullptr, nullptr, nullptr);
    SFile::Read(file, CMap::s_wdtHeader, 0x20, nullptr, nullptr, nullptr);
    SFile::Read(file, chunkHeader, 8, nullptr, nullptr, nullptr);
    SFile::Read(file, CMap::s_areaInfo, 0x8000, nullptr, nullptr, nullptr);

    if (CMap::s_wdtHeader[0] & 0x1) {
        // TODO MWMO name + MODF entry -> AllocMapObjDef, FUN_007beae0, FUN_007b0cc0, linked
        // under the map (DAT_00cf08f4 = 1)
    }

    CMap::SetTerrainShaderLevel((CMap::s_wdtHeader[0] & 0x2) ? 2 : 1);
    CMap::LoadSettings();

    SFile::Close(file);
}

// ref: FUN_007bd8a0
// What the map may do on this device: the world enables' shader bits, gated on the terrain
// shaders actually having loaded. DAT_00ce04a2 / a1 / a0 / 9e are the intermediate flags the
// reference keeps; DAT_00ce0498 is the world-space vertex mode shaders can rely on.
void CMap::LoadSettings() {
    uint8_t vertexShaders = (CWorld::s_enables2 & CWorld::Enables2::Enable_VertexShader) != 0;
    uint8_t pixelShaders = (CWorld::s_enables & CWorld::Enables::Enable_PixelShader) != 0;
    uint8_t specularWanted = 0;

    if ((CWorld::s_enables & CWorld::Enables::Enable_8000000) && pixelShaders) {
        specularWanted = 1;
    }

    uint32_t vertexShaded = 0;   // (DAT_00cf08d0 >> 2) & 1 in the reference: the MPHD's third bit
    vertexShaded = (CMap::s_wdtHeader[0] >> 2) & 0x1;

    uint8_t terrainShaders = 0;
    CMap::s_terrainSpecular = 0;

    if (pixelShaders) {
        auto a = CMap::GetTerrain0PixelShader(vertexShaded, 1, 0);
        auto b = CMap::GetTerrain0PixelShader(vertexShaded, 0, 0);
        terrainShaders = a && a->Valid() && b && b->Valid();
    }
    CMap::s_terrainShaders = terrainShaders;

    if (specularWanted && terrainShaders) {
        auto a = CMap::GetTerrain0PixelShader(vertexShaded, 1, 0);
        auto b = CMap::GetTerrain0PixelShader(vertexShaded, 0, 0);
        CMap::s_terrainSpecular = a && a->Valid() && b && b->Valid();
    }

    if (vertexShaders) {
        CMap::s_chunkVerticesWorldSpace = 0;
        if (terrainShaders && CMap::s_terrainVertexShaders[0]) {
            CMap::s_chunkVerticesWorldSpace = CMap::s_terrainVertexShaders[0]->Valid() != 0;
        }
    }

    CMap::s_shaderVertexMode = terrainShaders ? CMap::s_chunkVerticesWorldSpace : 0;
}

// ref: FUN_007b7330
void CMap::SetTerrainShaderLevel(int32_t level) {
    CMap::s_terrainVertexFormat = level;
    CMap::s_terrainShadersDirty = 1;
}

// ref: FUN_0079e470
// The Terrain vertex shader permutation: 64 x point lights, 16 x (layers - 1), 8 x specular
// support, 4 x vertex colour, 2 x a specular layer in the chunk, 1 x shadow map
CGxShader* CMap::GetTerrainVertexShader(int32_t lights, int32_t layers, int32_t specular, int32_t color, int32_t chunkSpecular, int32_t shadow) {
    uint32_t index = (shadow != 0) + (chunkSpecular + (color - 4 + (specular + (layers + lights * 4) * 2) * 2) * 2) * 2;
    return CMap::s_terrainVertexShaders[index];
}

// ref: FUN_0079e5c0
// The pixel shader for a layer count under a shadow level: Terrain1 without shadows, the
// Terrain2/3 sets (or their PCF variants on the ps_2_0 and arbfp1 profiles) with them. The x1
// term is the cube-map specular variant (a layer with MCLY flag 0x400, the list's high bit), the
// x16 term the variant for a layer carrying flag 0x80 (the list's low bit); the shipped
// Terrain1.bls confirms it, permutation 1 declares a cube sampler on s0.
CGxShader* CMap::GetTerrainPixelShader(int32_t twoChunk, int32_t layers, int32_t shadowLevel, int32_t specular, int32_t flag80) {
    layers = layers - 1;

    // The unshadowed answer, kept because it is also the fallback below.
    CGxShader* unshadowed = CMap::s_terrain1PixelShaders[specular + (layers + (twoChunk + flag80 * 2) * 4) * 2];

    CGxShader* shader = nullptr;

    switch (shadowLevel) {
    case 0:
        return unshadowed;

    case 1:
        if (GxCaps().m_shaderTargets[GxSh_Pixel] != GxShPS_ps_2_0 && GxCaps().m_shaderTargets[GxSh_Pixel] != GxShPS_arbfp1) {
            shader = CMap::s_terrain3PixelShaders[specular + (layers + (twoChunk + flag80 * 2) * 0xc) * 2];
        } else {
            shader = CMap::s_terrain2PixelShaders[specular + (layers + (twoChunk + flag80 * 2) * 4) * 2];
        }

        break;

    case 2:
    case 3:
        shader = CMap::s_terrain3PixelShaders[specular - 8 + layers * 2 + (shadowLevel * 4 + (twoChunk + flag80 * 2) * 0xc) * 2];

        break;

    default:
        return nullptr;
    }

    // DIVERGENCE, deliberate, and the reason it is here rather than in a comment on the caller:
    // the reference returns whatever is in the slot and trusts that the shadowed sets loaded,
    // because in the reference they always do. frozen asks the archives for them too, but if a
    // .bls is missing or fails its profile fallback the slot is null, and a null pixel shader
    // does not draw untextured terrain -- it draws NO TERRAIN AT ALL. That is exactly what
    // happened on 2026-09-27: releasing the shadow realloc latch raised the shader level to 2
    // for the first time, these arrays were never loaded by anything, and the world lost its
    // ground.
    //
    // Degrading to the unshadowed shader loses the shadows and keeps the terrain, which is the
    // right way round for a missing asset. It is not a substitute for loading them.
    if (!shader || !shader->Valid()) {
        return unshadowed;
    }

    return shader;
}

// ref: FUN_0079e4b0
CGxShader* CMap::GetTerrain0PixelShader(int32_t a1, int32_t a2, int32_t env) {
    if (a1 == 0) {
        return CMap::s_terrainPixelShaders[0];
    }

    if (a2) {
        return env ? CMap::s_terrainEnvPixelShader[0] : CMap::s_terrainPixelShaders[1];
    }

    return CMap::s_terrainPixelShaders[2];
}

// ref: FUN_0079e4f0
// Load the two shadow-mapped terrain pixel shader sets, replacing whatever is already there.
//
// Called from MapMemInitialize and again from CWorldParam::HwPCFCallback, because which of the
// two names each set is loaded from depends on whether hardware PCF is on: the _pcf variants
// sample a real depth texture with the hardware's own comparison, the plain ones do the
// comparison in the shader against an R32F map. Changing that setting has to reload them, which
// is why this releases before it creates rather than filling in the gaps.
//
// WITHOUT THIS EVERY SHADOWED TERRAIN DRAW HAS NO PIXEL SHADER. Nothing in frozen called it
// before 2026-09-27 -- the arrays were declared, documented as 'loaded by the shadow map
// system, which is not ported', and left null. See the fallback in GetTerrainPixelShader.
void CMap::CreateTerrainShadowShaders() {
    // ORDERING GUARD, and frozen needs it where the reference does not. CWorldParam::Initialize
    // registers hwPCF with a default of "1", and CVar::Register runs the callback as part of
    // registration -- so HwPCFCallback fires, sees the flag change, and asks for a reload long
    // before MapMemInitialize has a device to load through. Returning here makes
    // MapMemInitialize's own call the first real one, which is the order the reference ends up in
    // anyway.
    if (!g_theGxDevicePtr) {
        return;
    }

    for (int32_t i = 0; i < 0x20; i++) {
        if (CMap::s_terrain2PixelShaders[i]) {
            g_theGxDevicePtr->ShaderDestroy(&CMap::s_terrain2PixelShaders[i]);
        }
    }

    for (int32_t i = 0; i < 0x60; i++) {
        if (CMap::s_terrain3PixelShaders[i]) {
            g_theGxDevicePtr->ShaderDestroy(&CMap::s_terrain3PixelShaders[i]);
        }
    }

    int32_t pcf = CShaderEffect::s_usePcfFiltering;

    g_theGxDevicePtr->ShaderCreate(
        CMap::s_terrain2PixelShaders, GxSh_Pixel, "Shaders\\Pixel",
        pcf ? "Terrain2_pcf" : "Terrain2", 0x20);

    g_theGxDevicePtr->ShaderCreate(
        CMap::s_terrain3PixelShaders, GxSh_Pixel, "Shaders\\Pixel",
        pcf ? "Terrain3_pcf" : "Terrain3", 0x60);

    // Whether the archives actually carry these is not answerable from the source, and a set that
    // silently fails to load is what left the world with no ground in the first place. Counted and
    // reported so that a run answers it instead of a reading of the code.
    int32_t valid2 = 0;
    int32_t valid3 = 0;

    for (int32_t i = 0; i < 0x20; i++) {
        if (CMap::s_terrain2PixelShaders[i] && CMap::s_terrain2PixelShaders[i]->Valid()) {
            valid2++;
        }
    }

    for (int32_t i = 0; i < 0x60; i++) {
        if (CMap::s_terrain3PixelShaders[i] && CMap::s_terrain3PixelShaders[i]->Valid()) {
            valid3++;
        }
    }

    fprintf(
        stderr,
        "CMap: shadowed terrain pixel shaders, pcf=%d: %s %d/32 valid, %s %d/96 valid\n",
        pcf,
        pcf ? "Terrain2_pcf" : "Terrain2", valid2,
        pcf ? "Terrain3_pcf" : "Terrain3", valid3);
}

// ref: FUN_0079e7c0
// Everything the map subsystem allocates once: the terrain shader sets, the low-detail index
// pool and buffer, then the object heaps.
//
// TAGGED EXPLICITLY because the string matcher got it wrong the moment the Terrain2 and Terrain3
// names landed here. It bound this to FUN_007a2c60 -- a MapObj.cpp function whose only
// qualification is that it also carries a string beginning 'Terrain' -- while the real body is
// FUN_0079e7c0, 2068 bytes of exactly these allocations, and its call to
// CreateTerrainShadowShaders sits at 0x0079e979.
void CMap::MapMemInitialize() {
    CMapChunk::Initialize();
    CMapObj::Initialize();
    VBBList::InitializeLists();
    DetailDoodad::Initialize();

    // TODO FUN_007afee0, FUN_007cb990, FUN_007a03c0; the two 0x2c-byte records at
    // DAT_00d253d0 / DAT_00d253a4

    for (int32_t i = 0; i < 64 * 64; i++) {
        CMap::s_areaGrid[i] = nullptr;
        CMap::s_areaInfo[i][0] = 0;
    }

    // The cell list the segment query fills (DAT_00cf4928): 0x800 entries, never grown.
    CMap::s_cellList.SetCount(0x800);

    // TODO the map state flags
    // (DAT_00ce04c8, DAT_00ce04c4, DAT_00ce04a4 = -2, DAT_00adfbc4 = -1, DAT_00cf08f4 = 0,
    // DAT_00cf08f0 = 0, DAT_00ce04ac = 0), none of which frozen carries yet; and FUN_0079e3c0,
    // the liquid initialise (its depth ramps are LiquidSurface.cpp's DepthRamp, built on first
    // use; the splash textures and the WaterRipples shaders of FUN_0079e1a0 are not ported).

    for (int32_t i = 0; i < 0x80; i++) {
        CMap::s_terrainVertexShaders[i] = nullptr;
    }
    for (int32_t i = 0; i < 3; i++) {
        CMap::s_terrainPixelShaders[i] = nullptr;
    }
    CMap::s_terrainEnvPixelShader[0] = nullptr;
    for (int32_t i = 0; i < 0x20; i++) {
        CMap::s_terrain1PixelShaders[i] = nullptr;
        CMap::s_terrain2PixelShaders[i] = nullptr;
    }
    for (int32_t i = 0; i < 0x60; i++) {
        CMap::s_terrain3PixelShaders[i] = nullptr;
    }
    for (int32_t i = 0; i < 8; i++) {
        CMap::s_terrain1wPixelShaders[i] = nullptr;
    }
    CMap::s_terrainShadowMapPixelShader[0] = nullptr;

    // The shadowed terrain sets load after the slots above are cleared and before the unshadowed
    // ones (0x0079e979). It must stay below the clearing: the reference never clears the Terrain2
    // and Terrain3 arrays here, and calling this above the loop wipes both sets as soon as they
    // load, so every shadowed terrain shader falls back to its unshadowed one.
    CMap::CreateTerrainShadowShaders();

    g_theGxDevicePtr->ShaderCreate(CMap::s_terrainVertexShaders, GxSh_Vertex, "Shaders\\Vertex", "Terrain", 0x80);
    g_theGxDevicePtr->ShaderCreate(CMap::s_terrainPixelShaders, GxSh_Pixel, "Shaders\\Pixel", "Terrain0", 3);
    g_theGxDevicePtr->ShaderCreate(CMap::s_terrainEnvPixelShader, GxSh_Pixel, "Shaders\\Pixel", "Terrain0_env", 1);

    // The reference switches on the pixel shader profile (CGxCaps +0xc4): 1 and 2 are the
    // ps_1_x profiles with their own Terrain1w variants, 8..10 the register-combiner and
    // texture-shader profiles; everything else gets the full 32-permutation set
    switch (GxCaps().m_shaderTargets[GxSh_Pixel]) {
    case GxShPS_ps_1_1:
        g_theGxDevicePtr->ShaderCreate(CMap::s_terrain1PixelShaders, GxSh_Pixel, "Shaders\\Pixel", "Terrain1", 6);
        g_theGxDevicePtr->ShaderCreate(CMap::s_terrain1wPixelShaders, GxSh_Pixel, "Shaders\\Pixel", "Terrain1w", 6);
        break;
    case GxShPS_ps_1_4:
        g_theGxDevicePtr->ShaderCreate(CMap::s_terrain1PixelShaders, GxSh_Pixel, "Shaders\\Pixel", "Terrain1", 8);
        g_theGxDevicePtr->ShaderCreate(&CMap::s_terrain1wPixelShaders[0], GxSh_Pixel, "Shaders\\Pixel", "Terrain1w_1", 1);
        g_theGxDevicePtr->ShaderCreate(&CMap::s_terrain1wPixelShaders[1], GxSh_Pixel, "Shaders\\Pixel", "Terrain1w_1", 1);
        g_theGxDevicePtr->ShaderCreate(&CMap::s_terrain1wPixelShaders[2], GxSh_Pixel, "Shaders\\Pixel", "Terrain1w_2", 1);
        g_theGxDevicePtr->ShaderCreate(&CMap::s_terrain1wPixelShaders[3], GxSh_Pixel, "Shaders\\Pixel", "Terrain1w_2", 1);
        g_theGxDevicePtr->ShaderCreate(&CMap::s_terrain1wPixelShaders[4], GxSh_Pixel, "Shaders\\Pixel", "Terrain1w_3", 1);
        g_theGxDevicePtr->ShaderCreate(&CMap::s_terrain1wPixelShaders[5], GxSh_Pixel, "Shaders\\Pixel", "Terrain1w_3", 1);
        g_theGxDevicePtr->ShaderCreate(&CMap::s_terrain1wPixelShaders[6], GxSh_Pixel, "Shaders\\Pixel", "Terrain1w_4", 1);
        g_theGxDevicePtr->ShaderCreate(&CMap::s_terrain1wPixelShaders[7], GxSh_Pixel, "Shaders\\Pixel", "Terrain1w_4", 1);
        break;
    case GxShPS_nvts:
    case GxShPS_nvts2:
    case GxShPS_nvts3:
        g_theGxDevicePtr->ShaderCreate(CMap::s_terrain1PixelShaders, GxSh_Pixel, "Shaders\\Pixel", "Terrain1", 4);
        g_theGxDevicePtr->ShaderCreate(CMap::s_terrain1wPixelShaders, GxSh_Pixel, "Shaders\\Pixel", "Terrain1w", 4);
        break;
    default:
        g_theGxDevicePtr->ShaderCreate(CMap::s_terrain1PixelShaders, GxSh_Pixel, "Shaders\\Pixel", "Terrain1", 0x20);
        break;
    }

    g_theGxDevicePtr->ShaderCreate(CMap::s_terrainShadowMapPixelShader, GxSh_Pixel, "Shaders\\Pixel", "TerrainSM", 1);


    CMap::s_lowDetailIndexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Dynamic, 0x1800, GxPoolHintBit_Unk0, "CMap::lowDetailIndexPool");
    CMap::s_lowDetailIndexBuf = g_theGxDevicePtr->BufCreate(CMap::s_lowDetailIndexPool, 2, 0xc00, 0);

    // TODO FUN_007d58b0(0, 1, 0x10, 0x221, 0x18): the liquid vertex buffer block list

    CMap::MapMemInitializeHeaps();

    uint32_t vendor;

    if (OsGetCpuInfo(&vendor) & 0x4) {
        CMap::s_useSse = 1;
    }
}

void CMap::MapMemInitializeHeaps() {
    CMap::s_lightHeap           = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapLight),         128,    "WLIGHT",           true));
    CMap::s_cacheLightHeap      = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapCacheLight),    256,    "WCACHELIGHT",      true));
    CMap::s_mapObjGroupHeap     = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapObjGroup),      128,    "WMAPOBJGROUP",     true));
    CMap::s_mapObjHeap          = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapObj),           32,     "WMAPOBJ",          true));
    CMap::s_baseObjLinkHeap     = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapBaseObjLink),   10000,  "WBASEOBJLINK",     true));
    CMap::s_areaHeap            = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapArea),          16,     "WAREA",            true));
    // The reference sizes this one at 33404 bytes; CMapAreaMed is a skeleton until its module is
    // ported. See docs/ref/parity-map-memory.md.
    CMap::s_areaMedHeap         = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapAreaMed),       16,     "WAREAMED",         true));
    CMap::s_areaLowHeap         = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapAreaLow),       16,     "WAREALOW",         true));
    CMap::s_chunkHeap           = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapChunk),         256,    "WCHUNK",           true));
    CMap::s_doodadDefHeap       = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapDoodadDef),     5000,   "WDOODADDEF",       true));
    CMap::s_entityHeap          = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapEntity),        128,    "WENTITY",          true));
    CMap::s_mapObjDefGroupHeap  = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapObjDefGroup),   128,    "WMAPOBJDEFGROUP",  true));
    CMap::s_mapObjDefHeap       = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CMapObjDef),        64,     "WMAPOBJDEF",       true));
    CMap::s_chunkLiquidHeap     = STORM_NEW(uint32_t)(ObjectAllocAddHeap(sizeof(CChunkLiquid),      64,     "WCHUNKLIQUID",     true));
}

// ----------------------------------------------------------------------------------------------
// Map memory (reference MapMem.cpp, 0x007bfe40 .. 0x007c0c60): one allocator and one release per
// pooled kind. Every Free reads the mem handle before running the destructor, because the
// reference reads it afterwards, which C++ does not allow.

// ref: FUN_007bfe40
void* CMap::MapMemAlloc(uint32_t bytes) {
    return SMemAlloc(bytes, __FILE__, __LINE__, 0x0);
}

// ref: FUN_007bfe60
void CMap::MapMemFree(void* ptr) {
    SMemFree(ptr, __FILE__, __LINE__, 0x0);
}

// ref: FUN_007bfe80
void CMap::UnlinkDoodadDef(CMapDoodadDef* def) {
    if (def->m_linktoslot.IsLinked()) {
        def->m_linktoslot.Unlink();
        def->m_linktofull.Unlink();
    }
}

// ref: FUN_007bff20
CMapObj* CMap::AllocMapObj() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_mapObjHeap, &memHandle, &mem, false)) {
        return nullptr;
    }

    auto mapObj = mem ? new (mem) CMapObj() : nullptr;
    mapObj->m_memHandle = memHandle;

    return mapObj;
}

// ref: FUN_007bff70
void CMap::FreeMapObj(CMapObj* mapObj) {
    mapObj->m_link.Unlink();

    uint32_t memHandle = mapObj->m_memHandle;
    mapObj->~CMapObj();
    ObjectFree(*CMap::s_mapObjHeap, memHandle);
}

// ref: FUN_007bffe0
CMapObjGroup* CMap::AllocMapObjGroup() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_mapObjGroupHeap, &memHandle, &mem, false)) {
        return nullptr;
    }

    auto group = mem ? new (mem) CMapObjGroup() : nullptr;
    group->m_memHandle = memHandle;

    return group;
}

// ref: FUN_007c0030
void CMap::FreeMapObjGroup(CMapObjGroup* group) {
    group->m_link.Unlink();

    uint32_t memHandle = group->m_memHandle;
    group->~CMapObjGroup();
    ObjectFree(*CMap::s_mapObjGroupHeap, memHandle);
}

// ref: FUN_007c00a0
void CMap::FreeArea(CMapArea* area) {
    area->m_lameAssLink.Unlink();

    uint32_t memHandle = area->m_memHandle;
    area->~CMapArea();
    ObjectFree(*CMap::s_areaHeap, memHandle);
}

// ref: FUN_007c0110
void CMap::FreeAreaMed(CMapAreaMed* area) {
    area->m_lameAssLink.Unlink();

    uint32_t memHandle = area->m_memHandle;
    area->~CMapAreaMed();
    ObjectFree(*CMap::s_areaMedHeap, memHandle);
}

// ref: FUN_007c0180
void CMap::FreeChunk(CMapChunk* chunk) {
    chunk->m_lameAssLink.Unlink();

    uint32_t memHandle = chunk->m_memHandle;
    chunk->~CMapChunk();
    ObjectFree(*CMap::s_chunkHeap, memHandle);
}

// ref: FUN_007c01f0
CMapDoodadDef* CMap::AllocDoodadDef() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_doodadDefHeap, &memHandle, &mem, false)) {
        return nullptr;
    }

    auto def = mem ? new (mem) CMapDoodadDef() : nullptr;
    def->m_memHandle = memHandle;

    return def;
}

// ref: FUN_007c0240
void CMap::FreeDoodadDef(CMapDoodadDef* def) {
    // A fade time of -1 asks the sound engine for its default
    SI2::StopOrFadeOut(&def->m_soundKit, 0, -1.0f, 1);

    def->m_lameAssLink.Unlink();
    CMap::UnlinkDoodadDef(def);

    uint32_t memHandle = def->m_memHandle;
    def->~CMapDoodadDef();
    ObjectFree(*CMap::s_doodadDefHeap, memHandle);
}

// ref: FUN_007c3020
// The last link to a doodad def has gone: its model is let go (callbacks first, so nothing calls
// back into a def about to be freed), it leaves its distance row, and it returns to the heap.
void CMap::ReleaseDoodadDef(CMapDoodadDef* def) {
    if (def->m_linkCount != 0) {
        return;
    }

    if (def->m_model) {
        def->m_model->SetSequenceDoneCallback(nullptr, 0);
        def->m_model->SetAnimEventCallback(nullptr, 0);
        def->m_model->m_lightingCallback = nullptr;
        def->m_model->m_lightingArg = nullptr;
        def->m_model->Release();
        def->m_model = nullptr;
    }

    def->m_rowLink.Unlink();

    CMap::FreeDoodadDef(def);
}

// ref: FUN_007c02d0
void CMap::FreeEntity(CMapEntity* entity) {
    entity->m_lameAssLink.Unlink();

    uint32_t memHandle = entity->m_memHandle;
    entity->~CMapEntity();
    ObjectFree(*CMap::s_entityHeap, memHandle);
}

// ref: FUN_007c0340
// Lights are never unlinked here: the reference goes straight to the destructor.
void CMap::FreeLight(CMapLight* light) {
    uint32_t memHandle = light->m_memHandle;
    light->~CMapLight();
    ObjectFree(*CMap::s_lightHeap, memHandle);
}

// ref: FUN_007c0370
void CMap::FreeMapObjDefGroup(CMapObjDefGroup* group) {
    group->m_lameAssLink.Unlink();

    uint32_t memHandle = group->m_memHandle;
    group->~CMapObjDefGroup();
    ObjectFree(*CMap::s_mapObjDefGroupHeap, memHandle);
}

// ref: FUN_007c03e0
CMapObjDef* CMap::AllocMapObjDef() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_mapObjDefHeap, &memHandle, &mem, false)) {
        return nullptr;
    }

    auto def = mem ? new (mem) CMapObjDef() : nullptr;
    def->m_memHandle = memHandle;

    return def;
}

// ref: FUN_007c0430
void CMap::FreeMapObjDef(CMapObjDef* def) {
    def->m_lameAssLink.Unlink();
    CMap::UnlinkMapObjDef(def);

    // The building's props go with it. The stand-in released these on tile unload; without this the
    // models, and the arrays their lighting callbacks point into, would leak on every map change.
    for (uint32_t i = 0; i < def->m_doodadCount; i++) {
        if (def->m_doodads[i]) {
            ParticleFxForgetModel(def->m_doodads[i]);
            def->m_doodads[i]->DetachFromScene();
            def->m_doodads[i]->Release();
        }
    }

    if (def->m_doodads) {
        SMemFree(def->m_doodads, __FILE__, __LINE__, 0);
    }

    if (def->m_doodadScale) {
        SMemFree(def->m_doodadScale, __FILE__, __LINE__, 0);
    }

    if (def->m_doodadAmbient) {
        SMemFree(def->m_doodadAmbient, __FILE__, __LINE__, 0);
    }

    def->m_doodads = nullptr;
    def->m_doodadScale = nullptr;
    def->m_doodadAmbient = nullptr;
    def->m_doodadCount = 0;

    uint32_t memHandle = def->m_memHandle;
    def->~CMapObjDef();
    ObjectFree(*CMap::s_mapObjDefHeap, memHandle);
}

// ref: FUN_0079e6a0
// The def leaves the uniqueId hash: the two links are the table's slot chain and its full list,
// which the reference unlinks by hand rather than through TSHashTable::Unlink.
void CMap::UnlinkMapObjDef(CMapObjDef* def) {
    if (def->m_linktoslot.IsLinked()) {
        def->m_linktoslot.Unlink();
        def->m_linktofull.Unlink();
    }
}

// ref: FUN_007c04a0
void CMap::FreeChunkLiquid(CChunkLiquid* liquid) {
    liquid->m_link.Unlink();

    uint32_t memHandle = liquid->m_memHandle;
    liquid->~CChunkLiquid();
    ObjectFree(*CMap::s_chunkLiquidHeap, memHandle);
}

// ref: FUN_007c0500
// Render chunks come off the free list when one is waiting, otherwise from SMemAlloc. The
// reference constructs a fresh one through an inlined TSList::NewNode, which links it to the
// head of the free list and unlinks it again before returning; that nets to nothing and is not
// repeated here.
CMapRenderChunk* CMap::AllocRenderChunk() {
    auto chunk = CMap::s_renderChunkFreeList.Head();

    if (chunk) {
        chunk->m_link.Unlink();
        new (chunk) CMapRenderChunk();
        return chunk;
    }

    void* mem = SMemAlloc(sizeof(CMapRenderChunk), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);

    if (!mem) {
        return nullptr;
    }

    return new (mem) CMapRenderChunk();
}

// ref: FUN_007c0610
void CMap::FreeRenderChunk(CMapRenderChunk* chunk) {
    chunk->m_link.Unlink();
    chunk->~CMapRenderChunk();
    CMap::s_renderChunkFreeList.LinkToHead(chunk);
}

// ref: FUN_007c0670
CMapEntity* CMap::AllocEntity(int32_t linkToHead) {
    CMapEntity* entity;
    uint32_t memHandle;
    void* mem = nullptr;

    if (ObjectAlloc(*CMap::s_entityHeap, &memHandle, &mem, false)) {
        entity = mem ? new (mem) CMapEntity() : nullptr;
        entity->m_memHandle = memHandle;
    } else {
        entity = nullptr;
    }

    if (linkToHead) {
        CMap::s_entityList.LinkToHead(entity);
    } else {
        CMap::s_entityList.LinkToTail(entity);
    }

    return entity;
}

// ref: FUN_007c0750
// A new link records its owner, counts against it, and joins the owner's parent-link list; the
// caller fills in ref and files it on the referenced object's side.
CMapBaseObjLink* CMap::AllocBaseObjLink(CMapBaseObj* owner) {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_baseObjLinkHeap, &memHandle, &mem, false)) {
        // The reference goes on to write through the null link; there is nothing sensible to
        // do with a failed 10000-a-block pool but stop here
        return nullptr;
    }

    auto link = static_cast<CMapBaseObjLink*>(mem);

    if (link) {
        link->refLink = {};
        link->ownerLink = {};
    }

    link->memHandle = memHandle;

    owner->m_linkCount++;
    link->owner = owner;
    link->ref = nullptr;
    owner->m_parentLinkList.LinkToTail(link);

    return link;
}

// ref: FUN_007c07c0
CMapArea* CMap::AllocArea() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_areaHeap, &memHandle, &mem, false)) {
        CMap::s_areaList.LinkToTail(nullptr);
        return nullptr;
    }

    auto area = mem ? new (mem) CMapArea() : nullptr;
    area->m_memHandle = memHandle;
    CMap::s_areaList.LinkToTail(area);

    return area;
}

// ref: FUN_007c0830
CMapChunk* CMap::AllocChunk() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_chunkHeap, &memHandle, &mem, false)) {
        CMap::s_chunkList.LinkToTail(nullptr);
        return nullptr;
    }

    auto chunk = mem ? new (mem) CMapChunk() : nullptr;
    chunk->m_memHandle = memHandle;
    CMap::s_chunkList.LinkToTail(chunk);

    return chunk;
}

// ref: FUN_007c08a0
CMapLight* CMap::AllocLight() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_lightHeap, &memHandle, &mem, false)) {
        CMap::s_lightList.LinkToTail(nullptr);
        return nullptr;
    }

    auto light = mem ? new (mem) CMapLight() : nullptr;
    light->m_memHandle = memHandle;
    CMap::s_lightList.LinkToTail(light);

    return light;
}

// ref: FUN_007c0910
CMapObjDefGroup* CMap::AllocMapObjDefGroup() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_mapObjDefGroupHeap, &memHandle, &mem, false)) {
        CMap::s_mapObjDefGroupList.LinkToTail(nullptr);
        return nullptr;
    }

    auto group = mem ? new (mem) CMapObjDefGroup() : nullptr;
    group->m_memHandle = memHandle;
    CMap::s_mapObjDefGroupList.LinkToTail(group);

    return group;
}

// How many of the eight-by-eight cells fit in a yard, and the half a cell the rounding wants.
static const float CELLS_PER_YARD = 0.2399999946355819f;  // DAT_00a3fd6c
static const float CELL_ROUND_BIAS = 0.5f;                // DAT_00adfd74

// Frozen's own name for addressing the reference already does inside its point queries: a world
// point resolves to a cell, the cell's high bits pick the tile and the middle bits the chunk.
// The tile's row comes from x and its column from y, which is the map's own convention and the
// reason the chunk indices look transposed.
// The cell size of the streaming lookups, a chunk's worth (DAT_00a3ffb8), and a tile's
// (DAT_00a3ffbc); the half the rounding wants is DAT_00aee0e8.
static const float CHUNKS_PER_YARD = 0.029999999329447746f;
static const float TILES_PER_YARD = 0.0018749999580904841f;

// ref: FUN_007b4960
// The tile under a point.
CMapArea* CMap::GetTargetArea(const C3Vector& target) {
    int32_t row = static_cast<int32_t>(roundf(-(target.x - MAP_HALF_EXTENT) * TILES_PER_YARD - 0.5f));
    int32_t col = static_cast<int32_t>(roundf(-(target.y - MAP_HALF_EXTENT) * TILES_PER_YARD - 0.5f));

    return CMap::s_areaGrid[row * 64 + col];
}

// ref: FUN_007b49c0
// The chunk under a point, through its tile; null where the tile is not loaded.
CMapChunk* CMap::GetTargetChunk(const C3Vector& target) {
    uint32_t col = static_cast<uint32_t>(roundf(-(target.y - MAP_HALF_EXTENT) * CHUNKS_PER_YARD - 0.5f));
    uint32_t row = static_cast<uint32_t>(roundf(-(target.x - MAP_HALF_EXTENT) * CHUNKS_PER_YARD - 0.5f));

    auto area = CMap::s_areaGrid[((static_cast<int32_t>(row) >> 4) & 0x3f) * 64 + ((static_cast<int32_t>(col) >> 4) & 0x3f)];

    if (!area) {
        return nullptr;
    }

    return area->m_chunks[(row & 0xf) * 16 + (col & 0xf)];
}

CMapChunk* CMap::ChunkAt(const C3Vector& position) {
    float cellFromY = -(position.y - MAP_HALF_EXTENT) * CELLS_PER_YARD;
    float cellFromX = -(position.x - MAP_HALF_EXTENT) * CELLS_PER_YARD;

    int32_t col = static_cast<int32_t>(roundf(cellFromY - CELL_ROUND_BIAS));
    int32_t row = static_cast<int32_t>(roundf(cellFromX - CELL_ROUND_BIAS));

    auto area = CMap::s_areaGrid[((row >> 7) & 0x3f) * 64 + ((col >> 7) & 0x3f)];

    if (!area) {
        return nullptr;
    }

    return area->m_chunks[((row >> 3) & 0xf) * 16 + ((col >> 3) & 0xf)];
}

// How far the map reaches, corner to corner, in yards. DAT_009e2ac8, and exactly twice
// MAP_HALF_EXTENT -- the bounds test runs in YARDS, before the cell scaling, which is why it
// needs its own constant rather than a cell count.
static const float MAP_EXTENT = 34133.33203125f;

// Two bits a cell, eight cells to a row of the low-quality texture map: which layer of the four
// is showing at each. DAT_00a3fb88 and DAT_00a3fb98, the reference keeping the shifts as dwords.
static const uint16_t LAYER_MASK[8] = {
    0x0003, 0x000c, 0x0030, 0x00c0, 0x0300, 0x0c00, 0x3000, 0xc000
};
static const uint32_t LAYER_SHIFT[8] = { 0, 2, 4, 6, 8, 10, 12, 14 };

// ref: FUN_007a0530
// The terrain type of the ground under a world point.
//
// Same addressing as ChunkAt above, with the cell's own bits kept: the chunk's low-quality
// texture map says which of its four layers is showing at that cell, two bits each, and the
// layer's MCLY effectId names a GroundEffectTexture row whose m_terrainType is the answer.
//
// THE BOUNDS TEST IS IN YARDS and comes before the scaling, so it uses the full map extent rather
// than a cell count. The hole test is the one CMapChunk already has a table for: s_holeMask is
// indexed by the cell's 2x2 block, which is what SMChunk::holes stores a bit per.
//
// WHAT IT IS FOR, which is not obvious from here: the map's segment query (FUN_007a2760) stores
// this on a CMapStaticEntity at +0xb8, and that field decides which branch the query takes when
// it links models onto the ray list. An entity whose terrain type has never been resolved reads
// as never placed. So this is the first half of what the ray chain is waiting on.
//
// GetLoadedArea rather than s_areaGrid directly, which is the reference's own choice: a tile
// still streaming is passed over instead of half-read.
bool CMap::GetTerrainType(const C3Vector& position, int32_t* terrainType) {
    float yardsFromY = -(position.y - MAP_HALF_EXTENT);
    float yardsFromX = -(position.x - MAP_HALF_EXTENT);

    if (yardsFromY < 0.0f || yardsFromX < 0.0f) {
        return false;
    }

    if (yardsFromY > MAP_EXTENT || yardsFromX > MAP_EXTENT) {
        return false;
    }

    int32_t col = static_cast<int32_t>(roundf(yardsFromY * CELLS_PER_YARD - CELL_ROUND_BIAS));
    int32_t row = static_cast<int32_t>(roundf(yardsFromX * CELLS_PER_YARD - CELL_ROUND_BIAS));

    CMapArea* area = CMap::GetLoadedArea((col >> 7) & 0x3f, (row >> 7) & 0x3f);

    if (!area) {
        return false;
    }

    CMapChunk* chunk = area->m_chunks[((row >> 3) & 0xf) * 16 + ((col >> 3) & 0xf)];

    // FROZEN-ONLY. The reference dereferences the header, the texture map and the layers without
    // checking; they are filled together when the chunk's data lands, so a chunk in the grid but
    // not yet read would fault here rather than anywhere that points at the cause.
    if (!chunk || !chunk->m_header || !chunk->m_lowQualityTextureMap || !chunk->m_layers) {
        return false;
    }

    uint32_t cellY = static_cast<uint32_t>(col) & 7;
    uint32_t cellX = static_cast<uint32_t>(row) & 7;

    if (CMapChunk::s_holeMask[(cellY >> 1) + (cellX >> 1) * 4] & chunk->m_header->holes) {
        return false;
    }

    uint32_t layer = (chunk->m_lowQualityTextureMap[cellX] & LAYER_MASK[cellY])
                   >> LAYER_SHIFT[cellY];

    auto effect = g_groundEffectTextureDB.GetRecord(chunk->m_layers[layer].effectId);

    if (!effect) {
        return false;
    }

    *terrainType = effect->m_terrainType;

    return true;
}

// ref: FUN_007c1660
// The terrain height under a world point, and which chunk answered.
//
// The same cell addressing as GetTerrainType above, with two differences worth naming because
// both are the reference's choices rather than oversights. There is NO bounds test in yards: the
// masks and the grid lookup are what keep a wild point from reading out of the array, so a point
// off the map comes back false through the area test rather than being rejected up front. And the
// grid is indexed here rather than going through GetLoadedArea, which is a separate function in
// the reference and not called from this one -- the m_asyncObject test inlined below is exactly
// what GetLoadedArea does, so routing through it would behave the same and make one call the
// reference does not.
//
// The chunk is an OUT parameter, cleared before anything else, and its caller
// (FUN_007c28f0, the placement query) uses it to tell the two failures apart: a point over
// nothing at all, versus a point over a chunk whose height it then compares against.
bool CMap::GetTerrainHeight(const C3Vector& position, float* height, CMapChunk** outChunk) {
    *outChunk = nullptr;

    int32_t col = static_cast<int32_t>(roundf(-(position.y - MAP_HALF_EXTENT) * CELLS_PER_YARD
                                             - CELL_ROUND_BIAS));
    int32_t row = static_cast<int32_t>(roundf(-(position.x - MAP_HALF_EXTENT) * CELLS_PER_YARD
                                             - CELL_ROUND_BIAS));

    auto area = CMap::s_areaGrid[((row >> 7) & 0x3f) * 64 + ((col >> 7) & 0x3f)];

    if (!area || area->m_asyncObject) {
        return false;
    }

    auto chunk = area->m_chunks[((row >> 3) & 0xf) * 16 + ((col >> 3) & 0xf)];

    if (!chunk) {
        return false;
    }

    *outChunk = chunk;

    return chunk->HeightAt(position, static_cast<uint32_t>(col), static_cast<uint32_t>(row),
                           height);
}

// How close under a surface still counts as being at it.
static const float LIQUID_EPSILON = 0.009999999776482582f;   // DAT_009f1968

// ref: FUN_007a0820
// The same addressing as ChunkAt, but the fractions within the cell are kept: the layer's own
// height is interpolated at the point rather than taken flat. A tile still loading is passed
// over rather than half-read.
bool CMap::GetTerrainLiquid(const C3Vector& position, uint32_t* liquidType, float* height,
                            int32_t strict) {
    float cellFromY = -(position.y - MAP_HALF_EXTENT) * CELLS_PER_YARD;
    float cellFromX = -(position.x - MAP_HALF_EXTENT) * CELLS_PER_YARD;

    int32_t col = static_cast<int32_t>(roundf(cellFromY - CELL_ROUND_BIAS));
    int32_t row = static_cast<int32_t>(roundf(cellFromX - CELL_ROUND_BIAS));

    auto area = CMap::s_areaGrid[((row >> 7) & 0x3f) * 64 + ((col >> 7) & 0x3f)];

    if (!area || area->m_asyncObject) {
        return false;
    }

    auto chunk = area->m_chunks[((row >> 3) & 0xf) * 16 + ((col >> 3) & 0xf)];

    if (!chunk) {
        return false;
    }

    uint32_t tile[2] = { static_cast<uint32_t>(col) & 7, static_cast<uint32_t>(row) & 7 };
    float frac[2] = { cellFromY - static_cast<float>(col), cellFromX - static_cast<float>(row) };

    for (auto liquid = chunk->m_liquidList.Head(); liquid; liquid = chunk->m_liquidList.Next(liquid)) {
        if (!liquid->CoversTile(tile[0], tile[1])) {
            continue;
        }

        if (!liquid->GetHeightAt(frac, tile, height)) {
            continue;
        }

        if (position.z >= *height + LIQUID_EPSILON) {
            continue;
        }

        if (!strict) {
            *liquidType = liquid->m_liquidType;

            return true;
        }

        // Strict: the ground under the point has to be below it too, or a point inside a
        // cliff with water on the far side would read as swimming. A cell the chunk has a hole
        // in leaves the height alone, and the sentinel is below everything, so a hole counts as
        // open water -- which is what the reference does.
        float ground = -10000.0f;

        chunk->HeightAt(position, static_cast<uint32_t>(col), static_cast<uint32_t>(row), &ground);

        if (ground < position.z + LIQUID_EPSILON) {
            *liquidType = liquid->m_liquidType;

            return true;
        }
    }

    return false;
}

// ref: FUN_007b5630
// The waiting list, walked once a frame. An entity sits on it from the moment it is created
// until its model is in; then it is placed, told how big it is, and let go. An entity with no
// model at all is placed straight away -- there is nothing to wait for.
//
// Placing it is what sets the bit both traversal walks test, so nothing draws a doodad until
// this has run on it.
void CMap::UpdatePendingEntities() {
    for (auto entity = CMap::s_pendingEntityList.Head(); entity; ) {
        auto next = CMap::s_pendingEntityList.Next(entity);

        if (entity->m_model && !entity->m_model->IsLoaded(0, 0)) {
            entity = next;

            continue;
        }

        // Bit 4 says someone else owns the placement.
        if (!(entity->m_flags7c & 0x10)) {
            // The reference reads the placement matrix off the entity at one offset both def
            // types share; frozen keeps it on the subclasses, so it is fetched by type.
            if (entity->m_type & CMapBaseObj::Type_DoodadDef) {
                entity->Place(static_cast<CMapDoodadDef*>(entity)->m_placement);
            }

            // TODO FUN_007a06a0(&m_position): under liquid, the entity draws at the dimmed
            // opacity DAT_00a40304 rather than one.

            // TODO FUN_007b55e0: a doodad standing inside a building joins that building's
            // group, so the interior light reaches it.

            entity->m_flags |= 0x81;
        }

        entity->m_rowLink.Unlink();

        entity = next;
    }
}

// Frozen's own, with no reference counterpart: the particle system wants every doodad model in
// the world, and nothing else keeps a list of them. Walking the chunks is the cheapest way to
// reach them all, and the frame stamp keeps a doodad on two chunks from being handed over twice.
void CMap::ForEachDoodadModel(void (*fn)(CM2Model* model, void* arg), void* arg) {
    // Its own counter, not the scene's. Sharing that one would have this walk and the
    // traversal quietly deciding for each other which doodads they had already seen.
    static int32_t s_walkStamp = 0;

    s_walkStamp--;

    for (int32_t i = 0; i < 64 * 64; i++) {
        auto area = CMap::s_areaGrid[i];

        if (!area) {
            continue;
        }

        for (int32_t c = 0; c < 256; c++) {
            auto chunk = area->m_chunks[c];

            if (!chunk) {
                continue;
            }

            for (auto link = chunk->m_entityLinkList.Head(); link; link = chunk->m_entityLinkList.Next(link)) {
                auto entity = static_cast<CMapStaticEntity*>(link->owner);

                if (!entity->m_model || entity->m_walkStamp == s_walkStamp) {
                    continue;
                }

                entity->m_walkStamp = s_walkStamp;

                fn(entity->m_model, arg);
            }
        }
    }
}

// ref: FUN_007b5590
// Every entity goes back into a distance row once a frame. Bit 2 of the state word takes one
// out of the scene entirely -- it is not hidden, it simply is not there this frame -- so those
// are passed over rather than filed.
void CMap::BucketEntities(int32_t update) {
    if (!update) {
        return;
    }

    for (auto obj = CMap::s_entityList.Head(); obj; obj = CMap::s_entityList.Next(obj)) {
        auto entity = static_cast<CMapEntity*>(obj);

        if (entity->m_flags7c & 0x4) {
            continue;
        }

        CWorldScene::AddEntity(entity);
    }
}

// ref: FUN_007c0980
CChunkLiquid* CMap::AllocChunkLiquid() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_chunkLiquidHeap, &memHandle, &mem, false)) {
        CMap::s_chunkLiquidList.LinkToTail(nullptr);
        return nullptr;
    }

    auto liquid = mem ? new (mem) CChunkLiquid() : nullptr;
    liquid->m_memHandle = memHandle;
    CMap::s_chunkLiquidList.LinkToTail(liquid);

    return liquid;
}

// ref: FUN_007c09f0
void CMap::FreeBaseObjLink(CMapBaseObjLink* link) {
    link->ownerLink.Unlink();
    link->refLink.Unlink();

    link->owner->m_linkCount--;
    link->ref = nullptr;
    link->owner = nullptr;

    uint32_t memHandle = link->memHandle;
    link->~CMapBaseObjLink();
    ObjectFree(*CMap::s_baseObjLinkHeap, memHandle);
}

// ref: FUN_007c0a90
CMapAreaLow* CMap::AllocAreaLow() {
    uint32_t memHandle;
    void* mem = nullptr;

    if (!ObjectAlloc(*CMap::s_areaLowHeap, &memHandle, &mem, false)) {
        return nullptr;
    }

    auto area = mem ? new (mem) CMapAreaLow() : nullptr;
    area->m_memHandle = memHandle;

    return area;
}

// ref: FUN_007c0ae0
void CMap::FreeAreaLowObject(uint32_t* heap, CMapAreaLow* area) {
    area->m_link.Unlink();
    ObjectFree(*heap, area->m_memHandle);
}

// ref: FUN_007c0c60
void CMap::FreeAreaLow(CMapAreaLow* area) {
    CMap::FreeAreaLowObject(CMap::s_areaLowHeap, area);
}

// ----------------------------------------------------------------------------------------------
// Tiles and chunks

// ref: FUN_007d9a70
// A tile at grid column x, row y: pooled, linked under the map, placed by its indices. The
// corner is the tile's largest-x, largest-y point (both axes run negative with the index); the
// box spans one tile back from it.
CMapArea* CMap::CreateArea(int32_t x, int32_t y) {
    auto area = CMap::AllocArea();
    auto link = CMap::AllocBaseObjLink(area);
    CMap::s_areaLinkList.LinkToTail(link);

    area->m_chunkBaseX = x << 4;
    area->m_areaY = y;

    float cornerY = static_cast<float>(y << 4) * CHUNK_SIZE;
    area->m_flags = 0;
    area->m_areaX = x;
    area->m_chunkBaseY = y << 4;
    area->m_bounds.t.y = cornerY;

    float cornerX = static_cast<float>(x << 4) * -CHUNK_SIZE + MAP_HALF_EXTENT;
    cornerY = -cornerY + MAP_HALF_EXTENT;
    area->m_corner.x = cornerY;
    area->m_bounds.t.x = cornerY;
    area->m_corner.y = cornerX;
    area->m_bounds.t.y = cornerX;
    area->m_bounds.b.x = cornerY - TILE_SIZE;
    area->m_bounds.b.y = cornerX - TILE_SIZE;

    CMap::s_areaGrid[y * 64 + x] = area;

    return area;
}

// ref: FUN_007c3700
void CMap::UnloadArea(CMapArea* area) {
    CMap::s_areaGrid[area->m_areaY * 64 + area->m_areaX] = nullptr;
    area->Destroy();
    CMap::FreeArea(area);
}

// ref: FUN_007c35d0
void CMap::DestroyChunk(CMapChunk* chunk) {
    chunk->Destroy();
    CMap::FreeChunk(chunk);
}

// ref: FUN_007b4830
// Squared distance in the plane from a point to a box, zero along an axis the point is inside
float CMap::AreaDistanceSq(const CAaBox& box, const C2Vector& point) {
    float x;

    if (box.b.x <= point.x) {
        x = point.x <= box.t.x ? point.x : box.t.x;
    } else {
        x = box.b.x;
    }

    float dx = x - point.x;

    if (point.y < box.b.y) {
        return (box.b.y - point.y) * (box.b.y - point.y) + dx * dx;
    }

    if (box.t.y < point.y) {
        return (box.t.y - point.y) * (box.t.y - point.y) + dx * dx;
    }

    return (point.y - point.y) * (point.y - point.y) + dx * dx;
}

// ref: FUN_007bd480
// Ten tries at the archive before giving up on the map entirely
int32_t CMap::SafeOpen(const char* path, SFile** file) {
    for (int32_t i = 10; i; i--) {
        if (SFile::Open(path, file)) {
            return 1;
        }
    }

    SErrDisplayAppFatal("CMap::SafeOpen() failed %s", path);
    return 0;
}

// ref: FUN_007bd4d0
// Ten tries at reading a whole file, then the fatal
int32_t CMap::SafeRead(const char* path, SFile* file, void* buffer, uint32_t size) {
    for (int32_t i = 10; i; i--) {
        if (SFile::Read(file, buffer, size, nullptr, nullptr, nullptr)) {
            return 1;
        }

        SysMsgPrintf(SYSMSG_ERROR, "CMap::SafeRead() failed %s\n", path);
    }

    SErrDisplayAppFatal("CMap::SafeRead() failed %s", path);
    return 0;
}

// ref: FUN_007c2ff0
// The cleanup a cancelled tile read runs: the async object goes, then the buffer it was reading
// into (the tile gave up ownership when it cancelled)
void CMap::AsyncLoadCleanup(CAsyncObject* object) {
    void* buffer = object->buffer;

    AsyncFileReadDestroyObject(object);
    CMap::MapMemFree(buffer);
}

// ref: FUN_007d9990
// A map texture: trilinear, wrapped both ways. The reference collects the load status into a
// CStatus and, when the load fails, prints through a routine that is a bare ret in the retail
// client (FUN_005eeb70).
HTEXTURE CMap::LoadTexture(const char* name) {
    CStatus status;

    CGxTexFlags flags(GxTex_LinearMipLinear, 1, 1, 0, 0, 0, 1);
    auto texture = TextureCreate(name, flags, &status, 0);

    // TODO FUN_004b4f90(&status, 2) is not identified

    return texture;
}

// ----------------------------------------------------------------------------------------------
// Update

// ref: FUN_007b6b00
// The per-frame map update: unload everything when the window moved too far, animate liquids,
// pick WMO shaders, stream tiles, update WMO defs and entities, and, while a map is loading,
// keep streaming until every tile round the target is in. The non-streaming loading loop
// (four rounds of AsyncFileReadWaitAll with progress callbacks) and the streaming-mode loop are
// recorded where they go; FUN_007cf840, FUN_007ad020, FUN_007b9560, FUN_007b6110, FUN_007b5630
// and FUN_007b5590 are not ported yet.
void CMap::Update(int32_t update) {
    if (CWorld::s_reloadMap) {
        CMap::UnloadAll();
    }

    // TODO DAT_00ce04c0 = 0; DAT_00ce04bc = 0; DAT_00ce04ac = 0 (per-frame counters)
    // TODO FUN_007cf840(dt): chunk liquid animation
    CMapObj::UpdateAll();
    CMap::RecycleBufBlocks();
    CMap::UpdateAreas(update);
    CMap::UpdateMapObjDefs(update);
    CMap::UpdatePendingEntities();
    CMap::BucketEntities(update);

    if (CMap::s_loading) {
        if (CMap::s_streamingMode == 0) {
            // Loading behind a loading screen: drain every read, update, and repeat, so each
            // round loads what the previous one's reads revealed. The progress callback marks a
            // quarter, a half, two thirds and three quarters.
            static const float LOAD_PROGRESS[4] = { 0.25f, 0.5f, 0.66f, 0.75f };

            for (int32_t round = 0; round < 4; round++) {
                AsyncFileReadWaitAll();

                if (CMap::s_loadProgressCallback) {
                    CMap::s_loadProgressCallback(LOAD_PROGRESS[round], CMap::s_loadProgressArg);
                }

                if (round == 3) {
                    break;
                }

                CMap::UpdateAreas(update);
                CMap::UpdateMapObjDefs(update);
                CMapObj::UpdateAll();
                CMap::UpdatePendingEntities();
            }
        } else {
            // TODO the streaming-mode load loop (FUN_007b4960, FUN_007b5e80, FUN_007b50b0)
        }
    }
}

// ref: FUN_007c3730
// Drops every loaded tile and every mid-detail tile, then the low-detail ones (FUN_007cbfe0 is
// not ported yet)
void CMap::UnloadAll() {
    for (auto link = CMap::s_areaLinkList.Head(); link; ) {
        auto next = CMap::s_areaLinkList.Next(link);
        auto area = static_cast<CMapArea*>(link->owner);

        CMap::FreeBaseObjLink(link);
        CMap::s_areaGrid[area->m_areaY * 64 + area->m_areaX] = nullptr;
        area->Destroy();
        CMap::FreeArea(area);

        link = next;
    }

    // TODO the 64x64 grid of CMapAreaMed at DAT_00ce08d0: FreeAreaMed each
    // TODO FUN_007cbfe0(): the low-detail areas
}

// ref: FUN_007b53b0
void CMap::ClearFrameChunkList() {
    for (auto chunk = CMap::s_frameChunkList.Head(); chunk; ) {
        auto next = CMap::s_frameChunkList.Next(chunk);
        chunk->m_frameLink.Unlink();
        chunk = next;
    }
}

// ref: FUN_007b5420
// The liquids animated this frame (DAT_00adfc34, link at CChunkLiquid +0x68); FUN_007cde30
// per liquid and the fade test on +0x30 are not ported yet
// Ages every liquid layer the frame reached and lets go of the ones that have been out of
// sight long enough. A layer only leaves the list here, which is why one that was seen keeps
// its place for the two seconds its surface lives on.
void CMap::UpdateFrameLiquids() {
    auto liquid = CWorldScene::s_frameLiquidList.Head();

    while (liquid) {
        auto next = CWorldScene::s_frameLiquidList.Next(liquid);

        liquid->UpdateAnim();

        if (liquid->m_animTime < 0.0f) {
            liquid->m_frameLink.Unlink();
        }

        liquid = next;
    }
}

// ref: FUN_0079a870
// The map's frame: the visibility traversal from the camera, the scene clear, then the passes.
// Ported so far: the render chunk pools, the outdoor traversal, the clear and the terrain pass.
// Everything else the reference does here is listed in place as it comes; the map objects,
// liquids, sky and the rest still draw from the stand-in and CGWorldFrame around this call.
void CMap::Render(const C3Vector& cameraPos, float dt) {
    if (!CWorldScene::s_cameraDef) {
        CWorldScene::BucketMapObjDefGroups();
    }

    CWorldScene::UpdateCameraLiquid();

    GxRsPush();
    GxXformPush(GxXform_World);

    // The horizon starts the frame as far below anything as makes no difference, so the first
    // ridge to shade a column always wins. Without this it would keep the highest skyline it
    // ever saw and progressively hide the world.
    for (uint32_t i = 0; i < CWorldScene::HORIZON_COLUMNS; i++) {
        CWorldScene::s_horizonColumnFlags[i] = 0;
        CWorldScene::s_horizonBuffer[i] = -1000000.0f;
    }

    CWorldScene::s_visibleMapObjCount = 0;
    CWorldScene::s_visibleChunkCount = 0;
    CWorldScene::s_visibleEntityCount = 0;
    CWorldScene::s_visibleCount8624 = 0;

    CWorldScene::s_frustums[0].SetCorners(CWorldScene::s_frustumCorners);
    CWorldScene::s_farChunkDistance = CWorld::GetFarClip() - 33.33333206176758f;
    // TODO CWorldScene::s_hasMapObjs from the map object def list; DAT_00cd877c from the
    // camera's field of view: fogEnd / cos(fov * 0.5) - fogEnd
    // TODO FUN_00782f20(): the timed object transforms

    CMap::CreateRenderChunkPools();

    // The grass buffers, rebuilt only when something has asked for it. The reference creates
    // them here, next to the render chunk pools and after the frustum is set -- not at the top
    // of the frame, which is where frozen had them.
    DetailDoodad::CreateBuffers();

    // TODO FUN_007ae060(), FUN_007b2a80(): the detail doodad buffers

    memset(CWorldScene::s_rowStats, 0, sizeof(CWorldScene::s_rowStats));

    for (uint32_t i = 0; i < CWorldScene::HORIZON_COLUMNS; i++) {
        CWorldScene::s_horizonBuffer[i] = -1000000.0f;
    }

    MapOcclusion::ClearVolumes();

    // TODO FUN_007cc810() feeds the fixed horizon occluders (MapHorizonTable.hpp has the five of
    // them) through FUN_007927e0, CWorldScene's occluder segment add, which is not ported. The
    // occlusion VOLUMES are not built here -- they belong at the top of CWorldScene::Traverse.

    if (!CWorldScene::s_cameraDef) {
        CWorldScene::s_frameStamp++;
        CWorldScene::s_window.minX = 0.0f;
        CWorldScene::s_window.minY = 0.0f;
        CWorldScene::s_window.maxX = 1.0f;
        CWorldScene::s_window.maxY = 1.0f;
        CWorldScene::s_window.depth = 0.0f;
        CWorldScene::s_portalWindow.minX = 0.0f;
        CWorldScene::s_portalWindow.depth = 0.0f;
        CWorldScene::s_portalWindow.minY = 0.0f;
        CWorldScene::s_portalWindow.points = nullptr;
        CWorldScene::s_nearChunkDistance = -10000.0f;
        CWorldScene::s_portalWindow.maxX = 1.0f;
        CWorldScene::s_portalWindow.maxY = 1.0f;
        CWorldScene::s_portalWindow.pointCount = 0;
        CWorldScene::Traverse(&CWorldScene::s_portalWindow, 0);
    } else {
        CWorldScene::s_frameStamp++;

        // THE INDOOR TRAVERSAL. Read off FUN_0079a870's own instructions rather than the
        // decompilation, because two details there were wrong: the window reset sits INSIDE the
        // flagged-object block, and the depth that decides the branch is s_portalWindow's, not
        // s_window's.
        //
        // The camera's own building is s_cameraDef and its rooms are s_cameraGroupIndices; a second
        // building carrying def flag 0x400 is s_cameraDefFlagged with its own room list. Both are
        // filled by CWorldScene::UpdateCameraDef from one segment dropped straight down.
        if (CWorldScene::s_cameraDefFlagged && CWorldScene::s_cameraDefFlagged->m_mapObj
            && CWorldScene::s_cameraFlaggedGroupIndices.Count()) {
            CMapObj::EnterPortalWalk(CWorldScene::s_cameraDefFlagged,
                                     &CWorldScene::s_cameraFlaggedGroupIndices[0],
                                     CWorldScene::s_cameraFlaggedGroupIndices.Count());

            // Both windows reset INVERTED -- every minimum at +FLT_MAX and every maximum at
            // -FLT_MAX, with the depths at -1. That is the point of the first walk: it starts with
            // nothing visible and the portals widen it to only what they actually expose. The
            // maximum is -FLT_MAX, not FLT_MIN; FLT_MIN is a small positive number and would not
            // invert anything.
            const float INVERTED_MIN = 3.4028234663852886e+38f;
            const float INVERTED_MAX = -3.4028234663852886e+38f;

            CWorldScene::s_window.minX = INVERTED_MIN;
            CWorldScene::s_window.minY = INVERTED_MIN;
            CWorldScene::s_window.maxX = INVERTED_MAX;
            CWorldScene::s_window.maxY = INVERTED_MAX;
            CWorldScene::s_window.depth = -1.0f;
            CWorldScene::s_window.points = nullptr;
            CWorldScene::s_window.pointCount = 0;

            CWorldScene::s_portalWindow.minX = INVERTED_MIN;
            CWorldScene::s_portalWindow.minY = INVERTED_MIN;
            CWorldScene::s_portalWindow.maxX = INVERTED_MAX;
            CWorldScene::s_portalWindow.maxY = INVERTED_MAX;
            CWorldScene::s_portalWindow.depth = -1.0f;
            CWorldScene::s_portalWindow.points = nullptr;
            CWorldScene::s_portalWindow.pointCount = 0;

            // TODO two SetCount(0) calls on the window arrays at 0x00cdd0e8 and 0x00cdd0f8, which
            // FUN_00795d00 and FUN_00795d20 append to and which frozen does not carry. They
            // accumulate the windows the walk opens; without them the walk still runs, it just has
            // nowhere to record what it found for the second pass below.
        }

        if (CWorldScene::s_cameraDef->m_mapObj && CWorldScene::s_cameraGroupIndices.Count()) {
            CMapObj::EnterPortalWalk(CWorldScene::s_cameraDef,
                                     &CWorldScene::s_cameraGroupIndices[0],
                                     CWorldScene::s_cameraGroupIndices.Count());
        }

        if (CWorldScene::s_portalWindow.depth < 0.0f) {
            // Nothing the portals expose. DIVERGED, deliberately: the reference tears all 64
            // distance rows down here (FUN_00794250) so the frame draws nothing at all. That is
            // Storm list surgery and is not ported, and leaving it out on its own would draw
            // whatever the previous frame left in the rows. Falling back to the OUTDOOR traversal
            // instead keeps the world on screen -- it over-draws where the reference would draw
            // nothing, which is the safe direction to be wrong in while the teardown is missing.
            CWorldScene::s_window.minX = 0.0f;
            CWorldScene::s_window.minY = 0.0f;
            CWorldScene::s_window.maxX = 1.0f;
            CWorldScene::s_window.maxY = 1.0f;
            CWorldScene::s_window.depth = 0.0f;
            CWorldScene::s_portalWindow.minX = 0.0f;
            CWorldScene::s_portalWindow.minY = 0.0f;
            CWorldScene::s_portalWindow.maxX = 1.0f;
            CWorldScene::s_portalWindow.maxY = 1.0f;
            CWorldScene::s_portalWindow.depth = 0.0f;
            CWorldScene::s_portalWindow.points = nullptr;
            CWorldScene::s_portalWindow.pointCount = 0;
            CWorldScene::s_nearChunkDistance = -10000.0f;

            CWorldScene::Traverse(&CWorldScene::s_portalWindow, 0);
        } else {
            // The portals opened something: traverse in portal mode, starting one chunk beyond the
            // depth they reached.
            CWorldScene::s_nearChunkDistance = CWorldScene::s_portalWindow.depth + CHUNK_SIZE;

            CWorldScene::Traverse(&CWorldScene::s_portalWindow, 1);
        }

        // TODO FUN_00799f80(&{0, 0, 1, 1}): a second map-object-def pass the reference runs after
        // both arms, over a frustum stack indexed by its own depth counter, through
        // CWorldScene::VisitMapObjDefGroup. 467 bytes, and it needs FUN_00793270 and FUN_00799b70.
    }

    // TODO FUN_0079a260(), FUN_00793450(): the visible map objects' doodads and the entity callbacks
    // TODO FUN_007cecd0(): a list emptied here

    CImVector clearColor = { 0x00, 0x00, 0x00, 0xFF };

    if (!g_theGxDevicePtr->MasterEnable(GxMasterEnable_PolygonFill)) {
        clearColor.value = 0xFF000000;
    } else if (CWorldScene::s_window.depth < 0.0f) {
        // TODO the interior fog colour (light block +0xa0)
        clearColor.value = 0xFF000000;
    } else if (!CWorld::IsCameraUnderLiquid()) {
        // TODO with no skybox override (light block +0x1cc) and the sky flag DAT_00d38ad0 set,
        // the fog colour; otherwise transparent black
        clearColor.value = 0;
    } else {
        const C3Vector& fog = CWorld::GetFogColor();
        clearColor.b = CM2Lighting::FogColorByte(fog.z);
        clearColor.g = CM2Lighting::FogColorByte(fog.y);
        clearColor.r = CM2Lighting::FogColorByte(fog.x);
        clearColor.a = 0xFF;
    }

    GxSceneClear(0x3, clearColor);

    // TODO FUN_009a80c0(); FUN_007bb670(&cameraPos): the map shadow; the M2 scene's AdvanceTime
    // and Animate (CGWorldFrame::OnWorldRender still does them); FUN_006fda20(); FUN_007bb570()

    CShaderEffect::UpdateProjMatrix();
    CWorld::SetupFogRenderStates();

    // The map object shadow plane, where the reference builds it -- FUN_007bb670 is called from
    // CMap::Render at 0x0079abda, after the traversal and before the draws. Gated on the shadow
    // quality like every other part of that path, so it is inert until the quality is wired.
    if (ShadowMapGetQuality() > 0) {
        auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, __FILE__, __LINE__);

        if (player) {
            MapShadowSetupPlane(player->GetPosition());
        }
    }

    CWorldScene::RenderTerrain();

    CWorldScene::RenderMapObjs();

    // TODO the passes the reference runs between the map objects and the liquid, in its order:
    // FUN_00795f80 (the map-object groups carrying group flag 0x8, drawn through FUN_007abac0 in
    // their own viewport), FUN_007968d0, FUN_00796c10 twice, and two calls to FUN_007f31c0.
    // The procedural liquid textures' one-time upload, in the reference's own position: between
    // CWorldScene::DrawEntityShadows and BuildPendingMapObjSurfaces, at 0x0079acc9. Each of the
    // three latches after its first upload, so this costs three compares a frame thereafter.
    //
    // The TODO this replaces said TWO textures; there are three -- the river and ocean depth ramps
    // and the WMO water texture.
    Liquid::UpdateProceduralTextures();

    // The map objects' water, queued into the same two buckets the terrain's goes into. This is the
    // reference's own position for it, at 0x0079acce: after the ramp textures and before the first
    // bucket is drained, and OUTSIDE the liquid enable that gates the draw -- so the surfaces are
    // built and queued even on a frame that will not draw them, and the queue empties either way.
    Liquid::BuildPendingMapObjSurfaces();

    // The liquid. Bucket 0 is plain water and magma, bucket 1 procedural water; which one a
    // surface went into was decided by its settings back in Liquid::Add.
    if (CWorld::s_enables & CWorld::Enables::Enable_Liquid) {
        Liquid::Draw(cameraPos, 0);

        // Bucket 1 is NOT drained here. It is CWorldScene::DrawLiquidPass's, called from the
        // world frame's transparent block, which is where the reference drains it (FUN_00790a80 at
        // 0x004f9170 and 0x004f91b0). Frozen used to drain it here because that wrapper was
        // unported; it is ported now, so the divergence this comment used to record is closed.
    }

    // The buildings' props, culled against the frustum this pass has just established.
    CMap::CullMapObjDoodads();

    GxXformPop(GxXform_World);
    GxRsPop();

    // TODO FUN_006164b0(), and the decal pass behind CWorld enable 0x200000
    (void)dt;
}

// ref: FUN_007b5500
// The render chunks that drew recently: each ages by the frame time, one idle for two seconds
// gives its buffer slot back, and one without a slot leaves the list
void CMap::AgeRenderChunks() {
    float dt = CWorld::GetTickTimeSec();

    for (auto chunk = CMap::s_activeRenderChunkList.Head(); chunk; ) {
        auto next = CMap::s_activeRenderChunkList.Next(chunk);

        chunk->m_age += dt;

        if (2.0f < chunk->m_age) {
            chunk->ReleaseBufEntry();
        }

        if (!chunk->m_bufEntry) {
            chunk->m_link.Unlink();
        }

        chunk = next;
    }
}

// ----------------------------------------------------------------------------------------------
// Render chunk buffers

// ref: FUN_007ba340
// Once per client, before the chunk vertex table: the terrain shader state starts dirty at
// vertex format 1, the chunk batch is 768 indices over vertices 0..144, and the buffer pools
// are empty. The two zero rows the alpha unpackers read are static and already zero.
void CMap::InitializeRenderChunks() {
    CMap::s_terrainShadersDirty = 1;
    CMap::s_terrainVertexFormat = 1;
    CMap::s_chunkBatch.m_count = CMap::s_chunkIndexCount;
    CMap::s_chunkBatch.m_maxIndex = CMap::s_chunkVertexCount - 1;
    CMap::s_renderChunkPoolVertices = 0;
    CMap::s_renderChunkPoolIndices = 0;
    CMap::s_chunkBatch.m_primType = static_cast<EGxPrim>(6);
    CMap::s_chunkBatch.m_start = 0;
    CMap::s_chunkBatch.m_minIndex = 0;
    CMap::s_renderChunkVertexPool = nullptr;
    CMap::s_renderChunkIndexPool = nullptr;
    CMap::s_bufBlocks.SetCount(0);
    memset(CMapChunk::s_zeroAlphaRow, 0, 0x40);
    memset(CMapChunk::s_zeroShadowRow, 0, 0x40);
}

// ref: FUN_007b9340
// A buffer slot for a render chunk. A two-chunk batch takes a whole free block and uses its
// first entry (creating its double-size buffers on first use); a single chunk takes a free
// entry, or breaks a free block into two single entries, keeping one and freeing the other.
// Buffers already created are marked stale so FillBuffers refills them.
CMapChunkBufEntry* CMap::AllocBufEntry(uint32_t flags, CMapRenderChunk* renderChunk) {
    uint32_t stride = CMap::s_terrainVertexFormat == 2 ? sizeof(CMapChunkVertexColor) : sizeof(CMapChunkVertex);

    if (flags & 0x3) {
        auto block = CMap::s_freeBufBlockList.Head();

        if (!block) {
            return nullptr;
        }

        block->freeLink.Unlink();

        auto entry = &block->entries[0];
        entry->renderChunk = renderChunk;
        entry->flags = flags;

        if (!entry->vertexBuf) {
            uint32_t index = block->index;
            entry->vertexBuf = g_theGxDevicePtr->BufCreate(CMap::s_renderChunkVertexPool, stride, 0x122, index * stride * 0x91);
            entry->indexBuf = g_theGxDevicePtr->BufCreate(CMap::s_renderChunkIndexPool, 2, 0x600, index * 0x600);
            return entry;
        }

        entry->vertexBuf->unk1C = 0;
        entry->indexBuf->unk1C = 0;
        return entry;
    }

    auto entry = CMap::s_freeBufEntryList.Head();

    if (entry) {
        entry->link.Unlink();
        entry->renderChunk = renderChunk;
        entry->flags = flags;
        entry->vertexBuf->unk1C = 0;
        entry->indexBuf->unk1C = 0;
        return entry;
    }

    auto block = CMap::s_freeBufBlockList.Head();

    if (!block) {
        return nullptr;
    }

    block->freeLink.Unlink();

    if (block->entries[0].vertexBuf) {
        // TODO FUN_006c42b0: destroy the double-size buffers (frozen has no BufDestroy yet)
        block->entries[0].vertexBuf = nullptr;
        block->entries[0].indexBuf = nullptr;
    }

    CMapChunkBufEntry* last = nullptr;

    for (int32_t i = 0; i < 2; i++) {
        uint32_t index = block->index;
        auto e = &block->entries[i];
        e->vertexBuf = g_theGxDevicePtr->BufCreate(CMap::s_renderChunkVertexPool, stride, 0x91, (index + i) * stride * 0x91);
        e->indexBuf = g_theGxDevicePtr->BufCreate(CMap::s_renderChunkIndexPool, 2, 0x300, (index + i) * 0x600);
        e->flags = 0;
        e->renderChunk = nullptr;

        if (i == 0) {
            CMap::s_freeBufEntryList.LinkToTail(e);
        }

        last = e;
    }

    last->renderChunk = renderChunk;
    last->flags = flags;
    return last;
}

// ref: FUN_007b9560
// A block whose two single entries are both free again and that is not already waiting as a
// whole block drops its single-size buffers and rejoins the free block list
void CMap::RecycleBufBlocks() {
    for (uint32_t i = 0; i < CMap::s_bufBlocks.Count(); i++) {
        auto block = &CMap::s_bufBlocks[i];

        if (block->freeLink.IsLinked() || !block->entries[0].link.IsLinked() || !block->entries[1].link.IsLinked()) {
            continue;
        }

        for (int32_t n = 0; n < 2; n++) {
            auto e = &block->entries[n];

            if (e->vertexBuf) {
                // TODO FUN_006c42b0(e->vertexBuf): destroy unless it is a stream buffer
            }
            if (e->indexBuf) {
                // TODO FUN_006c42b0(e->indexBuf)
            }

            e->vertexBuf = nullptr;
            e->indexBuf = nullptr;
            e->flags = 0;
            e->link.Unlink();
        }

        CMap::s_freeBufBlockList.LinkToHead(block);
    }
}

// ref: FUN_007ba600
// When the terrain shader level changed: every render chunk gives up its slot, the old pools
// go, and new ones are made for every chunk the far clip can reach (one chunk per 33 yards,
// plus one, squared), carved into that many half-blocks.
void CMap::CreateRenderChunkPools() {
    if (!CMap::s_terrainShadersDirty) {
        return;
    }

    CMap::ReleaseAllBufEntries();
    CMap::DestroyRenderChunkPools();

    int32_t span = 1 - static_cast<int32_t>(CWorld::GetFarClip() * -0.029999999329447746f);
    int32_t chunks = span * span;

    CMap::s_renderChunkPoolVertices = chunks * 0x122;
    CMap::s_renderChunkPoolIndices = chunks * 0x600;

    uint32_t stride = CMap::s_terrainVertexFormat == 2 ? sizeof(CMapChunkVertexColor) : sizeof(CMapChunkVertex);

    CMap::s_renderChunkVertexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic, CMap::s_renderChunkPoolVertices * stride, GxPoolHintBit_Unk3, "CMapRenderChunk_vtx");
    CMap::s_renderChunkIndexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Dynamic, chunks * 0xc00, GxPoolHintBit_Unk3, "CMapRenderChunk_idx");

    CMap::s_bufBlocks.SetCount((chunks * 2 + 1) >> 1);

    for (uint32_t i = 0; i < CMap::s_bufBlocks.Count(); i++) {
        auto block = &CMap::s_bufBlocks[i];
        block->index = i * 2;
        block->entries[0].block = block;
        block->entries[1].block = block;
        CMap::s_freeBufBlockList.LinkToTail(block);
        CMap::s_bufBlockList.LinkToTail(block);
    }

    CMap::s_terrainShadersDirty = 0;
}

// ref: FUN_007ba5a0
// The reference destroys both pools through the device (vfunc +0xd4), which frozen's device
// does not expose yet
void CMap::DestroyRenderChunkPools() {
    CMap::FreeBufBlocks();

    CMap::s_renderChunkPoolVertices = 0;
    CMap::s_renderChunkPoolIndices = 0;
    CMap::s_terrainShadersDirty = 1;

    if (CMap::s_renderChunkVertexPool) {
        // TODO g_theGxDevicePtr->PoolDestroy(s_renderChunkVertexPool)
        CMap::s_renderChunkVertexPool = nullptr;
    }

    if (CMap::s_renderChunkIndexPool) {
        // TODO g_theGxDevicePtr->PoolDestroy(s_renderChunkIndexPool)
        CMap::s_renderChunkIndexPool = nullptr;
    }
}

// ref: FUN_007ba3d0
// Empties every buffer list, destroys every entry's buffers and drops the block array
void CMap::FreeBufBlocks() {
    for (auto entry = CMap::s_freeBufEntryList.Head(); entry; ) {
        auto next = CMap::s_freeBufEntryList.Next(entry);
        entry->link.Unlink();
        entry = next;
    }

    for (auto block = CMap::s_freeBufBlockList.Head(); block; ) {
        auto next = CMap::s_freeBufBlockList.Next(block);
        block->freeLink.Unlink();
        block->link.Unlink();
        block = next;
    }

    for (uint32_t i = 0; i < CMap::s_bufBlocks.Count(); i++) {
        auto block = &CMap::s_bufBlocks[i];

        for (int32_t n = 0; n < 2; n++) {
            auto e = &block->entries[n];

            if (e->vertexBuf) {
                // TODO FUN_006c42b0(e->vertexBuf)
                e->vertexBuf = nullptr;
            }
            if (e->indexBuf) {
                // TODO FUN_006c42b0(e->indexBuf)
                e->indexBuf = nullptr;
            }
        }
    }

    CMap::s_bufBlocks.SetCount(0);
}

// ref: FUN_0079e780
void CMap::ReleaseAllBufEntries() {
    for (auto chunk = CMap::s_chunkList.Head(); chunk; chunk = CMap::s_chunkList.Next(chunk)) {
        if (chunk->m_renderChunk) {
            chunk->m_renderChunk->ReleaseBufEntry();
        }
    }
}

// ref: FUN_007b7bd0
// The map's own light and fog on top of the scene lights selected for a chunk. The reference
// adds the day/night block's sun (a CM2Light at DAT_00ce04a8 + 0x58); frozen keeps the outdoor
// light as a direction and two colours, so it goes in as an ambient and a diffuse until that
// block is ported.
void CMap::SetupChunkLighting(CM2Lighting* lighting) {
    lighting->AddAmbient(CWorld::GetOutdoorAmbient());
    lighting->AddDiffuse(CWorld::GetOutdoorDiffuse(), CWorld::GetOutdoorDirection());
    lighting->SetFog(CWorld::GetFogColor(), CWorld::GetFogStart(), CWorld::GetFogEnd());
}

// ref: FUN_007b54a0
// The detail doodad batches (DAT_00adfc40): rebuilt after ten frames (FUN_007b0d40 / FUN_007b30d0),
// freed after twenty (FUN_007b3960)
void CMap::UpdateDetailDoodads() {
    // TODO
}

// ref: FUN_007b47f0
static int32_t CompareAreaDistance(const void* a, const void* b) {
    float da = *reinterpret_cast<const float*>(static_cast<const uint8_t*>(a) + 4);
    float db = *reinterpret_cast<const float*>(static_cast<const uint8_t*>(b) + 4);

    if (da < db) {
        return -1;
    }

    if (db < da) {
        return 1;
    }

    return 0;
}

struct AREADISTANCE {
    CMapArea* area;
    float distanceSq;
};

// How near a still-loading tile has to be for CMap::UpdateAreas to move its read up the queue.
// Both are squared: 33.333 yards, one chunk, in any direction at all (DAT_00a3ffc8), and 266.667
// yards, eight chunks, but only ahead of the camera (DAT_00a3ffcc).
static const float AREA_PRIORITY_NEAR_SQ = 1111.111083984375f;
static const float AREA_PRIORITY_FAR_SQ = 71111.109375f;

// ref: FUN_007b5950
// Streaming. Tiles outside the tile window go (unless their read is in progress); every tile
// the WDT lists inside it exists; all of them are sorted by distance to the target; each
// unloaded one starts its read, tiles covering the inner rectangle are waited for when nothing
// else is loading, and every loaded tile's chunks are refreshed against the window through the
// quadtree walk. The streaming-mode prioritisation (FUN_004b9950 / FUN_004ba3d0 / FUN_004b9970)
// and the tile-edge pass (FUN_007b4bc0) are not ported yet.
void CMap::UpdateAreas(int32_t update) {
    int32_t maxRow = CMap::s_chunkWindowMaxY >> 4;
    int32_t minCol = CMap::s_chunkWindowMinX >> 4;
    int32_t minRow = CMap::s_chunkWindowMinY >> 4;
    int32_t maxCol = CMap::s_chunkWindowMaxX >> 4;

    AREADISTANCE sorted[1023];
    uint32_t count = 0;

    CMap::ClearFrameChunkList();
    CMap::UpdateFrameLiquids();
    CMap::AgeRenderChunks();
    CMap::UpdateDetailDoodads();

    int32_t waitForTarget = 1;
    if (CMap::s_streamingMode || CMap::s_loading) {
        waitForTarget = 0;
    }

    C2Vector target = { CWorld::s_targetPos.x, CWorld::s_targetPos.y };

    for (auto link = CMap::s_areaLinkList.Head(); link; ) {
        auto area = static_cast<CMapArea*>(link->owner);
        auto next = CMap::s_areaLinkList.Next(link);

        if (area->m_areaX < minCol || maxCol < area->m_areaX || area->m_areaY < minRow || maxRow < area->m_areaY) {
            if (!area->m_asyncObject || !area->m_asyncObject->isCurrent) {
                CMap::FreeBaseObjLink(link);
                CMap::UnloadArea(area);
            }
        } else {
            sorted[count].distanceSq = CMap::AreaDistanceSq(area->m_bounds, target);
            sorted[count].area = area;
            count++;
        }

        link = next;
    }

    for (int32_t row = minRow; row <= maxRow; row++) {
        for (int32_t col = minCol; col <= maxCol; col++) {
            if ((CMap::s_areaInfo[row * 64 + col][0] & 0x1) && !CMap::s_areaGrid[row * 64 + col]) {
                auto area = CMap::CreateArea(col, row);
                sorted[count].area = area;
                sorted[count].distanceSq = CMap::AreaDistanceSq(area->m_bounds, target);
                count++;
            }
        }
    }

    if (count) {
        qsort(sorted, count, sizeof(AREADISTANCE), &CompareAreaDistance);
    }

    int32_t rect[4];

    // The furthest still-loading tile inside the outer threshold, which is where the prioritisation
    // pass below starts and walks back towards the camera. -1 when there is none.
    int32_t lastLoading = -1;

    for (uint32_t i = 0; i < count; i++) {
        auto area = sorted[i].area;

        if (!area->m_fileBuffer) {
            area->Load();
        }

        if (minCol <= area->m_areaX && area->m_areaX <= maxCol && minRow <= area->m_areaY && area->m_areaY <= maxRow) {
            int32_t x0 = area->m_areaX * 16;
            int32_t y0 = area->m_areaY * 16;
            int32_t x1 = x0 + 15;
            int32_t y1 = y0 + 15;

            if (x0 <= CMap::s_chunkInnerMaxX && y0 <= CMap::s_chunkInnerMaxY && CMap::s_chunkInnerMinX <= x1 && CMap::s_chunkInnerMinY <= y1 && waitForTarget && area->m_asyncObject) {
                AsyncFileReadWait(area->m_asyncObject);
            }

            if (!area->m_asyncObject) {
                rect[0] = area->m_chunkBaseY;
                rect[1] = area->m_chunkBaseX;
                rect[2] = area->m_chunkBaseY + 15;
                rect[3] = area->m_chunkBaseX + 15;
                CMap::UpdateAreaChunks(update, area, rect, 0);
            } else if (CMap::s_streamingMode && sorted[i].distanceSq < AREA_PRIORITY_FAR_SQ) {
                lastLoading = static_cast<int32_t>(i);
            }
        }
    }

    // Reorder the read queue so the tiles that matter arrive first. Walked BACKWARDS, furthest to
    // nearest, because each one is moved to the front of its priority band -- so the last moved
    // ends up first, and that is the nearest tile.
    //
    // A tile qualifies if it is within 33.33 yards (one chunk) whatever direction it is in, or
    // within 266.67 yards AND in the frustum. Both thresholds are squared, read out of the image at
    // 0x00a3ffc8 and 0x00a3ffcc.
    if (CMap::s_streamingMode) {
        AsyncFileReadLockQueue();

        for (int32_t i = lastLoading; i >= 0; i--) {
            auto area = sorted[i].area;

            if (!area->m_asyncObject) {
                continue;
            }

            bool wanted = sorted[i].distanceSq < AREA_PRIORITY_NEAR_SQ
                || (sorted[i].distanceSq < AREA_PRIORITY_FAR_SQ
                    && !CWorldScene::BoxOutsideFrustum(area->m_bounds));

            if (wanted) {
                AsyncFileReadLinkObject(area->m_asyncObject, 1);
            }
        }

        AsyncFileReadUnlockQueue();
    }

    if (update) {
        // TODO FUN_007b4bc0(): the tile-edge pass
    }
}

// ref: FUN_007b4df0
// Quadtree over a tile's chunk rectangle: a rectangle touching the window is split in four to
// depth two and then handed to CreateChunks; one outside it has its chunks destroyed.
void CMap::UpdateAreaChunks(int32_t update, CMapArea* area, const int32_t* rect, int32_t depth) {
    int32_t sub[4];

    if (rect[1] <= CMap::s_chunkWindowMaxX && rect[0] <= CMap::s_chunkWindowMaxY && CMap::s_chunkWindowMinX <= rect[3] && CMap::s_chunkWindowMinY <= rect[2]) {
        if (depth == 2) {
            area->CreateChunks(update, rect);
            return;
        }

        depth++;

        sub[0] = rect[0];
        sub[1] = rect[1];
        sub[2] = ((rect[2] - rect[0]) >> 1) + rect[0];
        sub[3] = ((rect[3] - rect[1]) >> 1) + rect[1];
        CMap::UpdateAreaChunks(update, area, sub, depth);

        sub[3] = rect[3];
        sub[1] = ((rect[3] - rect[1]) >> 1) + 1 + rect[1];
        CMap::UpdateAreaChunks(update, area, sub, depth);

        sub[2] = rect[2];
        sub[0] = ((rect[2] - rect[0]) >> 1) + 1 + rect[0];
        CMap::UpdateAreaChunks(update, area, sub, depth);

        sub[1] = rect[1];
        sub[3] = ((rect[3] - rect[1]) >> 1) + rect[1];
        CMap::UpdateAreaChunks(update, area, sub, depth);
        return;
    }

    area->DestroyChunks(rect);
}

// ref: FUN_007c1ff0
// Files a placed object under a WMO group: entities go on the group's entity list, doodad defs
// on its doodad list; any other kind gets the link on the owner's side only.
//
// BOTH ENDS WERE BACKWARDS here until 2026-09-27, and the comment stated the inverted rule as
// if it were the intent. The disassembly at 0x007c1ff0 is unambiguous: `testb $0x2, 0x7c(%esi)`
// then `je` to the LinkToHead call, so flag 0x2 CLEAR is the head case and SET is the tail one;
// and the doodad-def arm at 0x007c202a falls into that same LinkToHead rather than the tail.
// Its neighbour FUN_007c2040 (LinkEntityToChunks) uses the identical shape, which is what
// brought this to light.
//
// List ends are not cosmetic: these lists are walked in order by the per-chunk and per-group
// passes, so the wrong end reverses the order things are visited in.
void CMap::LinkToMapObjDefGroup(CMapBaseObj* owner, CMapObjDefGroup* group) {
    auto link = CMap::AllocBaseObjLink(owner);
    link->ref = group;

    if (owner->m_type & CMapBaseObj::Type_Entity) {
        if (static_cast<CMapEntity*>(owner)->m_flags7c & 0x2) {
            group->m_entityLinkList.LinkToTail(link);
        } else {
            group->m_entityLinkList.LinkToHead(link);
        }
    } else if (owner->m_type & CMapBaseObj::Type_DoodadDef) {
        group->m_doodadDefLinkList.LinkToHead(link);
    }
}

// A tile, but only once its file has finished loading: one still waiting on its async read counts
// as absent.
// ref: FUN_0079b440
CMapArea* CMap::GetLoadedArea(int32_t x, int32_t y) {
    auto area = CMap::s_areaGrid[y * 64 + x];

    if (area && area->m_asyncObject) {
        area = nullptr;
    }

    return area;
}

// Copy a BSP leaf's faces into a cache entry, deduplicating their vertices through a 1024-slot
// open-addressed table keyed by the group vertex index. A leaf with more than 300 faces, or more
// than 450 distinct vertices, is marked in status and left partly built. The reference reads the
// face refs from +0x8 of its second argument; the caller is not ported, so this takes them
// directly.
// ref: FUN_0079ae80
void CMap::BuildBspLeafCache(CMapBspLeafCache* leaf, const uint16_t* faceRefs, const CAaBspNode* node, const SMOPoly* polys, const C3Vector* vertices, const uint16_t* indices) {
    int16_t slotVertex[1024];
    uint16_t slotKey[1024];

    memset(static_cast<void*>(leaf), 0, sizeof(*leaf));
    leaf->node = node;
    leaf->status = 0;
    leaf->vertexCount = 0;
    leaf->faceCount = 0;
    memset(slotKey, 0xFF, sizeof(slotKey));

    if (node->nFaces > 300) {
        leaf->status = 1;
        return;
    }

    auto refs = &faceRefs[node->faceStart];

    for (int32_t i = 0; i < node->nFaces; i++) {
        uint16_t face = refs[i];
        leaf->faceSource[i] = face;

        auto corner = &indices[face * 3];

        for (int32_t k = 0; k < 3; k++) {
            uint16_t vertex = corner[k];
            uint32_t slot = vertex & 0x3FF;

            while (slotKey[slot] != vertex) {
                if (slotKey[slot] == 0xFFFF) {
                    if (leaf->vertexCount > 0x1C1) {
                        leaf->status = 2;
                        return;
                    }

                    leaf->vertices[leaf->vertexCount] = vertices[vertex];
                    leaf->vertexSource[leaf->vertexCount] = vertex;
                    slotVertex[slot] = leaf->vertexCount;
                    slotKey[slot] = vertex;
                    leaf->vertexCount++;

                    break;
                }

                slot = (slot + 1) & 0x3FF;
            }

            leaf->faceIndices[leaf->faceCount * 3 + k] = slotVertex[slot];
        }

        leaf->faceFlags[leaf->faceCount] = polys[face].flags & 0xFF7F;
        leaf->faceCount++;
    }
}

// Append the cells of a line that runs along its second coordinate, from line[1] to line[3] in
// either direction, as (line[1] + step, line[0]) pairs.
// ref: FUN_007a20e0
void CMap::AddCellSpanY(const int32_t* line) {
    int32_t y = line[1];
    auto cells = CMap::s_cellList.Ptr();

    if (line[3] < y) {
        do {
            cells[CMap::s_cellListCount++] = y;
            y--;
            cells[CMap::s_cellListCount++] = line[0];
        } while (line[3] <= y);

        return;
    }

    do {
        cells[CMap::s_cellListCount++] = y;
        y++;
        cells[CMap::s_cellListCount++] = line[0];
    } while (y <= line[3]);
}

// The same along the first coordinate, from line[0] to line[2], as (line[1], line[0] + step).
// ref: FUN_007a2180
void CMap::AddCellSpanX(const int32_t* line) {
    int32_t x = line[0];
    auto cells = CMap::s_cellList.Ptr();

    if (line[2] < x) {
        do {
            cells[CMap::s_cellListCount++] = line[1];
            cells[CMap::s_cellListCount++] = x;
            x--;
        } while (line[2] <= x);

        return;
    }

    do {
        cells[CMap::s_cellListCount++] = line[1];
        cells[CMap::s_cellListCount++] = x;
        x++;
    } while (x <= line[2]);
}

static const float AREA_HALF_EXTENT = 17066.666015625f;     // DAT_009e2acc
static const float AREA_EXTENT = 34133.33203125f;           // DAT_009e2ac8

// ref: FUN_007a0490
uint32_t CMap::GetChunkAreaID(const C3Vector& point) {
    int32_t col = static_cast<int32_t>(lrintf(-(point.y - AREA_HALF_EXTENT) * 0.03f - 0.5f));
    int32_t row = static_cast<int32_t>(lrintf(-(point.x - AREA_HALF_EXTENT) * 0.03f - 0.5f));

    CMapArea* area = CMap::s_areaGrid[((row >> 4) & 0x3F) * 64 + ((col >> 4) & 0x3F)];

    if (!area || area->m_asyncObject) {
        return 0;
    }

    CMapChunk* chunk = area->m_chunks[(row & 0xF) * 16 + (col & 0xF)];

    return chunk ? chunk->m_areaId : 0;
}

// ref: FUN_007a06a0
// MCSH holds one bit per sixty-fourth of a chunk (64 >> mampValue on a side), row by row.
bool CMap::IsTerrainShadowed(const C3Vector& point) {
    float fromY = -(point.y - AREA_HALF_EXTENT);
    float fromX = -(point.x - AREA_HALF_EXTENT);

    if (fromY <= 0.0f || fromX <= 0.0f || AREA_EXTENT <= fromY || AREA_EXTENT <= fromX) {
        return false;
    }

    int32_t col = static_cast<int32_t>(lrintf(fromY * 0.03f - 0.5f));
    int32_t row = static_cast<int32_t>(lrintf(fromX * 0.03f - 0.5f));

    CMapArea* area = CMap::GetLoadedArea((col >> 4) & 0x3F, (row >> 4) & 0x3F);

    if (!area) {
        return false;
    }

    CMapChunk* chunk = area->m_chunks[(row & 0xF) * 16 + (col & 0xF)];

    if (!chunk || !chunk->m_shadow) {
        return false;
    }

    uint8_t shift = area->m_header ? area->m_header->mampValue : 0;
    int32_t x = (static_cast<int32_t>(lrintf(fromY * 1.92f - 0.5f)) & 0x3F) >> shift;
    int32_t y = (static_cast<int32_t>(lrintf(fromX * 1.92f - 0.5f)) & 0x3F) >> shift;

    uint8_t bits = chunk->m_shadow[(8 >> shift) * y + (x >> 3)];

    return (bits & (1 << (x & 7))) != 0;
}

// ref: FUN_007a13e0
bool CMap::GetEntityMapObjGroup(CMapStaticEntity* entity, CMapObjDef** def, CMapObj** mapObj,
                                CMapObjDefGroup** defGroup, CMapObjGroup** group, int32_t skipFlagged) {
    for (auto link = entity->m_parentLinkList.Head(); link; link = entity->m_parentLinkList.Next(link)) {
        CMapBaseObj* ref = link->ref;

        if (!ref || !(ref->m_type & CMapBaseObj::Type_MapObjDefGroup)) {
            continue;
        }

        *defGroup = static_cast<CMapObjDefGroup*>(ref);

        auto parent = (*defGroup)->m_parentLinkList.Head();

        if (!parent) {
            continue;
        }

        *def = static_cast<CMapObjDef*>(parent->ref);

        if (skipFlagged && ((*def)->m_flags & 0x400)) {
            continue;
        }

        *mapObj = (*def)->m_mapObj;
        *group = *mapObj ? (*mapObj)->GetGroup((*defGroup)->m_groupIndex, 0) : nullptr;

        if (*group) {
            return true;
        }
    }

    return false;
}

// ref: FUN_007a09d0
bool CMap::GetMapObjLiquid(const C3Vector& point, uint32_t* liquidType, float* height) {
    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        if (def->m_flags & 0x100) {
            continue;
        }

        const CAaBox& box = def->m_bounds;

        if (!(box.b.x <= point.x && box.b.y <= point.y && box.b.z <= point.z
              && point.x <= box.t.x && point.y <= box.t.y && point.z <= box.t.z)) {
            continue;
        }

        if (!def->m_mapObj) {
            continue;
        }

        C3Vector local = point * def->m_inversePlacement;

        if (def->m_mapObj->PointInBounds(local) && def->m_mapObj->GetLiquidAt(0x2000, local, liquidType, height)) {
            C3Vector surface = { local.x, local.y, *height };
            *height = (surface * def->m_placement).z;

            return true;
        }
    }

    return false;
}

// ref: FUN_007a0b00
bool CMap::GetLiquidAt(const C3Vector& point, uint32_t* liquidType, float* height, int32_t* unused, int32_t flag) {
    *unused = 0;

    if (CMap::GetMapObjLiquid(point, liquidType, height)) {
        return true;
    }

    return CMap::GetTerrainLiquid(point, liquidType, height, flag);
}

// ref: FUN_007a1a30
void CMap::UpdateEntityGroupLiquid(CMapEntity* entity) {
    entity->m_flags7c &= ~0x60u;

    CMapObjDef* def = nullptr;
    CMapObj* mapObj = nullptr;
    CMapObjDefGroup* defGroup = nullptr;
    CMapObjGroup* group = nullptr;

    if (!CMap::GetEntityMapObjGroup(entity, &def, &mapObj, &defGroup, &group, 0)) {
        return;
    }

    C3Vector bottom = { entity->m_position.x, entity->m_position.y, entity->m_bounds.b.z };
    C3Vector local = bottom * def->m_inversePlacement;

    uint32_t liquidType = 0;
    float height = 0.0f;

    if (!group->GetLiquidAt(local, &liquidType, &height)) {
        return;
    }

    entity->m_flags7c |= 0x20;

    C3Vector surface = { local.x, local.y, height };
    entity->m_liquidHeight = (surface * def->m_placement).z;

    if (entity->m_bounds.t.z < entity->m_liquidHeight) {
        entity->m_flags7c &= ~0x40u;
    } else {
        entity->m_flags7c |= 0x40;
    }

    entity->m_fieldBC = static_cast<uint16_t>(liquidType);
}

// ref: FUN_007a1bc0
// The ambient an outdoor entity eases toward is the outdoor light's (the day/night block's sun at
// DAT_00ce04a8 + 0x58, whose ambient colour is +0x88); it is dimmed to half in the terrain's baked
// shadow below shadow quality 2, and raised to 2.5 otherwise.
void CMap::UpdateEntity(CMapEntity* entity) {
    entity->m_flags7c &= 0xFFFFFC96;
    entity->m_flags = (entity->m_flags & ~0x6u) | 0x1;

    RelinkEntity(entity);

    if (!(entity->m_flags7c & 0x1)) {
        uint32_t liquidType = 0;
        int32_t unused = 0;

        if (CMap::GetLiquidAt(entity->m_position, &liquidType, &entity->m_liquidHeight, &unused, 1)) {
            entity->m_flags7c |= 0x20;

            if (entity->m_liquidHeight <= entity->m_bounds.t.z) {
                entity->m_flags7c |= 0x40;
            } else {
                entity->m_flags7c &= ~0x40u;
            }

            entity->m_fieldBC = static_cast<uint16_t>(liquidType);
        }
    } else {
        CMap::UpdateEntityGroupLiquid(entity);
    }

    uint32_t areaID = 0;

    if (!(entity->m_flags7c & 0x2000) && (entity->m_flags7c & 0x40)
        && entity->m_position.z < entity->m_liquidHeight + 0.01f
        && CWorld::GetEntityAreaID(entity, &areaID)) {
        const LiquidTypeRec* liquid = CWorld::GetAreaLiquidType(areaID, entity->m_fieldBC);

        if (liquid && ((liquid->m_flags & 0x4) || entity->m_position.z < entity->m_liquidHeight)) {
            entity->m_flags7c = (entity->m_flags7c & ~0x300u) | ((static_cast<uint32_t>(liquid->m_flags) << 8) & 0x300);
        }
    }

    if (!(entity->m_flags & CMapBaseObj::Flag_Interior)) {
        auto channel = [](float value) -> uint8_t {
            if (!(0.0f < value)) {
                return 0;
            }

            return static_cast<uint8_t>(lrintf(value < 1.0f ? value * 255.0f + 0.5f : 255.0f));
        };

        const C3Vector& ambient = CWorld::GetOutdoorAmbient();
        entity->m_ambientTarget.b = channel(ambient.z);
        entity->m_ambientTarget.g = channel(ambient.y);
        entity->m_ambientTarget.r = channel(ambient.x);
        entity->m_ambientTarget.a = 0xFF;

        if (ShadowMapGetQuality() < 2 && !(entity->m_flags & 0x200) && CMap::IsTerrainShadowed(entity->m_position)) {
            entity->m_flags7c |= 0x8;
            entity->m_dirLightScaleTarget = 0.5f;
            return;
        }

        entity->m_dirLightScaleTarget = 2.5f;
        return;
    }

    if (!(entity->m_flags7c & 0x1000)) {
        entity->m_dirLightScaleTarget = 1.0f;
        return;
    }

    entity->m_dirLightScaleTarget = static_cast<float>(entity->m_interiorDirColor.a) * 0.003921568859368563f * (2.5f - 1.0f) + 1.0f;
}

uint64_t CMap::s_segmentHitGUID;
int32_t CMap::s_queryStamp;
int32_t CMap::s_useSse;
CMapBspNodeCache* CMap::s_bspNodeCache;
uint32_t CMap::s_bspNodeCacheVictim;

static const float SEGMENT_CELL_SIZE = 4.166666507720947f;   // an eighth of a chunk
static const float SEGMENT_CELL_SCALE = 0.23999999463558197f; // DAT_00a3fda0

static void PushCell(int32_t first, int32_t second) {
    auto cells = CMap::s_cellList.Ptr();
    cells[CMap::s_cellListCount++] = first;
    cells[CMap::s_cellListCount++] = second;
}

// ref: FUN_007a23e0
void CMap::AddCellLineFirst(const float* from, const float* to, const int32_t* cells) {
    float slope = (to[1] - from[1]) / (to[0] - from[0]);
    float fromA = from[0];
    float fromB = from[1];

    int32_t first = cells[1];
    int32_t second = cells[0];
    int32_t step;
    int32_t start;

    if (second < cells[2]) {
        step = 1;
        start = second + 1;
    } else {
        step = -1;
        start = second;
    }

    float boundary = static_cast<float>(start) * SEGMENT_CELL_SIZE;

    PushCell(cells[1], cells[0]);

    int32_t last = first;

    if (second != cells[2] + step) {
        do {
            if (0x7FB < CMap::s_cellListCount) {
                return;
            }

            first = static_cast<int32_t>(lrintf((boundary - (-(fromA * slope) + fromB)) * (1.0f / slope) * SEGMENT_CELL_SCALE - 0.5f));

            if (first != last) {
                PushCell(first, second);
            }

            boundary += static_cast<float>(step) * SEGMENT_CELL_SIZE;
            second += step;
            PushCell(first, second);
            last = first;
        } while (second != cells[2] + step);
    }

    if (first != cells[3]) {
        PushCell(cells[3], cells[2]);
    }
}

// ref: FUN_007a2230
void CMap::AddCellLineSecond(const float* from, const float* to, const int32_t* cells) {
    int32_t second = cells[0];
    float slope = (to[1] - from[1]) / (to[0] - from[0]);
    float fromA = from[0];
    float fromB = from[1];

    int32_t first = cells[1];
    int32_t step;
    int32_t start;

    if (first < cells[3]) {
        step = 1;
        start = first + 1;
    } else {
        step = -1;
        start = first;
    }

    float boundary = static_cast<float>(start) * SEGMENT_CELL_SIZE;

    PushCell(first, second);

    int32_t last = second;

    if (first != cells[3] + step) {
        do {
            if (0x7FB < CMap::s_cellListCount) {
                return;
            }

            second = static_cast<int32_t>(lrintf((boundary * slope + -(fromA * slope) + fromB) * SEGMENT_CELL_SCALE - 0.5f));

            if (second != last) {
                PushCell(first, second);
            }

            first += step;
            boundary += static_cast<float>(step) * SEGMENT_CELL_SIZE;
            PushCell(first, second);
            last = second;
        } while (first != cells[3] + step);
    }

    if (second == cells[2]) {
        return;
    }

    PushCell(cells[3], cells[2]);
}

// Put a model on the scene's ray list, with the kind of test the query wants.
static void LinkRayModel(CM2Model* model, uint32_t kind, void* owner) {
    if (!model->m_rayPrev) {
        CM2Scene* scene = model->m_scene;
        model->m_rayPrev = &scene->m_rayModelList;
        model->m_rayNext = scene->m_rayModelList;
        scene->m_rayModelList = model;

        if (model->m_rayNext) {
            model->m_rayNext->m_rayPrev = &model->m_rayNext;
        }
    }

    model->m_rayQueryType = kind;
    model->m_rayOwner = owner;
    model->m_rayKey = 0;
}

// ref: FUN_007a2760
// A placed doodad joins the ray list as kind 3 (its collision mesh) for queries of bit 0 or bit
// 20, kind 2 for bit 3, and kind 1 or 0 (the 24th bit) for bits 1-2; an object (a non-zero GUID
// at +0xb8) only for bits 20-22, as kind 3 or 0.
void CMap::AddRayModels(CMapBaseObjRefList* list, uint32_t queryFlags) {
    for (auto link = list->Head(); link; link = list->Next(link)) {
        auto entity = static_cast<CMapStaticEntity*>(link->owner);

        if ((queryFlags & 0x1000000) && entity->m_visible) {
            continue;
        }

        if ((entity->m_flags & 0x100) || !(entity->m_flags & 0x80) || entity->m_queryStamp == CMap::s_queryStamp) {
            continue;
        }

        CM2Model* model = entity->m_model;

        if (!model) {
            continue;
        }

        uint64_t guid = (entity->m_type & CMapBaseObj::Type_Entity) ? static_cast<CMapEntity*>(entity)->m_param64 : 0;

        if (guid == 0) {
            if (queryFlags & 0x1) {
                if (model->m_loaded) {
                    LinkRayModel(model, 3, entity);
                }
            } else if (queryFlags & 0xE) {
                if (!(queryFlags & 0x8)) {
                    if (model->m_loaded) {
                        LinkRayModel(model, (queryFlags >> 0x18) & 1, entity);
                    }
                } else if (model->m_loaded) {
                    LinkRayModel(model, 2, entity);
                }
            }
        } else if (queryFlags & 0x100000) {
            if (model->m_loaded) {
                LinkRayModel(model, 3, entity);
            }
        } else if ((queryFlags & 0x600000) && model->m_loaded) {
            LinkRayModel(model, 0, entity);
        }

        entity->m_queryStamp = CMap::s_queryStamp;
    }
}

// ref: FUN_007a2960
// Entities that carry an object (state bit 2 of +0x7c) are asked through the object query
// callback (DAT_00ce04b0) which model stands for them. NOT INSTALLED in frozen: nothing registers
// that callback yet, and with it null the reference does nothing here either.
void CMap::AddRayObjects(CMapBaseObjRefList* list, uint32_t queryFlags) {
    (void)list;
    (void)queryFlags;
}

// ref: FUN_007a3570
// Each listed cell: its chunk's models and objects joined to the ray list once per chunk, its
// terrain triangles (query bit 8), and the liquid over it (bits 16-17), keeping the nearest.
bool CMap::QuerySegmentCells(const C3Vector& start, const C3Vector& end, float* t, uint32_t queryFlags, CMapChunk** outChunk) {
    auto cells = CMap::s_cellList.Ptr();
    int32_t remaining = CMap::s_cellListCount;

    uint32_t lastFirst = cells[0] & 0x2000;
    uint32_t lastSecond = cells[1] & 0x2000;
    uint32_t modelQuery = queryFlags & 0x40F0000F;

    CMapChunk* chunk = nullptr;
    CMapChunk* hitChunk = nullptr;

    CM2Scene* scene = CWorld::GetM2Scene();

    if (modelQuery && scene) {
        scene->BeginRayQuery();
    }

    C3Vector dir = { end.x - start.x, end.y - start.y, end.z - start.z };
    float invLength = 1.0f / sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    dir = { dir.x * invLength, dir.y * invLength, dir.z * invLength };

    float best = *t;
    C3Vector local = start;
    int32_t index = 0;

    while (remaining != 0) {
        uint32_t first = static_cast<uint32_t>(cells[index]);
        uint32_t second = static_cast<uint32_t>(cells[index + 1]);
        remaining -= 2;
        index += 2;

        if (0x2000 < first || 0x2000 < second) {
            break;
        }

        uint32_t firstChunk = first & 0x1FF8;

        if (firstChunk != lastSecond || (second & 0x1FF8) != lastFirst) {
            CMapArea* area = CMap::s_areaGrid[((second >> 7) & 0x3F) * 64 + ((first >> 7) & 0x3F)];

            if (!area || area->m_asyncObject) {
                break;
            }

            chunk = area->m_chunks[((second >> 3) & 0xF) * 16 + ((first >> 3) & 0xF)];

            if (!chunk) {
                break;
            }

            lastFirst = second & 0x1FF8;
            local = { start.x - chunk->m_position.x, start.y - chunk->m_position.y, start.z - chunk->m_position.z };

            if (modelQuery) {
                CMap::AddRayModels(&chunk->m_entityLinkList, queryFlags);
            }

            if (queryFlags & 0x40F00000) {
                CMap::AddRayObjects(&chunk->m_mapObjDefLinkList, queryFlags);
            }

            lastSecond = firstChunk;
        }

        uint32_t cellA = first & 7;
        uint32_t cellB = second & 7;

        C3Ray ray;
        ray.origin = local;
        ray.dir = dir;

        if (queryFlags & 0x100) {
            float hitT = 3.4028235e+38f;

            if (chunk->IntersectCell(cellA, cellB, ray, &hitT)) {
                float fraction = hitT * invLength;

                if (fraction < best && 0.0f < fraction) {
                    hitChunk = chunk;
                    best = fraction;
                }
            }
        }

        if (queryFlags & 0x30000) {
            bool typedOnly = (queryFlags & 0x10000) && !(queryFlags & 0x20000);

            for (auto liquid = chunk->m_liquidList.Head(); liquid; liquid = chunk->m_liquidList.Next(liquid)) {
                if (typedOnly) {
                    auto type = g_liquidTypeDB.GetRecord(static_cast<int32_t>(liquid->m_liquidType));

                    if (!type || !(type->m_flags & 0x4)) {
                        continue;
                    }
                }

                if (!liquid->CoversTile(cellA, cellB)) {
                    continue;
                }

                int32_t stride = static_cast<int32_t>(liquid->m_tileEndY - liquid->m_tileY) + 1;
                int32_t base = (static_cast<int32_t>(cellB) - static_cast<int32_t>(liquid->m_tileX)) * stride
                    - static_cast<int32_t>(liquid->m_tileY) + static_cast<int32_t>(cellA);
                const int32_t offsets[2][3] = { { 0, stride + 1, stride }, { 0, 1, stride + 1 } };

                for (auto& tri : offsets) {
                    int32_t indices[3] = { base + tri[0], base + tri[1], base + tri[2] };
                    float hitT = 0.0f;

                    if (IntersectRayTriangle(ray, liquid->m_vertices, indices, &hitT, nullptr, 0.01f)) {
                        float fraction = hitT * invLength;

                        if (fraction < best && 0.0f < fraction) {
                            best = fraction;
                            hitChunk = chunk;
                        }
                    }
                }
            }
        }
    }

    if (modelQuery && scene) {
        if (!(queryFlags & 0x16000AE)) {
            scene->RayQueryCollision(start, end, &best);
        } else {
            C3Vector viewStart = start * scene->m_view;
            C3Vector viewEnd = end * scene->m_view;
            scene->RayQuery(viewStart, viewEnd, &best, 0);
        }
    }

    if (*t <= best) {
        return false;
    }

    *t = best;

    if (outChunk) {
        *outChunk = hitChunk;
    }

    return true;
}

// ref: FUN_007a39f0
bool CMap::QuerySegmentTerrain(const C3Vector& start, const C3Vector& end, float* t, uint32_t queryFlags, CMapChunk** chunk) {
    float fromA0 = -(start.y - AREA_HALF_EXTENT);
    float fromB0 = -(start.x - AREA_HALF_EXTENT);
    float fromA1 = -(end.y - AREA_HALF_EXTENT);
    float fromB1 = -(end.x - AREA_HALF_EXTENT);

    float spanA = fromA1 - fromA0;
    float spanB = fromB1 - fromB0;

    int32_t cells[4] = {
        static_cast<int32_t>(lrintf(fromB0 * SEGMENT_CELL_SCALE - 0.5f)),
        static_cast<int32_t>(lrintf(fromA0 * SEGMENT_CELL_SCALE - 0.5f)),
        static_cast<int32_t>(lrintf(fromB1 * SEGMENT_CELL_SCALE - 0.5f)),
        static_cast<int32_t>(lrintf(fromA1 * SEGMENT_CELL_SCALE - 0.5f))
    };

    CMap::s_cellListCount = 0;

    if (std::fabs(spanA) < 2.384185791015625e-07f || cells[1] == cells[3]) {
        CMap::AddCellSpanX(cells);
    } else if (std::fabs(spanB) < 2.384185791015625e-07f || cells[0] == cells[2]) {
        CMap::AddCellSpanY(cells);
    } else {
        const float from[2] = { fromA0, fromB0 };
        const float to[2] = { fromA1, fromB1 };

        if (std::fabs(spanA) <= std::fabs(spanB)) {
            CMap::AddCellLineFirst(from, to, cells);
        } else {
            CMap::AddCellLineSecond(from, to, cells);
        }
    }

    return CMap::QuerySegmentCells(start, end, t, queryFlags, chunk);
}

// ref: FUN_007a30d0
// Every building the segment's box reaches, each loaded group it crosses listed with how far along
// the segment it starts, nearest first; then each group's faces, its models onto the ray list,
// and last the models themselves.
bool CMap::QuerySegmentObjects(const C3Vector& start, const C3Vector& end, uint32_t queryFlags, uint32_t defSkipFlags,
                               float* t, uint16_t* face, CMapObj** outMapObj, CMapObjDef** outDef,
                               CMapObjDefGroup** outDefGroup) {
    struct Entry {
        CMapObjDef* def;
        CMapObjDefGroup* defGroup;
        float distance;
    };

    static Entry s_entries[499];
    uint32_t count = 0;

    uint32_t modelQuery = queryFlags & 0x40F0000F;
    CM2Scene* scene = CWorld::GetM2Scene();

    if (modelQuery && scene) {
        scene->BeginRayQuery();
    }

    float length = sqrtf((end.z - start.z) * (end.z - start.z) + (end.y - start.y) * (end.y - start.y)
        + (end.x - start.x) * (end.x - start.x));

    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        CMapObj* mapObj = def->m_mapObj;

        if ((def->m_flags & defSkipFlags) || !mapObj || !mapObj->m_rootLoaded) {
            continue;
        }

        if (!SegmentIntersectsBox(def->m_bounds, start, end)) {
            continue;
        }

        for (uint32_t j = 0; j < def->m_defGroups.Count(); j++) {
            CMapObjDefGroup* defGroup = def->m_defGroups[j];

            if (!defGroup || !mapObj->IsGroupLoaded(j) || !SegmentIntersectsBox(defGroup->m_bounds, start, end)) {
                continue;
            }

            if (count >= 499) {
                break;
            }

            C3Vector local = start * def->m_inversePlacement;
            Entry& entry = s_entries[count++];
            entry.def = def;
            entry.defGroup = defGroup;

            if (mapObj->PointInGroupBox(local, j)) {
                entry.distance = 0.0f;
                continue;
            }

            CAaBox bounds;
            mapObj->GroupBounds(j, &bounds);

            C3Vector nearest = {
                std::min(std::max(local.x, bounds.b.x), bounds.t.x),
                std::min(std::max(local.y, bounds.b.y), bounds.t.y),
                std::min(std::max(local.z, bounds.b.z), bounds.t.z)
            };

            entry.distance = sqrtf((local.y - nearest.y) * (local.y - nearest.y) + (local.z - nearest.z) * (local.z - nearest.z)
                + (local.x - nearest.x) * (local.x - nearest.x)) / length;
        }
    }

    std::sort(s_entries, s_entries + count, [](const Entry& a, const Entry& b) {
        return a.distance < b.distance;
    });

    uint32_t skipFlags = CMapObj::QuerySkipFlags(queryFlags);
    float best = *t;
    bool hit = false;
    uint32_t hitFace = 0xFFFFFFFF;

    for (uint32_t i = 0; i < count; i++) {
        Entry& entry = s_entries[i];
        CMapObj* mapObj = entry.def->m_mapObj;

        C3Vector localStart = start * entry.def->m_inversePlacement;
        C3Vector localEnd = end * entry.def->m_inversePlacement;

        if (entry.distance <= best
            && mapObj->SegmentVsGroupBounds(localStart, localEnd, entry.defGroup->m_groupIndex)
            && mapObj->QuerySegmentGroup(localStart, localEnd, &best, queryFlags, skipFlags, entry.defGroup->m_groupIndex, &hitFace)) {
            hit = true;

            if (outMapObj) {
                *outMapObj = mapObj;
            }

            if (outDef) {
                *outDef = entry.def;
            }

            if (outDefGroup) {
                *outDefGroup = entry.defGroup;
            }
        }

        if (modelQuery) {
            CMap::AddRayModels(&entry.defGroup->m_doodadDefLinkList, queryFlags);
        }

        if (queryFlags & 0x40F00000) {
            CMap::AddRayObjects(&entry.defGroup->m_entityLinkList, queryFlags);
        }
    }

    if (hit) {
        *t = best;

        if (face) {
            *face = static_cast<uint16_t>(hitFace);
        }
    }

    if (modelQuery && scene) {
        C3Vector viewStart = start * scene->m_view;
        C3Vector viewEnd = end * scene->m_view;
        float fraction = *t;

        void* owner = scene->RayQuery(viewStart, viewEnd, &fraction, 0);

        if (fraction < *t) {
            auto entity = static_cast<CMapBaseObj*>(owner);

            if (entity && (entity->m_type & CMapBaseObj::Type_Entity)) {
                uint64_t guid = static_cast<CMapEntity*>(entity)->m_param64;

                if (guid) {
                    CMap::s_segmentHitGUID = guid;
                }
            }

            *t = fraction;

            if (face) {
                *face = 0xFFFF;
            }

            return true;
        }
    }

    if (!hit && face) {
        *face = 0xFFFF;
    }

    return hit;
}

// ref: FUN_007a3b70
// Buildings first (query bits 0-7, 16-17, 20-23, 30), then terrain (0-3, 8, 16-17, 20-23, 30); `t`
// carries the nearest hit from one to the other, and the point is placed along the segment at it.
//
// NOT PORTED: filling `result` (FUN_007a2c60, the hit's map object, group, face and floor
// details). Every frozen caller passes null today; one that needs it will find this note.
bool CMap::QuerySegment(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* t,
                        uint32_t queryFlags, void* result) {
    (void)result;

    CMap::s_queryStamp++;
    bool found = false;

    CM2Scene* scene = CWorld::GetM2Scene();

    if (queryFlags & 0x40F300FF) {
        if (scene) {
            scene->m_rayHitModel = nullptr;
        }

        CMap::s_segmentHitGUID = 0;

        uint16_t face = 0;
        CMapObj* mapObj = nullptr;
        CMapObjDef* def = nullptr;
        CMapObjDefGroup* defGroup = nullptr;

        if (CMap::QuerySegmentObjects(start, end, queryFlags, 0x100, t, &face, &mapObj, &def, &defGroup)) {
            found = true;
        }
    }

    if (queryFlags & 0x40F3010F) {
        if (scene) {
            scene->m_rayHitModel = nullptr;
        }

        if (CMap::QuerySegmentTerrain(start, end, t, queryFlags, nullptr)) {
            CMap::s_segmentHitGUID = 0;
            found = true;
        }
    }

    if (!found) {
        return false;
    }

    if (hit) {
        float fraction = *t;
        hit->x = start.x + (end.x - start.x) * fraction;
        hit->y = (end.y - start.y) * fraction + start.y;
        hit->z = fraction * (end.z - start.z) + start.z;
    }

    return true;
}

// ref: FUN_007a3d50
void CMap::QueryFrustumLiquid(CMapChunk* chunk, const CWFrustum& frustum, const CiRect& cells, CChunkLiquid* liquid, CFacetList& list) {
    (void)chunk;

    CMapObjGroup::s_hitFlags = 0;
    CMapObjGroup::s_hitRecordCount = 0;
    CMapObjGroup::s_hitFacePoolCount = 0;
    CMapObjGroup::s_hitIndexPoolCount = 0;
    CMapObjGroup::s_hitPlacementCount = 0;

    // The owner the reference threads through is the address of a local byte; the liquid
    // collector never reads it.
    uint8_t owner;

    if (liquid->QueryHull(&owner, frustum, cells)) {
        CWorld::AddHitFacets(list, 0, 0);
    }
}

// ref: FUN_007a5330
bool CMap::QueryFrustumCell(int32_t col, int32_t row, const CiRect& cells, const CWFrustum& frustum, CFacetList& list, uint32_t flags) {
    uint32_t before = list.facets.Count();

    CMapArea* area = CMap::s_areaGrid[((row >> 4) & 0x3f) * 64 + ((col >> 4) & 0x3f)];

    if (!area || area->m_asyncObject) {
        return false;
    }

    CMapChunk* chunk = area->m_chunks[(row & 0xf) * 16 + (col & 0xf)];

    if (!chunk) {
        return false;
    }

    CiRect local;
    local.minY = std::max(cells.minY - row * 8, 0);
    local.minX = std::max(cells.minX - col * 8, 0);
    local.maxY = std::min(cells.maxY - row * 8, 7);
    local.maxX = std::min(cells.maxX - col * 8, 7);

    CWFrustum moved = frustum;
    moved.Translate({ -chunk->m_position.x, -chunk->m_position.y, -chunk->m_position.z });

    if (flags & 0x100) {
        chunk->GatherFacets(local, moved, list);
    }

    if (flags & 0x30000) {
        bool waterOnly = (flags & 0x10000) && !(flags & 0x20000);

        for (auto liquid = chunk->m_liquidList.Head(); liquid; liquid = chunk->m_liquidList.Next(liquid)) {
            if (waterOnly) {
                auto rec = g_liquidTypeDB.GetRecord(static_cast<int32_t>(liquid->m_liquidType));

                // Frozen-only null check: the reference reads the row's flags unguarded.
                if (!rec || !(rec->m_flags & 0x4)) {
                    continue;
                }
            }

            CMap::QueryFrustumLiquid(chunk, moved, local, liquid, list);
        }
    }

    if (flags & 0xf) {
        CAaBox box;
        BoundsFromPoints(box, frustum.corners, 8);

        for (auto link = chunk->m_entityLinkList.Head(); link; link = chunk->m_entityLinkList.Next(link)) {
            auto entity = static_cast<CMapStaticEntity*>(link->owner);

            if ((entity->m_flags & 0x100) || !(entity->m_flags & 0x80) || entity->m_queryStamp == CMap::s_queryStamp) {
                continue;
            }

            if (!entity->m_model || !box.Intersects(entity->m_collisionBounds)) {
                continue;
            }

            // The reference reads the placement at +0xd8, which both entity kinds share. Frozen
            // keeps it on CMapDoodadDef; a unit's CMapEntity never carries flag 0x80 here
            // (CMap::UpdateEntity sets only bit 0), so no other kind reaches this point.
            if (!(entity->m_type & CMapBaseObj::Type_DoodadDef)) {
                continue;
            }

            entity->m_model->GetCollisionTriangles(box, static_cast<CMapDoodadDef*>(entity)->m_placement, list.facets);
            entity->m_queryStamp = CMap::s_queryStamp;
        }
    }

    return before != list.facets.Count();
}

// ref: FUN_007a4ee0
bool CMap::QueryFrustumObjects(const CWFrustum& frustum, CFacetList& list, uint32_t flags, uint32_t* hitFlags) {
    bool hit = false;

    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        if (def->m_flags & 0x100) {
            continue;
        }

        if (!AaBoxVsPlanes6(frustum.planes, def->m_bounds)) {
            continue;
        }

        C3Vector corners[8] = {};

        for (int32_t i = 0; i < 8; i++) {
            corners[i] = frustum.corners[i] * def->m_inversePlacement;
        }

        CWFrustum local(corners);

        if (!def->m_mapObj) {
            continue;
        }

        CMapObjGroup::s_hitFlags = 0;
        CMapObjGroup::s_hitRecordCount = 0;
        CMapObjGroup::s_hitFacePoolCount = 0;
        CMapObjGroup::s_hitIndexPoolCount = 0;
        CMapObjGroup::s_hitPlacementCount = 0;

        // The reference passes the address of a local byte as the placement; CMapObjGroup's
        // RecordHits ignores it and places every record by the def (object + 0x70). Frozen's
        // RecordHits stores what it is given, so it is given that matrix.
        hit |= def->m_mapObj->QueryHullGroups(local, flags, &def->m_placement, def);

        CWorld::AddHitFacets(list, 0, 0);

        if (hitFlags) {
            *hitFlags |= CMapObjGroup::s_hitFlags;
        }
    }

    return hit;
}

// ref: FUN_007a5dd0
bool CMap::QueryFrustumFacets(const CWFrustum& frustum, CFacetList& list, uint32_t flags, uint32_t* hitFlags) {
    CMap::s_queryStamp++;

    list.facets.SetCount(0);

    CAaBox bounds;
    BoundsFromPoints(bounds, frustum.corners, 8);

    // Map cells, rows from x and columns from y, both counted down from the map's far corner.
    CiRect cells;
    cells.maxX = static_cast<int32_t>(std::nearbyint(-(bounds.b.y - MAP_HALF_EXTENT) * CELLS_PER_YARD - 0.5f));
    cells.maxY = static_cast<int32_t>(std::nearbyint(-(bounds.b.x - MAP_HALF_EXTENT) * CELLS_PER_YARD - 0.5f));
    cells.minX = static_cast<int32_t>(std::nearbyint(-(bounds.t.y - MAP_HALF_EXTENT) * CELLS_PER_YARD - 0.5f));
    cells.minY = static_cast<int32_t>(std::nearbyint(-(bounds.t.x - MAP_HALF_EXTENT) * CELLS_PER_YARD - 0.5f));

    for (int32_t row = cells.minY >> 3; row <= cells.maxY >> 3; row++) {
        for (int32_t col = cells.minX >> 3; col <= cells.maxX >> 3; col++) {
            CMap::QueryFrustumCell(col, row, cells, frustum, list, flags);
        }
    }

    CMap::QueryFrustumObjects(frustum, list, flags, hitFlags);

    return list.facets.Count() != 0;
}

// ref: FUN_0079b160
CMapBspNodeCache::CMapBspNodeCache() {
    this->Clear();
}

// ref: FUN_0079b1c0
void CMapBspNodeCache::Clear() {
    memset(static_cast<void*>(this->keys), 0, sizeof(this->keys));
    memset(static_cast<void*>(this->entries), 0, sizeof(this->entries));
}

uint32_t CMapBspNodeCache::Bucket(const CAaBspNode* node) {
    return (node->nFaces ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node) >> 5)) & 0x7f;
}

// ref: FUN_0079b1f0
CMapBspLeafCache* CMapBspNodeCache::Lookup(const uint16_t* faceRefs, const CAaBspNode* node, const SMOPoly* polys, const C3Vector* vertices, const uint16_t* indices) {
    uint32_t bucket = CMapBspNodeCache::Bucket(node);
    int32_t hit = -1;
    int32_t empty = -1;

    for (int32_t way = 0; way < 8; way++) {
        const CAaBspNode* key = this->keys[bucket * 8 + way];

        if (key == node) {
            hit = way;
            break;
        }

        if (!key) {
            empty = way;
            break;
        }
    }

    CMapBspLeafCache* leaf;

    if (hit < 0) {
        if (empty < 0) {
            CMap::s_bspNodeCacheVictim++;
            empty = static_cast<int32_t>(CMap::s_bspNodeCacheVictim & 7);
        }

        uint32_t slot = bucket * 8 + empty;
        this->keys[slot] = node;
        leaf = &this->entries[slot];
        CMap::BuildBspLeafCache(leaf, faceRefs, node, polys, vertices, indices);
    } else {
        leaf = &this->entries[bucket * 8 + hit];
    }

    return leaf->status == 0 ? leaf : nullptr;
}

// ref: FUN_0079ae10
void CMapBspNodeCache::Evict(const CAaBspNode* node) {
    uint32_t bucket = CMapBspNodeCache::Bucket(node);

    for (int32_t way = 0; way < 8; way++) {
        if (this->keys[bucket * 8 + way] == node) {
            this->keys[bucket * 8 + way] = nullptr;
            memset(static_cast<void*>(&this->entries[bucket * 8 + way]), 0, sizeof(CMapBspLeafCache));
            return;
        }
    }
}

// ref: FUN_0079b0d0
void CMap::EvictBspLeaves(const CAaBspNode* nodes, uint32_t count) {
    if (!CMap::s_bspNodeCache) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (nodes[i].flags & CAaBspNode::Flag_Leaf) {
            CMap::s_bspNodeCache->Evict(&nodes[i]);
        }
    }
}
