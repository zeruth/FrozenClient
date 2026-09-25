#ifndef WORLD_SHADOW_MAP_HPP
#define WORLD_SHADOW_MAP_HPP

#include <cstdint>

// The cascaded shadow map (reference module around FUN_00872ce0..FUN_00876d90, "ShadowMap.wfx").
// Only the state the terrain pass reads is here; the maps themselves are not ported, so the
// quality stays at 0 and every terrain draw takes the unshadowed shader.

// The extShadowQuality setting in effect (DAT_00d43154)
extern int32_t g_shadowMapQuality;
// A reallocation is pending, which masks the quality to 0 for the frame (DAT_00b1d51c)
extern int32_t g_shadowMapRealloc;
// The fog scale the terrain fog constant is multiplied by (DAT_00d4300c)
extern float g_shadowMapFogScale;

// The quality in effect: 0 while a reallocation is pending
int32_t ShadowMapGetQuality();
// The terrain shader permutation for the quality in effect: 0 unshadowed, 1..3 the shadowed sets
int32_t ShadowMapGetShaderLevel();
// The shadow map textures and constants a terrain draw samples
void ShadowMapBindTerrain();

#endif
