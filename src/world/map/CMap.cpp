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
#include "async/CAsyncObject.hpp"
#include "gx/Texture.hpp"
#include "gx/texture/CGxTex.hpp"
#include "util/CStatus.hpp"
#include "util/SFile.hpp"
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

void CMap::Load(const char* mapName, int32_t mapID) {
    // TODO

    auto nameOfs = SStrCopy(CMap::s_mapPath, "World\\Maps\\");
    SStrCopy(&CMap::s_mapPath[nameOfs], mapName);

    SStrCopy(CMap::s_mapName, mapName);

    SStrPrintf(CMap::s_wdtFilename, sizeof(CMap::s_wdtFilename), "%s\\%s.wdt", CMap::s_mapPath, CMap::s_mapName);

    // TODO

    CMap::s_mapID = mapID;

    // TODO
}

void CMap::MapMemInitialize() {
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
