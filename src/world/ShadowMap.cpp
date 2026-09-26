#include "world/ShadowMap.hpp"
#include <tempest/Matrix.hpp>
#include <cmath>
#include "gx/Transform.hpp"
#include "gx/Types.hpp"
#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
#include "world/MapShadow.hpp"

int32_t g_shadowMapQuality = 0;
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

// The PCF kernel, DAT_00d431d0: eight taps whose .xy are texel offsets already divided by the map
// size, .zw always zero. The table is docs/ref/parity-shadowmap.md section 6d, read off the image.
static const float s_pcfTaps[8][2] = {
    {  0.8f, -1.0f },
    { -0.2f, -0.8f },
    {  0.2f, -0.6f },
    {  1.0f, -0.4f },
    { -0.6f, -0.2f },
    {  0.6f,  0.2f },
    { -1.0f, -0.4f },
    { -0.4f, -0.6f },
};

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

    float pixelConstants[8][4] = {};

    for (int32_t i = 0; i < 8; i++) {
        pixelConstants[i][0] = s_pcfTaps[i][0];
        pixelConstants[i][1] = s_pcfTaps[i][1];
    }

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0x25, &vertexConstants[0][0], 12);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 3, &pixelConstants[0][0], 8);
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

    float taps[8][4] = {};

    for (int32_t i = 0; i < 8; i++) {
        taps[i][0] = s_pcfTaps[i][0];
        taps[i][1] = s_pcfTaps[i][1];
    }

    // The fourth component is zero, which is what FUN_00875c10 stores at DAT_00d43198 next to the
    // direction: the reference's constant is a full register whose w it clears every time.
    float lightDir[4] = {
        g_shadowMapLightDirWorld.x, g_shadowMapLightDirWorld.y, g_shadowMapLightDirWorld.z, 0.0f
    };

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0xe0, &vertexConstants[0][0], 12);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 4, lightDir, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 5, &taps[0][0], 8);

    if (quality > 2) {
        // TODO the three filter textures into stages 5..7, from the ring buffers at DAT_00d43290,
        // DAT_00d432cc and DAT_00d43308 with their own cursors. They are the blur chain
        // FUN_008753f0 fills, and frozen builds none of it, so the quality never gets here.
    }
}
