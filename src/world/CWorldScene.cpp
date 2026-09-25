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
#include <tempest/Intersect.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>
#include <cmath>
#include <cstring>

static const float CHUNK_SIZE = 33.33333206176758f;
static const float CHUNKS_PER_UNIT = 0.0299999993f;         // DAT_00a3f7ec
static const float MAP_HALF_EXTENT = 17066.666f;

STORM_EXPLICIT_LIST(CMapRenderChunk, m_link) CWorldScene::s_renderChunkLists[CWorldScene::RENDER_LIST_COUNT];
CWorldScene::Row CWorldScene::s_rows[CWorldScene::ROW_COUNT];
C4Plane CWorldScene::s_rowPlanes[CWorldScene::ROW_COUNT];
C3Vector CWorldScene::s_frustumCorners[8];
CWorldScene::Frustum CWorldScene::s_frustums[CWorldScene::FRUSTUM_DEPTH_MAX];
int32_t CWorldScene::s_frustumDepth;
C3Vector CWorldScene::s_cameraTarget;
C3Vector CWorldScene::s_viewDir;
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
uint32_t CWorldScene::s_rowStats[0x60];
float CWorldScene::s_farChunkDistance;
float CWorldScene::s_nearChunkDistance;
float CWorldScene::s_occluderFarClip;
int32_t CWorldScene::s_visibleChunkCount;
int32_t CWorldScene::s_visibleMapObjCount;
int32_t CWorldScene::s_visibleEntityCount;
int32_t CWorldScene::s_visibleCount8624;
int32_t CWorldScene::s_frameStamp;
void* CWorldScene::s_cameraGroup;
float CWorldScene::s_cameraGroundHeight;
int32_t CWorldScene::s_hasMapObjs;
CWorldScene::ViewWindow CWorldScene::s_window;
CWorldScene::ViewWindow CWorldScene::s_portalWindow;
const int32_t CWorldScene::s_quadrantVertex[4] = { 0, 8, 0x88, 0x90 };

static_assert(sizeof(CWorldScene::Frustum) == 0xfc, "a traversal frustum is 0xfc bytes");

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

// ----------------------------------------------------------------------------------------------
// The camera and its frustum

// ref: FUN_007912c0
// The plane through three points, facing along (b - a) x (c - a)
static void PlaneFromPoints(C4Plane* plane, const C3Vector& a, const C3Vector& b, const C3Vector& c) {
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

// ref: FUN_004c2270
// A row vector through a matrix
static void TransformVector4(C4Vector* out, const C4Vector& v, const C44Matrix& m) {
    out->x = v.x * m.a0 + m.c0 * v.z + m.b0 * v.y + m.d0 * v.w;
    out->y = m.c1 * v.z + m.a1 * v.x + m.b1 * v.y + m.d1 * v.w;
    out->z = m.c2 * v.z + m.a2 * v.x + m.b2 * v.y + m.d2 * v.w;
    out->w = m.c3 * v.z + m.a3 * v.x + m.b3 * v.y + m.d3 * v.w;
}

// ref: FUN_006bf6d0
// The eight corners of a view frustum in the space the view matrix maps from: the clip-space
// cube unprojected through the inverse view-projection. A perspective projection is walked at
// its near and far distances with w set to the view depth, so no divide is needed; an
// orthographic one at the unit cube. Near face first, each face (-x,-y) (-x,+y) (+x,+y) (+x,-y).
static void FrustumCorners(const C44Matrix& view, const C44Matrix& proj, C3Vector* corners) {
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

// ref: FUN_00984240
void CWorldScene::Frustum::SetCorners(const C3Vector* corners) {
    for (int32_t i = 0; i < 8; i++) {
        this->corners[i] = corners[i];
    }

    this->ComputePlanes();
}

// ref: FUN_00983e70
// The four side planes and the far plane from the corners; the near plane is the far plane
// turned around through a near corner
void CWorldScene::Frustum::ComputePlanes() {
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
int32_t CWorldScene::Frustum::SphereInside(const CAaSphere& sphere) {
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

    CWorldScene::s_window = { 3.4028235e+38f, 3.4028235e+38f, -3.4028235e+38f, -3.4028235e+38f, -1.0f, 0.0f, 0.0f };
    CWorldScene::s_portalWindow = { 3.4028235e+38f, 3.4028235e+38f, -3.4028235e+38f, -3.4028235e+38f, -1.0f, 0.0f, 0.0f };
    // TODO DAT_00cd8620 = 0, DAT_00cd861c = 0, FUN_00794190(&array, 0) twice

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

    // TODO the horizon occlusion matrix: identity when the view is vertical, otherwise the
    // flattened view (FUN_006bfe60) translated by -cameraPos times the projection
    C44Matrix identity;
    CWorldScene::s_occlusionMatrix = identity;

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
int32_t CWorldScene::SphereOccludedByVolumes(const CAaSphere& sphere) {
    // TODO the volume list: for each, every plane's signed distance to the centre must be at
    // most -radius for the sphere to be occluded
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

// ref: FUN_00790af0
// Narrows the current frustum to a window of the screen: the near and far faces' corners are
// interpolated across the window's rectangle
void CWorldScene::SubFrustum(const ViewWindow* window) {
    C3Vector corners[8];
    const C3Vector* src = CWorldScene::s_frustumCorners;

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
    // TODO FUN_007cd850(&cameraPos, s_frustumCorners, portal)

    CWorldScene::s_frustumDepth++;
    CWorldScene::s_frustums[CWorldScene::s_frustumDepth] = CWorldScene::s_frustums[CWorldScene::s_frustumDepth - 1];
    CWorldScene::SubFrustum(window);

    for (uint32_t i = 0; i < ROW_COUNT; i++) {
        auto row = &CWorldScene::s_rows[i];

        CWorldScene::TraverseRowChunks(row, i);
        // TODO FUN_0079a160(row, window, portal)
        // TODO FUN_007935a0(row)
        // TODO FUN_00793060(row)

        int32_t band = CWorldScene::DistanceBand(static_cast<float>(i) * CHUNK_SIZE);
        // TODO FUN_007987a0(row, band)
        // TODO FUN_00793760(row)
        (void)band;
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
    const Frustum& frustum = CWorldScene::s_frustums[CWorldScene::s_frustumDepth];

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
        // TODO FUN_00799980(&chunk->m_entityLinkList, band)
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
