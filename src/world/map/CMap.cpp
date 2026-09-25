#include "world/map/CMap.hpp"
#include "world/map/CChunkLiquid.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapAreaLow.hpp"
#include "world/map/CMapAreaMed.hpp"
#include "world/map/CMapCacheLight.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapDoodadDef.hpp"
#include "world/map/CMapEntity.hpp"
#include "world/map/CMapLight.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapRenderChunk.hpp"
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
#include "util/CStatus.hpp"
#include "world/CWorldScene.hpp"
#include "util/SFile.hpp"
#include "world/CWorld.hpp"
#include <cstdlib>
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
STORM_EXPLICIT_LIST(CMapLight, m_lameAssLink) CMap::s_lightList;
STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) CMap::s_renderChunkFreeList;

uint8_t CMap::s_chunkVerticesWorldSpace;
int32_t CMap::s_terrainVertexFormat;
uint8_t CMap::s_terrainSpecular;

CMapArea* CMap::s_areaGrid[64 * 64];
STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) CMap::s_areaLinkList;

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

CGxShader* CMap::s_terrainVertexShaders[0x80];
CGxShader* CMap::s_terrain2PixelShaders[0x60];
CGxShader* CMap::s_terrain2PcfPixelShaders[0x20];
CGxShader* CMap::s_terrainPixelShaders[3];
CGxShader* CMap::s_terrainEnvPixelShader[1];
CGxShader* CMap::s_terrain1PixelShaders[0x20];
CGxShader* CMap::s_terrain1wPixelShaders[8];
CGxShader* CMap::s_terrainShadowMapPixelShader[1];
CGxPool* CMap::s_lowDetailIndexPool;
CGxBuf* CMap::s_lowDetailIndexBuf;
int32_t CMap::s_terrainShadersDirty;

STORM_EXPLICIT_LIST(CMapChunk, m_frameLink) CMap::s_frameChunkList;

uint8_t CMap::s_terrainShaders;
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

    // TODO FUN_007cc310(s_mapPath, s_mapName): the WDL
    CMap::LoadWdt();
    // TODO FUN_007bd540(): the .tex cache
    // TODO FUN_007f2790(mapID)

    CMap::Update(0);

    if (waitAll) {
        // TODO FUN_004bae10(): AsyncFileReadWaitAll
    }

    // TODO the load progress callback (DAT_00cdfff4)(1.0f, DAT_00cdfff0)

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

    // TODO DAT_00ce0498 = terrainShaders ? s_chunkVerticesWorldSpace : 0
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

    switch (shadowLevel) {
    case 0:
        return CMap::s_terrain1PixelShaders[specular + (layers + (twoChunk + flag80 * 2) * 4) * 2];

    case 1:
        break;

    case 2:
    case 3:
        return CMap::s_terrain2PixelShaders[specular - 8 + layers * 2 + (shadowLevel * 4 + (twoChunk + flag80 * 2) * 0xc) * 2];

    default:
        return nullptr;
    }

    if (GxCaps().m_shaderTargets[GxSh_Pixel] != GxShPS_ps_2_0 && GxCaps().m_shaderTargets[GxSh_Pixel] != GxShPS_arbfp1) {
        return CMap::s_terrain2PixelShaders[specular + (layers + (twoChunk + flag80 * 2) * 0xc) * 2];
    }

    return CMap::s_terrain2PcfPixelShaders[specular + (layers + (twoChunk + flag80 * 2) * 4) * 2];
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

