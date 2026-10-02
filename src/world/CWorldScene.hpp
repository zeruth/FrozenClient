#ifndef WORLD_C_WORLD_SCENE_HPP
#define WORLD_C_WORLD_SCENE_HPP

#include "world/map/CMapChunk.hpp"
#include "world/map/DetailDoodad.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapRenderChunk.hpp"
#include "gx/Texture.hpp"
#include "world/CWFrustum.hpp"
#include "world/map/CMapBaseObj.hpp"
#include "world/map/CMapEntity.hpp"
#include <storm/List.hpp>
#include <storm/Array.hpp>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Plane.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CChunkLiquid;
class CGxShader;
class CMapObjDef;
class CMapStaticEntity;

// The per-frame scene of the world (reference WorldScene.cpp): the camera's view of the map, the
// distance rows the map's objects are bucketed into, the render lists the visibility traversal
// fills, and the passes that drain them.
// The plane through three points, facing along (b - a) x (c - a). ref: FUN_007912c0
void PlaneFromPoints(C4Plane* plane, const C3Vector& a, const C3Vector& b, const C3Vector& c);

class CWorldScene {
    public:
        // Types

        // The Terrain vertex shader's constant block (DAT_00d250a0, 37 registers), filled once per
        // frame by SetupTerrainConstants and per chunk by CMapRenderChunk::SetupVertexShader
        struct TerrainConstants {
            C44Matrix view;                     // c0-c3: the world-to-view transform
            // c4-c7: the native projection, UNCHANGED. This comment used to say "z row negated",
            // which is what the reference does and the opposite of what SetupTerrainConstants
            // actually assigns -- it stores m_projNative as is, and the note beside that
            // assignment explains why (negating made D3D clip every chunk, terrain invisible on
            // the first run, 2026-09-25).
            //
            // The two are equivalent, which is worth writing down because the difference keeps
            // coming back as a suspect. frozen's view space is +z forward and the reference's is
            // -z forward, so vz_f = -vz_ref, and with the reference negating row 2 of the
            // projection (m22' = -m22, m23' = -m23):
            //
            //   clip.z_ref = vz_ref * -m22 + m32   clip.z_f = vz_f * m22 + m32   -- both -vz_ref*m22 + m32
            //   clip.w_ref = vz_ref * -m23         clip.w_f = vz_f * m23         -- both -vz_ref*m23
            //
            // Identical in both components. So frozen's un-negated projection over a +z-forward
            // view produces exactly the reference's clip coordinates, and the liquid path -- which
            // feeds the REFERENCE's own shaders out of the archives -- agrees with terrain.
            C44Matrix proj;
            C44Matrix viewTransposed;           // c8-c11
            float fog[4];                       // c12: -1/(end-start), end/(end-start), rate, 0
            float layerScroll[4][4];            // c13-c16: the UV scroll of animated layers
            float unused[4];                    // c17
            float texScale[5][4];               // c18-c22: chunk XY to UV per layer, then the alpha map
            float position[4];                  // c23: the chunk origin
            float lightDir[4];                  // c24: the sun direction in view space
            float sunAmbient[4];                // c25
            float sunDiffuse[4];                // c26
            float sunSpecular[4];               // c27: w is the specular exponent
            struct {
                float pos[4];                   // camera-relative, w 1
                float color[4];
                float attenuation[4];           // constant, linear, quadratic
            } lights[3];                        // c28-c36: the nearest point lights
        };

        // A list slot of a distance row whose element type is not ported yet (a TSExplicitList's
        // link offset and terminator); nothing links into it
        struct RowListStub {
            int32_t linkOffset = 0;
            void* prev = nullptr;
            void* next = nullptr;
        };

        // One edge of something that hides what is behind it from the horizon test: two points,
        // and the row list it sits on (reference CWorldOccluder, 0x20 bytes, named by its own
        // allocation tag).
        struct Occluder {
            C3Vector a;                          // +0x00
            C3Vector b;                          // +0x0c
            TSLink<Occluder> m_link;             // +0x18
        };

        // One 33-yard distance band along the camera's forward (reference 0x6c bytes at
        // DAT_00cd9048, 64 rows): every map object the frame can reach, bucketed by how far
        // along the view it starts, so the traversal walks near to far
        struct Row {
            STORM_EXPLICIT_LIST(CMapChunk, m_rowLink) chunks;               // +0x00
            STORM_EXPLICIT_LIST(CMapObjDefGroup, m_rowLink) mapObjDefGroups;      // +0x0c
            STORM_EXPLICIT_LIST(CMapEntity, m_entityRowLink) entities;      // +0x18
            STORM_EXPLICIT_LIST(CMapStaticEntity, m_rowLink) staticEntities; // +0x24
            STORM_EXPLICIT_LIST(CChunkLiquid, m_frameLink) liquids;         // +0x30
            STORM_EXPLICIT_LIST(Occluder, m_link) occluders;                // +0x3c: edges that shade the horizon
            RowListStub list6;                                              // +0x48
            STORM_EXPLICIT_LIST(CMapChunk, m_frameLink) occluderChunks;     // +0x54: chunks that shade the horizon buffer
            RowListStub list8;                                              // +0x60
        };

