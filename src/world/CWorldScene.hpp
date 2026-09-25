#ifndef WORLD_C_WORLD_SCENE_HPP
#define WORLD_C_WORLD_SCENE_HPP

#include "world/map/CMapRenderChunk.hpp"
#include "gx/Texture.hpp"
#include <storm/List.hpp>
#include <tempest/Matrix.hpp>
#include <cstdint>

class CGxShader;

// The per-frame scene of the world (reference WorldScene.cpp): the render lists the visibility
// traversal fills, and the passes that drain them.
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

        static const uint32_t TERRAIN_CONSTANT_COUNT = 37;
        static const uint32_t TERRAIN_CONSTANT_LAYER_SCROLL = 13;

        // The render lists (DAT_00cdaf60, 21 of them). Index 0 holds chunks drawn with the solid
        // placeholder textures, 4 + (layers - 1) * 4 + specular * 2 + colour the textured chunks
        // keyed by the pixel shader they need, and 20 the chunks bound but not drawn.
        static const uint32_t RENDER_LIST_COUNT = 21;
        static const uint32_t RENDER_LIST_SOLID = 0;
        static const uint32_t RENDER_LIST_TEXTURED = 4;
        static const uint32_t RENDER_LIST_HIDDEN = 20;

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

        // Static functions
        static void Initialize();
        static void SelectChunkShaders(int32_t specular, int32_t color);
        static void SetupTerrainConstants(const C44Matrix& world, const C44Matrix& view);
        static void RenderTerrain();
        static void RenderChunkLists();
        static void RenderSolidChunks();
        static void RenderHiddenChunks();
};

#endif
