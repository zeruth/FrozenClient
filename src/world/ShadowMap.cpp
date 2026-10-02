#include "world/ShadowMap.hpp"
#include <tempest/Matrix.hpp>
#include <cmath>
#include "gx/Transform.hpp"
#include "gx/Types.hpp"
#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/CGxCaps.hpp"
#include "gx/Gx.hpp"
#include "world/MapShadow.hpp"
#include "common/Handle.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/RenderTarget.hpp"
#include "gx/Texture.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include <cstdio>
#include <cstring>

int32_t g_shadowMapQuality = 0;
int32_t g_shadowMapSize = 1024;
float g_shadowMapPcfTaps[8][4] = { { 0.0f, 0.0f, 0.0f, 0.0f } };
int32_t g_shadowMapRealloc = 0;
float g_shadowMapFogScale = 1.0f;

// The map size as a float (DAT_00d431bc), which the light matrices divide by.
static float s_sizeFloat;

// The permutation per quality (DAT_00b1d554)
static const int32_t s_shaderLevels[7] = { 0, 1, 1, 2, 2, 3, 3 };

// ref: FUN_00873f80
// The quality in effect: 0 while a reallocation is pending.
int32_t ShadowMapGetQuality() {
    return g_shadowMapRealloc ? 0 : g_shadowMapQuality;
}

// ref: FUN_00873ff0
int32_t ShadowMapGetShaderLevel() {
    int32_t quality = g_shadowMapRealloc ? 0 : g_shadowMapQuality;
    return s_shaderLevels[quality];
}

// ref: FUN_00873f60
// The description the console prints when the quality changes. Six levels plus a seventh entry for
// anything out of range, read out of the reference's table at 0x00b1d538 on 2026-09-26.
const char* ShadowMapQualityName(int32_t quality) {
    static const char* const s_names[7] = {
        "[LOWEST]Precomputed terrain and no dynamic shadows.",
        "[LOW]Precomputed terrain and dynamic PC/NPC shadows (low-res).",
        "[MEDIUM]Precomputed terrain and dynamic PC/NPC shadows (high-res).",
        "[MED-HIGH]Full environmental and PC/NPC shadows, low-res, lg-dist.",
        "[HIGH]Full environmental and PC/NPC shadows, hi-res, lg-dist.",
        "[VERY HIGH]Cascaded shadow maps.",
        "[INVALID]Unsupported quality level."
    };

    return static_cast<uint32_t>(quality) < 7 ? s_names[quality] : s_names[6];
}

// ref: FUN_008740d0
// Can this device do that quality? Three things decide it: the graphics API, the vertex and pixel
// shader profiles, and whether a format the map can be rendered into exists.
//
// THE CAPS OFFSETS ARE NOW IDENTIFIED. The reference reads caps+0xb4 and caps+0xc4 and compares
// them against 1/2/3 and 6/10 for the first, 3/4 and 0xb/0xc for the second. Those are exactly
// EGxShVertexShader vs_1_1/vs_2_0/vs_3_0 and arbvp1/nvvp3, and EGxShPixelShader ps_2_0/ps_3_0 and
// nvfp2/arbfp1 -- so caps+0xb4 is m_shaderTargets and caps+0xc4 is m_shaderTargets[GxSh_Pixel],
// four ints along, which is what GxSh_Pixel == 4 gives. CGxCaps.hpp's note about unnamed reference
// offsets is updated with this.
//
// caps+0xac and caps+0xa8, checked as "either is non-zero", are READ as m_texFmt[GxTex_D24X8] and
// m_texFmt[GxTex_R32F]: those are the last two entries of m_texFmt, they sit three and two ints
// below m_shaderTargets where the array's tail would land, and they are precisely the two formats
// the shadow targets are created with. Stated as a reading because the middle of the reference's
// CGxCaps is still unmapped, not as a measurement.
int32_t ShadowMapQualitySupported(int32_t quality) {
    // Four separate reads, because the reference makes four separate calls to its caps accessor --
    // one per field -- rather than holding a reference to the object.
    int32_t vertexProfile = GxCaps().m_shaderTargets[GxSh_Vertex];
    int32_t pixelProfile = GxCaps().m_shaderTargets[GxSh_Pixel];

    int32_t targetOk = (GxCaps().m_texFmt[GxTex_D24X8] || GxCaps().m_texFmt[GxTex_R32F]) ? 1 : 0;

    EGxApi api = g_theGxDevicePtr ? g_theGxDevicePtr->m_api : GxApi_OpenGl;

    switch (quality) {
    case 0:
        return 1;

    case 1:
    case 2:
        switch (api) {
        case GxApi_D3d9:
        case GxApi_D3d9Ex:
            if (vertexProfile != GxShVS_vs_1_1 && vertexProfile != GxShVS_vs_2_0
                && vertexProfile != GxShVS_vs_3_0) {
                return 0;
            }

            // ps_2_0 goes straight to the target check; anything else must be ps_3_0 first.
            if (pixelProfile != GxShPS_ps_2_0 && pixelProfile != GxShPS_ps_3_0) {
                return 0;
            }

            return targetOk;

        case GxApi_D3d10:
        case GxApi_D3d11:
            return targetOk;

        case GxApi_GLL:
            if ((vertexProfile == GxShVS_arbvp1 || vertexProfile == GxShVS_nvvp3)
                && (pixelProfile == GxShPS_arbfp1 || pixelProfile == GxShPS_nvfp2)) {
                return targetOk;
            }

            return 0;

        default:
            return 0;
        }

    case 3:
    case 4:
    case 5:
        switch (api) {
        case GxApi_D3d9:
        case GxApi_D3d9Ex:
            // The cascades want vs_3_0 and ps_3_0, nothing less.
            if (vertexProfile != GxShVS_vs_3_0 || pixelProfile != GxShPS_ps_3_0) {
                return 0;
            }

            return targetOk;

        case GxApi_D3d10:
        case GxApi_D3d11:
            return targetOk;

        default:
            return 0;
        }

    default:
        return 0;
    }
}