        // A rectangle of the screen in 0..1 the traversal is limited to (a portal), with the
        // depth it was reached at (-1 when unused); reference DAT_00adf570 and DAT_00adf58c
        struct ViewWindow {
            float minX;
            float minY;
            float maxX;
            float maxY;
            float depth;
            // An outline in place of the rectangle: screen-space points (x and y read) and how
            // many. A rectangle window has none.
            const C3Vector* points;
            uint32_t pointCount;
        };

        static const uint32_t TERRAIN_CONSTANT_COUNT = 37;
        static const uint32_t TERRAIN_CONSTANT_LAYER_SCROLL = 13;

        // The render lists (DAT_00cdaf60, 21 of them). Index 0 holds chunks drawn with the solid
        // placeholder textures, 4 + (layers - 1) * 4 + specular * 2 + colour the textured chunks
        // keyed by the pixel shader they need, and 20 the chunks bound but not drawn.
        static const uint32_t RENDER_LIST_COUNT = 21;
        static const uint32_t RENDER_LIST_SOLID = 0;
        static const uint32_t RENDER_LIST_TEXTURED = 4;
        static const uint32_t RENDER_LIST_HIDDEN = 20;

        static const uint32_t ROW_COUNT = 64;
        static const uint32_t FRUSTUM_DEPTH_MAX = 8;
        static const uint32_t HORIZON_COLUMNS = 0x180;

