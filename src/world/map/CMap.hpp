#ifndef WORLD_MAP_C_MAP_HPP
#define WORLD_MAP_C_MAP_HPP

#include "world/map/CChunkLiquid.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapAreaLow.hpp"
#include "world/map/MapLowDetail.hpp"
#include "world/map/CMapAreaMed.hpp"
#include "world/map/CMapBaseObj.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapDoodadDef.hpp"
#include "world/map/CMapStaticEntity.hpp"
#include "world/map/CMapEntity.hpp"
#include "world/map/CMapLight.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjGroup.hpp"
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
class CWFrustum;
struct CFacetList;
struct CAaBspNode;
struct SMOPoly;

// One entry of CMap's BSP leaf cache (reference: 0x2460 bytes, 1024 of them after a 0x1000-byte
// key table, looked up by FUN_0079b1f0): a leaf's faces with their vertices deduplicated into a
// compact local array.
struct CMapBspLeafCache {
    const CAaBspNode* node;
    uint8_t status;                 // 0 built, 1 more than 300 faces, 2 more than 450 vertices
    uint16_t vertexCount;
    C3Vector vertices[450];
    uint16_t vertexSource[450];     // each local vertex's group vertex index
    uint16_t faceCount;
    uint16_t faceIndices[300 * 3];  // local vertex indices, three per face
    uint16_t faceFlags[300];        // the face's MOPY flags without F_COLLIDE_HIT
    uint16_t faceSource[300];       // each face's group face index
};

// The leaf cache the bspcache CVar turns on (reference: one 0x919000-byte block at DAT_00cdd7a0,
// Map.cpp): 128 buckets of eight ways, keyed by the leaf node's address, each way holding a
// compacted copy of that leaf. A query that finds its leaf here tests the copy's faces with a
// per-vertex outcode prefilter instead of walking the group's own arrays.
struct CMapBspNodeCache {
    const CAaBspNode* keys[1024];
    CMapBspLeafCache entries[1024];

    // ref: FUN_0079b160
    CMapBspNodeCache();
    // Forget every leaf. ref: FUN_0079b1c0
    void Clear();
    // The cached copy of `node`, building it on a miss (round-robin eviction when the bucket is
    // full); null when the leaf is too big to cache. ref: FUN_0079b1f0
    CMapBspLeafCache* Lookup(const uint16_t* faceRefs, const CAaBspNode* node, const SMOPoly* polys, const C3Vector* vertices, const uint16_t* indices);
    // Drop `node` if it is cached. ref: FUN_0079ae10
    void Evict(const CAaBspNode* node);