// ref: FUN_00874210
// Take a new quality, if the device can do it. The realloc flag going up is what makes
// ShadowMapGetQuality report 0 until the targets have been rebuilt, so the frame that changes the
// setting draws unshadowed rather than sampling a map that is the wrong size.
int32_t ShadowMapSetQuality(int32_t quality) {
    if (!ShadowMapQualitySupported(quality)) {
        return 0;
    }

    g_shadowMapQuality = quality;
    g_shadowMapRealloc = 1;

    return 1;
}

// ref: FUN_00873fe0
// The device-restore hook: after a reset every target is gone, so ask for the realloc.
void ShadowMapDeviceRestore() {
    g_shadowMapRealloc = 1;
}

// The light direction the shadow passes sample with, in two forms: as given (DAT_00d43180, which
// the rest of MapShadow.cpp reads) and turned by the current world matrix and re-normalised
// (DAT_00d4318c, which IS the pixel c4 constant ShadowMapBindScene uploads).
C3Vector g_shadowMapLightDir = { 0.0f, 0.0f, -1.0f };
C3Vector g_shadowMapLightDirWorld = { 0.0f, 0.0f, -1.0f };

// ref: FUN_00875c10
// The reference takes a second argument -- the camera position, from the map's own copy at
// 0x00cd8f74 -- and never reads it, so this drops it.
//
// The rotation is the current world matrix's 3x3 block, pulled off the transform stack. It is the
// identity at the only call site, which is why the stored pair comes out equal there; the multiply
// is reproduced anyway because the reference does it unconditionally.
void ShadowMapSetLightDirection(const C3Vector& dir) {
    g_shadowMapLightDir = dir;

    C44Matrix world;
    GxXformWorld(world);

    C3Vector turned = {
        dir.x * world.a0 + dir.y * world.b0 + dir.z * world.c0,
        dir.x * world.a1 + dir.y * world.b1 + dir.z * world.c1,
        dir.x * world.a2 + dir.y * world.b2 + dir.z * world.c2
    };

    float length = sqrtf(turned.x * turned.x + turned.y * turned.y + turned.z * turned.z);

    if (length > 0.0f) {
        float inv = 1.0f / length;
        turned.x *= inv;
        turned.y *= inv;
        turned.z *= inv;
    }

    g_shadowMapLightDirWorld = turned;
}

// The intensity and the flag at DAT_00b1d518 / DAT_00d43168.
float g_shadowMapIntensity = 1.0f;
int32_t g_shadowMapFlag = 0;

// ref: FUN_00874010
// Two stores and nothing else. Worth recording that NOTHING in the reference reads either global:
// a text-section sweep for both addresses finds exactly one reference each, this write. So the
// values are inert there too, and frozen keeps them for the same reason -- to have the state the
// reference has -- not because a pass depends on them.
void ShadowMapSetIntensity(float intensity, int32_t flag) {
    g_shadowMapIntensity = intensity;
    g_shadowMapFlag = flag;
}

// The PCF kernel in TEXELS, before the map-size scale. Every one of these sixteen values was
// read out of the reference's .rdata on 2026-09-28 rather than off an image, and they agree with
// what was there: 0.8, -0.2, 0.2, 1.0, -0.6, 0.6, -1.0, -0.4 against -1.0, -0.8, -0.6, -0.4,
// -0.2, 0.2, -0.4, -0.6.
//
// THE SCALE IS THE WHOLE POINT and it was missing. FUN_008742e0 multiplies every one of these by
// 1 / mapSize before the shader ever sees them, so a tap of 1.0 is ONE TEXEL. frozen uploaded the
// raw table, where 1.0 is the entire shadow map -- a kernel eight taps wide across 1024 texels
// instead of across one. The comment here even said 'already divided by the map size'; nothing
// divided them.
static const float s_pcfTapsTexels[8][2] = {
    {  0.8f, -1.0f },
    { -0.2f, -0.8f },
    {  0.2f, -0.6f },
    {  1.0f, -0.4f },
    { -0.6f, -0.2f },
    {  0.6f,  0.2f },
    { -1.0f, -0.4f },
    { -0.4f, -0.6f },
};

