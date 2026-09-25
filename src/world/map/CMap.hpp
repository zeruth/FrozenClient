#ifndef WORLD_MAP_C_MAP_HPP
#define WORLD_MAP_C_MAP_HPP

#include "world/map/CChunkLiquid.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapAreaLow.hpp"
#include "world/map/CMapAreaMed.hpp"
#include "world/map/CMapBaseObj.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapDoodadDef.hpp"
#include "world/map/CMapEntity.hpp"
#include "world/map/CMapLight.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapRenderChunk.hpp"
#include "gx/Texture.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CAsyncObject;
class CMapObj;
class CMapObjGroup;
class SFile;

class CMap {
    public:
        // Static variables: the object heaps CMap::MapMemInitialize creates, in the reference's
        // order (DAT_00d253fc .. DAT_00d25430)
        static uint32_t* s_lightHeap;
        static uint32_t* s_cacheLightHeap;
        static uint32_t* s_mapObjGroupHeap;
        static uint32_t* s_mapObjHeap;
        static uint32_t* s_baseObjLinkHeap;
        static uint32_t* s_areaHeap;
        static uint32_t* s_areaMedHeap;
        static uint32_t* s_areaLowHeap;
        static uint32_t* s_chunkHeap;
        static uint32_t* s_doodadDefHeap;
        static uint32_t* s_entityHeap;
        static uint32_t* s_mapObjDefGroupHeap;
        static uint32_t* s_mapObjDefHeap;
        static uint32_t* s_chunkLiquidHeap;

        // Every allocated object of a kind, linked by the allocator (reference list globals noted)
        static STORM_EXPLICIT_LIST(CMapArea, m_lameAssLink) s_areaList;                   // 0x00aeed8c
        static STORM_EXPLICIT_LIST(CMapChunk, m_lameAssLink) s_chunkList;                 // 0x00aeeda4
        static STORM_EXPLICIT_LIST(CChunkLiquid, m_link) s_chunkLiquidList;               // 0x00aeedb0
        static STORM_EXPLICIT_LIST(CMapObjDefGroup, m_lameAssLink) s_mapObjDefGroupList;  // 0x00aeedbc
        static STORM_EXPLICIT_LIST(CMapBaseObj, m_lameAssLink) s_entityList;              // 0x00aeedc8
        static STORM_EXPLICIT_LIST(CMapLight, m_lameAssLink) s_lightList;                 // 0x00aeedd4
        // Render chunks are not pooled through ObjectAlloc: freed ones wait here for reuse
        static STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) s_renderChunkFreeList;        // 0x00aeed74

        // Chunk vertex mode, decided at map load (DAT_00ce049f): non-zero keeps every chunk's
        // vertices in world space in one shared buffer; zero keeps them chunk-local and folds
        // the origin into a per-chunk matrix.
        static uint8_t s_chunkVerticesWorldSpace;
        // Terrain vertex format (DAT_00d1d06c, set with the terrain shader level): 1 drops the
        // baked MCCV colour from the vertex.
        static int32_t s_terrainVertexFormat;
        // Terrain layers load their "_s.blp" specular variant (DAT_00ce049d, set at map load
        // from the specular CVar bit and the shader check)
        static uint8_t s_terrainSpecular;

        // The loaded tiles, [row * 64 + col] (DAT_00ce48d0), and the links whose owners they are
        // (DAT_00adfbec); a tile lives in both from CreateArea until UnloadArea
        static CMapArea* s_areaGrid[64 * 64];
        static STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) s_areaLinkList;

        // The window of map chunk coordinates kept loaded around the camera, as CWorld::Update
        // maintains it (DAT_00cd77d8 minRow, DAT_00cd77dc minCol, DAT_00cd77e0 maxRow,
        // DAT_00cd77e4 maxCol)
        static int32_t s_chunkWindowMinY;
        static int32_t s_chunkWindowMinX;
        static int32_t s_chunkWindowMaxY;
        static int32_t s_chunkWindowMaxX;

        static int32_t s_mapID;
        static char s_mapName[];
        static char s_mapPath[];
        static char s_wdtFilename[];

        // Static functions
        static void Initialize();
        static void Load(const char* mapName, int32_t mapID);
        static void MapMemInitialize();

        // Map memory: one Alloc/Free pair per pooled kind. Alloc takes a slot from the kind's
        // heap, constructs it, records the mem handle and (for most kinds) links it into the
        // kind's list; Free unlinks, destroys and returns the slot.
        static void* MapMemAlloc(uint32_t bytes);
        static void MapMemFree(void* ptr);
        static CMapObj* AllocMapObj();
        static void FreeMapObj(CMapObj* mapObj);
        static CMapObjGroup* AllocMapObjGroup();
        static void FreeMapObjGroup(CMapObjGroup* group);
        static CMapArea* AllocArea();
        static void FreeArea(CMapArea* area);
        static void FreeAreaMed(CMapAreaMed* area);
        static CMapAreaLow* AllocAreaLow();
        static void FreeAreaLow(CMapAreaLow* area);
        static CMapChunk* AllocChunk();
        static void FreeChunk(CMapChunk* chunk);
        static CMapDoodadDef* AllocDoodadDef();
        static void FreeDoodadDef(CMapDoodadDef* def);
        static void UnlinkDoodadDef(CMapDoodadDef* def);
        static CMapEntity* AllocEntity(int32_t linkToHead);
        static void FreeEntity(CMapEntity* entity);
        static CMapLight* AllocLight();
        static void FreeLight(CMapLight* light);
        static CMapObjDefGroup* AllocMapObjDefGroup();
        static void FreeMapObjDefGroup(CMapObjDefGroup* group);
        static CMapObjDef* AllocMapObjDef();
        static void FreeMapObjDef(CMapObjDef* def);
        static void UnlinkMapObjDef(CMapObjDef* def);
        static CChunkLiquid* AllocChunkLiquid();
        static void FreeChunkLiquid(CChunkLiquid* liquid);
        static CMapRenderChunk* AllocRenderChunk();
        static void FreeRenderChunk(CMapRenderChunk* chunk);
        static CMapBaseObjLink* AllocBaseObjLink(CMapBaseObj* owner);
        static void FreeBaseObjLink(CMapBaseObjLink* link);
        static void LinkToMapObjDefGroup(CMapBaseObj* owner, CMapObjDefGroup* group);

        // Tiles and chunks
        static CMapArea* CreateArea(int32_t x, int32_t y);
        static void UnloadArea(CMapArea* area);
        static void DestroyChunk(CMapChunk* chunk);
        static float AreaDistanceSq(const CAaBox& box, const C2Vector& point);
        static int32_t SafeOpen(const char* path, SFile** file);
        static void AsyncLoadCleanup(CAsyncObject* object);
        static HTEXTURE LoadTexture(const char* name);

    private:
        static void FreeAreaLowObject(uint32_t* heap, CMapAreaLow* area);
};

#endif
