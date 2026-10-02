#include "model/CM2Shared.hpp"
#include <new>
#include "world/map/MapHorizonTable.hpp"
#include "gx/Draw.hpp"
#include "gx/Buffer.hpp"
#include "world/CWorldScene.hpp"
#include "world/map/LiquidMaterialSettings.hpp"
#include "world/Shadow.hpp"
#include "gx/Shader.hpp"
#include "world/map/Particulates.hpp"
#include "world/map/WaterRipples.hpp"
#include "world/map/CChunkLiquid.hpp"
#include "world/map/LiquidSurface.hpp"
#include "world/map/MapOcclusion.hpp"
#include "world/CWorld.hpp"
#include "world/ShadowMap.hpp"
#include "world/map/CMap.hpp"
#include "db/Db.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
#include "model/CM2Model.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/CGUnit_C.hpp"
#include "world/map/CMapEntity.hpp"
#include "world/map/CMapStaticEntity.hpp"
#include "world/CWFrustum.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/RenderState.hpp"
#include "gx/Transform.hpp"
#include "gx/shader/CGxShader.hpp"
#include "model/CM2Lighting.hpp"
#include "model/CM2Scene.hpp"
#include <tempest/Intersect.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>
#include <cstdlib>
#include <cmath>
#include <cstring>

static const float CHUNK_SIZE = 33.33333206176758f;
static const float CHUNKS_PER_UNIT = 0.0299999993f;         // DAT_00a3f7ec
static const float MAP_HALF_EXTENT = 17066.666f;
// How near the camera an out-of-sight doodad has to be to keep animating (DAT_009e8cc8).
static const float ANIMATE_RANGE_SQ = 100.0f;

STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) CWorldScene::s_renderChunkLists[CWorldScene::RENDER_LIST_COUNT];
CWorldScene::Row CWorldScene::s_rows[CWorldScene::ROW_COUNT];
STORM_EXPLICIT_LIST(CChunkLiquid, m_frameLink) CWorldScene::s_frameLiquidList;
STORM_EXPLICIT_LIST(DetailDoodad::CDetailDoodadData, m_frameLink) CWorldScene::s_frameDetailDoodadList;
STORM_EXPLICIT_LIST(CMapEntity, m_entityRowLink) CWorldScene::s_frameEntityList;
C4Plane CWorldScene::s_rowPlanes[CWorldScene::ROW_COUNT];
C3Vector CWorldScene::s_frustumCorners[8];
CWFrustum CWorldScene::s_frustums[CWorldScene::FRUSTUM_DEPTH_MAX];
CWFrustum CWorldScene::s_clipFrustum;
int32_t CWorldScene::s_frustumDepth;
C3Vector CWorldScene::s_cameraPos;
C3Vector CWorldScene::s_cameraTarget;
C3Vector CWorldScene::s_viewDir;
TSGrowableArray<C4Plane> CWorldScene::s_occlusionPlanes;
TSGrowableArray<CWorldScene::OcclusionVolume> CWorldScene::s_occlusionVolumes;
float CWorldScene::s_cameraLiquidDepth;
uint32_t CWorldScene::s_cameraLiquidType;
C4Plane CWorldScene::s_viewPlane;
C4Plane CWorldScene::s_viewPlane2d;
CAaBox CWorldScene::s_frustumBounds;
int32_t CWorldScene::s_frustumChunkRect[4];
int32_t CWorldScene::s_cameraQuadrant;
int32_t CWorldScene::s_targetQuadrant;
C44Matrix CWorldScene::s_viewMatrix;
C44Matrix CWorldScene::s_projMatrix;
C44Matrix CWorldScene::s_viewProjMatrix;
float CWorldScene::s_viewProjW[4];
C44Matrix CWorldScene::s_occlusionMatrix;
float CWorldScene::s_horizonBuffer[CWorldScene::HORIZON_COLUMNS];
uint8_t CWorldScene::s_horizonColumnFlags[CWorldScene::HORIZON_COLUMNS];
STORM_EXPLICIT_LIST(CWorldScene::Occluder, m_link) CWorldScene::s_freeOccluders;
STORM_EXPLICIT_LIST(CWorldScene::Occluder, m_link) CWorldScene::s_debugOccluders;
int32_t CWorldScene::s_visitingGroupEntities;
int32_t CWorldScene::s_visibleDoodadCount;
uint32_t CWorldScene::s_rowStats[0x60];
float CWorldScene::s_farChunkDistance;
float CWorldScene::s_nearChunkDistance;
float CWorldScene::s_occluderFarClip;
int32_t CWorldScene::s_visibleChunkCount;
int32_t CWorldScene::s_visibleMapObjCount;
int32_t CWorldScene::s_visibleEntityCount;
int32_t CWorldScene::s_visibleCount8624;
int32_t CWorldScene::s_frameStamp;
CMapObjDef* CWorldScene::s_cameraDef;
CMapObjDef* CWorldScene::s_cameraDefFlagged;
char CWorldScene::s_cameraAreaName[0x104];
char CWorldScene::s_cameraSubAreaName[0x40];
TSGrowableArray<uint32_t> CWorldScene::s_cameraGroupIndices;
TSGrowableArray<uint32_t> CWorldScene::s_cameraFlaggedGroupIndices;
float CWorldScene::s_cameraGroundHeight;
int32_t CWorldScene::s_hasMapObjs;
const char* CWorldScene::s_mapObjSkybox;
CWorldScene::ViewWindow CWorldScene::s_window;
CWorldScene::ViewWindow CWorldScene::s_portalWindow;
TSGrowableArray<CWorldScene::ViewWindow> CWorldScene::s_portalViews;
TSGrowableArray<CWorldScene::ViewWindow> CWorldScene::s_exteriorViews;
STORM_EXPLICIT_LIST(CMapObjDefGroup, m_renderLink) CWorldScene::s_visibleMapObjGroups;
STORM_EXPLICIT_LIST(CMapObjDefGroup, m_liquidQueueLink) CWorldScene::s_pendingLiquidGroups;
CMapObjDef* CWorldScene::s_visibleCallbackDef;
STORM_EXPLICIT_LIST(CMapObjDefGroup, m_rowLink) CWorldScene::s_mapObjDefGroupCandidates;
STORM_EXPLICIT_LIST(CMapEntity, m_hiddenLink) CWorldScene::s_hiddenEntities;
const int32_t CWorldScene::s_quadrantVertex[4] = { 0, 8, 0x88, 0x90 };

// The reference record is 0xfc bytes: 0xf4 of data and an 8-byte link. The link is
// wider here, so only the data part can match.
static_assert(offsetof(CWFrustum, unknownC0) == 0xc0, "a traversal frustum is six planes then eight corners");

// The box corner each of the eight corners takes from the min (0) or max (1) per axis
// (DAT_00adf3f4, DAT_00adf414, DAT_00adf434)
static const int32_t s_boxCornerX[8] = { 0, 1, 1, 0, 0, 1, 1, 0 };
static const int32_t s_boxCornerY[8] = { 0, 0, 1, 1, 0, 0, 1, 1 };
static const int32_t s_boxCornerZ[8] = { 0, 0, 0, 0, 1, 1, 1, 1 };
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
    // resets, the two records at DAT_00cd8610

    // The liquid module's configuration, with the arguments the reference's own call site passes
    // at 0x0079980e: a 1, a texture scale of 1.0, a 0, and an EMPTY procedural shader suffix --
    // the string at 0x009e14ff, which is a single NUL. This also takes the hold on the settings
    // bank that ReleaseMaterialSettings gives back.
    Liquid::Initialize(1, 1.0f, 0, "");

    CImVector grey = { 0x80, 0x80, 0x80, 0xFF };
    CWorldScene::s_solidTexture = TextureCreateSolid(grey);

    CImVector black = { 0x00, 0x00, 0x00, 0xFF };
    CWorldScene::s_blackTexture = TextureCreateSolid(black);
}

// ref: FUN_007d3e10
// The shaders for one permutation of the chunk lists (a specular layer; a layer carrying flag
// 0x80): the pixel shader per layer count, as far as the device has texture stages and the
// shader loaded, and the draw for chunks that have none.
void CWorldScene::SelectChunkShaders(int32_t specular, int32_t flag80) {
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
        auto shader = CMap::GetTerrainPixelShader(twoChunk, layers, shadowLevel, specular, flag80);

        if (layers <= GxCaps().m_numTmus && shader && shader->Valid()) {
            CWorldScene::s_layerPixelShaders[layers - 1] = shader;
        }
    }

    CWorldScene::s_chunkDraw = DrawChunkUnsupported;
}

// ref: FUN_007cfbe0
// The frame's share of the terrain vertex constants: the view transform (with its transpose),
// the native projection, the sun in view space and the fog ramp. The point-light block is
// cleared for the chunks to fill.
void CWorldScene::SetupTerrainConstants(const C44Matrix& world, const C44Matrix& view) {
    auto constants = &CWorldScene::s_terrainConstants;
    memset(constants, 0, sizeof(*constants));

    C44Matrix worldView = world * view;
    constants->view = worldView;
    constants->viewTransposed = worldView.Transpose();
    constants->proj = g_theGxDevicePtr->m_projNative;

    // Diverged: the reference negates the third row of the native projection here (its device
    // flag at +0x1b4 is never set, so it always does). frozen's view space is +z forward and its
    // native projection is built for that (GxuXformCreateProjection_Exact puts 1 in c3, and the
    // stand-in terrain shader drew correctly from view * m_projNative unchanged), so the
    // negation flipped clip z and D3D clipped every chunk: terrain invisible on the first run,
    // 2026-09-25. The reference's own view convention that makes the negation right there has
    // not been traced.

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

        // Diverged: frozen's D3D backend never reports the two capability fields the reference
        // gates on above (int134, int138), and fogs shader-drawn geometry through the
        // fixed-function table fog (CGxDeviceD3d::IStateSetD3dDefaults), so the ramp and the
        // enable go through the render states here as the stand-in did. The vertex shader's
        // own fog output is unused on that path.
        if (!GxCaps().int138) {
            bool fogActive = CWorld::GetFogEnd() > 1.0f && CWorld::GetFogStart() < CWorld::GetFarClip();
            g_theGxDevicePtr->RsSet(GxRs_FogStart, CWorld::GetFogStart());
            g_theGxDevicePtr->RsSet(GxRs_FogEnd, CWorld::GetFogEnd());
            g_theGxDevicePtr->RsSet(GxRs_FogColor, fogColor.value);
            g_theGxDevicePtr->RsSet(GxRs_Fog, fogActive ? 1 : 0);
        }
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

    // A list's low permutation bit is "a layer carries flag 0x80", its high bit the specular layer
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

// ----------------------------------------------------------------------------------------------
// The camera and its frustum

// ref: FUN_007912c0
// The plane through three points, facing along (b - a) x (c - a)
void PlaneFromPoints(C4Plane* plane, const C3Vector& a, const C3Vector& b, const C3Vector& c) {
    plane->n.x = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
    plane->n.y = (b.z - a.z) * (c.x - a.x) - (c.z - a.z) * (b.x - a.x);
    plane->n.z = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);

    float inv = 1.0f / sqrtf(plane->n.x * plane->n.x + plane->n.y * plane->n.y + plane->n.z * plane->n.z);
    float x = plane->n.x;
    plane->n.x = x * inv;
    float y = plane->n.y;
    plane->n.y = y * inv;
    float z = plane->n.z;
    plane->n.z = z * inv;
    plane->d = -(y * inv * a.y + a.x * x * inv + a.z * z * inv);
}