// The map size the quality asks for: 2048 at quality 2 and at anything above 3, 1024 otherwise.
//
// Read off the head of FUN_00875d30, where the compare order makes it look stranger than it is --
// `if (3 < quality || (size = 1024, quality == 2)) size = 2048` assigns the default inside the
// condition, so quality 2 takes 2048 and quality 3 keeps the 1024 it was just given.
void ShadowMapUpdateSize() {
    g_shadowMapSize = (g_shadowMapQuality > 3 || g_shadowMapQuality == 2) ? 0x800 : 0x400;
}

// ref: FUN_008742e0
// Rebuild the PCF kernel for the current map size: each texel offset divided by the edge, .zw
// zero. The reference keeps the reciprocal itself at DAT_00d43200, which is also the fourth
// tap's x -- the two overlap in memory because that tap's offset is exactly one texel.
//
// It runs from the reallocation path, because that is the only thing that changes the size.
void ShadowMapBuildPcfTaps() {
    s_sizeFloat = static_cast<float>(static_cast<uint32_t>(g_shadowMapSize));

    float inv = g_shadowMapSize ? 1.0f / static_cast<float>(g_shadowMapSize) : 0.0f;

    for (int32_t i = 0; i < 8; i++) {
        g_shadowMapPcfTaps[i][0] = s_pcfTapsTexels[i][0] * inv;
        g_shadowMapPcfTaps[i][1] = s_pcfTapsTexels[i][1] * inv;
        g_shadowMapPcfTaps[i][2] = 0.0f;
        g_shadowMapPcfTaps[i][3] = 0.0f;
    }
}

// ------------------------------------------------------------------------------------------------
// The maps
// ------------------------------------------------------------------------------------------------

// The callbacks MapShadow.cpp registers (DAT_00d43158..DAT_00d43164).
static ShadowBuildMatrixCallback s_buildMatrix;
static ShadowSetupViewCallback s_setupView;
static ShadowCollectCallback s_collect;
static ShadowRenderCallback s_render;

// The lit-pass target and its depth twin, and the main map (DAT_00d43250, DAT_00d43254,
// DAT_00d43148). With hardware PCF the shaders sample the DEPTH textures and the colour one is a
// dummy the device needs bound; without it the colour targets are R32F and hold the depth.
static HTEXTURE s_litColor;
static HTEXTURE s_litDepth;
static HTEXTURE s_mainMap;

// The cascades have to be cleared and restarted (DAT_00d4314c): the map was reloaded under them.
int32_t g_shadowMapCascadesStale;

// Bumped every render step; the collect callback gets it at quality 5 (DAT_00d4316c).
static uint8_t s_frame;

// The depth scale the caster pixel shader writes with and the light matrices read back
// (DAT_00d431c8), and the map size as a float (DAT_00d431bc).
static float s_depthScale;

// The twelve rows every shadowed shader samples with: the main map's three, then each cascade's
// (DAT_00d43348).
static float s_lightMatrices[12][4];

ShadowCascade g_shadowCascades[3];
C3Vector g_shadowMainCenter;
float g_shadowMainExtent;
C3Vector g_shadowMainUp;
float g_shadowMainScissor[4];
int32_t g_shadowMapCasterLevel;
C4Plane g_mapShadowPlane = { { 0.0f, 0.0f, 0.0f }, 0.0f };
C4Plane g_mapShadowPlaneCascade = { { 0.0f, 0.0f, 0.0f }, 0.0f };

// The cascades' half-sides and how far the focus may drift, squared, before one is redrawn
// around it (DAT_00b1d520, DAT_00b1d52c).
static const float s_cascadeExtents[3] = { 40.0f, 160.0f, 640.0f };
static const float s_cascadeRecenter[3] = { 4.0f, 16.0f, 1024.0f };

// The texture callback the reference hands TextureCreate for a render target (FUN_005eeb70, a
// nullsub): the device fills the texels, nothing uploads them.
static void ShadowTargetCallback(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&) {
}

static CGxTex* ShadowGxTex(HTEXTURE texture) {
    return texture ? TextureGetGxTex(texture, 1, nullptr) : nullptr;
}

ShadowView::ShadowView() {
    memset(this->mask, 0, sizeof(this->mask));
    memset(this->active, 0, sizeof(this->active));
    memset(this->behind, 0, sizeof(this->behind));
    memset(this->bounds, 0, sizeof(this->bounds));
    memset(this->scissor, 0, sizeof(this->scissor));
    memset(this->center, 0, sizeof(this->center));
    memset(this->buffer, 0, sizeof(this->buffer));
    memset(this->extent, 0, sizeof(this->extent));
    memset(this->ortho, 0, sizeof(this->ortho));
    memset(this->viewport, 0, sizeof(this->viewport));

    for (int32_t i = 0; i < 3; i++) {
        this->projection[i].Identity();
    }

    this->last = -1;
    this->whole = 0;
}

