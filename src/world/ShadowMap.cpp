#include "world/ShadowMap.hpp"
#include <tempest/Matrix.hpp>
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

// ref: FUN_00874660
// With a shadow map in use this binds the light matrices (vertex c37..c48), the cascade
// constants (pixel c3..c10) and the map textures (stages 5..8). frozen has no shadow map yet, so
// the quality is 0 and the reference itself would return here.
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
// Terrain gets no light-direction constant -- only the matrices, the taps and the sampler. The
// reference uploads TWELVE vertex registers there, three rows of the main matrix plus three cascade
// matrices of three rows each; frozen builds no cascades (they are quality > 2) so those nine stay
// zero, which is what the shader reads when its permutation does not sample them.
//
// The rows are the TRANSPOSE of MapShadowTexMatrix: that matrix is row-vector world -> texture, so
// u = x*a0 + y*b0 + z*c0 + d0, and a shader computing dot(pos4, cN) needs the matrix's COLUMNS.
void ShadowMapBindTerrain() {
    if (g_shadowMapRealloc || g_shadowMapQuality <= 0) {
        return;
    }

    auto shadowMap = MapShadowTexture();

    if (!shadowMap) {
        return;
    }

    C44Matrix texMatrix = MapShadowTexMatrix().Transpose();
    const float* rows = reinterpret_cast<const float*>(&texMatrix);

    float vertexConstants[12][4] = {};

    for (int32_t i = 0; i < 3; i++) {
        vertexConstants[i][0] = rows[i * 4 + 0];
        vertexConstants[i][1] = rows[i * 4 + 1];
        vertexConstants[i][2] = rows[i * 4 + 2];
        vertexConstants[i][3] = rows[i * 4 + 3];
    }

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
// Same shape as ShadowMapBindTerrain: with no shadow map the quality is 0 and the reference
// itself returns here.
void ShadowMapBindMapObj(int32_t lit) {
    if (g_shadowMapRealloc || g_shadowMapQuality <= 0) {
        return;
    }

    (void)lit;

    // TODO the map object shadow bind proper: the cascade constants into pixel c3 and the
    // shadow texture into stage 4, choosing the unlit map and the second constant set when
    // the geometry is unlit and the quality is above 2.
}

// ref: FUN_008744e0
void ShadowMapBindScene() {
    if (g_shadowMapRealloc || g_shadowMapQuality <= 0) {
        return;
    }

    // TODO the constant and texture binds proper, as in ShadowMapBindTerrain.
}
