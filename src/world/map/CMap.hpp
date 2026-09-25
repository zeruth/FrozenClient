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
#include <storm/Array.hpp>
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CAsyncObject;
class CGxBuf;
class CGxPool;
class CGxShader;
class CM2Lighting;
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

        // Chunks touched this update, emptied at the start of the next (DAT_00adfc1c); a chunk's
        // m_frameLink is its place in it
        static STORM_EXPLICIT_LIST(CMapChunk, m_frameLink) s_frameChunkList;

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
        // The inner rectangle round the target, two chunks inside the window (DAT_00cd77c8
        // minRow, DAT_00cd77cc minCol, DAT_00cd77d0 maxRow, DAT_00cd77d4 maxCol): tiles that
        // cover it are waited for synchronously
        static int32_t s_chunkInnerMinY;
        static int32_t s_chunkInnerMinX;
        static int32_t s_chunkInnerMaxY;
        static int32_t s_chunkInnerMaxX;

        // The WDT: MVER (DAT_00ce04cc), MPHD (DAT_00cf08d0) and MAIN (DAT_00ce88d0, one entry per
        // tile, bit 0 = the tile exists)
        static uint32_t s_wdtVersion;
        static uint32_t s_wdtHeader[8];
        static uint32_t s_areaInfo[64 * 64][2];

        // Set while a map is loading behind a loading screen (DAT_00adfbc8); streaming waits for
        // the tiles round the target only when clear
        static int32_t s_loading;
        static int32_t s_streamingMode;       // DAT_00ce0494: SFile streaming mode or trial

        // The terrain shaders MapMemInitialize loads (docs/ref/parity-map-memory.md)
        static CGxShader* s_terrainVertexShaders[0x80];   // DAT_00ce0008: Terrain, 128 permutations
        static CGxShader* s_terrainPixelShaders[3];       // DAT_00ce0488: Terrain0, 3 permutations
        static CGxShader* s_terrainEnvPixelShader[1];     // DAT_00ce0004: Terrain0_env
        static CGxShader* s_terrain1PixelShaders[0x20];   // DAT_00ce0408: Terrain1, up to 32
        static CGxShader* s_terrain1wPixelShaders[8];     // DAT_00ce0428: Terrain1w / Terrain1w_1..4
        static CGxShader* s_terrainShadowMapPixelShader[1]; // DAT_00ce0000: TerrainSM
        // The shadow-mapped terrain pixel shaders (DAT_00ce0208 / DAT_00ce0388: Terrain2, Terrain3
        // and their _pcf variants), loaded by the shadow map system (FUN_0079e7c0's caller), which
        // is not ported: every entry is null, so the shadow levels fall through to no shader
        static CGxShader* s_terrain2PixelShaders[0x60];
        static CGxShader* s_terrain2PcfPixelShaders[0x20];
        static CGxPool* s_lowDetailIndexPool;             // DAT_00cdfffc
        static CGxBuf* s_lowDetailIndexBuf;               // DAT_00cdfff8
        static int32_t s_terrainShadersDirty;             // DAT_00d1d058
        // The terrain shaders loaded and valid (DAT_00ce049e, from LoadSettings)
        static uint8_t s_terrainShaders;

        // Render chunk buffers: two pools sized for every chunk the far clip can reach
        // (CreateRenderChunkPools), carved into blocks of two slots. Free single slots wait in
        // s_freeBufEntryList, blocks free for a two-chunk batch in s_freeBufBlockList; every
        // block in use is on s_bufBlockList. Render chunks that drew recently sit on
        // s_activeRenderChunkList and age out of their slot after two seconds.
        static TSGrowableArray<CMapChunkBufBlock> s_bufBlocks;                        // DAT_00d252f4/f8
        static STORM_EXPLICIT_LIST(CMapChunkBufEntry, link) s_freeBufEntryList;         // 0x00aeec88
        static STORM_EXPLICIT_LIST(CMapChunkBufBlock, freeLink) s_freeBufBlockList;     // 0x00aeec70
        static STORM_EXPLICIT_LIST(CMapChunkBufBlock, link) s_bufBlockList;             // 0x00aeec7c
        static STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) s_activeRenderChunkList;    // 0x00adfc28
        static CGxPool* s_renderChunkVertexPool;          // DAT_00d1d068
        static CGxPool* s_renderChunkIndexPool;           // DAT_00d1d064
        static uint32_t s_renderChunkPoolVertices;        // DAT_00d1d060: chunks x 0x122
        static uint32_t s_renderChunkPoolIndices;         // DAT_00d1d05c: chunks x 0x600
        static uint16_t s_chunkVertexCount;               // DAT_00af14a0: 145
        static uint16_t s_chunkIndexCount;                // DAT_00af14c0: 768

        static int32_t s_mapID;
        static char s_mapName[];
        static char s_mapPath[];
        static char s_wdtFilename[];

        // Static functions
        static void Initialize();
        static void Load(const char* mapName, int32_t mapID);
        static void LoadWdt();
        static void LoadSettings();
        static void SetTerrainShaderLevel(int32_t level);
        static CGxShader* GetTerrain0PixelShader(int32_t a1, int32_t a2, int32_t env);
        static CGxShader* GetTerrainVertexShader(int32_t lights, int32_t layers, int32_t specular, int32_t color, int32_t chunkSpecular, int32_t shadow);
        static CGxShader* GetTerrainPixelShader(int32_t twoChunk, int32_t layers, int32_t shadowLevel, int32_t specular, int32_t color);
        static void MapMemInitialize();
        static void Update(int32_t update);
        static void Render(const C3Vector& cameraPos, float dt);
        static void UpdateAreas(int32_t update);
        static void UpdateAreaChunks(int32_t update, CMapArea* area, const int32_t* rect, int32_t depth);
        static void UnloadAll();
        static void ClearFrameChunkList();
        static void UpdateFrameLiquids();
        static void AgeRenderChunks();
        static void UpdateDetailDoodads();

        // Render chunk buffers
        static CMapChunkBufEntry* AllocBufEntry(uint32_t flags, CMapRenderChunk* renderChunk);
        static void RecycleBufBlocks();
        static void CreateRenderChunkPools();
        static void DestroyRenderChunkPools();
        static void FreeBufBlocks();
        static void ReleaseAllBufEntries();
        static void SetupChunkLighting(CM2Lighting* lighting);

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
        static void MapMemInitializeHeaps();
};

#endif