// ref: FUN_00873fa0
void ShadowMapSetBuildMatrixCallback(ShadowBuildMatrixCallback callback) {
    s_buildMatrix = callback;
}

// ref: FUN_00873fb0
void ShadowMapSetSetupViewCallback(ShadowSetupViewCallback callback) {
    s_setupView = callback;
}

// ref: FUN_00873fc0
void ShadowMapSetCollectCallback(ShadowCollectCallback callback) {
    s_collect = callback;
}

// ref: FUN_00873fd0
void ShadowMapSetRenderCallback(ShadowRenderCallback callback) {
    s_render = callback;
}

// ref: FUN_00874030
void ShadowMapSetDepthScale(float scale) {
    s_depthScale = scale;
}

// ref: FUN_00874240
// Close every map, the cascades' included.
//
// NOT PORTED: the reference ends by registering ShadowMapDeviceRestore through the device's
// vtable slot 0x80. frozen has no device-restore callback registry, so after a real device reset
// nothing asks for the realloc; that wants the registry, not a call at the wrong time.
void ShadowMapReleaseTargets() {
    if (s_litColor) {
        HandleClose(s_litColor);
        s_litColor = nullptr;
    }

    if (s_litDepth) {
        HandleClose(s_litDepth);
        s_litDepth = nullptr;
    }

    if (s_mainMap) {
        HandleClose(s_mainMap);
        s_mainMap = nullptr;
    }

    for (auto& cascade : g_shadowCascades) {
        for (auto& texture : cascade.textures) {
            if (texture) {
                HandleClose(texture);
                texture = nullptr;
            }
        }
    }
}

// ref: FUN_00875760
// Clear every map to white -- "nothing casts here" -- and restart the cascades from nothing.
static void ShadowMapResetCascades() {
    if (g_shadowMapQuality < 3) {
        g_shadowMapCascadesStale = 0;
        return;
    }

    int32_t pcf = CShaderEffect::s_usePcfFiltering;
    CImVector white = { 0xFF, 0xFF, 0xFF, 0xFF };

    CGxTex* savedColor = nullptr;
    CGxTex* savedDepth = nullptr;
    GxRenderTargetGet(GxBuffers_Color, savedColor);

    float viewport[6];
    GxXformViewport(viewport[0], viewport[1], viewport[2], viewport[3], viewport[4], viewport[5]);

    if (pcf) {
        GxRsSet(GxRs_ColorWrite, 0);
        GxRenderTargetGet(GxBuffers_Depth, savedDepth);
    }

    GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

    if (!pcf) {
        GxRenderTargetSet(GxBuffers_Depth, nullptr, 0);

        if (s_litColor) {
            GxRenderTargetSet(GxBuffers_Color, ShadowGxTex(s_litColor), 0);
            GxSceneClear(3, white);
        }

        if (s_mainMap) {
            GxRenderTargetSet(GxBuffers_Color, ShadowGxTex(s_mainMap), 0);
            GxSceneClear(3, white);
        }
    } else {
        if (s_litColor) {
            GxRenderTargetSet(GxBuffers_Color, ShadowGxTex(s_litColor), 0);
        }

        if (s_litDepth) {
            GxRenderTargetSet(GxBuffers_Depth, ShadowGxTex(s_litDepth), 0);
            GxSceneClear(3, white);
        }

        if (s_mainMap) {
            GxRenderTargetSet(GxBuffers_Depth, ShadowGxTex(s_mainMap), 0);
            GxSceneClear(3, white);
        }
    }

    // The cascades go into the depth slot with PCF and the colour slot without.
    EGxBuffer slot;

    if (!pcf) {
        GxRenderTargetSet(GxBuffers_Depth, nullptr, 0);
        slot = GxBuffers_Color;
    } else {
        slot = GxBuffers_Depth;

        if (s_litColor) {
            GxRenderTargetSet(GxBuffers_Color, ShadowGxTex(s_litColor), 0);
        }
    }

    for (auto& cascade : g_shadowCascades) {
        for (auto texture : cascade.textures) {
            if (texture) {
                GxRsSet(GxRs_ScissorTest, 0);
                GxRenderTargetSet(slot, ShadowGxTex(texture), 0);
                GxSceneClear(3, white);
            }
        }
    }

    GxXformSetViewport(viewport[0], viewport[1], viewport[2], viewport[3], viewport[4], viewport[5]);

    GxRsSet(GxRs_ScissorTest, 0);
    GxRenderTargetSet(GxBuffers_Color, savedColor, 0);

    if (pcf) {
        GxRsSet(GxRs_ColorWrite, 0xF);
        GxRsSet(GxRs_ScissorTest, 0);
        GxRenderTargetSet(GxBuffers_Depth, savedDepth, 0);
    }

    for (auto& cascade : g_shadowCascades) {
        cascade.center = { 0.0f, 0.0f, 0.0f };
        cascade.pending = { 0.0f, 0.0f, 0.0f };
        cascade.tile = 0;
        cascade.visible = 0;
    }

    g_shadowMapCascadesStale = 0;
}

