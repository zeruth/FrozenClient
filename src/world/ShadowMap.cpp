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

int32_t g_shadowMapQuality = 0;
int32_t g_shadowMapSize = 1024;
float g_shadowMapPcfTaps[8][4] = { { 0.0f, 0.0f, 0.0f, 0.0f } };
int32_t g_shadowMapRealloc = 0;
float g_shadowMapFogScale = 1.0f;

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

// ref: FUN_00874240
// Free every shadow target so the next render step allocates them again.
//
// The reference closes three texture handles -- the map at DAT_00d43148 and the lit and unlit
// variants at DAT_00d43250 and DAT_00d43254 -- and then walks three filter ring entries of
// 0x3c bytes each, closing two handles in every one. frozen renders a single map with an
// explicit depth surface and builds no filter chain, so MapShadowReleaseTargets covers all of
// the textures that actually exist here.
//
// NOT PORTED, and it is the interesting half: the reference ends by registering
// ShadowMapDeviceRestore through the device's vtable slot 0x80, so releasing the targets is
// also what arms the hook that will ask for the next release. frozen has no device-restore
// callback registry at all -- ShadowMapDeviceRestore is defined and called from nowhere -- so
// after a real device reset the targets here are stale and nothing asks for them again. That
// wants the registry, not a call, and is left as a gap rather than faked with a direct call
// that would run at the wrong time.
void ShadowMapReleaseTargets() {
    MapShadowReleaseTargets();
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

// The twelve vertex registers both the terrain and the scene binds upload: three rows of the main
// world -> shadow-texture matrix followed by three cascade matrices of three rows each. frozen
// builds no cascades (they are quality > 2 only), so those nine stay zero, which is what the
// shader reads when its permutation does not sample them.
//
// The rows are the TRANSPOSE of MapShadowTexMatrix: that matrix is row-vector world -> texture, so
// u = x*a0 + y*b0 + z*c0 + d0, and a shader computing dot(pos4, cN) needs the matrix's COLUMNS.
static void ShadowMapLightMatrices(float out[12][4]) {
    C44Matrix texMatrix = MapShadowTexMatrix().Transpose();
    const float* rows = reinterpret_cast<const float*>(&texMatrix);

    for (int32_t i = 0; i < 12; i++) {
        out[i][0] = 0.0f;
        out[i][1] = 0.0f;
        out[i][2] = 0.0f;
        out[i][3] = 0.0f;
    }

    for (int32_t i = 0; i < 3; i++) {
        out[i][0] = rows[i * 4 + 0];
        out[i][1] = rows[i * 4 + 1];
        out[i][2] = rows[i * 4 + 2];
        out[i][3] = rows[i * 4 + 3];
    }
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
    float inv = g_shadowMapSize ? 1.0f / static_cast<float>(g_shadowMapSize) : 0.0f;

    for (int32_t i = 0; i < 8; i++) {
        g_shadowMapPcfTaps[i][0] = s_pcfTapsTexels[i][0] * inv;
        g_shadowMapPcfTaps[i][1] = s_pcfTapsTexels[i][1] * inv;
        g_shadowMapPcfTaps[i][2] = 0.0f;
        g_shadowMapPcfTaps[i][3] = 0.0f;
    }
}

// ref: FUN_00874660, called from FUN_00798da0 at 0x00799139
// What the terrain pass binds to sample the shadow map: the world -> shadow-texture matrices at
// vertex c37, the PCF kernel at pixel c3, and the map itself on sampler 5.
//
// Terrain gets no light-direction constant -- only the matrices, the taps and the sampler. See
// ShadowMapLightMatrices for what the twelve vertex registers hold.
void ShadowMapBindTerrain() {
    if (g_shadowMapRealloc || g_shadowMapQuality <= 0) {
        return;
    }

    auto shadowMap = MapShadowTexture();

    if (!shadowMap) {
        return;
    }

    float vertexConstants[12][4];
    ShadowMapLightMatrices(vertexConstants);

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0x25, &vertexConstants[0][0], 12);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 3, &g_shadowMapPcfTaps[0][0], 8);
    g_theGxDevicePtr->RsSet(GxRs_Texture5, shadowMap);
}

// ref: FUN_008745d0
// The map object half of the bind, and much smaller than the terrain one: ONE pixel constant and
// ONE texture. The constant is the shadow PLANE -- the four floats MapShadowSetupPlane writes,
// which the reference keeps at DAT_00d4319c and this uploads to pixel c3. That is what ties item
// 10's two named functions together: FUN_007bb670 builds the plane, this hands it to the shader.
//
// `lit` picks which map: non-zero takes the plain one, zero takes the variant at DAT_00d43250
// indexed by the colour-target flag, and at quality > 2 also swaps in a second plane
// (DAT_00d431ac). frozen renders ONE map and builds only the one plane, so both substitutions
// fall back to what it has; neither is reachable, because nothing sets the quality above 0 and
// the cascades start at 3.
void ShadowMapBindMapObj(int32_t lit) {
    if (g_shadowMapRealloc || g_shadowMapQuality <= 0) {
        return;
    }

    auto shadowMap = MapShadowTexture();

    if (!shadowMap) {
        return;
    }

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 3, reinterpret_cast<const float*>(&g_mapShadowPlane), 1);
    g_theGxDevicePtr->RsSet(GxRs_Texture4, shadowMap);
}

// ref: FUN_008744e0
// The state a whole pass shares, rather than one draw: the same twelve light matrices the terrain
// bind uploads but at vertex c224, the world-space light direction at pixel c4, and the PCF kernel
// at pixel c5 -- the same eight taps the terrain bind puts at c3, because the two shader sets
// number their registers differently.
void ShadowMapBindScene() {
    int32_t quality = g_shadowMapQuality;

    if (g_shadowMapRealloc || quality <= 0) {
        return;
    }

    float vertexConstants[12][4];
    ShadowMapLightMatrices(vertexConstants);


    // The fourth component is zero, which is what FUN_00875c10 stores at DAT_00d43198 next to the
    // direction: the reference's constant is a full register whose w it clears every time.
    float lightDir[4] = {
        g_shadowMapLightDirWorld.x, g_shadowMapLightDirWorld.y, g_shadowMapLightDirWorld.z, 0.0f
    };

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0xe0, &vertexConstants[0][0], 12);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 4, lightDir, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 5, &g_shadowMapPcfTaps[0][0], 8);

    if (quality > 2) {
        // TODO the three filter textures into stages 5..7, from the ring buffers at DAT_00d43290,
        // DAT_00d432cc and DAT_00d43308 with their own cursors. They are the blur chain
        // FUN_008753f0 fills, and frozen builds none of it, so the quality never gets here.
    }
}