// ref: FUN_006bf6d0
// The eight corners of a view frustum in the space the view matrix maps from: the clip-space
// cube unprojected through the inverse view-projection. A perspective projection is walked at
// its near and far distances with w set to the view depth, so no divide is needed; an
// orthographic one at the unit cube. Near face first, each face (-x,-y) (-x,+y) (+x,+y) (+x,-y).
void FrustumCorners(const C44Matrix& view, const C44Matrix& proj, C3Vector* corners) {
    C44Matrix invView = view.Inverse(view.Determinant());
    C44Matrix invProj = proj.Inverse(proj.Determinant());
    C44Matrix inv = invProj * invView;

    C4Vector clip[8];

    if (2.38418579e-07f <= fabsf(proj.d3 - 1.0f)) {
        float nearW = -proj.d2 / (proj.c2 + 1.0f);
        float farW = -proj.d2 / (proj.c2 - 1.0f);
        float n = -nearW;

        clip[0] = { n, n, n, nearW };
        clip[1] = { n, nearW, n, nearW };
        clip[2] = { nearW, nearW, n, nearW };
        clip[3] = { nearW, n, n, nearW };
        float f = -farW;
        clip[4] = { f, f, farW, farW };
        clip[5] = { f, farW, farW, farW };
        clip[6] = { farW, farW, farW, farW };
        clip[7] = { farW, f, farW, farW };
    } else {
        clip[0] = { -1.0f, -1.0f, -1.0f, 1.0f };
        clip[1] = { -1.0f, 1.0f, -1.0f, 1.0f };
        clip[2] = { 1.0f, 1.0f, -1.0f, 1.0f };
        clip[3] = { 1.0f, -1.0f, -1.0f, 1.0f };
        clip[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
        clip[5] = { -1.0f, 1.0f, 1.0f, 1.0f };
        clip[6] = { 1.0f, 1.0f, 1.0f, 1.0f };
        clip[7] = { 1.0f, -1.0f, 1.0f, 1.0f };
    }

    for (int32_t i = 0; i < 8; i++) {
        C4Vector out;
        TransformVector4(&out, clip[i], inv);
        corners[i].x = out.x;
        corners[i].y = out.y;
        corners[i].z = out.z;
    }
}

// Every plane starts as (0, 0, 1, 0), the corners and the first nine words past them are zeroed,
// then the corners are copied in and the planes rebuilt from them.
//
// ref: FUN_00983fe0
CWFrustum::CWFrustum(const C3Vector* corners) {
    for (int32_t i = 0; i < 6; i++) {
        this->planes[i].n = { 0.0f, 0.0f, 1.0f };
        this->planes[i].d = 0.0f;
    }

    for (int32_t i = 0; i < 8; i++) {
        this->corners[i] = { 0.0f, 0.0f, 0.0f };
    }

    for (int32_t i = 0; i < 9; i++) {
        this->unknownC0[i] = 0;
    }

    for (int32_t i = 0; i < 8; i++) {
        this->corners[i] = corners[i];
    }

    this->ComputePlanes();
}

// ref: FUN_00984240
void CWFrustum::SetCorners(const C3Vector* corners) {
    for (int32_t i = 0; i < 8; i++) {
        this->corners[i] = corners[i];
    }

    this->ComputePlanes();
}

// ref: FUN_00983e70
// The four side planes and the far plane from the corners; the near plane is the far plane
// turned around through a near corner
void CWFrustum::ComputePlanes() {
    PlaneFromPoints(&this->planes[0], this->corners[1], this->corners[5], this->corners[6]);
    PlaneFromPoints(&this->planes[1], this->corners[0], this->corners[7], this->corners[4]);
    PlaneFromPoints(&this->planes[2], this->corners[0], this->corners[4], this->corners[5]);
    PlaneFromPoints(&this->planes[3], this->corners[3], this->corners[6], this->corners[7]);
    PlaneFromPoints(&this->planes[4], this->corners[5], this->corners[4], this->corners[6]);

    this->planes[5].n.x = -this->planes[4].n.x;
    this->planes[5].n.y = -this->planes[4].n.y;
    this->planes[5].n.z = -this->planes[4].n.z;
    this->planes[5].d = -(-this->planes[4].n.z * this->corners[2].z + this->corners[2].y * -this->planes[4].n.y + this->corners[2].x * -this->planes[4].n.x);
}

// ref: FUN_00983d20
// Non-zero while the sphere is not entirely behind any plane
int32_t CWFrustum::SphereInside(const CAaSphere& sphere) {
    int32_t last = 0;

    for (int32_t i = 0; i < 6; i++) {
        last = i;
        const C4Plane& p = this->planes[i];

        if (p.n.y * sphere.c.y + p.n.z * sphere.c.z + p.n.x * sphere.c.x + p.d < -sphere.r) {
            return 0;
        }
    }

    return last - 2;
}

// ref: FUN_00795400
// The scene's view of the camera for this update: its target and direction, the plane facing
// along the view and its flattened twin, the 64 row planes, the frustum corners in world space
// (from the device's view and projection), the base frustum, its bounds and the chunk
// rectangle they cover, and which way the target lies. Not ported yet: the portal window arrays
// (FUN_00794190), the horizon occlusion matrix (FUN_006bfe60) and the sound listener
// (FUN_009a81f0).
void CWorldScene::UpdateCamera(const C3Vector& cameraPos, const C3Vector& cameraTarget) {
    // if (DAT_00cd8610) FUN_005eeb70(): nothing

    CWorldScene::s_window = { 3.4028235e+38f, 3.4028235e+38f, -3.4028235e+38f, -3.4028235e+38f, -1.0f, nullptr, 0 };
    CWorldScene::s_portalWindow = { 3.4028235e+38f, 3.4028235e+38f, -3.4028235e+38f, -3.4028235e+38f, -1.0f, nullptr, 0 };
    CMapObj::s_sawExterior = 0;
    CWorldScene::s_mapObjSkybox = nullptr;
    CWorldScene::s_portalViews.SetCount(0);
    CWorldScene::s_exteriorViews.SetCount(0);

    CWorldScene::s_cameraPos = cameraPos;
    CWorldScene::s_cameraTarget = cameraTarget;

    C3Vector dir = {
        cameraTarget.x - cameraPos.x,
        cameraTarget.y - cameraPos.y,
        cameraTarget.z - cameraPos.z
    };
    float inv = 1.0f / sqrtf(dir.x * dir.x + dir.z * dir.z + dir.y * dir.y);
    CWorldScene::s_viewDir.x = dir.x * inv;
    CWorldScene::s_viewDir.y = dir.y * inv;
    CWorldScene::s_viewDir.z = dir.z * inv;

    CWorldScene::s_viewPlane.d = -(CWorldScene::s_viewDir.z * cameraPos.z + cameraPos.y * CWorldScene::s_viewDir.y + cameraPos.x * CWorldScene::s_viewDir.x);

    CWorldScene::s_occluderFarClip = CWorld::GetFarClip() - 100.0f;
    if (CWorldScene::s_occluderFarClip < 400.0f) {
        CWorldScene::s_occluderFarClip = 400.0f;
    }

    float z = 0.0f;
    float len2 = CWorldScene::s_viewDir.x * CWorldScene::s_viewDir.x + CWorldScene::s_viewDir.y * CWorldScene::s_viewDir.y;
    CWorldScene::s_viewPlane2d.n.x = CWorldScene::s_viewDir.x;
    CWorldScene::s_viewPlane2d.n.y = CWorldScene::s_viewDir.y;

    if (9.99999975e-05f < len2) {
        z = 1.0f / sqrtf(len2);
        CWorldScene::s_viewPlane2d.n.x = CWorldScene::s_viewDir.x * z;
        CWorldScene::s_viewPlane2d.n.y = CWorldScene::s_viewDir.y * z;
        z = z * 0.0f;
    }

    CWorldScene::s_viewPlane2d.n.z = z;
    CWorldScene::s_viewPlane2d.d = -(z * cameraPos.z + CWorldScene::s_viewPlane2d.n.y * cameraPos.y + cameraPos.x * CWorldScene::s_viewPlane2d.n.x);
    CWorldScene::s_viewPlane.n = CWorldScene::s_viewDir;

    C3Vector dir2d = { CWorldScene::s_viewPlane2d.n.x, CWorldScene::s_viewPlane2d.n.y, z };
    CWorldScene::BuildRowPlanes(dir2d, cameraPos);

    CWorldScene::s_viewMatrix = g_theGxDevicePtr->m_xforms[GxXform_View].m_mtx[g_theGxDevicePtr->m_xforms[GxXform_View].m_level];
    CWorldScene::s_projMatrix = g_theGxDevicePtr->m_projection;
    // TODO the device viewport copy at DAT_00cd8fb0

    FrustumCorners(CWorldScene::s_viewMatrix, CWorldScene::s_projMatrix, CWorldScene::s_frustumCorners);

    for (int32_t i = 0; i < 8; i++) {
        CWorldScene::s_frustumCorners[i].x += cameraPos.x;
        CWorldScene::s_frustumCorners[i].y += cameraPos.y;
        CWorldScene::s_frustumCorners[i].z += cameraPos.z;
    }

    CWorldScene::s_frustums[0].SetCorners(CWorldScene::s_frustumCorners);

    // The reference translates a matrix by -cameraPos here whose identity the decompilation
    // loses; the products below read the view and projection copies directly
    CWorldScene::s_viewProjMatrix = CWorldScene::s_viewMatrix * CWorldScene::s_projMatrix;
    CWorldScene::s_viewProjW[0] = CWorldScene::s_viewProjMatrix.a3;
    CWorldScene::s_viewProjW[1] = CWorldScene::s_viewProjMatrix.b3;
    CWorldScene::s_viewProjW[2] = CWorldScene::s_viewProjMatrix.c3;
    CWorldScene::s_viewProjW[3] = CWorldScene::s_viewProjMatrix.d3;

    CWorldScene::s_clipFrustum.SetCorners(CWorldScene::s_frustumCorners);

    BoundsFromPoints(CWorldScene::s_frustumBounds, CWorldScene::s_frustumCorners, 8);

    CWorldScene::s_frustumChunkRect[1] = static_cast<int32_t>(roundf(-(CWorldScene::s_frustumBounds.t.y - MAP_HALF_EXTENT) * CHUNKS_PER_UNIT - 0.5f));
    CWorldScene::s_frustumChunkRect[0] = static_cast<int32_t>(roundf(-(CWorldScene::s_frustumBounds.t.x - MAP_HALF_EXTENT) * CHUNKS_PER_UNIT - 0.5f));
    CWorldScene::s_frustumChunkRect[3] = static_cast<int32_t>(roundf(-(CWorldScene::s_frustumBounds.b.y - MAP_HALF_EXTENT) * CHUNKS_PER_UNIT - 0.5f));
    CWorldScene::s_frustumChunkRect[2] = static_cast<int32_t>(roundf(-(CWorldScene::s_frustumBounds.b.x - MAP_HALF_EXTENT) * CHUNKS_PER_UNIT - 0.5f));

    CWorldScene::s_frustumDepth = 0;
    CWorldScene::s_frustums[0].SetCorners(CWorldScene::s_frustumCorners);

    CWorldScene::s_cameraQuadrant = 0;
    if (cameraPos.x < cameraTarget.x) {
        CWorldScene::s_cameraQuadrant = 2;
    }
    if (cameraPos.y < cameraTarget.y) {
        CWorldScene::s_cameraQuadrant++;
    }

    CWorldScene::s_targetQuadrant = 0;
    if (cameraTarget.x < cameraPos.x) {
        CWorldScene::s_targetQuadrant = 2;
    }
    if (cameraTarget.y < cameraPos.y) {
        CWorldScene::s_targetQuadrant++;
    }

    CWorldScene::s_viewPlane.n = CWorldScene::s_viewDir;
    CWorldScene::s_viewPlane.d = -(CWorldScene::s_viewDir.z * cameraPos.z + cameraPos.y * CWorldScene::s_viewDir.y + cameraPos.x * CWorldScene::s_viewDir.x);

    // The horizon occlusion matrix: what ShadeHorizon projects a chunk's silhouette through to
    // find the screen columns it covers. It was identity, and identity is not harmless here --
    // the projection then leaves world coordinates, so every ridge raised the skyline at a
    // column derived from dividing one world coordinate by another. The horizon test was running
    // the whole time on that.
    //
    // The reference builds it the same way it builds any view: look along the FLATTENED view
    // direction from the origin with +Z up, translate by minus the camera, and multiply by the
    // projection. The flattening is the point -- the horizon is a skyline, so pitch must not
    // tilt it. s_viewPlane2d.n already holds that direction, normalized, with z zero.
    //
    // A camera looking straight up or down has no flattened direction to aim along, which is
    // what the length test catches; there the reference leaves identity, and so does this.
    if (len2 <= 9.99999975e-05f) {
        C44Matrix identity;
        CWorldScene::s_occlusionMatrix = identity;
    } else {
        C3Vector eye = { 0.0f, 0.0f, 0.0f };
        C3Vector target = { CWorldScene::s_viewPlane2d.n.x, CWorldScene::s_viewPlane2d.n.y, 0.0f };
        C3Vector up = { 0.0f, 0.0f, 1.0f };

        C44Matrix flattened;
        MatrixLookAt(flattened, eye, target, up);

        C3Vector back = { -cameraPos.x, -cameraPos.y, -cameraPos.z };
        flattened.Translate(back);

        CWorldScene::s_occlusionMatrix = flattened * CWorldScene::s_projMatrix;
    }

    // TODO FUN_009a81f0(PushSecondsUntil()): the sound listener
}

// ref: FUN_007906c0
// The front plane of each of the 64 distance rows: the flattened view direction, a chunk
// further along it per row
void CWorldScene::BuildRowPlanes(const C3Vector& dir, const C3Vector& cameraPos) {
    float distance = 0.0f;

    for (uint32_t i = 0; i < ROW_COUNT; i++) {
        auto plane = &CWorldScene::s_rowPlanes[i];
        plane->n = dir;
        plane->d = -((cameraPos.y + distance * dir.y) * dir.y + (distance * dir.z + cameraPos.z) * dir.z + dir.x * (cameraPos.x + dir.x * distance));
        distance += CHUNK_SIZE;
    }
}

// ref: FUN_0078fb20
int32_t CWorldScene::BoxOutsideFrustum(const CAaBox& box) {
    return AaBoxVsPlanes6(CWorldScene::s_frustums[CWorldScene::s_frustumDepth].planes, box) == 0;
}

// ref: FUN_0078fb60
// Which of the five detail bands a distance along the view falls in
int32_t CWorldScene::DistanceBand(float distance) {
    const WorldDetailBands& bands = CWorld::GetDetailBands();

    if (distance < 0.0f) {
        return 0;
    }

    float sq = distance * distance;

    if (sq < bands.farDistSq[0]) {
        return 0;
    }
    if (sq < bands.farDistSq[1]) {
        return 1;
    }
    if (sq < bands.farDistSq[2]) {
        return 2;
    }
    if (bands.farDistSq[3] <= sq) {
        return 4;
    }

    return 3;
}

// ref: FUN_0078fdc0
// Whether the horizon buffer hides a box: its corners project into screen columns, and it is
// occluded (2) when every column's horizon stands above the box's highest projected point.
// Nothing feeds the buffer yet, so everything is visible.
uint32_t CWorldScene::BoxOccluded(const CAaBox& box, uint32_t flags) {
    if (!(CWorld::s_enables & CWorld::Enables::Enable_Culling) || CWorldScene::s_viewDir.z < -0.899999976f || 0.899999976f < CWorldScene::s_viewDir.z) {
        return 0;
    }

    float minX = 3.4028235e+38f;
    float maxX = -3.4028235e+38f;
    float maxY = -3.4028235e+38f;
    const C3Vector* ends[2] = { &box.b, &box.t };

    for (int32_t i = 0; i < 8; i++) {
        C4Vector corner = { ends[s_boxCornerX[i]]->x, ends[s_boxCornerY[i]]->y, ends[s_boxCornerZ[i]]->z, 1.0f };
        C4Vector p;
        TransformVector4(&p, corner, CWorldScene::s_occlusionMatrix);

        if (!(flags & 0x8) && p.z < 50.0f) {
            return 0;
        }

        float inv = 1.0f / p.z;
        float sx = p.x * inv;
        float sy = p.y * inv;

        if (sx < minX) {
            minX = sx;
        }
        if (maxX < sx) {
            maxX = sx;
        }
        if (maxY < sy) {
            maxY = sy;
        }
    }

    int32_t first = static_cast<int32_t>(roundf(minX * 64.0f - 0.5f)) + 0xc0;
    int32_t last = static_cast<int32_t>(roundf(maxX * 64.0f - 0.5f)) + 0xc1;

    if (first < static_cast<int32_t>(HORIZON_COLUMNS) && -1 < last) {
        if (first < 0) {
            first = 0;
        }
        if (static_cast<int32_t>(HORIZON_COLUMNS) - 1 < last) {
            last = HORIZON_COLUMNS - 1;
        }

        for (; first <= last; first++) {
            if (CWorldScene::s_horizonBuffer[first] < maxY) {
                return 0;
            }
        }

        return 2;
    }

    return 0;
}

// ref: FUN_0078fc40
// The sphere form of BoxOccluded: the centre projects to a column, the radius through the
// projection to a half-width
uint32_t CWorldScene::SphereOccluded(const C3Vector& center, float radius, uint32_t flags) {
    if (!(CWorld::s_enables & CWorld::Enables::Enable_Culling) || fabsf(radius) < 2.38418579e-07f || CWorldScene::s_viewDir.z < -0.899999976f || 0.899999976f < CWorldScene::s_viewDir.z) {
        return 0;
    }

    C4Vector c = { center.x, center.y, center.z, 1.0f };
    C4Vector p;
    TransformVector4(&p, c, CWorldScene::s_occlusionMatrix);

    C4Vector r = { radius, radius, 0.0f, 0.0f };
    C4Vector pr;
    TransformVector4(&pr, r, CWorldScene::s_projMatrix);

    if (!(flags & 0x8) && p.z < 50.0f) {
        return 0;
    }

    float inv = 1.0f / p.z;
    int32_t first = static_cast<int32_t>(roundf((p.x * inv - pr.y * inv) * 64.0f - 0.5f)) + 0xc0;
    int32_t last = static_cast<int32_t>(roundf((pr.y * inv + p.x * inv) * 64.0f - 0.5f)) + 0xc1;

    if (first < static_cast<int32_t>(HORIZON_COLUMNS) && -1 < last) {
        if (first < 0) {
            first = 0;
        }
        if (static_cast<int32_t>(HORIZON_COLUMNS) - 1 < last) {
            last = HORIZON_COLUMNS - 1;
        }

        for (; first <= last; first++) {
            if (CWorldScene::s_horizonBuffer[first] < p.y * inv + pr.x * inv) {
                return 0;
            }
        }

        return 2;
    }

    return 0;
}

// ref: FUN_007cce00
// Whether a sphere sits entirely behind one of the occlusion volumes (DAT_00d2dcf0, plane
// ranges into DAT_00d2dce0). No volumes are registered yet, so the reference's own early-out
// applies.
// ref: FUN_007cce00
// A sphere is hidden by a volume when it sits fully behind every one of that volume's planes --
// one plane it is in front of is enough to see it past. The radius is added to the signed
// distance so a sphere only grazing a plane still counts as in front.
//
// The volumes come from the low-detail terrain (the same lazy setup FUN_007cd850 drives), which
// frozen has not ported, so the list is empty and this answers no to everything. That is the
// safe direction: nothing is wrongly culled while it stands.
int32_t CWorldScene::SphereOccludedByVolumes(const CAaSphere& sphere) {
    for (uint32_t v = 0; v < CWorldScene::s_occlusionVolumes.Count(); v++) {
        const auto& volume = CWorldScene::s_occlusionVolumes[v];

        int32_t behind = 0;

        while (behind < volume.planeCount) {
            const auto& plane = CWorldScene::s_occlusionPlanes[volume.firstPlane + behind];

            float distance = plane.n.x * sphere.c.x
                           + plane.n.y * sphere.c.y
                           + plane.n.z * sphere.c.z
                           + plane.d
                           + sphere.r;

            if (distance > 0.0f) {
                break;
            }

            behind++;
        }

        if (behind == volume.planeCount) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_00790650
// The corner of a box on the camera's side of the target along each axis
void CWorldScene::BoxNearPoint(const CAaBox& box, C3Vector* point) {
    const C3Vector& cameraPos = CWorld::GetCameraPos();

    point->x = cameraPos.x <= CWorldScene::s_cameraTarget.x ? box.b.x : box.t.x;
    point->y = cameraPos.y <= CWorldScene::s_cameraTarget.y ? box.b.y : box.t.y;

    if (CWorldScene::s_cameraTarget.z < cameraPos.z) {
        point->z = box.t.z;
        return;
    }

    point->z = box.b.z;
}

// ref: FUN_00790520
// One of the chunk's 145 vertices in world space
void CWorldScene::ChunkVertexPoint(const CMapChunk* chunk, int32_t vertex, C3Vector* point) {
    point->x = CMapChunk::s_vertexTable[vertex][0] + chunk->m_position.x;
    point->y = CMapChunk::s_vertexTable[vertex][1] + chunk->m_position.y;
    point->z = chunk->m_heights[vertex] + chunk->m_position.z;
}

// ref: FUN_00790620
float CWorldScene::ViewPlane2dDistance(const C3Vector& point) {
    return point.x * CWorldScene::s_viewPlane2d.n.x + point.z * CWorldScene::s_viewPlane2d.n.z + point.y * CWorldScene::s_viewPlane2d.n.y + CWorldScene::s_viewPlane2d.d;
}

// ref: FUN_00792d80
// Puts a chunk in the distance row its nearest vertex falls in (behind the camera counts as
// the first row); one past the last row is dropped
void CWorldScene::BucketChunk(CMapChunk* chunk, const C3Vector& point) {
    float distance = point.x * CWorldScene::s_viewPlane2d.n.x + point.z * CWorldScene::s_viewPlane2d.n.z + point.y * CWorldScene::s_viewPlane2d.n.y + CWorldScene::s_viewPlane2d.d;
    int32_t row = 0;

    if (distance <= 0.0f || (row = static_cast<int32_t>(roundf(distance * CHUNKS_PER_UNIT - 0.5f))) < static_cast<int32_t>(ROW_COUNT)) {
        CWorldScene::s_rows[row].chunks.LinkToTail(chunk);
    }
}

// ref: FUN_00792df0
// The same banding as BucketChunk, for one liquid layer rather than a whole chunk. The caller
// picks the point: the chunk's nearest vertex, with the height pulled into the layer's own range.
void CWorldScene::AddLiquid(CChunkLiquid* liquid, const C3Vector& center) {
    float distance = center.x * CWorldScene::s_viewPlane2d.n.x + center.z * CWorldScene::s_viewPlane2d.n.z + center.y * CWorldScene::s_viewPlane2d.n.y + CWorldScene::s_viewPlane2d.d;
    int32_t row = 0;

    if (distance <= 0.0f || (row = static_cast<int32_t>(roundf(distance * CHUNKS_PER_UNIT - 0.5f))) < static_cast<int32_t>(ROW_COUNT)) {
        CWorldScene::s_rows[row].liquids.LinkToTail(liquid);
    }
}

// ref: FUN_00792e60
// Where one entity goes for the frame. An entity whose model has nothing to draw, or that has
// asked to be left out, is set aside as hidden; everything else is filed under the distance row
// its nearest corner falls in. Anything past the last row is simply dropped, which is how the
// far clip removes entities without a test of its own.
//
// The sort distance is taken along the real view plane, not the flattened one the rows use, so
// two entities in the same row still sort against each other correctly.
void CWorldScene::AddEntity(CMapEntity* entity) {
    uint32_t flags = entity->m_flags7c;

    // Bit 0x4000 takes an entity out of the scene, but not while it is carrying something: a
    // model with attachments still has to be placed, or whatever is hanging off it goes with
    // it. The reference reads the model without checking there is one; frozen checks.
    if (!(flags & 0x4000) || (entity->m_model && entity->m_model->m_attachList)) {
        C3Vector nearPoint = { 0.0f, 0.0f, 0.0f };

        CWorldScene::BoxNearPoint(entity->m_bounds, &nearPoint);

        entity->m_visible = 2;
        entity->m_sortDistance = nearPoint.x * CWorldScene::s_viewPlane.n.x
                               + nearPoint.y * CWorldScene::s_viewPlane.n.y
                               + nearPoint.z * CWorldScene::s_viewPlane.n.z
                               + CWorldScene::s_viewPlane.d;

        if (!(flags & 0x1)) {
            float distance = nearPoint.x * CWorldScene::s_viewPlane2d.n.x
                           + nearPoint.y * CWorldScene::s_viewPlane2d.n.y
                           + nearPoint.z * CWorldScene::s_viewPlane2d.n.z
                           + CWorldScene::s_viewPlane2d.d;

            int32_t row = 0;

            if (distance <= 0.0f
                || (row = static_cast<int32_t>(roundf(distance * CHUNKS_PER_UNIT - 0.5f))) < static_cast<int32_t>(ROW_COUNT)) {
                CWorldScene::s_rows[row].entities.LinkToHead(entity);
            }

            return;
        }
    }

    CWorldScene::s_hiddenEntities.LinkToTail(entity);
}

// ref: FUN_007935a0
// Every liquid layer this row collected moves to the frame list, which is what the draw walks
// later; the row's link and the frame list's are the same one, so the move is a relink and the
// next pointer has to be read first.
//
// On the way, a layer that survives the frustum and both occlusion tests gets its surface woken
// for the frame. The reference guards that half behind a world flag which is set once at startup
// and never cleared, so only this path is live and frozen does not carry the flag.
void CWorldScene::TraverseRowLiquids(Row* row) {
    auto liquid = row->liquids.Head();

    while (liquid) {
        auto next = row->liquids.Next(liquid);

        CWorldScene::s_frameLiquidList.LinkToTail(liquid);

        CAaBox box;
        liquid->GetBounds(&box);

        // THREE GATES, in the reference's order: the clip frustum, the box occluders, and then
        // the layer's bounding sphere against the occlusion VOLUMES. The third was a TODO here
        // naming FUN_007ce520 and FUN_007cce00; the first is now CChunkLiquid::GetBoundingSphere
        // and the second has been ported as SphereOccludedByVolumes for a while, so the note was
        // half stale. The sphere is the cheaper test and the reference still runs it LAST, which
        // is worth preserving rather than tidying: the volumes are a coarser structure than the
        // box occluders and reject less, so asking them first would cost more than it saves.
        if (AaBoxVsPlanes6(CWorldScene::s_clipFrustum.planes, box)
            && !CWorldScene::BoxOccluded(box, 0)
            && !CWorldScene::SphereOccludedByVolumes(liquid->GetBoundingSphere())) {
            liquid->UpdateForFrame();

            // Then the surface joins this frame's queue. Which bucket it lands in is the
            // settings' business, not this caller's. ref: the call at 0x00793740
            if (liquid->m_surface) {
                Liquid::Add(liquid->m_surface);
            }
        }

        liquid = next;
    }
}

// ref: FUN_0078fae0
void CWorldScene::GetFrustumCorners(C3Vector* corners) {
    for (int32_t i = 0; i < 8; i++) {
        corners[i] = CWorldScene::s_frustumCorners[i];
    }
}

// ref: FUN_00790af0
// Narrows the current frustum to a window of the screen: the near and far faces' corners are
// interpolated across the window's rectangle
void CWorldScene::SubFrustum(const C3Vector* src, const ViewWindow* window) {
    C3Vector corners[8];

    for (int32_t face = 0; face < 8; face += 4) {
        const C3Vector* c = &src[face];
        C3Vector* out = &corners[face];

        C3Vector e12 = { c[2].x - c[1].x, c[2].y - c[1].y, c[2].z - c[1].z };
        C3Vector a = { e12.x * window->minY + c[1].x, c[1].y + e12.y * window->minY, e12.z * window->minY + c[1].z };
        C3Vector b = { e12.x * window->maxY + c[1].x, e12.y * window->maxY + c[1].y, window->maxY * e12.z + c[1].z };

        C3Vector e03 = { c[3].x - c[0].x, c[3].y - c[0].y, c[3].z - c[0].z };
        C3Vector cc = { e03.x * window->minY + c[0].x, e03.y * window->minY + c[0].y, c[0].z + e03.z * window->minY };
        C3Vector d = { e03.x * window->maxY + c[0].x, e03.y * window->maxY + c[0].y, c[0].z + e03.z * window->maxY };

        C3Vector ac = { a.x - cc.x, a.y - cc.y, a.z - cc.z };
        out[0] = { ac.x * window->minX + cc.x, ac.y * window->minX + cc.y, ac.z * window->minX + cc.z };
        out[1] = { ac.x * window->maxX + cc.x, ac.y * window->maxX + cc.y, ac.z * window->maxX + cc.z };

        C3Vector bd = { b.x - d.x, b.y - d.y, b.z - d.z };
        out[3] = { bd.x * window->minX + d.x, bd.y * window->minX + d.y, bd.z * window->minX + d.z };
        out[2] = { bd.x * window->maxX + d.x, bd.y * window->maxX + d.y, d.z + window->maxX * bd.z };
    }

    CWorldScene::s_frustums[CWorldScene::s_frustumDepth].SetCorners(corners);
}

// ref: FUN_007d6690
// Whether a chunk rectangle ({minRow, minCol, maxRow, maxCol}) overlaps the one the frustum
// bounds cover
int32_t CWorldScene::ChunkRectInView(const int32_t* rect) {
    if (rect[1] <= CWorldScene::s_frustumChunkRect[3] && rect[0] <= CWorldScene::s_frustumChunkRect[2] && CWorldScene::s_frustumChunkRect[1] <= rect[3] && CWorldScene::s_frustumChunkRect[0] <= rect[2]) {
        return 1;
    }

    return 0;
}

// ref: FUN_0079a790
// The visibility traversal through a window of the screen: one frustum level deeper, narrowed
// to the window, then every distance row near to far. Only the chunks are visited so far; the
// low-detail areas (FUN_007cd850, FUN_00791980), map object defs (FUN_0079a160), liquids
// (FUN_007935a0), entities (FUN_00793060, FUN_007987a0) and occluders (FUN_00793760) are not
// ported yet.
void CWorldScene::Traverse(const ViewWindow* window, int32_t portal) {
    // The distant occluders, rebuilt for THIS traversal. The reference does it here and first,
    // not in CMap::Render, which is where frozen had it.
    //
    // Being precise about why, because the first version of this comment overstated it: CMap::Render
    // does NOT call Traverse twice. It has an outdoor branch and an indoor branch on s_cameraDef
    // and takes one of them, so Traverse runs once a frame from there. Traverse has a second
    // caller elsewhere, and building the volumes inside it is simply where the reference puts
    // them -- each traversal against the corners it was handed.
    MapOcclusion::BuildVolumes(CWorldScene::s_cameraPos, CWorldScene::s_frustumCorners, 0);

    CWorldScene::s_frustumDepth++;
    CWorldScene::s_frustums[CWorldScene::s_frustumDepth] = CWorldScene::s_frustums[CWorldScene::s_frustumDepth - 1];
    CWorldScene::SubFrustum(CWorldScene::s_frustumCorners, window);

    for (uint32_t i = 0; i < ROW_COUNT; i++) {
        auto row = &CWorldScene::s_rows[i];

        CWorldScene::TraverseRowChunks(row, i);
        CWorldScene::TraverseRowMapObjDefs(row, window, portal);
        CWorldScene::TraverseRowLiquids(row);
        CWorldScene::TraverseRowEntities(row);

        int32_t band = CWorldScene::DistanceBand(static_cast<float>(i) * CHUNK_SIZE);

        CWorldScene::TraverseRowStaticEntities(row, band);
        CWorldScene::TraverseRowOccluders(row);
    }

    // TODO FUN_00791980(window)

    CWorldScene::s_frustumDepth--;
}

// ref: FUN_00799d40
// The chunks of one distance row: each leaves the occluder list it may be on, is tested against
// the frustum and the horizon (bounds first, then the extended bounds), is built for drawing and
// put on the render list its render chunk's layers select, and, with culling on, joins the row's
// occluders when it is solid or starts within the rows. The doodads hanging off each chunk
// (FUN_00799980) are not visited yet.
void CWorldScene::TraverseRowChunks(Row* row, uint32_t rowIndex) {
    C3Vector point = { 0.0f, 0.0f, 0.0f };
    int32_t culling = (CWorld::s_enables & CWorld::Enables::Enable_Culling) && rowIndex <= 0x3e;
    const CWFrustum& frustum = CWorldScene::s_frustums[CWorldScene::s_frustumDepth];

    for (auto chunk = row->chunks.Head(); chunk; ) {
        auto next = row->chunks.Next(chunk);

        chunk->m_frameLink.Unlink();

        if (!AaBoxVsPlanes6(frustum.planes, chunk->m_bounds)) {
            chunk = next;
            continue;
        }

        CAaSphere sphere = { chunk->m_center, chunk->m_radius };
        if (CWorldScene::SphereOccludedByVolumes(sphere)) {
            chunk = next;
            continue;
        }

        if (CWorldScene::BoxOccluded(chunk->m_bounds, 0)) {
            chunk = next;
            continue;
        }

        int32_t band = CWorldScene::DistanceBand(chunk->m_sortDistance);
        CWorldScene::TraverseChunkDoodads(&chunk->m_entityLinkList, band);
        (void)band;

        if (!AaBoxVsPlanes6(frustum.planes, chunk->m_bounds2)) {
            chunk = next;
            continue;
        }

        if (CWorldScene::BoxOccluded(chunk->m_bounds2, 0)) {
            chunk = next;
            continue;
        }

        if (chunk->m_header->holes != 0xFFFF) {
            CWorldScene::s_visibleChunkCount++;
            chunk->PrepareRender();

            auto renderChunk = chunk->m_renderChunk;

            if (renderChunk) {
                uint32_t layers = (renderChunk->m_flags & 0x8) ? renderChunk->m_layerCount : 0;
                uint32_t permutation = 0;

                if (layers) {
                    uint32_t flags = renderChunk->m_flags10;

                    if (flags & 0x1) {
                        permutation = ((flags & 0x4) | 0x2) >> 1;
                    } else {
                        permutation = (flags >> 1) & 0x2;
                    }
                }

                CWorldScene::s_renderChunkLists[permutation + layers * 4].LinkToTail(renderChunk);
            }
        }

        if (culling) {
            bool occluder = chunk->m_header->holes != 0;

            if (!occluder) {
                CWorldScene::ChunkVertexPoint(chunk, CWorldScene::s_quadrantVertex[CWorldScene::s_targetQuadrant], &point);
                float distance = CWorldScene::ViewPlane2dDistance(point);

                if (distance <= 0.0f) {
                    occluder = true;
                } else {
                    float rows = distance * CHUNKS_PER_UNIT;
                    occluder = static_cast<int32_t>(roundf(rows - 0.5f)) < static_cast<int32_t>(ROW_COUNT);
                }
            }

            if (occluder) {
                CWorldScene::s_rows[rowIndex].occluderChunks.LinkToTail(chunk);
            }
        }

        chunk = next;
    }
}

// ----------------------------------------------------------------------------------------------
// The map objects of one distance row

// ref: FUN_00799310
// A group the traversal reached: it joins the frame's visible list once, takes its distance along
// the camera forward, and tells the frame that some building wants the exterior lit pass.
void CWorldScene::MarkMapObjGroupVisible(uint32_t groupIndex, CMapObjDef* def) {
    auto defGroup = def->m_defGroups[groupIndex];
    auto group = def->m_mapObj->GetGroup(defGroup->m_groupIndex, 0);

    if (!defGroup->m_renderLink.IsLinked()) {
        CWorldScene::s_visibleMapObjGroups.LinkToTail(defGroup);

        // A group carrying MLIQ joins the pending-liquid list so the liquid pass can build its
        // surface. The link offset the TODO here was waiting on is +0xb8, recovered from the queue
        // that drains this list and unlinks through it.
        if (group && (group->m_flags & 0x1000) && (CWorld::s_enables & 0x1000000)) {
            CWorldScene::s_pendingLiquidGroups.LinkToTail(defGroup);
        }

        C3Vector point = { 0.0f, 0.0f, 0.0f };
        CWorldScene::BoxNearPoint(defGroup->m_bounds, &point);

        const C4Plane& plane = CWorldScene::s_viewPlane;
        defGroup->m_sortDistance = plane.n.x * point.x + plane.n.y * point.y + plane.n.z * point.z + plane.d;

        if (group && (group->m_flags & 0x40)) {
            CWorldScene::s_hasMapObjs = 1;
        }

        defGroup->m_flags &= ~0x8000u;
    }

    // The portal walk sets this while it is inside a room, so a group first reached from in
    // there is marked as seen from inside.
    if (CMapObj::s_interiorFog) {
        defGroup->m_flags |= 0x8000;
    }

    // What the view had been narrowed to when the traversal arrived. A group reached through
    // two doorways collects two of these and draws once per doorway, clipped to each.
    auto record = CWFrustum::Alloc();

    if (record) {
        *record = CWorldScene::s_frustums[CWorldScene::s_frustumDepth];

        defGroup->m_frustums.LinkToTail(record);
    }
}

// ref: FUN_007b3a10
// One placed group, seen through a window of the screen. The frustum goes one level deeper and
// narrows to the window; a group that survives it is either handed to the visible list or, when
// it is an interior, walked into through its portals.
void CWorldScene::VisitMapObjDefGroup(CMapObjDef* def, CMapObjDefGroup* defGroup, const ViewWindow* window, int32_t portal) {
    CWorldScene::s_visibleCallbackDef = def;

    CWorldScene::s_frustumDepth++;
    CWorldScene::s_frustums[CWorldScene::s_frustumDepth] = CWorldScene::s_frustums[CWorldScene::s_frustumDepth - 1];
    CWorldScene::SubFrustum(CWorldScene::s_frustumCorners, window);

    uint32_t flags = def->m_mapObj->GroupFlags(defGroup->m_groupIndex);

    if (!CWorldScene::BoxOutsideFrustum(defGroup->m_bounds)) {
        if (portal || !CWorldScene::BoxOccluded(defGroup->m_bounds, 1)) {

            if (flags & 0x10000) {
                // The group draws on its own: hand it over once its file is in
                if (def->m_mapObj->GetGroup(defGroup->m_groupIndex, 0)) {
                    CWorldScene::MarkMapObjGroupVisible(defGroup->m_groupIndex, def);
                }
            } else if (flags & 0x8) {
                // The ordinary case: step into the building through this group and follow its
                // doorways. The walk marks the group itself and then every room it can reach.
                // The window is nudged in by a texel either way, as the reference does.
                float ndc[4] = {
                    window->minX * 2.0f - 1.0f,
                    window->minY * 2.0f - 1.0f,
                    window->maxX * 2.0f - 1.0f,
                    window->maxY * 2.0f - 1.0f
                };

                CMapObj::SetVisibleCallback(&CWorldScene::MarkMapObjGroupVisible, def);

                CMapObj::s_walkDef = def;
                CMapObj::s_portalPlacement = def->m_placement;

                def->m_mapObj->WalkFromOutside(def->m_placement, def->m_inversePlacement,
                                               CWorldScene::s_cameraPos, CWorldScene::s_cameraTarget,
                                               ndc, defGroup->m_groupIndex);
            }
        }
    }

    CWorldScene::s_frustumDepth--;
}

// ref: FUN_0079a160
// Every placed group bucketed into this distance row: each leaves the row, and the ones the
// frustum, the occlusion volumes and the horizon all keep are visited.
void CWorldScene::TraverseRowMapObjDefs(Row* row, const ViewWindow* window, int32_t portal) {
    for (auto defGroup = row->mapObjDefGroups.Head(); defGroup; ) {
        auto next = row->mapObjDefGroups.Next(defGroup);

        // The row holds it by m_rowLink, which is the one the bucketing used.
        defGroup->m_rowLink.Unlink();

        CAaSphere sphere = { defGroup->m_center, defGroup->m_radius };

        if (AaBoxVsPlanes6(CWorldScene::s_frustums[CWorldScene::s_frustumDepth].planes, defGroup->m_bounds)
            && !CWorldScene::SphereOccludedByVolumes(sphere)
            && !CWorldScene::BoxOccluded(defGroup->m_bounds, 1)) {

            auto def = static_cast<CMapObjDef*>(defGroup->m_parentLinkList.Head()->ref);
            CWorldScene::VisitMapObjDefGroup(def, defGroup, window, portal);
            CWorldScene::BucketGroupDoodads(&defGroup->m_doodadDefLinkList, static_cast<uint32_t>(row - CWorldScene::s_rows));
        }

        defGroup = next;
    }
}

// ref: FUN_00792ad0
// A group the def update found in reach: one that draws at all becomes a candidate for this
// frame, either unconditionally when its def is flagged always-visible, or when it falls inside
// the sixty-four distance rows.
void CWorldScene::BucketMapObjDefGroup(CMapObjDef* def, CMapObjDefGroup* defGroup) {
    uint32_t flags = def->m_mapObj->GroupFlags(defGroup->m_groupIndex);

    if (!(flags & 0x10008)) {
        return;
    }

    if (def->m_flags & 0x400) {
        CWorldScene::s_mapObjDefGroupCandidates.LinkToTail(defGroup);
        return;
    }

    C3Vector point = { 0.0f, 0.0f, 0.0f };
    CWorldScene::BoxNearPoint(defGroup->m_bounds, &point);

    const C4Plane& plane = CWorldScene::s_viewPlane;
    defGroup->m_sortDistance = plane.n.z * point.z + plane.n.y * point.y + plane.n.x * point.x + plane.d;

    float distance = CWorldScene::ViewPlane2dDistance(point);

    if (distance <= 0.0f || static_cast<int32_t>(roundf(distance * CHUNKS_PER_UNIT - 0.5f)) < static_cast<int32_t>(ROW_COUNT)) {
        CWorldScene::s_mapObjDefGroupCandidates.LinkToTail(defGroup);
    }
}

// ref: FUN_00792bd0
// The candidates spread over the distance rows, near to far, so the traversal meets each at the
// right depth. One past the last row ends the walk, as the reference leaves it.
void CWorldScene::BucketMapObjDefGroups() {
    for (auto defGroup = CWorldScene::s_mapObjDefGroupCandidates.Head(); defGroup; ) {
        auto next = CWorldScene::s_mapObjDefGroupCandidates.Next(defGroup);

        defGroup->m_rowLink.Unlink();

        C3Vector point = { 0.0f, 0.0f, 0.0f };
        CWorldScene::BoxNearPoint(defGroup->m_bounds, &point);

        const C4Plane& plane = CWorldScene::s_viewPlane;
        defGroup->m_sortDistance = plane.n.x * point.x + plane.n.y * point.y + plane.n.z * point.z + plane.d;

        float distance = CWorldScene::ViewPlane2dDistance(point);
        int32_t row = 0;

        if (0.0f < distance) {
            row = static_cast<int32_t>(roundf(distance * CHUNKS_PER_UNIT - 0.5f));

            if (static_cast<int32_t>(ROW_COUNT) - 1 < row) {
                return;
            }
        }

        CWorldScene::s_rows[row].mapObjDefGroups.LinkToTail(defGroup);

        defGroup = next;
    }
}

// ----------------------------------------------------------------------------------------------
// The map object pass

// ref: FUN_007964a0
// Every map object group the traversal reached, drawn. Each takes the view down one level of
// portal recursion, sets its instance transform and lighting, hands itself to CMapObj::Render,
// and gives its frustum records back.
void CWorldScene::RenderMapObjs() {
    // is behaving.

    GxRsPush();

    CWorldScene::s_frustumDepth++;
    CWorldScene::s_frustums[CWorldScene::s_frustumDepth] =
        CWorldScene::s_frustums[CWorldScene::s_frustumDepth - 1];

    ShadowMapBindScene();

    for (auto defGroup = CWorldScene::s_visibleMapObjGroups.Head(); defGroup; ) {
        auto next = CWorldScene::s_visibleMapObjGroups.Next(defGroup);

        CWorldScene::s_visibleMapObjGroups.UnlinkNode(defGroup);

        // The def that placed this group is the owner of its one parent link.
        auto parentLink = defGroup->m_parentLinkList.Head();
        auto def = parentLink ? static_cast<CMapObjDef*>(parentLink->ref) : nullptr;

        if (def && def->m_mapObj && (CWorld::s_enables & 0x100)) {
            // The instance's transform with the camera translation already folded in, so the
            // vertex program sees local coordinates.
            C44Matrix toCamera;
            toCamera.Identity();

            C3Vector back = {
                -CWorldScene::s_cameraPos.x,
                -CWorldScene::s_cameraPos.y,
                -CWorldScene::s_cameraPos.z
            };
            toCamera.Translate(back);

            C44Matrix worldView = def->m_placement * toCamera;
            CMapObj::SetInstanceTransform(worldView);

            // A def placed with a negative scale draws its faces the other way round.
            int32_t flipped = (def->m_flags >> 15) & 0x1;

            CM2Lighting lighting;
            lighting.Initialize(nullptr, *reinterpret_cast<const CAaSphere*>(&defGroup->m_center));

            auto scene = CWorld::GetM2Scene();

            if (scene) {
                scene->SelectLights(&lighting);
            }

            // The group's own MOLT lights on top of whatever the scene found.
            defGroup->SelectLights(&lighting);

            CMapObj::SetupLocalLights(&lighting, CWorldScene::s_cameraPos);

            // The colour added on top of a group's own baked light. The vertex program adds
            // constant 29 after multiplying the baked colour by the light, so this is the only
            // thing that lifts a building whose baked colours are zero -- and plenty are.
            //
            // Diverged: the reference takes it from the day/night block (+0x1ac) when the def's
            // two fog ids match the world's, and frozen has neither field. The zone's own
            // ambient stands in, so a building picks up the same colour cast as the terrain
            // around it; the building's declared ambient is a fixed colour and leaves it
            // looking untinted.
            const C3Vector& zone = CWorld::GetOutdoorAmbient();

            CMapObj::s_instanceColor.b = static_cast<uint8_t>(zone.z * 255.0f);
            CMapObj::s_instanceColor.g = static_cast<uint8_t>(zone.y * 255.0f);
            CMapObj::s_instanceColor.r = static_cast<uint8_t>(zone.x * 255.0f);
            CMapObj::s_instanceColor.a = 0xff;

            CMapObj::s_interiorFog = flipped;

            def->m_mapObj->Render(defGroup->m_groupIndex, def->m_inversePlacement, defGroup);
        }

        // The records this group collected go back to the pool.
        for (auto record = defGroup->m_frustums.Head(); record; ) {
            auto nextRecord = defGroup->m_frustums.Next(record);

            defGroup->m_frustums.UnlinkNode(record);
            CWFrustum::Free(record);

            record = nextRecord;
        }

        defGroup = next;
    }

    CWorldScene::s_frustumDepth--;

    GxRsPop();
}

// ----------------------------------------------------------------------------------------------
// The doodads of one chunk

// ref: FUN_00791120
int32_t CWorldScene::SphereOutsideFrustum(const C3Vector& center, float radius) {
    CAaSphere sphere;
    sphere.c = center;
    sphere.r = radius;

    return CWorldScene::s_frustums[CWorldScene::s_frustumDepth].SphereInside(sphere) == 0;
}

// ref: FUN_00791cb0
// A placed thing the traversal reached: it leaves whatever row it was in, takes its distance
// along the camera forward, and tells its model to draw. Past the far edge of its detail band
// it is dropped, and through the band's fade width it draws thinner rather than vanishing.
void CWorldScene::VisitStaticEntity(CMapStaticEntity* entity) {
    entity->m_rowLink.Unlink();

    if ((entity->m_flags & 0x20) || !(CWorld::s_enables & 0x1)) {
        return;
    }

    const C4Plane& plane = CWorldScene::s_viewPlane;
    entity->m_sortDistance = plane.n.x * entity->m_sphere.c.x
                           + plane.n.y * entity->m_sphere.c.y
                           + plane.n.z * entity->m_sphere.c.z
                           + plane.d
                           - entity->m_sphere.r;

    float alpha = 1.0f;

    if ((CWorld::s_enables & 0x4000) && !(entity->m_flags & 0x800)) {
        const WorldDetailBands& bands = CWorld::GetDetailBands();
        uint32_t band = entity->m_detailLevel;

        float dx = entity->m_sphere.c.x - CWorldScene::s_cameraPos.x;
        float dy = entity->m_sphere.c.y - CWorldScene::s_cameraPos.y;
        float dz = entity->m_sphere.c.z - CWorldScene::s_cameraPos.z;
        float distanceSq = dx * dx + dy * dy + dz * dz;

        if (bands.farDistSq[band] < distanceSq) {
            return;
        }

        if (bands.fadeStartSq[band] < distanceSq) {
            float fade = 1.0f - (sqrtf(distanceSq) - bands.fadeStart[band]) / bands.fadeWidth[band];

            if (fade <= 1.0f) {
                if (fade <= 0.01f) {
                    return;
                }

                alpha = fade;
            }
        }
    }

    if (!entity->m_model) {
        return;
    }

    entity->m_model->SetAnimating(1);

    // THE DRAW BITS ARE BITFIELDS, NOT `m_flags`. This wrote `m_flags |= 0x8`, and m_flags is a
    // plain word at CM2Model +0x04 that nothing reads for visibility -- the draw list is built from
    // the SEPARATE bitfield block beside it, where CM2Model::AnimateMT tests `m_flag8` to decide
    // whether to push the model onto m_scene->m_drawList. So every doodad was queued into a field
    // no one consumed and never drew.
    //
    // The reference's choice between two pairs of bits is on the model's +0x48, and that is its
    // ATTACH PARENT: CM2Model::SetVisible makes the same test to pick between m_flag80 and m_flag8.
    // An attached model is drawn through its parent, so it takes the second pair.
    if (entity->m_model->m_attachParent) {
        entity->m_model->m_flag80 = 1;
        entity->m_model->m_flag20000 = 1;
    } else {
        entity->m_model->m_flag8 = 1;
        entity->m_model->m_flag10000 = 1;
    }

    entity->m_model->m_baseAlpha = alpha;
}

// ref: FUN_00799980
// Every doodad a chunk holds, once per frame. One too fine for this row's detail band ends the
// walk when the map says its doodads are sorted that way, and a doodad already reached through
// another chunk this frame is skipped.
void CWorldScene::TraverseChunkDoodads(STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink)* links, uint32_t detailBand) {
    for (auto link = links->Head(); link; link = links->Next(link)) {
        auto entity = static_cast<CMapStaticEntity*>(link->owner);

        if (entity->m_detailLevel < detailBand) {
            if (CMap::s_wdtHeader[0] & 0x8) {
                return;
            }

            continue;
        }

        if (!entity->m_model || !(entity->m_flags & 0x80)) {
            // Not drawing, but its box still feeds the horizon buffer.
            // TODO CWorldScene::SubmitOccluderBox(box, 0.0f) with the model's bounds brought
            // out to world space. What that sink is for is still open; see the note there.
            //
            // The reference reads the model's bounds here without checking it has one; frozen
            // checks.
            continue;
        }

        if (entity->m_frameStamp == CWorldScene::s_frameStamp) {
            continue;
        }

        entity->m_frameStamp = CWorldScene::s_frameStamp;
        entity->m_visible = 1;

        if (!CWorldScene::SphereOutsideFrustum(entity->m_sphere.c, entity->m_sphere.r)) {
            entity->m_visible = 0;

            if (CWorldScene::SphereOccluded(entity->m_sphere.c, entity->m_sphere.r, 0x10) < 2) {
                CWorldScene::VisitStaticEntity(entity);

                CWorldScene::s_visibleEntityCount++;

                continue;
            }
        }

        // Out of sight: it keeps animating only if it asked to, or if the camera is near
        // enough that it would be noticed starting up again.
        float dx = entity->m_sphere.c.x - CWorldScene::s_cameraPos.x;
        float dy = entity->m_sphere.c.y - CWorldScene::s_cameraPos.y;
        float dz = entity->m_sphere.c.z - CWorldScene::s_cameraPos.z;

        int32_t keepAnimating = (entity->m_flags7c & 0x400)
            || (dx * dx + dy * dy + dz * dz) < ANIMATE_RANGE_SQ;

        entity->m_model->SetAnimating(keepAnimating);
    }
}

// ref: FUN_007987a0
// The static things of one distance row: doodads, placed once and never moving. Same test as
// the per-chunk walk plus the occlusion volumes, which only a row-level walk is coarse enough
// to bother with.
void CWorldScene::TraverseRowStaticEntities(Row* row, uint32_t detailBand) {
    for (auto entity = row->staticEntities.Head(); entity; ) {
        auto next = row->staticEntities.Next(entity);

        entity->m_rowLink.Unlink();

        if (entity->m_detailLevel < detailBand) {
            entity = next;

            continue;
        }

        if (!entity->m_model || !(entity->m_flags & 0x80)) {
            // TODO the occluder box, as in TraverseChunkDoodads.
            entity = next;

            continue;
        }

        entity->m_frameStamp = CWorldScene::s_frameStamp;
        entity->m_visible = 1;

        bool reached = CWorldScene::s_frustums[CWorldScene::s_frustumDepth].SphereInside(entity->m_sphere)
            && !CWorldScene::SphereOccludedByVolumes(entity->m_sphere);

        if (reached) {
            entity->m_visible = 0;

            if (CWorldScene::SphereOccluded(entity->m_sphere.c, entity->m_sphere.r, 0x10) < 2) {
                CWorldScene::VisitStaticEntity(entity);

                CWorldScene::s_visibleEntityCount++;

                entity = next;

                continue;
            }
        }

        float dx = entity->m_sphere.c.x - CWorldScene::s_cameraPos.x;
        float dy = entity->m_sphere.c.y - CWorldScene::s_cameraPos.y;
        float dz = entity->m_sphere.c.z - CWorldScene::s_cameraPos.z;

        int32_t keepAnimating = (entity->m_flags7c & 0x400)
            || (dx * dx + dy * dy + dz * dz) < ANIMATE_RANGE_SQ;

        entity->m_model->SetAnimating(keepAnimating);

        entity = next;
    }
}

// ref: FUN_00793060
// The moving things of one distance row: units, game objects, anything the server places. One
// the frustum, the occlusion volumes or the horizon hides joins the frame's hidden list, which
// the render pass walks to keep them animating without drawing them.
void CWorldScene::TraverseRowEntities(Row* row) {
    CWorldScene::s_visitingGroupEntities = 0;

    for (auto entity = row->entities.Head(); entity; ) {
        auto next = row->entities.Next(entity);

        entity->m_entityRowLink.Unlink();

        entity->m_visible = 1;

        bool hidden = !CWorldScene::s_frustums[CWorldScene::s_frustumDepth].SphereInside(entity->m_sphere)
            || CWorldScene::SphereOccludedByVolumes(entity->m_sphere)
            || CWorldScene::SphereOccluded(entity->m_sphere.c, entity->m_sphere.r, 0);

        if (hidden) {
            CWorldScene::s_hiddenEntities.LinkToTail(entity);

            entity = next;

            continue;
        }

        entity->m_visible = 0;

        if (entity->m_model) {
            entity->m_model->SetAnimating(1);

            // Bit 2 of the entity's state word hides it while leaving it animating, so the
            // draw bits are set from its complement rather than unconditionally.
            uint32_t draw = ~(entity->m_flags7c >> 2) & 0x1;

            // The same bitfield mistake as in VisitStaticEntity, and the same fix: these are the
            // bitfields the draw list is built from, not the m_flags word. The pair is chosen by
            // the model having an attach parent, which is the reference's +0x48 test.
            if (entity->m_model->m_attachParent) {
                entity->m_model->m_flag80 = draw;
                entity->m_model->m_flag20000 = draw;
            } else {
                entity->m_model->m_flag8 = draw;
                entity->m_model->m_flag10000 = draw;
            }

            // TODO the reference also hangs FUN_00780cd0 off the model here, the hook that
            // lets an entity answer for its own lighting.
        }

        // The entity is visible, so it joins the frame's list -- through the same link the row
        // used, which is why next was read first. That list is what the shadow pass draws from,
        // and it is already reduced to what the frame can see.
        //
        // TODO the reference asks the entity's own callback first and keeps it out when the
        // callback refuses; frozen has no entity callbacks yet, so every visible entity joins.
        CWorldScene::s_frameEntityList.LinkToTail(entity);

        entity = next;
    }
}

// ref: FUN_00790920
// What the camera is standing in. Outdoors the map answers; inside a building the question goes
// to the group the camera is in, in that group's own space.
//
// The depth is what the answer is for: how far under the surface the camera is, which is what
// decides how dark and how fogged the water looks.
void CWorldScene::UpdateCameraLiquid() {
    CWorldScene::s_cameraLiquidDepth = 0.0f;

    uint32_t liquidType = 0;
    float height = 0.0f;

    if (!CWorldScene::s_cameraDef) {
        if (CMap::GetTerrainLiquid(CWorldScene::s_cameraPos, &liquidType, &height, 1)) {
            CWorldScene::s_cameraLiquidDepth = height - CWorldScene::s_cameraPos.z;
        }
    } else {
        // Inside a building: into the def's own space, then ask the rooms the camera is standing in.
        // s_cameraGroupIndices is what UpdateCameraDef left behind, so this asks only those rooms
        // rather than every group of the building.
        auto def = CWorldScene::s_cameraDef;

        if (def->m_mapObj) {
            C3Vector local = CWorldScene::s_cameraPos * def->m_inversePlacement;

            for (uint32_t i = 0; i < CWorldScene::s_cameraGroupIndices.Count(); i++) {
                uint32_t groupIndex = CWorldScene::s_cameraGroupIndices[i];

                if (groupIndex == 0xffff) {
                    continue;
                }

                auto group = def->m_mapObj->GetGroup(groupIndex, 0);

                if (!group) {
                    continue;
                }

                if (group->GetLiquidAt(local, &liquidType, &height)) {
                    // The depth is a WORLD vertical distance, as the outdoor branch produces, and
                    // the group's height is in the building's space -- so measure it there too,
                    // against the same local position that answered the query.
                    CWorldScene::s_cameraLiquidDepth = height - local.z;

                    break;
                }
            }
        }
    }

    bool left = false;

    if (!liquidType) {
        left = CWorldScene::s_cameraLiquidType != 0;
    } else if (liquidType != CWorldScene::s_cameraLiquidType) {
        auto rec = g_liquidTypeDB.GetRecord(static_cast<int32_t>(liquidType));

        // Bit 3 marks the kinds that carry underwater motes. Frozen-only null check: the
        // reference reads the row unguarded.
        uint8_t motes = rec ? (rec->m_flags >> 3) & 1 : 0;
        CWorld::s_particulates->m_active = motes;

        if (motes) {
            CWorld::s_particulates->Respawn(liquidType);
            CWorld::s_particulates->SetSizeScale(rec->m_particleScale * 0.02777777798473835f);
        }
    }

    CWorldScene::s_cameraLiquidType = liquidType;

    // The world's "camera is submerged" switch is set from HERE now, not from the stand-in's own
    // LiquidAt. Both halves that feed it are reference queries over parsed liquid data --
    // CMap::GetTerrainLiquid walks the chunk's CChunkLiquid list, and CMapObjGroup::GetLiquidAt
    // samples the group's MLIQ grid -- so neither depends on the liquid material drawing.
    CWorld::SetCameraUnderLiquid(liquidType != 0);

    if (left) {
        // TODO FUN_007f1070(1): what the reference does on coming out of liquid.
    }

    // TODO FUN_008a2aa0(): the Liquid module's own per-frame tick.
}

// ref: FUN_00793980
// The frame's entities, drained. Each one leaves the list as it is handled, so the list is empty
// again by the time the next frame fills it.
//
// An entity that answers to a game object takes its footprint from the object -- for a unit that
// is the box CreatureModelData gives, shifted for whatever it is riding. One that does not is a
// scene model in its own right, and its footprint is the box the animation it is playing was
// authored with, so the shadow follows the animation rather than sitting at a fixed size.
void CWorldScene::DrawEntityShadows() {
    // TODO SFile::IsStreamingMode() gates a hint (FUN_00825150) the reference gives the
    // streamer when a model the frame wants has not arrived. Not ported.

    for (auto entity = CWorldScene::s_frameEntityList.Head(); entity; ) {
        auto next = CWorldScene::s_frameEntityList.Next(entity);

        entity->m_entityRowLink.Unlink();

        // TODO FUN_00791240(model, bounds, (m_flags7c >> 0xe) & 1): the reference refreshes the
        // model's out-of-frustum bit and every attachment's here, from bounds that include the
        // particle emitters (FUN_00825a60). Neither that nor the plain bounds helper is ported.

        if (!entity->m_model
            || !entity->m_model->IsLoaded(0, 0)
            || (entity->m_flags7c & 0x4)) {
            entity = next;

            continue;
        }

        auto object = ClntObjMgrObjectPtr(entity->m_param64, TYPE_UNIT, __FILE__, __LINE__);

        if (!object) {
            // Bit 11 is what marks an entity as casting at all.
            if (entity->m_flags7c & 0x800) {
                M2SequenceInfo info;

                entity->m_model->GetSequenceInfo(0, 0, info);

                // The reference's own gate and projector. They draw nothing yet -- the receiver
                // walk behind DecalDrawProjected is still a TODO -- but the footprint, the
                // projection volume and the decal state are all built the reference's way now.
                ShadowDrawBlob(info.extent, entity->m_model);
            }

            entity = next;

            continue;
        }

        // TODO the reference also skips a unit whose record at +0xd0 reads 9 at +0x110. What
        // that field is has not been established, so frozen casts for every unit; the effect is
        // a shadow under one kind of unit that the reference leaves without one.
        if (entity->m_flags7c & 0x800) {
            CAaBox shadowBox;

            static_cast<CGUnit_C*>(object)->GetShadowBox(shadowBox);

            ShadowDrawBlob(shadowBox, entity->m_model);
        }

        // TODO the object's own per-frame hook, vtable slot 29, runs here.

        entity = next;
    }
}

// ref: FUN_0078f6a0
// The skyline a ridge of terrain cuts. Each point is projected through the occlusion matrix,
// which flattens the view, and each pair of consecutive points covers a run of columns; every
// column in that run remembers the higher of the two skylines it has seen.
//
// Looking near enough to straight up or straight down there is no horizon to speak of, and the
// reference gives up rather than produce nonsense.
void CWorldScene::ShadeHorizon(const float (*table)[3], const float* heights,
                               const int32_t* indices, int32_t count,
                               const C3Vector& position, int32_t holes) {
    if (!(CWorld::s_enables & 0x20)) {
        return;
    }

    if (CWorldScene::s_viewDir.z < -0.9f || 0.9f < CWorldScene::s_viewDir.z) {
        return;
    }

    if (count <= 0 || count > 16) {
        return;
    }

    C3Vector projected[16];

    for (int32_t i = 0; i < count; i++) {
        int32_t v = indices[i];

        C3Vector p = {
            table[v][0] + position.x,
            table[v][1] + position.y,
            heights[v] + position.z
        };

        p = p * CWorldScene::s_occlusionMatrix;

        float inv = 1.0f / p.z;

        projected[i].x = p.x * inv;
        projected[i].y = p.y * inv;
        projected[i].z = p.z;
    }

    for (int32_t i = 0; i < count - 1; i++) {
        int32_t first = static_cast<int32_t>(roundf(projected[i].x * 64.0f - 0.5f)) + 192;
        int32_t last = static_cast<int32_t>(roundf(projected[i + 1].x * 64.0f - 0.5f)) + 192;

        if (last < first) {
            int32_t swap = first;
            first = last;
            last = swap;
        }

        if (first < 0) {
            first = 0;
        }

        if (last > static_cast<int32_t>(CWorldScene::HORIZON_COLUMNS) - 1) {
            last = CWorldScene::HORIZON_COLUMNS - 1;
        }

        if (holes) {
            // A chunk you can see through raises nothing; it reopens what it spans, except
            // where a column has been marked to keep what it has.
            for (int32_t c = first; c <= last; c++) {
                if (!(CWorldScene::s_horizonColumnFlags[c] & 0x1)) {
                    CWorldScene::s_horizonBuffer[c] = -1000001.0f;
                }
            }

            continue;
        }

        // Both ends have to be in front of the eye for the span to mean anything.
        if (projected[i].z < 0.0277f || projected[i + 1].z < 0.0277f) {
            continue;
        }

        float height = projected[i + 1].y < projected[i].y ? projected[i + 1].y : projected[i].y;

        for (int32_t c = first; c <= last; c++) {
            if (CWorldScene::s_horizonBuffer[c] < height) {
                CWorldScene::s_horizonBuffer[c] = height;
            }
        }
    }
}

// ref: FUN_00793760
// The chunks of one row that stand between the camera and whatever is behind them. A solid
// chunk raises the skyline along its silhouette; one with holes in it reopens what it spans.
// Both passes walk the same list, and the order matters: raising first and reopening second is
// what lets a doorway-shaped gap in a cliff stay see-through.
//
// Only chunks between the near and far occluder distances count. Nearer than that the
// silhouette is too coarse to mean anything, and past the far edge there is nothing left to
// hide.
void CWorldScene::TraverseRowOccluders(Row* row) {
    if (!(CWorld::s_enables & 0x20)) {
        // Occlusion off: the lists still have to be emptied, or next frame's entries pile onto
        // them.
        row->occluderChunks.UnlinkAll();

        for (auto occluder = row->occluders.Head(); occluder; ) {
            auto next = row->occluders.Next(occluder);
            CWorldScene::FreeOccluder(occluder);
            occluder = next;
        }

        return;
    }

    for (auto chunk = row->occluderChunks.Head(); chunk; ) {
        auto next = row->occluderChunks.Next(chunk);

        if (chunk->m_header && !chunk->m_header->holes) {
            chunk->m_frameLink.Unlink();

            if (chunk->m_sortDistance < CWorldScene::s_farChunkDistance
                && CWorldScene::s_nearChunkDistance < chunk->m_sortDistance) {
                chunk->FeedHorizon();
            }
        }

        chunk = next;
    }

    // What the first pass left behind is the chunks you can see through.
    for (auto chunk = row->occluderChunks.Head(); chunk; ) {
        auto next = row->occluderChunks.Next(chunk);

        chunk->m_frameLink.Unlink();
        chunk->FeedHorizon();

        chunk = next;
    }

    // The edges: each shades the horizon, marking its columns, then goes back to the free list --
    // or, with the debug enable 0x2000, onto the list that draws them.
    for (auto occluder = row->occluders.Head(); occluder; ) {
        auto next = row->occluders.Next(occluder);

        CWorldScene::ShadeHorizonPolyline(&occluder->a, 2, 1);

        if (!(CWorld::s_enables & 0x2000)) {
            CWorldScene::FreeOccluder(occluder);
        } else {
            CWorldScene::s_debugOccluders.LinkToTail(occluder);
        }

        occluder = next;
    }
}

// ref: FUN_007946d0
// A solid box, handed on as five quads rather than six: the four sides, each dropped a yard
// below the box so the shape closes against the ground, and the top. There is no bottom,
// because nothing is ever seen from under one.
//
// The box is let out by a twentieth of a yard first, so a surface lying exactly on it is
// treated as inside rather than falling on the boundary.
//
// The faces ARE a rasterised primitive and NOT the plane list SphereOccludedByVolumes reads, so
// despite the name the reference gives this neighbourhood, do not wire them to that test. That
// much of the older note here was right; what it said about the consumer was not, and the whole
// system was walked out on 2026-09-26:
//
// The sink is the object at 0x00adf4a0, whose field 0 is a MODE (the `cmpl $0x0, (%esi)` this
// function opens with; the tile walk below also tests it against 2). This function is __thiscall
// on it -- frozen makes it static and keeps the sink in globals instead.
//
//   feeders   this (five quads per box) and FUN_007944c0 (513 bytes, 4 callers), both appending
//             through FUN_00792360 (1135 bytes), which builds a pyramid from the camera through
//             the quad with MatrixLookAt / PlaneFromPoints / IntersectRayPlane and appends
//             32-byte records to two growable arrays on the object.
//   fed from   the per-chunk links FUN_00799980, the doodad row visit FUN_007987a0,
//              CMap::UpdateMapObjDefs FUN_007b6110, and FUN_007b4bc0, which walks the tile
//              window and submits a face for every tile whose area is NOT loaded -- a wall at
//              the edge of the streamed world.
//   consumer   FUN_00794b50 (2200 bytes), a DRAW: BufStream, PrimIndexPtr, GxPrimVertexPtr,
//              TextureGetGxTex and an M2 through CM2Model::WaitForLoad. Reached by
//              FUN_0077f980(cameraPos) and called TWICE from CGWorldFrame::OnWorldRender
//              (FUN_004f8ea0) at 0x004f9184 and 0x004f919a.
//
// So this is the BARRIER pass -- geometry drawn to close off the edge of the loaded world -- not
// a culling input, and it is item 11's missing call rather than item 7's. CMap::UpdateAreas does
// not read it at all.
void CWorldScene::SubmitOccluderBox(const CAaBox& box, float maxDistance) {
    if (box.b.x >= box.t.x || box.b.y >= box.t.y || box.b.z >= box.t.z) {
        return;
    }

    CAaBox grown;
    grown.b.x = box.b.x - 0.05f;
    grown.b.y = box.b.y - 0.05f;
    grown.b.z = box.b.z - 0.05f;
    grown.t.x = box.t.x + 0.05f;
    grown.t.y = box.t.y + 0.05f;
    grown.t.z = box.t.z + 0.05f;

    // Too far away to be worth hiding anything with.
    if (DistancePointBox(grown, CWorldScene::s_cameraPos) > maxDistance) {
        return;
    }

    float skirt = grown.b.z - 1.0f;

    const C3Vector faces[5][4] = {
        // -x, then +x: the pair order flips so both face outward.
        { { grown.b.x, grown.b.y, skirt }, { grown.b.x, grown.t.y, skirt },
          { grown.b.x, grown.t.y, grown.t.z }, { grown.b.x, grown.b.y, grown.t.z } },
        { { grown.t.x, grown.t.y, skirt }, { grown.t.x, grown.b.y, skirt },
          { grown.t.x, grown.b.y, grown.t.z }, { grown.t.x, grown.t.y, grown.t.z } },
        // -y, then +y.
        { { grown.t.x, grown.b.y, skirt }, { grown.b.x, grown.b.y, skirt },
          { grown.b.x, grown.b.y, grown.t.z }, { grown.t.x, grown.b.y, grown.t.z } },
        { { grown.b.x, grown.t.y, skirt }, { grown.t.x, grown.t.y, skirt },
          { grown.t.x, grown.t.y, grown.t.z }, { grown.b.x, grown.t.y, grown.t.z } },
        // the top.
        { { grown.b.x, grown.b.y, grown.t.z }, { grown.t.x, grown.b.y, grown.t.z },
          { grown.t.x, grown.t.y, grown.t.z }, { grown.b.x, grown.t.y, grown.t.z } },
    };

    for (int32_t i = 0; i < 5; i++) {
        // TODO FUN_00792360(quad, 4, maxDistance, 0, 0, 0).
        (void)faces[i];
    }
}

// ref: FUN_00792fa0
// Queued once a frame. The flag is what keeps a chunk that is visited twice from being drawn
// twice; the pass clears it as it takes the instance.
void CWorldScene::AddDetailDoodads(DetailDoodad::CDetailDoodadData* instance) {
    if (instance->m_queued) {
        return;
    }

    instance->m_queued = 1;

    CWorldScene::s_frameDetailDoodadList.LinkToTail(instance);
}

// ref: FUN_007984a0
// One chunk at a time: work out the light reaching it, put its origin in the world matrix, and
// let the module draw its batches. The state is set once for the whole pass, not per chunk.
//
// This function reads like a general model pass and is not one -- the vtable, matrix and
// position it touches are the CHUNK's, reached through the instance's back-pointer.
void CWorldScene::RenderDetailDoodads() {
    if (!CWorldScene::s_frameDetailDoodadList.Head()) {
        return;
    }

    GxRsPush();
    GxXformPush(GxXform_World);
    GxXformPush(GxXform_Tex1);

    int32_t shaderPath = DetailDoodad::SetupState();

    auto scene = CWorld::GetM2Scene();
    const C3Vector& cameraPos = CWorld::GetCameraPos();

    for (auto instance = CWorldScene::s_frameDetailDoodadList.Head(); instance; ) {
        // Read before the node moves: the pass unlinks it as it goes.
        auto next = CWorldScene::s_frameDetailDoodadList.Next(instance);

        auto chunk = instance->m_chunk;

        instance->m_queued = 0;

        CM2Lighting lighting;

        CAaSphere sphere;

        sphere.c = chunk->m_center;
        sphere.r = chunk->m_radius;

        lighting.Initialize(scene, sphere);

        if (scene) {
            scene->SelectLights(&lighting);
        }

        chunk->SelectLights(&lighting);

        // The grass is in the chunk's own space, so the chunk's origin goes in the matrix --
        // less the camera, which is what keeps the vertices small enough to stay precise.
        C44Matrix placement;

        C3Vector origin;

        origin.x = chunk->m_position.x - cameraPos.x;
        origin.y = chunk->m_position.y - cameraPos.y;
        origin.z = chunk->m_position.z - cameraPos.z;

        placement.Translate(origin);

        if (!shaderPath) {
            lighting.SetupGxLights(&cameraPos);
            lighting.SetupGxFog();

            GxXformSet(GxXform_World, placement);
        } else {
            // TODO ref: FUN_007b10e0 hands the matrix and 23 constants to the module's vertex
            // shader. Unreachable while the module's shaders are not loaded.
        }

        DetailDoodad::Draw(instance);


        // The reference unlinks here and relinks the instance into a third list, reusing the
        // one link for each in turn. Frozen keeps the instance on its chunk, so there is
        // nothing to relink it into and the unlink is the whole of it.
        instance->m_frameLink.Unlink();

        instance = next;
    }

    GxXformPop(GxXform_Tex1);
    GxXformPop(GxXform_World);
    GxRsPop();
}

// ref: FUN_00792fc0
// A set insert. The reference scans the whole array first and does nothing if the value is already
// there, which is what keeps a group from being walked twice when the query reports it from both
// its geometry and a portal.
void CWorldScene::AddGroupIndexUnique(TSGrowableArray<uint32_t>& list, uint32_t groupIndex) {
    for (uint32_t i = 0; i < list.Count(); i++) {
        if (list[i] == groupIndex) {
            return;
        }
    }

    list.Add(1, &groupIndex);
}

// ref: FUN_00790570
// A window over a rectangle of the screen at a depth, with no outline.
void CWorldScene::ViewWindowInit(ViewWindow& window, const float* rect, float depth) {
    window.minX = rect[0];
    window.minY = rect[1];
    window.maxX = rect[2];
    window.maxY = rect[3];
    window.depth = depth;
    window.points = nullptr;
    window.pointCount = 0;
}

// ref: FUN_0078f2f0
// The rectangle covering both: the smaller of each pair of mins, the larger of each pair of maxes.
static void ViewRectUnion(float* out, const float* a, const float* b) {
    float maxY = a[3] <= b[3] ? b[3] : a[3];
    float maxX = a[2] <= b[2] ? b[2] : a[2];
    float minY = b[1] <= a[1] ? b[1] : a[1];

    out[0] = a[0] < b[0] ? a[0] : b[0];
    out[1] = minY;
    out[2] = maxX;
    out[3] = maxY;
}

// ref: FUN_007905b0
// Grow the window over another: the union of the two rectangles, at the deeper of the two depths,
// and no outline any more.
void CWorldScene::ViewWindowMerge(ViewWindow& window, const ViewWindow& other) {
    float rect[4];
    ViewRectUnion(rect, &window.minX, &other.minX);

    window.minX = rect[0];
    window.minY = rect[1];
    window.maxX = rect[2];
    window.maxY = rect[3];

    if (!(other.depth < window.depth)) {
        window.depth = other.depth;
    }

    window.pointCount = 0;
}

// ref: FUN_00795d00
void CWorldScene::AddPortalView(const ViewWindow& window) {
    *CWorldScene::s_portalViews.New() = window;
}

// ref: FUN_00795d20
void CWorldScene::AddExteriorView(const ViewWindow& window) {
    *CWorldScene::s_exteriorViews.New() = window;
}

// ref: FUN_007968d0
// Replace the portal views with what they leave uncovered: the whole screen, cut by each view in
// turn into at most four rectangles around it, keeping no more than sixty pieces.
void CWorldScene::ComplementPortalViews() {
    float pieces[2][64][4] = {};
    int32_t pieceCount[2] = { 1, 0 };

    pieces[0][0][0] = 0.0f;
    pieces[0][0][1] = 0.0f;
    pieces[0][0][2] = 1.0f;
    pieces[0][0][3] = 1.0f;

    uint32_t pass = 0;

    for (; pass < CWorldScene::s_portalViews.Count(); pass++) {
        const ViewWindow& cut = CWorldScene::s_portalViews[pass];
        uint32_t from = pass & 1;
        uint32_t to = (pass - 1) & 1;

        pieceCount[to] = 0;

        if (pieceCount[from] > 60) {
            break;
        }

        for (int32_t i = 0; i < pieceCount[from]; i++) {
            float minX = pieces[from][i][0];
            float minY = pieces[from][i][1];
            float maxX = pieces[from][i][2];
            float maxY = pieces[from][i][3];

            auto emit = [&](float a, float b, float c, float d) {
                float* piece = pieces[to][pieceCount[to]];
                piece[0] = a;
                piece[1] = b;
                piece[2] = c;
                piece[3] = d;
                pieceCount[to]++;
            };

            if (maxY <= cut.minY || cut.maxY <= minY || maxX <= cut.minX || cut.maxX <= minX) {
                // Untouched by the cut.
                emit(minX, minY, maxX, maxY);
            } else {
                if (minX < cut.minX) {
                    emit(minX, minY, cut.minX, maxY);
                    minX = cut.minX;
                }

                if (cut.maxX < maxX) {
                    emit(cut.maxX, minY, maxX, maxY);
                    maxX = cut.maxX;
                }

                if (minY < cut.minY) {
                    emit(minX, minY, maxX, cut.minY);
                    minY = cut.minY;
                }

                if (cut.maxY < maxY) {
                    emit(minX, cut.maxY, maxX, maxY);
                }
            }

            if (pieceCount[to] > 60) {
                break;
            }
        }
    }

    uint32_t result = pass & 1;
    int32_t count = pieceCount[result];

    // Growing the list default-builds the new views (no outline); the views kept keep theirs.
    uint32_t oldCount = CWorldScene::s_portalViews.Count();
    CWorldScene::s_portalViews.SetCount(count);

    for (uint32_t i = oldCount; i < static_cast<uint32_t>(count); i++) {
        CWorldScene::s_portalViews[i].points = nullptr;
        CWorldScene::s_portalViews[i].pointCount = 0;
    }

    for (int32_t i = 0; i < count; i++) {
        ViewWindow& view = CWorldScene::s_portalViews[i];
        view.minX = pieces[result][i][0];
        view.minY = pieces[result][i][1];
        view.maxX = pieces[result][i][2];
        view.maxY = pieces[result][i][3];
        view.depth = 0.0f;
    }
}

// ref: FUN_00796c10
// Fill a list of screen regions at the far plane in the fog colour, with identity view and
// projection: rectangles as two triangles, outlines as fans. The rectangles are converted to clip
// space in place. `portal` picks the DayNight block's blended fog colour (+0xa0) over its plain
// one (+0x8c); frozen's stand-in light model keeps one fog colour, so both read it.
void CWorldScene::FillViewWindows(TSGrowableArray<ViewWindow>& views, int32_t portal) {
    if (!views.Count()) {
        return;
    }

    (void)portal;

    C44Matrix savedProjection;
    C44Matrix savedView;
    GxXformProjection(savedProjection);
    GxXformView(savedView);

    C44Matrix identity;
    GxXformSetView(identity);
    GxXformSetProjection(identity);
    g_theGxDevicePtr->XformPush(GxXform_World, identity);

    GxRsPush();
    GxRsSet(GxRs_DepthWrite, 1);
    GxRsSet(GxRs_DepthTest, 1);

    const C3Vector& fog = CWorld::GetFogColor();
    CImVector color;
    color.r = static_cast<uint8_t>(fog.x * 255.0f);
    color.g = static_cast<uint8_t>(fog.y * 255.0f);
    color.b = static_cast<uint8_t>(fog.z * 255.0f);
    color.a = 0xFF;
    GxFormatColor(color);

    GxRsSet(GxRs_BlendingMode, 0);
    GxRsSet(GxRs_MatDiffuse, color.value);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    for (uint32_t i = 0; i < views.Count(); i++) {
        if (views[i].pointCount == 0) {
            vertexCount += 4;
            indexCount += 6;
        } else {
            vertexCount += views[i].pointCount;
            indexCount += views[i].pointCount * 3 - 6;
        }
    }

    auto vertexBuf = GxBufStream(GxPoolTarget_Vertex, 0x10, vertexCount);
    auto indexBuf = GxBufStream(GxPoolTarget_Index, 2, indexCount);
    auto vertices = reinterpret_cast<float*>(GxBufLock(vertexBuf));
    auto indices = reinterpret_cast<uint16_t*>(GxBufLock(indexBuf));

    if (vertices && indices) {
        uint16_t base = 0;

        auto vertex = [&](float x, float y) {
            vertices[0] = x;
            vertices[1] = y;
            vertices[2] = 1.0f;
            reinterpret_cast<uint32_t*>(vertices)[3] = color.value;
            vertices += 4;
        };

        for (uint32_t i = 0; i < views.Count(); i++) {
            ViewWindow& view = views[i];

            if (view.pointCount == 0) {
                view.minY = view.minY * 2.0f - 1.0f;
                view.maxY = view.maxY * 2.0f - 1.0f;
                view.minX = view.minX * 2.0f - 1.0f;
                view.maxX = view.maxX * 2.0f - 1.0f;

                vertex(view.minY, view.minX);
                vertex(view.minY, view.maxX);
                vertex(view.maxY, view.maxX);
                vertex(view.maxY, view.minX);

                indices[0] = base;
                indices[1] = base + 1;
                indices[2] = base + 2;
                indices[3] = base;
                indices[4] = base + 2;
                indices[5] = base + 3;
                indices += 6;
                base += 4;
            } else {
                for (uint32_t k = 0; k < view.pointCount; k++) {
                    vertex(view.points[k].x, view.points[k].y);
                }

                for (uint32_t k = 0; view.pointCount != 2 && k < view.pointCount - 2; k++) {
                    indices[0] = base;
                    indices[1] = base + 1 + k;
                    indices[2] = base + 2 + k;
                    indices += 3;
                }

                base += view.pointCount;
            }
        }
    }

    GxBufUnlock(vertexBuf, 0);
    GxBufUnlock(indexBuf, 0);
    GxPrimVertexPtr(vertexBuf, GxVBF_PC);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_count = indexCount;
    batch.m_minIndex = 0;
    batch.m_maxIndex = static_cast<uint16_t>(vertexCount - 1);
    GxDraw(&batch, 1);

    GxRsPop();
    g_theGxDevicePtr->XformPop(GxXform_World);
    GxXformSetProjection(savedProjection);
    GxXformSetView(savedView);
}

// ref: FUN_00795d40
// Which building the camera is standing in. The whole thing is one downward segment: from the
// camera to 1760 units below it, which is more than the world is tall, so it always reaches ground
// or a floor.
//
// Called from CWorld::Update every frame.
void CWorldScene::UpdateCameraDef() {
    CWorldScene::s_cameraDef = nullptr;
    CWorldScene::s_cameraDefFlagged = nullptr;
    CWorldScene::s_cameraGroupIndices.SetCount(0);
    CWorldScene::s_cameraFlaggedGroupIndices.SetCount(0);

    CWorldScene::s_cameraAreaName[0] = '\0';
    CWorldScene::s_cameraSubAreaName[0] = '\0';

    if (!(CWorld::s_enables & CWorld::Enables::Enable_100)) {
        return;
    }

    // 1760 is DAT_00a3e6e8.
    const C3Vector& camera = CWorldScene::s_cameraPos;

    C3Vector start = camera;
    C3Vector end = { camera.x, camera.y, camera.z - 1760.0f };

    CMapObjDef* defs[2] = { nullptr, nullptr };
    uint32_t groups[4] = { 0xffff, 0xffff, 0xffff, 0xffff };

    // The terrain first: its hit caps how far down a building may be found, so a building under
    // the ground cannot claim the camera, and it names the area when no building does.
    float t = 1.0f;
    CMapChunk* chunk = nullptr;
    bool terrain = CMap::QuerySegmentTerrain(start, end, &t, 0x100, &chunk);

    if (!QuerySegmentMapObjs(start, end, t, defs, groups)) {
        if (terrain) {
            SStrCopy(CWorldScene::s_cameraAreaName, CMap::s_mapName, sizeof(CWorldScene::s_cameraAreaName));
            SStrPrintf(CWorldScene::s_cameraSubAreaName, sizeof(CWorldScene::s_cameraSubAreaName), "%i, %i",
                       chunk->m_indexY / 16, chunk->m_indexX / 16);
        }

        return;
    }

    CWorldScene::s_cameraDef = defs[0];

    if (defs[0]) {
        // The root's own path and the group's name, for the area display.
        const char* groupName = defs[0]->m_mapObj ? defs[0]->m_mapObj->GroupName(groups[0]) : nullptr;

        if (defs[0]->m_mapObj) {
            SStrCopy(CWorldScene::s_cameraAreaName, defs[0]->m_mapObj->m_name, sizeof(CWorldScene::s_cameraAreaName));
        }

        if (groupName) {
            SStrCopy(CWorldScene::s_cameraSubAreaName, groupName, sizeof(CWorldScene::s_cameraSubAreaName));
        }

        CWorldScene::AddGroupIndexUnique(CWorldScene::s_cameraGroupIndices,
                                        groups[0]);

        if (groups[1] != 0xffff) {
            CWorldScene::AddGroupIndexUnique(CWorldScene::s_cameraGroupIndices,
                                             groups[1]);
        }

        // The two groups' flags together decide whether the indoor pass needs a fresh full-screen
        // window seeded into the scene's.
        uint32_t flags = 0;

        if (groups[0] != 0xffff) {
            SMOGroupInfo* info = defs[0]->m_mapObj ? defs[0]->m_mapObj->GroupInfo(groups[0])
                                                   : nullptr;

            if (info) {
                flags |= info->flags;
            }
        }

        if (groups[1] != 0xffff) {
            SMOGroupInfo* info = defs[0]->m_mapObj ? defs[0]->m_mapObj->GroupInfo(groups[1])
                                                   : nullptr;

            if (info) {
                flags |= info->flags;
            }
        }

        if (flags & 0x40140) {
            // The whole screen at depth 0, merged into the window and kept as a portal view.
            static const float fullScreen[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

            ViewWindow window;
            CWorldScene::ViewWindowInit(window, fullScreen, 0.0f);
            CWorldScene::ViewWindowMerge(CWorldScene::s_window, window);
            CWorldScene::AddPortalView(window);
        }
    }

    CWorldScene::s_cameraDefFlagged = defs[1];

    if (defs[1]) {
        CWorldScene::AddGroupIndexUnique(CWorldScene::s_cameraFlaggedGroupIndices,
                                         groups[2]);

        if (groups[3] != 0xffff) {
            CWorldScene::AddGroupIndexUnique(CWorldScene::s_cameraFlaggedGroupIndices,
                                             groups[3]);
        }
    }
}

// ref: FUN_00790a80
// The transparent liquid pass: the fog states, then liquid bucket 1, then the post-liquid decals.
//
// This is where the reference drains bucket 1 -- from the world frame's transparent block, between
// an M2 pass and the weather (0x004f9170 above water, 0x004f91b0 below it) -- NOT from CMap::Render,
// which is where frozen used to do it because this wrapper was unported. That divergence is now
// closed and the note at CMap::Render's bucket-0 draw says so.
void CWorldScene::DrawLiquidPass() {
    CWorld::SetupFogRenderStates();

    if (CWorld::s_enables & CWorld::Enables::Enable_Liquid) {
        Liquid::Draw(CWorldScene::s_cameraPos, 1);
    }

    // The ripples over the water just drawn. Units moving through water spawn them through
    // CWorld::AddRipple, whose callers come with unit movement.
    WaterRipples::Draw();
}

// ------------------------------------------------------------------------------------------------
// The frame's world view state, and the UI shader pair the world's quad passes draw through. Both
// lived in the stand-in renderer until 2026-09-26, purely because that was the file that ran first.
// ------------------------------------------------------------------------------------------------

C44Matrix CWorldScene::s_viewNoTranslate;
C44Matrix CWorldScene::s_projNative;
C44Matrix CWorldScene::s_viewProjT;
C3Vector CWorldScene::s_worldCameraPos = { 0.0f, 0.0f, 0.0f };
bool CWorldScene::s_fogActive = false;
bool CWorldScene::s_viewUpdated = false;
bool CWorldScene::s_terrainShaderTried = false;
CGxShader* CWorldScene::s_uiVertexShader[2] = { nullptr, nullptr };
CGxShader* CWorldScene::s_uiPixelShader = nullptr;

// The UI shader pair, which is the only program this file still owns. It is what DayNight,
// Weather, OverheadIcons and ParticleFx draw their quads through, via CWorldScene::UiShaders.
//
// The stand-in's own terrain, blob-decal and detail-doodad programs used to be built here too, in
// D3D9 bytecode and again in ARB assembly. Nothing reads them any more: the chunks draw through
// CMap::GetTerrainVertexShader's .bls permutations, the detail doodads through DetailDoodad's own
// state, and the blob decal is gone. They were created on every map load and never bound.
void CWorldScene::EnsureUiShaders() {
    if (CWorldScene::s_terrainShaderTried) {
        return;
    }

    CWorldScene::s_terrainShaderTried = true;

    if (!g_theGxDevicePtr) {
        return;
    }

    g_theGxDevicePtr->ShaderCreate(CWorldScene::s_uiVertexShader, GxSh_Vertex, "Shaders\\Vertex", "UI", 2);
    g_theGxDevicePtr->ShaderCreate(&CWorldScene::s_uiPixelShader, GxSh_Pixel, "Shaders\\Pixel", "UI", 1);
}

void CWorldScene::UiShaders(CGxShader*& vs, CGxShader*& ps) {
    CWorldScene::EnsureUiShaders();
    vs = CWorldScene::s_uiVertexShader[0];
    ps = CWorldScene::s_uiPixelShader;
}

void CWorldScene::UpdateWorldView() {
    // Water is baked per vertex, so it has to follow the light rather than the load. Rebake only
    // Build exactly the transform the M2 scene uses: the eye-at-origin view translated by
    // -cameraPos, times the native projection, transposed for the shader.
    C44Matrix view;
    GxXformView(view);
    CWorldScene::s_viewNoTranslate = view; // camera at the origin; per-chunk matrices add their own translation

    C3Vector invCameraPos = { -CWorldScene::s_worldCameraPos.x, -CWorldScene::s_worldCameraPos.y, -CWorldScene::s_worldCameraPos.z };
    view.Translate(invCameraPos);

    C44Matrix proj;
    GxXformProjNative(proj);
    CWorldScene::s_projNative = proj;

    C44Matrix viewProj = view * proj;
    CWorldScene::s_viewProjT = viewProj.Transpose();

    // Data-driven fog: enable it whenever the fog begins within the view distance, so geometry
    // between the fog start and the far plane is hazed even when the fog end lies beyond the far
    // clip (linear fog handles the partial factor). Colours and distances come from Light.dbc.
    float fogEnd = CWorld::GetFogEnd();
    CWorldScene::s_fogActive = fogEnd > 1.0f && CWorld::GetFogStart() < CWorld::GetFarClip();

    // The fog RENDER STATES are not set here any more. CMap::Render already calls
    // CWorld::SetupFogRenderStates, and this block ran afterwards and overwrote its work with a
    // worse conversion: it truncated each channel instead of CM2Lighting::FogColorByte's clamped
    // rounding, never clamped above 1.0 -- so an overbright fog colour overflowed its byte and
    // corrupted the packed value through the shift -- and left alpha at 0 where the reference sets
    // 0xFF. Only the flag survives, which is all Weather asks for.

    CWorldScene::s_viewUpdated = true;
}

// ref: FUN_0078f900
void CWorldScene::ShadeHorizonPolyline(const C3Vector* points, int32_t count, int32_t mark) {
    if (!(CWorld::s_enables & 0x20)) {
        return;
    }

    if (CWorldScene::s_viewDir.z < -0.8999999761581421f || 0.8999999761581421f < CWorldScene::s_viewDir.z) {
        return;
    }

    // DAT_00cd8fd8: the projected points, a scratch the reference keeps as a global.
    static C3Vector s_projected[16];

    if (count > 16) {
        count = 16;
    }

    for (int32_t i = 0; i < count; i++) {
        C3Vector p = points[i] * CWorldScene::s_occlusionMatrix;
        s_projected[i] = p;

        float inv = 1.0f / s_projected[i].z;

        if (0.0f < inv) {
            s_projected[i].x = s_projected[i].x * inv;
            s_projected[i].y = inv * s_projected[i].y;
        }
    }

    for (int32_t i = 0; i < count - 1; i++) {
        const C3Vector& p0 = s_projected[i];
        const C3Vector& p1 = s_projected[i + 1];

        if (!(0.02777777798473835f <= p0.z) || !(0.02777777798473835f <= p1.z)) {
            continue;
        }

        float height = p1.y <= p0.y ? p1.y : p0.y;

        int32_t first = static_cast<int32_t>(std::nearbyint(p0.x * 64.0f - 0.5f)) + 0xc0;
        int32_t last = static_cast<int32_t>(std::nearbyint(p1.x * 64.0f - 0.5f)) + 0xc0;

        if (last < first) {
            int32_t swap = first;
            first = last;
            last = swap;
        }

        if (first < 0) {
            first = 0;
        }

        if (0x17f < last) {
            last = 0x17f;
        }

        if (mark && first <= last) {
            memset(&CWorldScene::s_horizonColumnFlags[first], 1, static_cast<size_t>(last - first + 1));
        }

        for (int32_t c = first; c <= last; c++) {
            if (CWorldScene::s_horizonBuffer[c] < height) {
                CWorldScene::s_horizonBuffer[c] = height;
            }
        }
    }
}

// ref: FUN_007cc9a0
CWorldScene::Occluder* CWorldScene::AllocOccluder() {
    Occluder* occluder = CWorldScene::s_freeOccluders.Head();

    if (!occluder) {
        void* memory = SMemAlloc(sizeof(Occluder), ".?AVCWorldOccluder@@", -2, 0x8);
        occluder = memory ? new (memory) Occluder() : nullptr;

        if (!occluder) {
            return nullptr;
        }
    }

    occluder->m_link.Unlink();

    return occluder;
}

// ref: FUN_007cca90
void CWorldScene::FreeOccluder(Occluder* occluder) {
    occluder->m_link.Unlink();
    CWorldScene::s_freeOccluders.LinkToTail(occluder);
}

// ref: FUN_007927e0
void CWorldScene::AddOccluder(const C3Vector& a, const C3Vector& b) {
    if (CWorldScene::s_viewDir.z < -0.8999999761581421f || 0.8999999761581421f < CWorldScene::s_viewDir.z) {
        return;
    }

    const C4Plane& view = CWorldScene::s_viewPlane2d;
    float distA = a.y * view.n.y + a.z * view.n.z + a.x * view.n.x + view.d;
    float distB = b.y * view.n.y + b.z * view.n.z + b.x * view.n.x + view.d;

    if (distA < 0.10000000149011612f && distB < 0.10000000149011612f) {
        return;
    }

    int32_t bandA = static_cast<int32_t>(std::nearbyint(distA * 0.029999999329447746f - 0.5f));
    int32_t bandB = static_cast<int32_t>(std::nearbyint(distB * 0.029999999329447746f - 0.5f));

    if (0x40 <= bandA && 0x40 <= bandB) {
        return;
    }

    Occluder* occluder = CWorldScene::AllocOccluder();

    if (!occluder) {
        return;
    }

    occluder->a = a;
    occluder->b = b;

    int32_t band = bandA;

    if (bandA != bandB) {
        // Near end first: the edge is cut at each row boundary from the near band outwards, and
        // every piece but the last goes to the row it lies in.
        C3Vector nearPoint;
        C3Vector farPoint;
        int32_t nearBand;
        int32_t farBand;

        if (bandB < bandA) {
            nearPoint = b;
            farPoint = occluder->a;
            nearBand = bandB;
            farBand = bandA;
        } else {
            nearPoint = occluder->a;
            farPoint = b;
            nearBand = bandA;
            farBand = bandB;
        }

        if (nearBand < 0) {
            nearBand = 0;
        }

        if (0x3f < farBand) {
            farBand = 0x3f;
        }

        band = nearBand;

        for (int32_t row = nearBand; row < farBand; row++) {
            const C4Plane& boundary = CWorldScene::s_rowPlanes[row + 1];

            float farDist = boundary.n.x * farPoint.x + boundary.n.z * farPoint.z + boundary.n.y * farPoint.y + boundary.d;
            float span = farDist - (boundary.n.z * nearPoint.z + boundary.n.y * nearPoint.y + boundary.n.x * nearPoint.x + boundary.d);

            C3Vector cut;

            if (span <= 9.999999747378752e-06f) {
                cut = farPoint;
            } else {
                float t = farDist / span;
                cut.y = (nearPoint.y - farPoint.y) * t + farPoint.y;
                cut.z = (nearPoint.z - farPoint.z) * t + farPoint.z;
                cut.x = (nearPoint.x - farPoint.x) * t + farPoint.x;
            }

            Occluder* piece = CWorldScene::AllocOccluder();

            if (piece) {
                piece->a = nearPoint;
                piece->b = cut;
                CWorldScene::s_rows[row].occluders.LinkToTail(piece);
            }

            nearPoint = cut;
        }

        band = farBand;

        occluder->a = nearPoint;
        occluder->b = farPoint;
    }

    CWorldScene::s_rows[band].occluders.LinkToTail(occluder);
}

namespace {

// The fixed occluders' boxes, built once from their vertices the way the reference's static
// initialiser builds them (FUN_007cc890): the vertices' extent, with the bottom lowered by the
// occluder's drop.
struct FixedOccluderBoxes {
    CAaBox boxes[5];

    FixedOccluderBoxes() {
        for (uint32_t i = 0; i < 5; i++) {
            const MapHorizon::SOccluder& occluder = MapHorizon::OCCLUDERS[i];
            CAaBox& box = boxes[i];

            box.b = { 3.4028234663852886e+38f, 3.4028234663852886e+38f, 3.4028234663852886e+38f };
            box.t = { -3.4028234663852886e+38f, -3.4028234663852886e+38f, -3.4028234663852886e+38f };

            for (uint32_t v = 0; v < occluder.vertexCount; v++) {
                const float* p = MapHorizon::VERTICES[occluder.firstVertex + v];

                if (p[0] < box.b.x) { box.b.x = p[0]; }
                if (p[1] < box.b.y) { box.b.y = p[1]; }
                if (box.t.x < p[0]) { box.t.x = p[0]; }
                if (box.t.y < p[1]) { box.t.y = p[1]; }
                if (box.t.z < p[2]) { box.t.z = p[2]; }
                if (p[2] + occluder.drop < box.b.z) { box.b.z = p[2] + occluder.drop; }
            }
        }
    }
};

}

// ref: FUN_007cc810
void CWorldScene::AddFixedOccluders() {
    static FixedOccluderBoxes s_boxes;

    for (uint32_t i = 0; i < 5; i++) {
        const MapHorizon::SOccluder& occluder = MapHorizon::OCCLUDERS[i];

        if (occluder.mapId != CMap::s_mapID) {
            continue;
        }

        if (CWorldScene::BoxOutsideFrustum(s_boxes.boxes[i])) {
            continue;
        }

        for (uint32_t v = 0; v < occluder.vertexCount; v += 2) {
            const float* p0 = MapHorizon::VERTICES[occluder.firstVertex + v];
            const float* p1 = MapHorizon::VERTICES[occluder.firstVertex + v + 1];

            CWorldScene::AddOccluder({ p0[0], p0[1], p0[2] }, { p1[0], p1[1], p1[2] });
        }
    }
}

// ref: FUN_007998a0
void CWorldScene::BucketGroupDoodads(CMapBaseObjRefList* doodads, uint32_t minRow) {
    if (!(CWorld::s_enables & 0x1)) {
        return;
    }

    const C4Plane& plane = CWorldScene::s_viewPlane2d;

    for (auto link = doodads->Head(); link; link = doodads->Next(link)) {
        auto entity = static_cast<CMapStaticEntity*>(link->owner);

        // Placed, not already on a row this frame, and with a model to draw.
        if (!(entity->m_flags & 0x80) || entity->m_rowLink.IsLinked() || !entity->m_model) {
            continue;
        }

        float distance = entity->m_sphere.c.y * plane.n.y + entity->m_sphere.c.z * plane.n.z
            + entity->m_sphere.c.x * plane.n.x + plane.d - entity->m_sphere.r;

        uint32_t row = minRow;

        if (!(distance < 0.0f)) {
            uint32_t band = static_cast<uint32_t>(static_cast<int32_t>(std::nearbyint(distance * 0.029999999329447746f - 0.5f)));

            if (0x40 <= band) {
                continue;
            }

            if (minRow <= band) {
                row = band;
            }
        }

        CWorldScene::s_rows[row].staticEntities.LinkToTail(entity);
    }
}

namespace {

// Whether any frustum the portal walk left on a group holds the sphere (FUN_00983fb0 per
// frustum, along the list).
bool SphereInGroupFrustums(CWFrustum* frustums, const CAaSphere& sphere) {
    for (CWFrustum* frustum = frustums; frustum; frustum = frustum->link.Next()) {
        if (frustum->SphereInside(sphere)) {
            return true;
        }
    }

    return false;
}

}

// ref: FUN_00799b70
void CWorldScene::VisitGroupDoodads(CMapBaseObjRefList* doodads, CWFrustum* frustums, uint32_t band, int32_t interior) {
    for (auto link = doodads->Head(); link; link = doodads->Next(link)) {
        auto entity = static_cast<CMapStaticEntity*>(link->owner);

        if (entity->m_detailLevel < band) {
            continue;
        }

        if (!entity->m_model || !(entity->m_flags & 0x80)) {
            // Not drawable yet: what its box would hide is still hidden.
            if (entity->m_model && entity->m_model->m_shared) {
                CAaBox box = TransformBox(entity->m_model->m_shared->aaBox154, static_cast<CMapDoodadDef*>(entity)->m_placement);
                CWorldScene::SubmitOccluderBox(box, 10.0f);
            }

            continue;
        }

        if (entity->m_frameStamp != CWorldScene::s_frameStamp) {
            entity->m_frameStamp = CWorldScene::s_frameStamp;
            entity->m_visible = 1;

            float dx = entity->m_sphere.c.x - CWorldScene::s_cameraPos.x;
            float dy = entity->m_sphere.c.y - CWorldScene::s_cameraPos.y;
            float dz = entity->m_sphere.c.z - CWorldScene::s_cameraPos.z;

            bool close = (entity->m_flags7c & 0x400) || dz * dz + dy * dy + dx * dx < 100.0f;
            entity->m_model->SetAnimating(close ? 1 : 0);
        }

        if (!entity->m_visible || !frustums || !SphereInGroupFrustums(frustums, entity->m_sphere)) {
            continue;
        }

        entity->m_visible = 0;

        if (interior) {
            entity->m_flags |= 0x8000;
        } else {
            entity->m_flags &= ~0x8000u;
        }

        CWorldScene::VisitStaticEntity(entity);
        CWorldScene::s_visibleDoodadCount++;
    }
}

// ref: FUN_00793270
void CWorldScene::VisitGroupEntities(CMapBaseObjRefList* entities, CWFrustum* frustums, int32_t force, int32_t interior) {
    CWorldScene::s_visitingGroupEntities = 0;

    for (auto link = entities->Head(); link; ) {
        auto next = entities->Next(link);
        auto entity = static_cast<CMapEntity*>(link->owner);

        link = next;

        if ((!(entity->m_flags7c & 0x1) && !force) || !entity->m_visible || (entity->m_flags7c & 0x4)) {
            continue;
        }

        entity->m_visible = 1;

        if (!frustums || !SphereInGroupFrustums(frustums, entity->m_sphere)) {
            continue;
        }

        entity->m_entityRowLink.Unlink();
        entity->m_visible = 0;

        if (interior) {
            entity->m_flags |= 0x8000;
        } else {
            entity->m_flags &= ~0x8000u;
        }

        if (entity->m_model) {
            entity->m_model->SetAnimating(1);

            uint32_t draw = ~(entity->m_flags7c >> 2) & 0x1;

            if (entity->m_model->m_attachParent) {
                entity->m_model->m_flag80 = draw;
                entity->m_model->m_flag20000 = draw;
            } else {
                entity->m_model->m_flag8 = draw;
                entity->m_model->m_flag10000 = draw;
            }
        }

        // The entity's own handler may keep it out of the frame (event 5); with no handler it
        // joins.
        typedef int32_t (*Handler)(void* param, int32_t event, uint32_t guidLow, uint32_t guidHigh, uint32_t param32);
        auto handler = reinterpret_cast<Handler>(entity->m_handler);

        if (!handler || handler(entity->m_handlerParam, 5, static_cast<uint32_t>(entity->m_param64), static_cast<uint32_t>(entity->m_param64 >> 32), entity->m_param32)) {
            CWorldScene::s_frameEntityList.LinkToTail(entity);
        }
    }

    CWorldScene::s_visitingGroupEntities = 1;
}

// ref: FUN_0079a260
void CWorldScene::VisitVisibleGroupContents() {
    for (auto defGroup = CWorldScene::s_visibleMapObjGroups.Head(); defGroup; ) {
        auto next = CWorldScene::s_visibleMapObjGroups.Next(defGroup);

        auto parent = defGroup->m_parentLinkList.Head();
        auto def = parent ? static_cast<CMapObjDef*>(parent->ref) : nullptr;

        if (def && def->m_mapObj) {
            uint32_t flags = def->m_mapObj->GroupFlags(defGroup->m_groupIndex);

            if (def == CWorldScene::s_cameraDef || !(flags & 0x10008)) {
                uint32_t band = static_cast<uint32_t>(CWorldScene::DistanceBand(defGroup->m_sortDistance));
                int32_t interior = (defGroup->m_flags & 0x8000) != 0;

                CWorldScene::VisitGroupDoodads(&defGroup->m_doodadDefLinkList, defGroup->m_frustums.Head(), band, interior);
                CWorldScene::VisitGroupEntities(&defGroup->m_entityLinkList, defGroup->m_frustums.Head(), 0, interior);
            }
        }

        defGroup = next;
    }
}

// ref: FUN_00793450
void CWorldScene::FinishHiddenEntities() {
    for (auto entity = CWorldScene::s_hiddenEntities.Head(); entity; entity = CWorldScene::s_hiddenEntities.Next(entity)) {
        entity->m_entityRowLink.Unlink();

        uint32_t close = 0;
        uint32_t unreached = 0;

        if (entity->m_model) {
            bool animate = false;

            if (!(entity->m_flags7c & 0x4000)) {
                float dx = entity->m_sphere.c.x - CWorldScene::s_cameraPos.x;
                float dy = entity->m_sphere.c.y - CWorldScene::s_cameraPos.y;
                float dz = entity->m_sphere.c.z - CWorldScene::s_cameraPos.z;

                animate = dy * dy + dz * dz + dx * dx < 1111.111083984375f;

                if (animate) {
                    close = 2;
                }

                animate = animate || (entity->m_flags7c & 0x400) != 0;

                if (!entity->m_model->m_attachParent) {
                    entity->m_model->m_flag8 = 0;
                    entity->m_model->m_flag10000 = 0;
                } else {
                    entity->m_model->m_flag80 = 0;
                    entity->m_model->m_flag20000 = 0;
                }
            }

            entity->m_model->SetAnimating(animate ? 1 : 0);
        }

        typedef int32_t (*Handler)(void* param, int32_t event, uint32_t guidLow, uint32_t guidHigh, uint32_t param32);
        auto handler = reinterpret_cast<Handler>(entity->m_handler);

        if (handler) {
            if (entity->m_visible < 2) {
                unreached = 4;
            }

            handler(entity->m_handlerParam, static_cast<int32_t>(unreached | close),
                    static_cast<uint32_t>(entity->m_param64), static_cast<uint32_t>(entity->m_param64 >> 32), entity->m_param32);
        }
    }
}

// ref: FUN_00799f80
void CWorldScene::VisitCandidateGroups(const ViewWindow* window) {
    CWorldScene::s_frustumDepth++;
    CWorldScene::s_frustums[CWorldScene::s_frustumDepth] = CWorldScene::s_frustums[CWorldScene::s_frustumDepth - 1];
    CWorldScene::SubFrustum(CWorldScene::s_frustumCorners, window);

    for (auto defGroup = CWorldScene::s_mapObjDefGroupCandidates.Head(); defGroup; ) {
        auto next = CWorldScene::s_mapObjDefGroupCandidates.Next(defGroup);

        defGroup->m_rowLink.Unlink();

        bool touches = false;

        for (auto seen = CWorldScene::s_visibleMapObjGroups.Head(); seen; seen = CWorldScene::s_visibleMapObjGroups.Next(seen)) {
            const CAaBox& a = defGroup->m_bounds;
            const CAaBox& b = seen->m_bounds;

            if (a.b.x <= b.t.x && a.b.y <= b.t.y && a.b.z <= b.t.z
                && b.b.x <= a.t.x && b.b.y <= a.t.y && b.b.z <= a.t.z) {
                touches = true;
                break;
            }
        }

        if ((touches || 0.0f <= CWorldScene::s_portalWindow.depth)
            && AaBoxVsPlanes6(CWorldScene::s_frustums[CWorldScene::s_frustumDepth].planes, defGroup->m_bounds)) {
            auto parent = defGroup->m_parentLinkList.Head();
            auto def = parent ? static_cast<CMapObjDef*>(parent->ref) : nullptr;

            if (def) {
                CWorldScene::VisitMapObjDefGroup(def, defGroup, window, 1);
            }

            uint32_t band = static_cast<uint32_t>(CWorldScene::DistanceBand(defGroup->m_sortDistance));
            int32_t interior = (defGroup->m_flags & 0x8000) != 0;

            CWorldScene::VisitGroupDoodads(&defGroup->m_doodadDefLinkList, defGroup->m_frustums.Head(), band, interior);
            CWorldScene::VisitGroupEntities(&defGroup->m_entityLinkList, defGroup->m_frustums.Head(), 1, interior);
        }

        defGroup = next;
    }

    CWorldScene::s_frustumDepth--;
}