// ref: FUN_00875d30
// Make the maps the quality asks for: the size, the kernel, the lit pair and the main map, the
// main map's extent, and at quality 3 and up the cascades -- two maps each below quality 5, one
// at 5. Every target starts cleared.
void ShadowMapEnsureTargets() {
    ShadowMapUpdateSize();
    ShadowMapBuildPcfTaps();

    s_litColor = nullptr;
    s_litDepth = nullptr;
    s_mainMap = nullptr;

    for (auto& cascade : g_shadowCascades) {
        cascade.textures[0] = nullptr;
        cascade.textures[1] = nullptr;
    }

    int32_t pcf = CShaderEffect::s_usePcfFiltering;

    // Point sampled without PCF, filtered with it (the hardware compare wants it), clamped, and a
    // render target either way: the reference builds these bits by hand on top of the defaults.
    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1);
    flags.m_filter = pcf ? GxTex_Linear : GxTex_Nearest;
    flags.m_wrapU = 0;
    flags.m_wrapV = 0;
    flags.m_renderTarget = 1;

    EGxTexFormat format = pcf ? GxTex_D24X8 : GxTex_R32F;
    uint32_t size = static_cast<uint32_t>(g_shadowMapSize);

    if (g_shadowMapQuality != 0) {
        if (!pcf) {
            s_litColor = TextureCreate(GxTex_2d, size, size, 0, format, format, flags, nullptr, ShadowTargetCallback, "ShadowCache", 0);
            s_litDepth = nullptr;
        } else {
            s_litColor = TextureCreate(GxTex_2d, size, size, 0, GxTex_Argb8888, GxTex_Argb8888, flags, nullptr, ShadowTargetCallback, "ShadowCache", 0);
            s_litDepth = TextureCreate(GxTex_2d, size, size, 0, format, format, flags, nullptr, ShadowTargetCallback, "ShadowCache", 0);
        }

        s_mainMap = TextureCreate(GxTex_2d, size, size, 0, format, format, flags, nullptr, ShadowTargetCallback, "ShadowCache", 0);

        g_shadowMainExtent = 20.0f;

        if (g_shadowMapQuality > 2) {
            for (int32_t i = 0; i < 3; i++) {
                auto& cascade = g_shadowCascades[i];

                cascade.textures[0] = TextureCreate(GxTex_2d, size, size, 0, format, format, flags, nullptr, ShadowTargetCallback, "ShadowCache", 0);

                if (g_shadowMapQuality < 5) {
                    cascade.textures[1] = TextureCreate(GxTex_2d, size, size, 0, format, format, flags, nullptr, ShadowTargetCallback, "ShadowCache", 0);
                }

                cascade.extent = s_cascadeExtents[i];
                cascade.recenterDistanceSq = s_cascadeRecenter[i];
            }
        }
    }

    fprintf(stderr, "ShadowMap: targets for quality %d, %dx%d, %s, cascades %s\n",
        g_shadowMapQuality, g_shadowMapSize, g_shadowMapSize, pcf ? "hardware PCF (depth maps)" : "R32F",
        g_shadowMapQuality > 2 ? "on" : "off");

    ShadowMapResetCascades();

    // TODO the device-restore registration (vtable slot 0x7c, ShadowMapDeviceRestore): frozen has
    // no registry for it. See ShadowMapReleaseTargets.

    g_shadowMapCascadesStale = 0;
    g_mapShadowPlaneCascade = { { 0.0f, 0.0f, 0.0f }, 0.0f };
    g_mapShadowPlane = { { 0.0f, 0.0f, 0.0f }, 0.0f };
}

// ref: FUN_00876360
int32_t ShadowMapInitialize() {
    ShadowMapEnsureTargets();
    return 1;
}

