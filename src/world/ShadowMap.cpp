#include "world/ShadowMap.hpp"

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
void ShadowMapBindTerrain() {
    if (g_shadowMapRealloc || g_shadowMapQuality <= 0) {
        return;
    }

    // TODO the shadow map bind proper (docs/ref/parity-shadowmap.md section 6b)
}