// ref: FUN_0079e7c0
// The static init the reference runs once: the chunk tables, the module inits, the grids, the
// terrain shaders, the low-detail index pool, then the object heaps. The module inits
// (FUN_007afee0, FUN_007cb990, FUN_007b2760, FUN_007a03c0, FUN_0079e3c0, FUN_0079e4f0), the
// liquid vertex buffer list (FUN_007d58b0) and the final capability flag (FUN_0086b9a0) are not
// ported yet.
void CMap::MapMemInitialize() {
    CMapChunk::Initialize();

    // TODO FUN_007afee0, FUN_007cb990, FUN_007b2760, FUN_007a03c0; the two 0x2c-byte records at
    // DAT_00d253d0 / DAT_00d253a4

    for (int32_t i = 0; i < 64 * 64; i++) {
        CMap::s_areaGrid[i] = nullptr;
        CMap::s_areaInfo[i][0] = 0;
    }

    // TODO the 0x800-entry growable array at DAT_00cf4928 and the map state flags
    // (DAT_00ce04c8, DAT_00ce04c4, DAT_00ce04a4 = -2, DAT_00adfbc4 = -1, DAT_00cf08f4 = 0,
    // DAT_00cf08f0 = 0, DAT_00ce04ac = 0), FUN_0079e3c0, FUN_0079e4f0

    for (int32_t i = 0; i < 0x80; i++) {
        CMap::s_terrainVertexShaders[i] = nullptr;
    }
    for (int32_t i = 0; i < 3; i++) {
        CMap::s_terrainPixelShaders[i] = nullptr;
    }
    CMap::s_terrainEnvPixelShader[0] = nullptr;
    for (int32_t i = 0; i < 0x20; i++) {
        CMap::s_terrain1PixelShaders[i] = nullptr;
    }
    for (int32_t i = 0; i < 8; i++) {
        CMap::s_terrain1wPixelShaders[i] = nullptr;
    }
    CMap::s_terrainShadowMapPixelShader[0] = nullptr;

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

    // TODO FUN_0086b9a0 -> DAT_00cf08f8 when bit 2 is set
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
    if (def->m_link94.IsLinked()) {
        def->m_link94.Unlink();
        def->m_link9c.Unlink();
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

    uint32_t memHandle = def->m_memHandle;
    def->~CMapObjDef();
    ObjectFree(*CMap::s_mapObjDefHeap, memHandle);
}

// ref: FUN_0079e6a0
void CMap::UnlinkMapObjDef(CMapObjDef* def) {
    if (def->m_link28.IsLinked()) {
        def->m_link28.Unlink();
        def->m_link30.Unlink();
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
    // TODO FUN_007ad020(): WMO material shader selection
    CMap::RecycleBufBlocks();
    CMap::UpdateAreas(update);
    // TODO FUN_007b6110(update): WMO def update
    // TODO FUN_007b5630(): entity update
    // TODO FUN_007b5590(update): entity placement

    if (CMap::s_loading) {
        if (CMap::s_streamingMode == 0) {
            // TODO four rounds of: AsyncFileReadWaitAll (FUN_004bae10), the progress callback,
            // UpdateAreas, FUN_007b6110, FUN_007ad020, FUN_007b5630
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
void CMap::UpdateFrameLiquids() {
    // TODO
}

// ref: FUN_0079a870
// The map's frame: the visibility traversal from the camera, the scene clear, then the passes.
// Ported so far: the render chunk pools, the outdoor traversal, the clear and the terrain pass.
// Everything else the reference does here is listed in place as it comes; the map objects,
// liquids, sky and the rest still draw from the stand-in and CGWorldFrame around this call.
void CMap::Render(const C3Vector& cameraPos, float dt) {
    if (!CWorldScene::s_cameraGroup) {
        // TODO FUN_00792bd0(): the map object defs into their distance rows
    }

    // TODO FUN_00790920(): the camera's liquid and height above the ground

    GxRsPush();
    GxXformPush(GxXform_World);

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

    // TODO FUN_007ae060(), FUN_007b2a80(): the detail doodad buffers

    memset(CWorldScene::s_rowStats, 0, sizeof(CWorldScene::s_rowStats));

    for (uint32_t i = 0; i < CWorldScene::HORIZON_COLUMNS; i++) {
        CWorldScene::s_horizonBuffer[i] = -1000000.0f;
    }

    // TODO FUN_007cd910(), FUN_007cc810(): the low-detail terrain

    if (!CWorldScene::s_cameraGroup) {
        CWorldScene::s_frameStamp++;
        CWorldScene::s_window.minX = 0.0f;
        CWorldScene::s_window.minY = 0.0f;
        CWorldScene::s_window.maxX = 1.0f;
        CWorldScene::s_window.maxY = 1.0f;
        CWorldScene::s_window.depth = 0.0f;
        CWorldScene::s_portalWindow.minX = 0.0f;
        CWorldScene::s_portalWindow.depth = 0.0f;
        CWorldScene::s_portalWindow.minY = 0.0f;
        CWorldScene::s_portalWindow.unknown14 = 0.0f;
        CWorldScene::s_nearChunkDistance = -10000.0f;
        CWorldScene::s_portalWindow.maxX = 1.0f;
        CWorldScene::s_portalWindow.maxY = 1.0f;
        CWorldScene::s_portalWindow.unknown18 = 0.0f;
        CWorldScene::Traverse(&CWorldScene::s_portalWindow, 0);
    } else {
        CWorldScene::s_frameStamp++;
        // TODO the portal walk from the camera's group (FUN_007b3b20 x2, FUN_00794190 x2), then
        // Traverse(&s_portalWindow, 1) or FUN_00794250(), and FUN_00799f80(&window)
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
    CWorldScene::RenderTerrain();

    // TODO FUN_007964a0(): the map objects; FUN_00795f80(); the liquids, sky and the passes after
    // them (see the reference body)

    GxXformPop(GxXform_World);
    GxRsPop();

    // TODO FUN_006164b0(), and the decal pass behind CWorld enable 0x200000
    (void)dt;
    (void)cameraPos;
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
            } else if (CMap::s_streamingMode) {
                // TODO remember the nearest still-loading tile for the prioritisation below
            }
        }
    }

    if (CMap::s_streamingMode) {
        // TODO FUN_004b9950 / FUN_004ba3d0 / FUN_004b9970: bump the reads of near tiles
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
// Files a placed object under a WMO group: entities go on the group's entity list (at the head
// when the entity carries flag 0x2), doodad defs on its doodad list; any other kind gets the link
// on the owner's side only.
void CMap::LinkToMapObjDefGroup(CMapBaseObj* owner, CMapObjDefGroup* group) {
    auto link = CMap::AllocBaseObjLink(owner);
    link->ref = group;

    if (owner->m_type & CMapBaseObj::Type_Entity) {
        if (static_cast<CMapEntity*>(owner)->m_flags7c & 0x2) {
            group->m_entityLinkList.LinkToHead(link);
        } else {
            group->m_entityLinkList.LinkToTail(link);
        }
    } else if (owner->m_type & CMapBaseObj::Type_DoodadDef) {
        group->m_doodadDefLinkList.LinkToTail(link);
    }
}