// ref: FUN_00874890
// Put each cascade's view together and hand the lot to the collect callback.
//
// Below quality 5 a cascade only does work when it is part-way through redrawing its hidden map,
// or when the focus has drifted far enough from what its visible map was drawn around: then one
// sub-tile of the hidden map is set up, the full extent is measured into `outer` so the next
// cascade can keep out what this one covers, and once the last sub-tile is done the maps swap.
// At quality 5 every cascade is redrawn whole around the focus snapped to its own grid.
static void ShadowMapSetupCascades(ShadowView& view, const C3Vector& focus, CWFrustum& outer, int32_t whole) {
    int32_t quality = g_shadowMapRealloc ? 0 : g_shadowMapQuality;
    int32_t frame = 0;

    float size = static_cast<float>(static_cast<uint32_t>(g_shadowMapSize));

    if (quality < 5) {
        C44Matrix scratch;
        scratch.Identity();

        for (int32_t i = 0; i < 3; i++) {
            auto& cascade = g_shadowCascades[i];

            view.buffer[i] = cascade.visible == 0;
            view.active[i] = 0;

            float dx = cascade.center.x - focus.x;
            float dy = cascade.center.y - focus.y;
            float dz = cascade.center.z - focus.z;

            if (cascade.tile == 0 && !(cascade.recenterDistanceSq < dz * dz + dy * dy + dx * dx)) {
                continue;
            }

            view.extent[i] = cascade.extent;

            if (cascade.tile == 0) {
                cascade.pending = focus;
            }

            view.center[i] = cascade.pending;

            float extent = view.extent[i];
            float step;
            uint32_t row;

            if (i < 2) {
                step = extent * 0.6666666865348816f;
                float left = static_cast<float>(cascade.tile % 3) * step - extent;
                view.ortho[i][0] = left;
                view.ortho[i][1] = left + step;
                row = cascade.tile / 3;
            } else {
                step = extent * 0.4000000059604645f;
                float left = static_cast<float>(cascade.tile % 5) * step - extent;
                view.ortho[i][0] = left;
                view.ortho[i][1] = left + step;
                row = cascade.tile / 5;
            }

            float bottom = static_cast<float>(static_cast<int32_t>(row)) * step - extent;
            view.ortho[i][2] = bottom;
            view.ortho[i][3] = bottom + step;

            // The right and top edges land on whole texels.
            view.ortho[i][1] = static_cast<float>((std::ceil((size / (extent + extent)) * view.ortho[i][1]) / size) * extent * 2.0f);
            view.ortho[i][3] = (extent + extent) * static_cast<float>(std::ceil((size / (extent * 2.0f)) * view.ortho[i][3]) / size);

            float half = 0.5f / view.extent[i];
            view.viewport[i][0] = view.ortho[i][0] * half + 0.5f;
            view.viewport[i][1] = view.ortho[i][1] * half + 0.5f;
            view.viewport[i][2] = 0.5f - view.ortho[i][3] * half;
            view.viewport[i][3] = 0.5f - half * view.ortho[i][2];

            s_setupView(view.frustum[i], view.projection[i], cascade.up, view, i);
            view.frustum[i].GetBounds(view.bounds[i]);

            if (i == 0 || view.active[i - 1] == 0) {
                view.behind[i] = 0;
            } else {
                view.behindFrustum[i] = outer;
                view.behind[i] = 1;
            }

            view.mask[i] = 8;

            // The whole extent, for the next cascade to keep out.
            float saved[4] = { view.ortho[i][0], view.ortho[i][1], view.ortho[i][2], view.ortho[i][3] };
            view.ortho[i][0] = -view.extent[i];
            view.ortho[i][1] = view.extent[i];
            view.ortho[i][2] = -view.extent[i];
            view.ortho[i][3] = view.extent[i];

            s_setupView(outer, scratch, cascade.up, view, i);
            outer.NegatePlanes();

            memcpy(view.ortho[i], saved, sizeof(saved));

            cascade.tile++;

            if ((i < 2 && cascade.tile > 8) || (i == 2 && cascade.tile > 24)) {
                cascade.center = cascade.pending;
                cascade.tile = 0;
                cascade.visible = cascade.visible == 0;
            }

            view.active[i] = 1;
            view.last = i;
        }
    } else {
        frame = s_frame;

        for (int32_t i = 0; i < 3; i++) {
            auto& cascade = g_shadowCascades[i];

            view.buffer[i] = 0;
            view.active[i] = 0;
            view.extent[i] = cascade.extent;

            // The focus snapped to the cascade's own grid: two, four and sixteen yards.
            float grid = i == 0 ? 2.0f : i == 1 ? 4.0f : 16.0f;
            float inv = 1.0f / grid;

            cascade.pending.x = static_cast<float>(std::floor(inv * focus.x) * grid);
            cascade.pending.y = static_cast<float>(std::floor(focus.y * inv) * grid);
            cascade.pending.z = static_cast<float>(std::floor(focus.z * inv) * grid);

            view.center[i] = cascade.pending;

            view.ortho[i][0] = -view.extent[i];
            view.ortho[i][1] = view.extent[i];
            view.ortho[i][2] = -view.extent[i];
            view.ortho[i][3] = view.extent[i];

            view.viewport[i][0] = 0.0f;
            view.viewport[i][1] = 1.0f;
            view.viewport[i][2] = 0.0f;
            view.viewport[i][3] = 1.0f;

            s_setupView(view.frustum[i], view.projection[i], cascade.up, view, i);
            view.frustum[i].GetBounds(view.bounds[i]);

            view.behind[i] = whole;

            if (whole) {
                view.behindFrustum[i] = outer;
            }

            view.mask[i] = 0xD;

            outer = view.frustum[i];
            outer.NegatePlanes();

            cascade.center = cascade.pending;

            view.active[i] = 1;
            view.last = i;

            whole = 1;
        }
    }

    s_collect(view, frame);
}

