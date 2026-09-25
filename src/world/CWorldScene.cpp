#include "world/CWorldScene.hpp"
#include "world/CWorld.hpp"
#include "world/ShadowMap.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapChunk.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/RenderState.hpp"
#include "gx/Transform.hpp"
#include "gx/shader/CGxShader.hpp"
#include "model/CM2Lighting.hpp"
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>
#include <cstring>

STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) CWorldScene::s_renderChunkLists[CWorldScene::RENDER_LIST_COUNT];
HTEXTURE CWorldScene::s_solidTexture;
HTEXTURE CWorldScene::s_blackTexture;
CWorldScene::TerrainConstants CWorldScene::s_terrainConstants;
int32_t CWorldScene::s_fogColorState;
void (*CWorldScene::s_chunkDraw)(CMapRenderChunk*);
CGxShader* CWorldScene::s_layerPixelShaders[4];
CGxShader* CWorldScene::s_terrain0PixelShader;
CGxShader* CWorldScene::s_terrain0PixelShaderNoAlpha;

static_assert(sizeof(CWorldScene::TerrainConstants) == 0x250, "the terrain constant block is 37 registers");

// The fixed-function chunk draws (FUN_007d0760 / FUN_007d0d70 without terrain shaders,
// FUN_007d13f0 / FUN_007d1ad0 / FUN_007d20a0 / FUN_007d2520 when a layer count has no pixel
// shader) are not ported: frozen draws terrain through the shader path only, and a chunk that
// lands here is skipped
static void DrawChunkUnsupported(CMapRenderChunk* chunk) {
}

// ref: FUN_00984c90
// A packed colour as four shader-constant floats
static void ImVectorToFloats(float* out, const CImVector& color) {
    float scale = 1.0f / 255.0f;
    out[0] = color.r * scale;
    out[1] = color.g * scale;
    out[2] = color.b * scale;
    out[3] = scale * color.a;
}

// The fog colour as the fixed-function state takes it
static CImVector FogColorImVector() {
    const C3Vector& fog = CWorld::GetFogColor();
    CImVector color;
    color.b = CM2Lighting::FogColorByte(fog.z);
    color.g = CM2Lighting::FogColorByte(fog.y);
    color.r = CM2Lighting::FogColorByte(fog.x);
    color.a = 0xFF;
    return color;
}

// ref: FUN_007997d0
// The scene's own state at world start. The chunk sort record (FUN_00799730), the scene sound
// registration (FUN_008a1770) and the two 16-byte records at DAT_00cd8610 are not ported yet.
void CWorldScene::Initialize() {
    // TODO FUN_00799730(), the DAT_00cd877c / DAT_00cd87b0 / DAT_00cd87a8 / s_cameraLiquidType
    // resets, FUN_008a1770(1, 1.0f, 0, ...), the two records at DAT_00cd8610

    CImVector grey = { 0x80, 0x80, 0x80, 0xFF };
    CWorldScene::s_solidTexture = TextureCreateSolid(grey);

    CImVector black = { 0x00, 0x00, 0x00, 0xFF };
    CWorldScene::s_blackTexture = TextureCreateSolid(black);
}

// ref: FUN_007d3e10
// The shaders for one (specular, colour) permutation of the chunk lists: the pixel shader per
// layer count, as far as the device has texture stages and the shader loaded, and the draw for
// chunks that have none.
void CWorldScene::SelectChunkShaders(int32_t specular, int32_t color) {
    CWorldScene::s_terrain0PixelShader = nullptr;
    CWorldScene::s_terrain0PixelShaderNoAlpha = nullptr;

    for (int32_t i = 0; i < 4; i++) {
        CWorldScene::s_layerPixelShaders[i] = nullptr;
    }

    // TODO the four slots at DAT_00d1d070, cleared here and never read by the terrain pass

    if (!CMap::s_terrainShaders) {
        CWorldScene::s_chunkDraw = DrawChunkUnsupported;
        return;
    }

    int32_t twoChunk = (CMap::s_wdtHeader[0] >> 2) & 0x1;
    int32_t shadowLevel = ShadowMapGetShaderLevel();

    CWorldScene::s_terrain0PixelShader = CMap::GetTerrain0PixelShader(twoChunk, 1, specular);
    CWorldScene::s_terrain0PixelShaderNoAlpha = CMap::GetTerrain0PixelShader(twoChunk, 0, specular);

    for (int32_t layers = 1; layers <= 4; layers++) {
        auto shader = CMap::GetTerrainPixelShader(twoChunk, layers, shadowLevel, specular, color);

        if (layers <= GxCaps().m_numTmus && shader && shader->Valid()) {
            CWorldScene::s_layerPixelShaders[layers - 1] = shader;
        }
    }

    CWorldScene::s_chunkDraw = DrawChunkUnsupported;
}