        // Static variables
        static STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) s_renderChunkLists[RENDER_LIST_COUNT];
        static HTEXTURE s_solidTexture;                     // DAT_00cd8618: solid 0xff808080
        static HTEXTURE s_blackTexture;                     // DAT_00cd8614: solid 0xff000000
        static TerrainConstants s_terrainConstants;         // DAT_00d250a0
        static int32_t s_fogColorState;                     // DAT_00d2509c: GxRs_FogColor on entry to the terrain pass
        static void (*s_chunkDraw)(CMapRenderChunk*);       // DAT_00d25098: the draw for chunks without a pixel shader
        static CGxShader* s_layerPixelShaders[4];           // DAT_00d1d080: the pixel shader per layer count
        static CGxShader* s_terrain0PixelShader;            // DAT_00d1d094
        static CGxShader* s_terrain0PixelShaderNoAlpha;     // DAT_00d1d090

        static Row s_rows[ROW_COUNT];                       // DAT_00cd9048
        // Every liquid layer this frame reached, in the order the rows were visited, near to
        // far. The rows hand their layers over one at a time through the same link.
        static STORM_EXPLICIT_LIST(CChunkLiquid, m_frameLink) s_frameLiquidList;  // DAT_00adfc34
        // Every chunk that scattered grass and is near enough to draw it this frame. A chunk
        // puts itself here from PrepareRender; the pass empties it.
        static STORM_EXPLICIT_LIST(DetailDoodad::CDetailDoodadData, m_frameLink) s_frameDetailDoodadList;
        // Edges waiting for reuse (the reference's free list at 0x00aeef5c) and the edges kept
        // for the debug draw while enable 0x2000 is set (DAT_00cdb0ac).
        static STORM_EXPLICIT_LIST(Occluder, m_link) s_freeOccluders;
        static STORM_EXPLICIT_LIST(Occluder, m_link) s_debugOccluders;
        static C4Plane s_rowPlanes[ROW_COUNT];              // DAT_00cdab48: the front plane of each row
        static C3Vector s_frustumCorners[8];                // DAT_00cdb108: the camera frustum in world space
        static CWFrustum s_frustums[FRUSTUM_DEPTH_MAX];       // DAT_00cdb168: per portal recursion depth
        // The camera's own frustum in world space, rebuilt with the corners (DAT_00cdd108).
        // The portal walk clips a doorway's outline against its four sides and its far plane
        // before measuring how much of the screen the doorway covers.
        static CWFrustum s_clipFrustum;
        static int32_t s_frustumDepth;                      // DAT_00cd8798
        static C3Vector s_cameraPos;                        // DAT_00cd8f5c
        static C3Vector s_cameraTarget;                     // DAT_00cd8f68
        static C3Vector s_viewDir;                          // DAT_00cd8f74: unit camera-to-target
        // How far the camera is below the liquid it is in, zero when it is in none. What the
        // underwater darkening reads to decide how deep the water looks.
        // One distant occluder: a run of planes in s_occlusionPlanes that together bound the
        // volume it hides. Filled by the low-detail terrain, which is not ported, so the list
        // is empty and nothing is ever occluded by one.
        struct OcclusionVolume {
            int32_t firstPlane;     // +0x00
            int32_t planeCount;     // +0x04
        };

        static TSGrowableArray<C4Plane> s_occlusionPlanes;   // DAT_00d2dcd8
        static TSGrowableArray<OcclusionVolume> s_occlusionVolumes;  // DAT_00d2dce8
        static float s_cameraLiquidDepth;                   // DAT_00cd8790
        // Which LiquidType the camera was in last frame, so a change can be noticed.
        static uint32_t s_cameraLiquidType;

        // The frame's world view state. Built by UpdateWorldView, read by the sky, the weather, the
        // overhead icons and the particle passes.
        static C44Matrix s_viewNoTranslate;
        static C44Matrix s_projNative;
        static C44Matrix s_viewProjT;
        static C3Vector s_worldCameraPos;
        static bool s_fogActive;
        static bool s_viewUpdated;
        static bool s_terrainShaderTried;
        static CGxShader* s_uiVertexShader[2];
        static CGxShader* s_uiPixelShader;

        // The transparent liquid pass, drained from the world frame's transparent block where the
        // reference drains it. ref: FUN_00790a80
        static void DrawLiquidPass();

        static void UpdateWorldView();
        static void EnsureUiShaders();
        static void UiShaders(CGxShader*& vs, CGxShader*& ps);
        static C4Plane s_viewPlane;                         // DAT_00cd8f80: the plane through the camera facing the view
        static C4Plane s_viewPlane2d;                       // DAT_00cd8f90: the same with the direction flattened
        static CAaBox s_frustumBounds;                      // DAT_00cd8f44
        static int32_t s_frustumChunkRect[4];               // DAT_00cdb0f4: {minRow, minCol, maxRow, maxCol} of the frustum bounds
        static int32_t s_cameraQuadrant;                    // DAT_00cd8f38: 2 when the target is +x of the camera, +1 when +y
        static int32_t s_targetQuadrant;                    // DAT_00cd8f3c: the opposite
        static C44Matrix s_viewMatrix;                      // DAT_00adf5e8
        static C44Matrix s_projMatrix;                      // DAT_00adf628
        static C44Matrix s_viewProjMatrix;                  // DAT_00adf5a8
        static float s_viewProjW[4];                        // DAT_00cd8fc8: its fourth column
        static C44Matrix s_occlusionMatrix;                 // DAT_00adf460: view (flattened) x projection for the horizon buffer
        static float s_horizonBuffer[HORIZON_COLUMNS];
        // One flag per horizon column (DAT_00cd87b8). A column whose bit 0 is set is not
        // punched back open by a chunk with holes in it.
        static uint8_t s_horizonColumnFlags[HORIZON_COLUMNS];      // DAT_00cd8938: the horizon height per screen column, from the occluders
        static uint32_t s_rowStats[0x60];                   // DAT_00cd87b8: cleared every frame; not read by the terrain traversal
        static float s_farChunkDistance;                    // DAT_00cd8784: farclip less a chunk
        static float s_nearChunkDistance;                   // DAT_00cd8780: where the occluder chunks start
        static float s_occluderFarClip;                     // DAT_00cd87ac
        static int32_t s_visibleChunkCount;                 // DAT_00cd8770
        static int32_t s_visibleMapObjCount;                // DAT_00cd8774
        static int32_t s_visibleEntityCount;                // DAT_00cd872c
        static int32_t s_visibleCount8624;                  // DAT_00cd8624
        static int32_t s_frameStamp;                        // DAT_00cd87b0
        // DAT_00cd87a4: the placed building the camera is inside, and DAT_00cd87a0 the second one
        // when the query found a def carrying flag 0x400 as well. Named s_cameraGroup before, which
        // was wrong twice over -- it is a def, not a group, and there are two of them.
        //
        // Still null: UpdateCameraDef computes them but nothing calls it yet, because CMap::Render
        // would then take an indoor branch that has no traversal in it. See the note there.
        static CMapObjDef* s_cameraDef;
        static CMapObjDef* s_cameraDefFlagged;
        // What UpdateCameraDef names the camera's whereabouts: the building's path or the map's
        // name (DAT_00cd8628), and the group's name or the tile (DAT_00cd8730).
        static char s_cameraAreaName[0x104];
        static char s_cameraSubAreaName[0x40];
        // The group indices of each of those two, which is what the portal walks are handed
        // (0x00cdb0d4 and 0x00cdb0e4). Set membership, not sequences -- a group appears once
        // however many times the query reports it.
        static TSGrowableArray<uint32_t> s_cameraGroupIndices;
        static TSGrowableArray<uint32_t> s_cameraFlaggedGroupIndices;
        static float s_cameraGroundHeight;                  // DAT_00cd8790
        static int32_t s_hasMapObjs;                        // DAT_00cd8778
        // The groups the traversal found, in the order it found them (DAT_00cdb080)
        static STORM_EXPLICIT_LIST(CMapObjDefGroup, m_renderLink) s_visibleMapObjGroups;
        // DAT_00cdb08c: placed groups whose liquid surface has not been built yet. The traversal
        // adds a group here the first time it is seen, and the liquid pass drains the list,
        // building one surface per entry. Its link offset is +0xb8, which is the third TSLink on
        // CMapObjDefGroup after m_rowLink and m_renderLink -- recovered 2026-09-27 from the queue
        // that unlinks through it, which is what the TODO here used to be waiting on.
        static STORM_EXPLICIT_LIST(CMapObjDefGroup, m_liquidQueueLink) s_pendingLiquidGroups;
        // The groups the def update put in reach of the frame, before the bucketing
        // spreads them over the distance rows (DAT_00cdaf48)
        static STORM_EXPLICIT_LIST(CMapObjDefGroup, m_rowLink) s_mapObjDefGroupCandidates;
        static ViewWindow s_window;                         // DAT_00adf570
        static ViewWindow s_portalWindow;                   // DAT_00adf58c
        // The screen regions seen through portals (DAT_00cdd0e8) and seen from outside them
        // (DAT_00cdd0f8). CMap::Render fills what the first leaves uncovered, and the second, at the
        // far plane in the fog colour (FillViewWindows).
        static TSGrowableArray<ViewWindow> s_portalViews;
        static TSGrowableArray<ViewWindow> s_exteriorViews;
        static const int32_t s_quadrantVertex[4];           // DAT_00aeee3c: the chunk vertex nearest the camera per quadrant

        // Put a group index in one of the two lists if it is not already there. ref: FUN_00792fc0
        static void AddGroupIndexUnique(TSGrowableArray<uint32_t>& list, uint32_t groupIndex);

        // Work out which building, and which room of it, the camera is in, by dropping a segment
        // straight down from it. Fills s_cameraDef, s_cameraDefFlagged and the two index lists.
        // ref: FUN_00795d40
        static void UpdateCameraDef();
        static void ViewWindowInit(ViewWindow& window, const float* rect, float depth);
        static void ViewWindowMerge(ViewWindow& window, const ViewWindow& other);
        static void AddPortalView(const ViewWindow& window);
        static void AddExteriorView(const ViewWindow& window);
        static void ComplementPortalViews();
        static void FillViewWindows(TSGrowableArray<ViewWindow>& views, int32_t portal);

        // Static functions
        static void Initialize();
        static void SelectChunkShaders(int32_t specular, int32_t flag80);
        static void SetupTerrainConstants(const C44Matrix& world, const C44Matrix& view);
        static void RenderTerrain();
        // Every map object the frame can see, drawn. ref: FUN_007964a0
        static void RenderMapObjs();
        static void RenderChunkLists();
        static void RenderSolidChunks();
        static void RenderHiddenChunks();

        static void UpdateCamera(const C3Vector& cameraPos, const C3Vector& cameraTarget);
        static void BuildRowPlanes(const C3Vector& dir, const C3Vector& cameraPos);
        // Raise the horizon along a ridge of terrain: each pair of consecutive points spans a
        // run of columns, and every column in it remembers the higher skyline. A chunk with
        // holes in it punches those columns back open instead. ref: FUN_0078f6a0
        static void ShadeHorizon(const float (*table)[3], const float* heights,
                                 const int32_t* indices, int32_t count,
                                 const C3Vector& position, int32_t holes);

        static int32_t BoxOutsideFrustum(const CAaBox& box);
        // A polyline through the horizon buffer: each pair of points raises the columns it spans to
        // the lower of its two heights, and with `mark` set the columns are flagged so a holed
        // chunk will not reopen them. ref: FUN_0078f900
        static void ShadeHorizonPolyline(const C3Vector* points, int32_t count, int32_t mark);
        // An occluder edge into the distance rows, split at every row boundary it crosses so each
        // piece shades the horizon when its row is reached. ref: FUN_007927e0
        static void AddOccluder(const C3Vector& a, const C3Vector& b);
        // A recycled edge, or a new one. ref: FUN_007cc9a0
        static Occluder* AllocOccluder();
        // Back on the free list. ref: FUN_007cca90
        static void FreeOccluder(Occluder* occluder);
        // The map's fixed horizon occluders that the frustum reaches. ref: FUN_007cc810
        static void AddFixedOccluders();
        // File one liquid layer under the distance row its centre falls in, or drop it when
        // that is past the last row. ref: FUN_00792df0
        static void AddLiquid(CChunkLiquid* liquid, const C3Vector& center);
        // File one entity under the distance row its nearest corner falls in, or set it aside
        // as hidden for the frame. ref: FUN_00792e60
        static void AddEntity(CMapEntity* entity);
        // Move a row's liquid layers to the frame list, testing each on the way.
        // ref: FUN_007935a0
        static void TraverseRowLiquids(Row* row);
        // Drain the frame's entity list, casting a blob under each entity that should have one.
        // ref: FUN_00793980
        static void DrawEntityShadows();
        // What the camera is standing in, and how deep. ref: FUN_00790920
        static void UpdateCameraLiquid();
        // Every entity the frame found visible, in the order the rows were walked. The row
        // visit hands them over through the same link the row used. DAT_00cdb098
        static STORM_EXPLICIT_LIST(CMapEntity, m_entityRowLink) s_frameEntityList;
        // ref: FUN_00791120
        static int32_t SphereOutsideFrustum(const C3Vector& center, float radius);
        // ref: FUN_00791cb0
        static void VisitStaticEntity(CMapStaticEntity* entity);
        // Hand one solid box to the occluder sink as five faces. ref: FUN_007946d0
        static void SubmitOccluderBox(const CAaBox& box, float maxDistance);

        // The occluders of one distance row: solid chunks raise the horizon, chunks you can
        // see through reopen it, and the reopening has to come second. ref: FUN_00793760
        static void TraverseRowOccluders(Row* row);

        // ref: FUN_00799980
        // Draw every chunk's grass. ref: FUN_007984a0
        static void RenderDetailDoodads();

        // Queue one chunk's grass for this frame's pass. ref: FUN_00792fa0
        static void AddDetailDoodads(DetailDoodad::CDetailDoodadData* instance);

        static void TraverseChunkDoodads(STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink)* links, uint32_t detailBand);
        // ref: FUN_007987a0
        static void TraverseRowStaticEntities(Row* row, uint32_t detailBand);
        // ref: FUN_00793060
        static void TraverseRowEntities(Row* row);

        // What the frame could not see: still animated, never drawn (DAT_00cdb0a4).
        static STORM_EXPLICIT_LIST(CMapEntity, m_hiddenLink) s_hiddenEntities;
        static int32_t DistanceBand(float distance);
        static uint32_t BoxOccluded(const CAaBox& box, uint32_t flags);
        static uint32_t SphereOccluded(const C3Vector& center, float radius, uint32_t flags);
        static int32_t SphereOccludedByVolumes(const CAaSphere& sphere);
        static void BoxNearPoint(const CAaBox& box, C3Vector* point);
        static void ChunkVertexPoint(const CMapChunk* chunk, int32_t vertex, C3Vector* point);
        static float ViewPlane2dDistance(const C3Vector& point);
        static void BucketChunk(CMapChunk* chunk, const C3Vector& point);
        static void SubFrustum(const C3Vector* corners, const ViewWindow* window);
        static void TraverseRowMapObjDefs(Row* row, const ViewWindow* window, int32_t portal);
        static void BucketMapObjDefGroup(CMapObjDef* def, CMapObjDefGroup* defGroup);
        static void BucketMapObjDefGroups();
        static void MarkMapObjGroupVisible(uint32_t groupIndex, CMapObjDef* def);
        static void VisitMapObjDefGroup(CMapObjDef* def, CMapObjDefGroup* defGroup, const ViewWindow* window, int32_t portal);
        // The def the visible-group callback belongs to while a visit is running
        // (DAT_00d1c420 with its callback pair at DAT_00d1bed8)
        static CMapObjDef* s_visibleCallbackDef;
        static void GetFrustumCorners(C3Vector* corners);
        static int32_t ChunkRectInView(const int32_t* rect);
        static void Traverse(const ViewWindow* window, int32_t portal);
        static void TraverseRowChunks(Row* row, uint32_t rowIndex);
};

#endif