// ref: FUN_00874fb0
// Draw each cascade the setup made active into the map it chose.
static void ShadowMapRenderCascades(ShadowView& view) {
    int32_t quality = g_shadowMapRealloc ? 0 : g_shadowMapQuality;
    int32_t pcf = CShaderEffect::s_usePcfFiltering;

    for (int32_t i = 0; i <= view.last; i++) {
        if (!view.active[i]) {
            continue;
        }

        auto& cascade = g_shadowCascades[i];
        HTEXTURE map = quality < 5 ? cascade.textures[view.buffer[i]] : cascade.textures[0];

        CGxTex* color;
        CGxTex* depth = nullptr;

        if (!pcf) {
            color = ShadowGxTex(map);
        } else {
            depth = ShadowGxTex(map);
            color = ShadowGxTex(s_litColor);
        }

        s_render(view, i, color, depth, cascade.up);
    }
}

// ref: FUN_00875f80
// The frame's maps: the main map around the focus (snapped to the texel grid), with only the
// player's surroundings in it; the lit pass over the same area when asked for; then the cascades.
int32_t ShadowMapRender(const C3Vector& focus, int32_t lit) {
    if (g_shadowMapRealloc) {
        ShadowMapReleaseTargets();
        ShadowMapEnsureTargets();
        g_shadowMapRealloc = 0;
    }

    int32_t quality = g_shadowMapQuality;

    if (!s_buildMatrix || !s_setupView || !s_collect || !s_render) {
        return 0;
    }

    if (g_shadowMapRealloc || quality <= 0) {
        return 1;
    }

    ShadowView view;
    s_frame++;

    float size = static_cast<float>(static_cast<uint32_t>(g_shadowMapSize));
    float inv = 1.0f / size;

    view.extent[0] = g_shadowMainExtent;
    view.center[0] = focus;

    g_shadowMainCenter.x = static_cast<float>(std::floor(size * focus.x + 0.5f) * inv);
    g_shadowMainCenter.y = static_cast<float>(std::floor(focus.y * size + 0.5f) * inv);
    g_shadowMainCenter.z = focus.z;

    int32_t pcf = CShaderEffect::s_usePcfFiltering;
    CGxTex* savedDepth = nullptr;

    if (pcf) {
        GxRsSet(GxRs_ColorWrite, 0);
        GxRenderTargetGet(GxBuffers_Depth, savedDepth);
    }

    view.ortho[0][0] = -view.extent[0];
    view.ortho[0][1] = view.extent[0];
    view.ortho[0][2] = -view.extent[0];
    view.ortho[0][3] = view.extent[0];

    view.viewport[0][0] = 0.0f;
    view.viewport[0][1] = 1.0f;
    view.viewport[0][2] = 0.0f;
    view.viewport[0][3] = 1.0f;

    s_setupView(view.frustum[0], view.projection[0], g_shadowMainUp, view, 0);
    view.frustum[0].GetBounds(view.bounds[0]);

    // Zero until here, which is the reference's own order: the main view's first frame is built
    // with a zero up and comes out the identity.
    g_shadowMainUp = { 1.0f, 0.0f, 0.0f };

    memcpy(g_shadowMainScissor, view.scissor[0], sizeof(g_shadowMainScissor));

    view.last = 0;
    int32_t frame = quality > 4 ? s_frame : 0;
    view.mask[0] = 3;
    view.whole = 0;

    s_collect(view, frame);

    CGxTex* color = ShadowGxTex(s_mainMap);
    CGxTex* depth = nullptr;

    if (pcf) {
        color = ShadowGxTex(s_litColor);
        depth = ShadowGxTex(s_mainMap);
    }

    s_render(view, 0, color, depth, g_shadowMainUp);

    if (lit) {
        view.mask[0] = quality < 5 ? (quality > 2 ? 5 : 1) : 0xD;

        s_collect(view, frame);

        color = ShadowGxTex(s_litColor);
        depth = pcf ? ShadowGxTex(s_litDepth) : nullptr;

        s_render(view, 0, color, depth, g_shadowMainUp);
    }

    if (quality > 2) {
        if (g_shadowMapCascadesStale) {
            ShadowMapResetCascades();
        }

        if (lit || quality < 5) {
            CWFrustum outer;
            int32_t whole = quality > 4;

            if (whole) {
                outer = view.frustum[0];
                outer.NegatePlanes();
            }

            view.whole = whole;

            ShadowMapSetupCascades(view, focus, outer, whole);
            ShadowMapRenderCascades(view);
        }
    }

    if (pcf) {
        GxRsSet(GxRs_ColorWrite, 0xF);
        GxRenderTargetSet(GxBuffers_Depth, savedDepth, 0);
    }

    return 1;
}