// ref: FUN_007cfbe0
// The frame's share of the terrain vertex constants: the view transform (with its transpose),
// the native projection with its z row negated (the reference tests the device's
// transposed-projection flag at +0x1b4, which nothing in the binary sets), the sun in view space
// and the fog ramp. The point-light block is cleared for the chunks to fill.
void CWorldScene::SetupTerrainConstants(const C44Matrix& world, const C44Matrix& view) {
    auto constants = &CWorldScene::s_terrainConstants;
    memset(constants, 0, sizeof(*constants));

    C44Matrix worldView = world * view;
    constants->view = worldView;
    constants->viewTransposed = worldView.Transpose();
    constants->proj = g_theGxDevicePtr->m_projNative;

    constants->proj.c0 = constants->proj.c0 * -1.0f;
    constants->proj.c1 = constants->proj.c1 * -1.0f;
    constants->proj.c2 = constants->proj.c2 * -1.0f;
    constants->proj.c3 = -1.0f * constants->proj.c3;

    constants->lights[0].attenuation[0] = 1.0f;
    constants->lights[1].attenuation[0] = 1.0f;
    constants->lights[2].attenuation[0] = 1.0f;

    C3Vector sunDir = { 0.0f, 0.0f, 0.0f };

    CM2Lighting lighting;
    CAaSphere origin = { sunDir, 0.0f };
    lighting.Initialize(nullptr, origin);

    // The reference adds the day/night block's sun light (a CM2Light at DAT_00ce04a8 + 0x58);
    // frozen keeps it as a direction and colours (diverged, as in CMap::SetupChunkLighting)
    lighting.AddAmbient(CWorld::GetOutdoorAmbient());
    lighting.AddDiffuse(CWorld::GetOutdoorDiffuse(), CWorld::GetOutdoorDirection());

    auto ambient = reinterpret_cast<C3Vector*>(constants->sunAmbient);
    auto diffuse = reinterpret_cast<C3Vector*>(constants->sunDiffuse);
    auto specular = reinterpret_cast<C3Vector*>(constants->sunSpecular);

    if (!lighting.GetSunlight(&sunDir, ambient, diffuse, specular)) {
        constants->sunAmbient[0] = 1.0f;
        constants->sunAmbient[1] = 1.0f;
        constants->sunAmbient[2] = 1.0f;
        constants->sunAmbient[3] = 1.0f;
        constants->lightDir[0] = 0.0f;
        constants->lightDir[1] = 0.0f;
        constants->lightDir[2] = 0.0f;
        constants->lightDir[3] = 0.0f;
        constants->sunSpecular[0] = 0.0f;
        constants->sunSpecular[1] = 0.0f;
        constants->sunSpecular[2] = 0.0f;
        constants->sunSpecular[3] = 0.0f;
    } else {
        const C44Matrix& m = constants->view;
        float x = -sunDir.x;
        float y = -sunDir.y;
        float z = -sunDir.z;
        constants->lightDir[0] = m.a0 * x + m.b0 * y + m.c0 * z;
        constants->lightDir[1] = m.a1 * x + m.b1 * y + m.c1 * z;
        constants->lightDir[2] = m.c2 * z + m.b2 * y + m.a2 * x;
        constants->lightDir[3] = 1.0f;
        constants->sunSpecular[3] = 20.0f;
    }

    if (g_theGxDevicePtr->MasterEnable(GxMasterEnable_Fog)) {
        float fogStart = CWorld::GetFogStart();
        float fogEnd = CWorld::GetFogEnd();
        float inv = 1.0f / (fogEnd - fogStart);
        constants->fog[0] = -(g_shadowMapFogScale * inv);
        constants->fog[1] = inv * fogEnd;
        constants->fog[2] = CWorld::GetFogRate();
        constants->fog[3] = 0.0f;
    } else {
        constants->fog[0] = 0.0f;
        constants->fog[1] = 1.0f;
        constants->fog[2] = 1.0f;
        constants->fog[3] = 0.0f;
    }
}

