#ifndef WORLD_C_WORLD_SCENE_HPP
#define WORLD_C_WORLD_SCENE_HPP

#include "world/map/CMapChunk.hpp"
#include "world/map/CMapRenderChunk.hpp"
#include "gx/Texture.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Plane.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CGxShader;

// The per-frame scene of the world (reference WorldScene.cpp): the camera's view of the map, the
// distance rows the map's objects are bucketed into, the render lists the visibility traversal
// fills, and the passes that drain them.
class CWorldScene {
    public:
        // Types

        // The Terrain vertex shader's constant block (DAT_00d250a0, 37 registers), filled once per
        // frame by SetupTerrainConstants and per chunk by CMapRenderChunk::SetupVertexShader
        struct TerrainConstants {
            C44Matrix view;                     // c0-c3: the world-to-view transform
            C44Matrix proj;                     // c4-c7: the native projection, z row negated
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

        // A view frustum as the traversal keeps it (reference 0xfc bytes at DAT_00cdb168, one per
        // portal recursion depth): six planes facing inward and the eight world-space corners they
        // were built from, near face first (corners 0-3), far face after (4-7), each face going
        // (-x,-y) (-x,+y) (+x,+y) (+x,-y) in clip space.
        struct Frustum {
            C4Plane planes[6];                  // +0x00: side, side, side, side, far, near
            C3Vector corners[8];                // +0x60
            uint32_t unknownC0[15];             // +0xc0: not read by the terrain traversal

            Frustum() = default;
            explicit Frustum(const C3Vector* corners);
            void SetCorners(const C3Vector* corners);
            void ComputePlanes();
            int32_t SphereInside(const CAaSphere& sphere);
        };

        // A list slot of a distance row whose element type is not ported yet (a TSExplicitList's
        // link offset and terminator); nothing links into it
        struct RowListStub {
            int32_t linkOffset = 0;
            void* prev = nullptr;
            void* next = nullptr;
        };

        // One 33-yard distance band along the camera's forward (reference 0x6c bytes at
        // DAT_00cd9048, 64 rows): every map object the frame can reach, bucketed by how far
        // along the view it starts, so the traversal walks near to far
        struct Row {
            STORM_EXPLICIT_LIST(CMapChunk, m_rowLink) chunks;               // +0x00
            RowListStub mapObjDefs;                                         // +0x0c: CMapObjDef, link +0xa8
            RowListStub entities;                                           // +0x18: CMapEntity, link +0xc8
            RowListStub entitiesFar;                                        // +0x24: CMapEntity, link +0xa8
            RowListStub liquids;                                            // +0x30: CChunkLiquid
            RowListStub list5;                                              // +0x3c
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
            float unknown14;
            float unknown18;
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
        static C4Plane s_rowPlanes[ROW_COUNT];              // DAT_00cdab48: the front plane of each row
        static C3Vector s_frustumCorners[8];                // DAT_00cdb108: the camera frustum in world space
        static Frustum s_frustums[FRUSTUM_DEPTH_MAX];       // DAT_00cdb168: per portal recursion depth
        static int32_t s_frustumDepth;                      // DAT_00cd8798
        static C3Vector s_cameraTarget;                     // DAT_00cd8f68
        static C3Vector s_viewDir;                          // DAT_00cd8f74: unit camera-to-target
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
        static float s_horizonBuffer[HORIZON_COLUMNS];      // DAT_00cd8938: the horizon height per screen column, from the occluders
        static uint32_t s_rowStats[0x60];                   // DAT_00cd87b8: cleared every frame; not read by the terrain traversal
        static float s_farChunkDistance;                    // DAT_00cd8784: farclip less a chunk
        static float s_nearChunkDistance;                   // DAT_00cd8780: where the occluder chunks start
        static float s_occluderFarClip;                     // DAT_00cd87ac
        static int32_t s_visibleChunkCount;                 // DAT_00cd8770
        static int32_t s_visibleMapObjCount;                // DAT_00cd8774
        static int32_t s_visibleEntityCount;                // DAT_00cd872c
        static int32_t s_visibleCount8624;                  // DAT_00cd8624
        static int32_t s_frameStamp;                        // DAT_00cd87b0
        static void* s_cameraGroup;                         // DAT_00cd87a4: the map object group the camera is in (portal mode)
        static float s_cameraGroundHeight;                  // DAT_00cd8790
        static int32_t s_hasMapObjs;                        // DAT_00cd8778
        static ViewWindow s_window;                         // DAT_00adf570
        static ViewWindow s_portalWindow;                   // DAT_00adf58c
        static const int32_t s_quadrantVertex[4];           // DAT_00aeee3c: the chunk vertex nearest the camera per quadrant

        // Static functions
        static void Initialize();
        static void SelectChunkShaders(int32_t specular, int32_t flag80);
        static void SetupTerrainConstants(const C44Matrix& world, const C44Matrix& view);
        static void RenderTerrain();
        static void RenderChunkLists();
        static void RenderSolidChunks();
        static void RenderHiddenChunks();

        static void UpdateCamera(const C3Vector& cameraPos, const C3Vector& cameraTarget);
        static void BuildRowPlanes(const C3Vector& dir, const C3Vector& cameraPos);
        static int32_t BoxOutsideFrustum(const CAaBox& box);
        static int32_t DistanceBand(float distance);
        static uint32_t BoxOccluded(const CAaBox& box, uint32_t flags);
        static uint32_t SphereOccluded(const C3Vector& center, float radius, uint32_t flags);
        static int32_t SphereOccludedByVolumes(const CAaSphere& sphere);
        static void BoxNearPoint(const CAaBox& box, C3Vector* point);
        static void ChunkVertexPoint(const CMapChunk* chunk, int32_t vertex, C3Vector* point);
        static float ViewPlane2dDistance(const C3Vector& point);
        static void BucketChunk(CMapChunk* chunk, const C3Vector& point);
        static void SubFrustum(const ViewWindow* window);
        static void GetFrustumCorners(C3Vector* corners);
        static int32_t ChunkRectInView(const int32_t* rect);
        static void Traverse(const ViewWindow* window, int32_t portal);
        static void TraverseRowChunks(Row* row, uint32_t rowIndex);
};

#endif
