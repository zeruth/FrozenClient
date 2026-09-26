#ifndef WORLD_SHADOW_MAP_HPP
#define WORLD_SHADOW_MAP_HPP

#include <cstdint>
#include <tempest/Vector.hpp>

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

// The map object half of the same bind: the shadow map goes to texture stage 4 and its
// cascade constants to pixel c3. `lit` picks the lit map over the unlit one.
// ref: FUN_008745d0
void ShadowMapBindMapObj(int32_t lit);

// The shadow map state shared by a whole pass: the light matrices into vertex c224, the
// cascade constants into pixel c4 and c5, and at the highest quality three filter textures
// into stages 5 through 7. ref: FUN_008744e0
void ShadowMapBindScene();

// The light direction the shadow passes sample with: as given, and turned by the world matrix and
// re-normalised. The second one is the pixel c4 constant ShadowMapBindScene uploads.
extern C3Vector g_shadowMapLightDir;
extern C3Vector g_shadowMapLightDirWorld;

// Store both forms of the light direction. ref: FUN_00875c10
void ShadowMapSetLightDirection(const C3Vector& dir);

// The intensity and flag the reference stores and never reads. ref: FUN_00874010
extern float g_shadowMapIntensity;
extern int32_t g_shadowMapFlag;
void ShadowMapSetIntensity(float intensity, int32_t flag);

// The console description of a quality level. ref: FUN_00873f60
const char* ShadowMapQualityName(int32_t quality);

// Whether the device can do a quality level at all. ref: FUN_008740d0
int32_t ShadowMapQualitySupported(int32_t quality);

// Take a new quality if the device supports it, and ask for a target realloc. ref: FUN_00874210
int32_t ShadowMapSetQuality(int32_t quality);

// After a device reset every target is gone: ask for the realloc. ref: FUN_00873fe0
void ShadowMapDeviceRestore();

#endif