// ref: FUN_00798da0
// The terrain pass: the state every chunk shares (fixed-function fog, material and texture
// stages for local-space chunks; the vertex constants for world-space ones; the fog colour as
// a state or a pixel constant depending on the shader level), then the three chunk passes, and
// the texture matrices back to identity.
void CWorldScene::RenderTerrain() {
    GxRsPush();

    g_theGxDevicePtr->RsGet(GxRs_FogColor, CWorldScene::s_fogColorState);

    if (!CMap::s_chunkVerticesWorldSpace) {
        g_theGxDevicePtr->RsSet(GxRs_FogStart, CWorld::GetFogStart());
        g_theGxDevicePtr->RsSet(GxRs_FogEnd, CWorld::GetFogEnd());
        g_theGxDevicePtr->RsSet(GxRs_MatDiffuse, 0xFF7F7F7Fu);

        if (CMap::s_terrainSpecular) {
            g_theGxDevicePtr->RsSet(GxRs_MatSpecular, 0xFFFFFFFFu);
            GxRsSet(GxRs_MatSpecularExp, 20.0f);
        }

        for (int32_t i = 0; i < 5; i++) {
            g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Unk69 + i), i);
            g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_TexGen0 + i), 2);
            g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Unk61 + i), 1);
        }
    } else {
        const C3Vector& cameraPos = CWorld::GetCameraPos();
        C3Vector move = { -cameraPos.x, -cameraPos.y, -cameraPos.z };
        C44Matrix world;
        world.Translate(move);

        C44Matrix view = g_theGxDevicePtr->m_xforms[GxXform_View].Top();
        CWorldScene::SetupTerrainConstants(world, view);
    }

    if (!CMap::s_terrainShaders) {
        g_theGxDevicePtr->RsSet(GxRs_FogColor, FogColorImVector().value);
        g_theGxDevicePtr->RsSet(GxRs_Fog, 1);
        g_theGxDevicePtr->RsSet(GxRs_ColorOp0, 1);
        g_theGxDevicePtr->RsSet(GxRs_AlphaOp0, 0);
        g_theGxDevicePtr->RsSet(GxRs_ColorOp1, 0);
        g_theGxDevicePtr->RsSet(GxRs_AlphaOp1, 0);
    } else {
        CImVector fogColor = FogColorImVector();

        if (!GxCaps().int134) {
            float color[4];
            ImVectorToFloats(color, fogColor);
            g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 2, color, 1);
        } else {
            g_theGxDevicePtr->RsSet(GxRs_FogColor, fogColor.value);
        }

        if (GxCaps().int138) {
            g_theGxDevicePtr->RsSet(GxRs_Fog, 1);
        }

        ShadowMapBindTerrain();
    }

    CWorldScene::RenderChunkLists();
    CWorldScene::RenderSolidChunks();
    CWorldScene::RenderHiddenChunks();

    C44Matrix identity;

    for (int32_t i = 0; i < 5; i++) {
        auto stack = &g_theGxDevicePtr->m_xforms[GxXform_Tex0 + i];

        if (!(stack->m_flags[stack->m_level] & CGxMatrixStack::F_Identity)) {
            stack->Top() = identity;
            stack->m_flags[stack->m_level] = CGxMatrixStack::F_Identity;
        }
    }

    GxRsPop();
}