    static uint32_t Bucket(const CAaBspNode* node);
};

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
        // Static entities waiting on their models. One leaves as soon as its model is in
        // and it has been placed, through the same link the distance rows use.
        static STORM_EXPLICIT_LIST(CMapStaticEntity, m_rowLink) s_pendingEntityList;      // 0x00adfc10
        static STORM_EXPLICIT_LIST(CMapLight, m_lameAssLink) s_lightList;                 // 0x00aeedd4
        // Render chunks are not pooled through ObjectAlloc: freed ones wait here for reuse
        static STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) s_renderChunkFreeList;        // 0x00aeed74
        // The WMO roots with a file read in flight (0x00adfc4c); a root leaves it in its
        // postload callback
        static STORM_EXPLICIT_LIST(CMapObj, m_link) s_mapObjLoadList;
        // The WMO groups with a file read in flight (0x00adfc58)
        static STORM_EXPLICIT_LIST(CMapObjGroup, m_link) s_mapObjGroupLoadList;

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
        // The map is one global WMO with no terrain (DAT_00cf08f4), and the links to its
        // placement (DAT_00adfc04).
        static int32_t s_globalMapObj;
        // The uniqueId the next global WMO placement is filed under: counts down from -2, so it
        // can never meet a tile's own (DAT_00ce04a4).
        static int32_t s_globalMapObjId;
        // On a global-WMO map, the links to the entities placed on the map itself, where a
        // terrain map would link them to chunks (DAT_00adfbf8).
        static STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) s_entityLinkList;
        // A map has been loaded since the last MapMemInitialize (DAT_00cf08f0).
        static int32_t s_mapLoaded;
        // Per-frame tallies CMap::Update zeroes (DAT_00ce04c0, DAT_00ce04bc, DAT_00ce04ac); the
        // frustum facet query adds to the second.
        static int32_t s_frameCountC0;
        static int32_t s_frameCountBC;
        static int32_t s_frameCountAC;
        // Streaming mode's load loop: how many entities / building groups round the target
        // are still waiting on their files, flagging each as wanted; `progress` gets the share
        // of `initial` that has arrived. ref: FUN_007b5e80, FUN_007b50b0
        static int32_t CountPendingEntities(CMapChunk* chunk, float* progress, int32_t initial);
        static int32_t CountPendingMapObjs(CMapChunk* chunk, float* progress, int32_t initial);
        // A placed entity pushes its box up into the chunk or building group that holds it.
        // ref: FUN_007b4fa0, FUN_007b55e0
        static void GrowParentBounds(CMapStaticEntity* entity, CMapBaseObj* parent);
        static void GrowParentsBounds(CMapStaticEntity* entity);
        static STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) s_mapObjDefLinkList;

        // Cell pairs a map query collects, two ints per cell (DAT_00cf4928, a 0x800-entry array
        // MapMemInitialize is meant to size; that part is not ported) and the running int count
        // written into it (DAT_00ce04c8).
        static TSGrowableArray<int32_t> s_cellList;
        static int32_t s_cellListCount;

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
        static int32_t s_streamingMode;
        // The loading screen's progress callback while a map loads (DAT_00cdfff4 / DAT_00cdfff0).
        static void (*s_loadProgressCallback)(float progress, void* arg);
        static void* s_loadProgressArg;       // DAT_00ce0494: SFile streaming mode or trial

        // The terrain shaders MapMemInitialize loads (docs/ref/parity-map-memory.md)
        static CGxShader* s_terrainVertexShaders[0x80];   // DAT_00ce0008: Terrain, 128 permutations
        static CGxShader* s_terrainPixelShaders[3];       // DAT_00ce0488: Terrain0, 3 permutations
        static CGxShader* s_terrainEnvPixelShader[1];     // DAT_00ce0004: Terrain0_env
        static CGxShader* s_terrain1PixelShaders[0x20];   // DAT_00ce0408: Terrain1, up to 32
        static CGxShader* s_terrain1wPixelShaders[8];     // DAT_00ce0428: Terrain1w / Terrain1w_1..4
        static CGxShader* s_terrainShadowMapPixelShader[1]; // DAT_00ce0000: TerrainSM
        // The shadow-mapped terrain pixel shaders, loaded by CreateTerrainShadowShaders.
        //
        // THE NAMES USED TO BE THE WRONG WAY ROUND and it cost a debugging session, so they say
        // what each array actually holds. The 0x60-entry one is DAT_00ce0208 and it is loaded with
        // Terrain3 (or Terrain3_pcf); the 0x20-entry one is DAT_00ce0388 and it is loaded with
        // Terrain2 (or Terrain2_pcf). Reading the reference's indexing the other way round selects
        // a set that is a third of the size and mostly null.
        static CGxShader* s_terrain3PixelShaders[0x60];   // DAT_00ce0208: Terrain3 / Terrain3_pcf
        static CGxShader* s_terrain2PixelShaders[0x20];   // DAT_00ce0388: Terrain2 / Terrain2_pcf
        // The map's .wdl: the low-detail height grid per tile and the far-away buildings.
        static CMapLowDetail s_lowDetail;
        static CGxPool* s_lowDetailIndexPool;             // DAT_00cdfffc
        static CGxBuf* s_lowDetailIndexBuf;               // DAT_00cdfff8
        static int32_t s_terrainShadersDirty;             // DAT_00d1d058
        // The terrain shaders loaded and valid (DAT_00ce049e, from LoadSettings)
        static uint8_t s_terrainShaders;
        // World-space vertices under the terrain shaders (DAT_00ce0498): render chunks then
        // pair up two chunks wide
        static uint8_t s_shaderVertexMode;

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
        // The batch one chunk contributes (DAT_00aeec60): 768 indices over 145 vertices;
        // AppendIndices advances a render chunk's batch by its max index per chunk
        static CGxBatch s_chunkBatch;

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
        static CGxShader* GetTerrainPixelShader(int32_t twoChunk, int32_t layers, int32_t shadowLevel, int32_t specular, int32_t flag80);
        // Load the two shadow-mapped terrain pixel shader sets, replacing any already loaded.
        // ref: FUN_0079e4f0
        static void CreateTerrainShadowShaders();

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
        static void InitializeRenderChunks();
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
        // Every doodad the map has placed, by its MDDF uniqueId, so two tiles sharing an edge
        // place the one that straddles it once (DAT_00d1ba90).
        static TSHashTable<CMapDoodadDef, HASHKEY_NONE> s_doodadUniqueIds;

        // ref: FUN_007becd0
        static CMapDoodadDef* CreateDoodadDef(const char* name, const SMDDF* mddf, const C3Vector& origin);

        static CMapDoodadDef* AllocDoodadDef();
        static void FreeDoodadDef(CMapDoodadDef* def);
        static void ReleaseDoodadDef(CMapDoodadDef* def);
        static CMapArea* GetTargetArea(const C3Vector& target);
        static CMapChunk* GetTargetChunk(const C3Vector& target);
        static void UnlinkDoodadDef(CMapDoodadDef* def);
        // The loaded chunk a world point stands on, or null where nothing is loaded.
        // Addressed the way CMap::GetTerrainLiquid does it: in cells, with the tile's row
        // coming from x and its column from y.
        static CMapChunk* ChunkAt(const C3Vector& position);
        // The liquid standing over a world point, if any: its LiquidType id through `liquidType`
        // and its surface height through `height`. With `strict` the ground under the point has
        // to be below it too, so a point inside a cliff over water does not read as swimming.
        // ref: FUN_007a0820
        static bool GetTerrainLiquid(const C3Vector& position, uint32_t* liquidType,
                                     float* height, int32_t strict);
        static CMapEntity* AllocEntity(int32_t linkToHead);
        // Put every entity the map holds into the distance row it belongs in this frame.
        // ref: FUN_007b5590
        static void BucketEntities(int32_t update);
        // Place every static entity whose model has now arrived, and let it off the waiting
        // list. Until this has run on one, nothing will draw it. ref: FUN_007b5630
        static void UpdatePendingEntities();
        // Every placed doodad's model, reached through the chunks that hold it. A doodad that
        // straddles two chunks is handed over once. Frozen's own: the reference has no such
        // walk, because the systems that want one keep their own lists.
        static void ForEachDoodadModel(void (*fn)(CM2Model* model, void* arg), void* arg);

        // The buildings' own props: built with the def when its root lands, and walked for the
        // frame's particle emitters.
        static void CreateMapObjDoodads(CMapObjDef* def, CMapObj* mapObj);
        static void ForEachMapObjDoodad(void (*fn)(CM2Model* model, void* arg), void* arg);
        static void CullMapObjDoodads();
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
        // An entity that moved: relinked where it now stands, its liquid and the liquid's kind,
        // and the light it should ease toward. ref: FUN_007a1bc0
        static void UpdateEntity(CMapEntity* entity);
        // The liquid at a point: a building's first, the terrain's otherwise. ref: FUN_007a0b00
        static bool GetLiquidAt(const C3Vector& point, uint32_t* liquidType, float* height, int32_t* unused, int32_t flag);
        // The liquid inside the buildings at a point. ref: FUN_007a09d0
        static bool GetMapObjLiquid(const C3Vector& point, uint32_t* liquidType, float* height);
        // The liquid of the building group an entity is linked into. ref: FUN_007a1a30
        static void UpdateEntityGroupLiquid(CMapEntity* entity);
        // The building group an entity is linked into. ref: FUN_007a13e0
        static bool GetEntityMapObjGroup(CMapStaticEntity* entity, CMapObjDef** def, CMapObj** mapObj,
                                         CMapObjDefGroup** defGroup, CMapObjGroup** group, int32_t skipFlagged);
        // The MCNK area id of the chunk under a point, zero when none is loaded. ref: FUN_007a0490
        static uint32_t GetChunkAreaID(const C3Vector& point);
        // Whether the terrain's baked shadow (MCSH) covers a point. ref: FUN_007a06a0
        static bool IsTerrainShadowed(const C3Vector& point);

        // The world segment query: buildings and the models in them, then the terrain cells and
        // their liquid and models, along start -> end. `t` comes in as the furthest fraction to
        // look and leaves as the nearest hit; `hit` gets the point. ref: FUN_007a3b70
        static bool QuerySegment(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* t,
                                 uint32_t queryFlags, void* result);
        // The buildings along a segment, nearest first. ref: FUN_007a30d0
        static bool QuerySegmentObjects(const C3Vector& start, const C3Vector& end, uint32_t queryFlags,
                                        uint32_t defSkipFlags, float* t, uint16_t* face, CMapObj** mapObj,
                                        CMapObjDef** def, CMapObjDefGroup** defGroup);
        // The terrain cells a segment crosses, listed and then tested. ref: FUN_007a39f0
        static bool QuerySegmentTerrain(const C3Vector& start, const C3Vector& end, float* t,
                                        uint32_t queryFlags, CMapChunk** chunk);
        // ref: FUN_007a3570
        static bool QuerySegmentCells(const C3Vector& start, const C3Vector& end, float* t,
                                      uint32_t queryFlags, CMapChunk** chunk);
        // The cells a line crosses, stepping along the first or the second cell axis.
        // ref: FUN_007a23e0, FUN_007a2230
        static void AddCellLineFirst(const float* from, const float* to, const int32_t* cells);
        static void AddCellLineSecond(const float* from, const float* to, const int32_t* cells);
        // The doodads of a list onto the ray list of the model scene, by query kind. ref: FUN_007a2760
        static void AddRayModels(CMapBaseObjRefList* list, uint32_t queryFlags);

        // The world triangles a frustum touches: terrain (mask 0x100), liquid (0x30000) and
        // doodad collision (0xf) cell by cell, then the buildings. ref: FUN_007a5dd0
        static bool QueryFrustumFacets(const CWFrustum& frustum, CFacetList& list, uint32_t flags, uint32_t* hitFlags);
        // One chunk of it: `cells` is the whole query in map cells. ref: FUN_007a5330
        static bool QueryFrustumCell(int32_t col, int32_t row, const CiRect& cells, const CWFrustum& frustum, CFacetList& list, uint32_t flags);
        // The buildings: each one the frustum reaches, with the frustum taken into its own
        // space. ref: FUN_007a4ee0
        static bool QueryFrustumObjects(const CWFrustum& frustum, CFacetList& list, uint32_t flags, uint32_t* hitFlags);
        // One liquid layer of a chunk. ref: FUN_007a3d50
        // The highest surface within `range` below `position`: terrain, chunk liquid, the
        // buildings and their doodads, and placed models. ref: FUN_007ade10
        static bool QueryGroundHeight(const C3Vector& position, float range, float* height);
        // The buildings for it: the chunk's defs, or the global WMO's when there is no chunk.
        // ref: FUN_007ada80
        static void QueryGroundMapObjs(CMapChunk* chunk, const C3Vector& start, const C3Vector& end, float* t);
        // Placed models whose box holds the segment, onto the ray list. ref: FUN_007ad940
        static void AddGroundRayModels(CMapBaseObjRefList* list, const C3Vector& start, const C3Vector& end);
        static void QueryFrustumLiquid(CMapChunk* chunk, const CWFrustum& frustum, const CiRect& cells, CChunkLiquid* liquid, CFacetList& list);
        // The entities of a list onto it through the object query callback. ref: FUN_007a2960
        static void AddRayObjects(CMapBaseObjRefList* list, uint32_t queryFlags);
        // What the last segment query hit, if it was an object (DAT_00cd7768).
        static uint64_t s_segmentHitGUID;
        // Bumped by every segment and frustum query so an entity reached twice is tested once
        // (DAT_00ce04c4).
        static int32_t s_queryStamp;
        // Whether the processor has SSE (OsGetCpuInfo bit 2), read once by MapMemInitialize.
        // The facet builders normalise with rsqrtss when it is set. DAT_00cf08f8
        static int32_t s_useSse;
        // The BSP leaf cache, while bspcache is on (DAT_00cdd7a0), and the way it evicts next
        // from a full bucket (DAT_00cdf7bc).
        static CMapBspNodeCache* s_bspNodeCache;
        static uint32_t s_bspNodeCacheVictim;
        // A group's leaves out of the cache, before its BSP goes away. ref: FUN_0079b0d0
        static void EvictBspLeaves(const CAaBspNode* nodes, uint32_t count);

        // Tiles and chunks
        static CMapArea* CreateArea(int32_t x, int32_t y);
        static void UnloadArea(CMapArea* area);
        static void DestroyChunk(CMapChunk* chunk);
        static float AreaDistanceSq(const CAaBox& box, const C2Vector& point);
        static void UpdateMapObjDefs(int32_t update);
        static void SetupMapObjDef(CMapObjDef* def, CMapObj* mapObj);
        static void CreateDefGroups(CMapObj* mapObj, CMapObjDef* def);
        static CMapObjDef* CreateMapObjDef(const char* name, const SMODF* modf, const C3Vector& origin, int32_t dedup);
        static int32_t SafeOpen(const char* path, SFile** file);
        static int32_t SafeRead(const char* path, SFile* file, void* buffer, uint32_t size);
        static void AsyncLoadCleanup(CAsyncObject* object);
        static HTEXTURE LoadTexture(const char* name);

        // ref: FUN_0079b440
        static CMapArea* GetLoadedArea(int32_t x, int32_t y);

        // The terrain type of the ground under a world point, from the ground-effect record of
        // the layer showing there. False when the point is off the map, over a tile that is not
        // loaded, or over a hole. ref: FUN_007a0530
        static bool GetTerrainType(const C3Vector& position, int32_t* terrainType);

        // The terrain height under a world point, and which chunk answered it. False when the
        // point is over a tile that is not loaded or a chunk that is not there; the chunk is
        // cleared in that case, so a caller can tell 'no chunk' from 'chunk but no height'.
        // ref: FUN_007c1660
        static bool GetTerrainHeight(const C3Vector& position, float* height,
                                     CMapChunk** outChunk);
        // ref: FUN_0079ae80
        static void BuildBspLeafCache(CMapBspLeafCache* leaf, const uint16_t* faceRefs, const CAaBspNode* node, const SMOPoly* polys, const C3Vector* vertices, const uint16_t* indices);
        // ref: FUN_007a20e0
        static void AddCellSpanY(const int32_t* line);
        // ref: FUN_007a2180
        static void AddCellSpanX(const int32_t* line);

    private:
        static void FreeAreaLowObject(uint32_t* heap, CMapAreaLow* area);
        static void MapMemInitializeHeaps();
};

#endif