// ref: FUN_008750b0
// The rows the shadowed shaders sample with: each map's matrix from the build callback, flipped in
// y, its depth scaled, moved half a texel, and stored as three columns.
void ShadowMapBuildLightMatrices() {
    if (!s_buildMatrix || !s_setupView || !s_collect || !s_render) {
        return;
    }

    C44Matrix scale;
    scale.Identity();
    scale.Scale({ 1.0f, -1.0f, s_depthScale });

    C44Matrix translate;
    translate.Identity();
    float half = 0.5f / s_sizeFloat;
    translate.Translate({ half, half, 0.0f });

    C44Matrix matrix;
    matrix.Identity();

    auto store = [](float* rows, const C44Matrix& m) {
        const float columns[12] = {
            m.a0, m.b0, m.c0, m.d0,
            m.a1, m.b1, m.c1, m.d1,
            m.a2, m.b2, m.c2, m.d2
        };

        memcpy(rows, columns, sizeof(columns));
    };

    s_buildMatrix(g_shadowMainCenter, g_shadowMainExtent, matrix, g_shadowMainUp, -1);
    matrix = matrix * scale * translate;
    store(&s_lightMatrices[0][0], matrix);

    for (int32_t i = 0; i < 3; i++) {
        auto& cascade = g_shadowCascades[i];

        s_buildMatrix(cascade.center, cascade.extent, matrix, cascade.up, i);
        matrix = matrix * scale * translate;
        store(&s_lightMatrices[3 + i * 3][0], matrix);
    }
}

// The three cascades' visible maps, on three stages from `first`.
static void ShadowMapBindCascades(EGxRenderState first) {
    for (int32_t i = 0; i < 3; i++) {
        auto& cascade = g_shadowCascades[i];
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(first + i), ShadowGxTex(cascade.textures[cascade.visible]));
    }
}

// The map the lit shaders sample: the depth twin with hardware PCF, the colour target without.
static CGxTex* ShadowMapLitTexture() {
    return ShadowGxTex(CShaderEffect::s_usePcfFiltering ? s_litDepth : s_litColor);
}

// ref: FUN_00874660, called from FUN_00798da0 at 0x00799139
// The terrain pass: the light rows at vertex c37, the PCF kernel at pixel c3, the lit map on
// stage 5 and at quality 3 and up the cascades on 6..8.
void ShadowMapBindTerrain() {
    int32_t quality = g_shadowMapQuality;

    if (g_shadowMapRealloc || quality <= 0) {
        return;
    }

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0x25, &s_lightMatrices[0][0], 12);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 3, &g_shadowMapPcfTaps[0][0], 8);
    g_theGxDevicePtr->RsSet(GxRs_Texture5, ShadowMapLitTexture());

    if (quality > 2) {
        ShadowMapBindCascades(GxRs_Texture6);
    }
}

// ref: FUN_008744e0
// What every model draw of a pass shares: the light rows at vertex c224, the light direction at
// pixel c4, the kernel at pixel c5, and at quality 3 and up the cascades on stages 5..7.
void ShadowMapBindScene() {
    int32_t quality = g_shadowMapQuality;

    if (g_shadowMapRealloc || quality <= 0) {
        return;
    }

    // The fourth component is zero, which is what FUN_00875c10 stores at DAT_00d43198 next to the
    // direction: the reference's constant is a full register whose w it clears every time.
    float lightDir[4] = {
        g_shadowMapLightDirWorld.x, g_shadowMapLightDirWorld.y, g_shadowMapLightDirWorld.z, 0.0f
    };

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0xe0, &s_lightMatrices[0][0], 12);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 4, lightDir, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 5, &g_shadowMapPcfTaps[0][0], 8);

    if (quality > 2) {
        ShadowMapBindCascades(GxRs_Texture5);
    }
}

// ref: FUN_008745d0
// A map object draw: one plane at pixel c3 and one map on stage 4. `lit` takes the main map and
// its plane; otherwise the lit map, and at quality 3 and up the cascade plane.
void ShadowMapBindMapObj(int32_t lit) {
    int32_t quality = g_shadowMapQuality;

    if (g_shadowMapRealloc || quality <= 0) {
        return;
    }

    const C4Plane* plane = &g_mapShadowPlane;
    CGxTex* map = ShadowGxTex(s_mainMap);

    if (lit == 0) {
        map = ShadowMapLitTexture();

        if (quality > 2) {
            plane = &g_mapShadowPlaneCascade;
        }
    }

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 3, reinterpret_cast<const float*>(plane), 1);
    g_theGxDevicePtr->RsSet(GxRs_Texture4, map);
}

// ref: FUN_00874760
// The detail doodads: the light rows at vertex c23, the cascade plane, light direction and kernel
// at pixel c3, c4 and c5, the lit map on stage 4 and the cascades on 5..7.
void ShadowMapBindDetailDoodads() {
    int32_t quality = g_shadowMapQuality;

    if (g_shadowMapRealloc || quality <= 0) {
        return;
    }

    float lightDir[4] = {
        g_shadowMapLightDirWorld.x, g_shadowMapLightDirWorld.y, g_shadowMapLightDirWorld.z, 0.0f
    };

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0x17, &s_lightMatrices[0][0], 12);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 3, reinterpret_cast<const float*>(&g_mapShadowPlaneCascade), 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 4, lightDir, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 5, &g_shadowMapPcfTaps[0][0], 8);
    g_theGxDevicePtr->RsSet(GxRs_Texture4, ShadowMapLitTexture());

    if (quality > 2) {
        ShadowMapBindCascades(GxRs_Texture5);
    }
}