// ref: FUN_007989c0
// The textured chunk lists, one (specular, colour) permutation and layer count at a time under
// the pixel shader for that count. Every chunk drawn moves to the active list to age out of its
// buffers; with the normal-vector debug enable they queue up instead and draw their normals after
// the shaders are dropped.
void CWorldScene::RenderChunkLists() {
    STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) debugList;
    const C3Vector& cameraPos = CWorld::GetCameraPos();

    for (int32_t permutation = 0; permutation < 4; permutation++) {
        CWorldScene::SelectChunkShaders(permutation >> 1, permutation & 0x1);

        for (int32_t layers = 0; layers < 4; layers++) {
            auto pixelShader = CWorldScene::s_layerPixelShaders[layers];

            if (pixelShader) {
                g_theGxDevicePtr->RsSet(GxRs_PixelShader, pixelShader);
            }

            auto list = &CWorldScene::s_renderChunkLists[RENDER_LIST_TEXTURED + permutation + layers * 4];

            for (auto chunk = list->Head(); chunk; ) {
                auto next = list->Next(chunk);

                chunk->Bind(1);

                if (CWorld::s_enables & CWorld::Enables::Enable_2) {
                    if (!pixelShader) {
                        CWorldScene::s_chunkDraw(chunk);
                    } else if (!CMap::s_chunkVerticesWorldSpace) {
                        chunk->DrawLocal();
                    } else {
                        chunk->DrawWorld();
                    }
                }

                chunk->m_link.Unlink();

                if (!(CWorld::s_enables & 0x40000000)) {
                    CMap::s_activeRenderChunkList.LinkToTail(chunk);
                } else {
                    debugList.LinkToTail(chunk);
                }

                chunk = next;
            }
        }
    }

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<CGxShader*>(nullptr));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<CGxShader*>(nullptr));

    if (debugList.Head()) {
        // The decompilation writes an identity into the world stack right after building this
        // translation; the vertices the debug pass streams are world-space, so the translation is
        // what makes them camera-relative
        C3Vector move = { -cameraPos.x, -cameraPos.y, -cameraPos.z };
        C44Matrix world;
        world.Translate(move);
        g_theGxDevicePtr->XformSet(GxXform_World, world);

        for (auto chunk = debugList.Head(); chunk; ) {
            auto next = debugList.Next(chunk);
            chunk->DrawDebug();
            chunk->m_link.Unlink();
            CMap::s_activeRenderChunkList.LinkToTail(chunk);
            chunk = next;
        }
    }

    debugList.UnlinkAll();
}

// ref: FUN_00793b10
// The chunks drawn with the solid placeholder textures, under the one-layer pixel shader when
// there is one
void CWorldScene::RenderSolidChunks() {
    CWorldScene::SelectChunkShaders(0, 0);

    auto pixelShader = CWorldScene::s_layerPixelShaders[0];

    if (pixelShader) {
        g_theGxDevicePtr->RsSet(GxRs_PixelShader, pixelShader);
    }

    auto list = &CWorldScene::s_renderChunkLists[RENDER_LIST_SOLID];

    for (auto chunk = list->Head(); chunk; ) {
        auto next = list->Next(chunk);

        chunk->Bind(1);

        if (CWorld::s_enables & CWorld::Enables::Enable_2) {
            if (!pixelShader) {
                chunk->DrawSolidLocal();
            } else if (!CMap::s_chunkVerticesWorldSpace) {
                chunk->DrawSolidLocal();
            } else {
                chunk->DrawSolidWorld();
            }
        }

        chunk->m_link.Unlink();
        CMap::s_activeRenderChunkList.LinkToTail(chunk);

        chunk = next;
    }
}

// ref: FUN_00793c30
// The chunks bound but not drawn (the reference's draw here is a nullsub): they still refresh
// their alpha textures and age like the rest
void CWorldScene::RenderHiddenChunks() {
    CWorldScene::SelectChunkShaders(0, 0);

    auto list = &CWorldScene::s_renderChunkLists[RENDER_LIST_HIDDEN];

    for (auto chunk = list->Head(); chunk; ) {
        auto next = list->Next(chunk);

        chunk->Bind(0);

        if (CWorld::s_enables & CWorld::Enables::Enable_2) {
            // FUN_005eeb70: nothing
        }

        chunk->m_link.Unlink();
        CMap::s_activeRenderChunkList.LinkToTail(chunk);

        chunk = next;
    }
}
