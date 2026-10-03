#ifndef WORLD_SHADOW_MAP_HPP
#define WORLD_SHADOW_MAP_HPP

#include "gx/Texture.hpp"
#include "world/CWFrustum.hpp"
#include <cstdint>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Plane.hpp>
#include <tempest/Vector.hpp>

class CGxTex;

// The shadow map (reference ShadowMap.cpp, 0x00873e00..0x00876370). It owns the textures, the
// light matrices every shadowed shader samples with, and the frame's render step; WHAT gets
// drawn into the maps is the map's business, reached through four callbacks MapShadow.cpp
// registers (FUN_007bd3a0).
//
// Two kinds of map:
//
//   the MAIN map, 20 yards either side of the focus, drawn every frame -- once with the
//   entities only and once more (the "lit" pass) with whatever the quality adds;
//
//   at quality 3 and up, three CASCADES of 40, 160 and 640 yards. Below quality 5 each is
//   double-buffered and AMORTISED: a frame redraws one sub-tile of the hidden buffer (3x3 of them
//   for the first two cascades, 5x5 for the last) and the buffers swap when the grid is done. At
//   quality 5 every cascade is redrawn whole each frame.

// The view the render step builds on its stack and hands to every callback: 0xb90 bytes in the
// reference, built by FUN_008753f0. Frozen's CWFrustum carries a list link after the reference's
// 0xf4 bytes, so the offsets past +0x6c are documentation of where a field came from.
struct ShadowView {
    // What each cascade draws, as a mask: 1 units, 2 the player's own surroundings, 4 doodads
    // with transparent batches, 8 everything else. 3 is the main pass, 1/5/13 its lit pass, 8 a
    // progressive cascade and 13 a whole one.
    uint32_t mask[3];                       // +0x000
    int32_t active[3];                      // +0x00c
    // Whether the cascade has an inner cascade to keep out: casters wholly inside behindFrustum
    // are already in the finer map.
    int32_t behind[3];                      // +0x018
    CAaBox bounds[3];                       // +0x024: frustum[i]'s corners boxed
    CWFrustum frustum[3];                   // +0x06c: the cascade's whole volume
    CWFrustum behindFrustum[3];             // +0x348: the inner cascade's, inside out
    CWFrustum cullFrustum[3];               // +0x624: narrowed to what the camera can see
    float scissor[3][4];                    // +0x900: in clip space, -1..1
    C3Vector center[3];                     // +0x930
    uint8_t buffer[3];                      // +0x954: which of the cascade's two maps is drawn
    float extent[3];                        // +0x958
    float ortho[3][4];                      // +0x964: left, right, bottom, top
    float viewport[3][4];                   // +0x994: minX, maxX, minY, maxY
    C44Matrix projection[3];                // +0x9c4
    int32_t last;                           // +0xa84: the highest cascade set up, -1 for none
    int32_t whole;                          // +0xa88: every cascade at once (quality 5)

    // ref: FUN_008753f0
    ShadowView();
};

// One cascade's persistent state: 0x3c bytes from DAT_00d43290.
struct ShadowCascade {
    HTEXTURE textures[2];                   // +0x00
    float extent;                           // +0x08: half the side, from DAT_00b1d520
    float recenterDistanceSq;               // +0x0c: from DAT_00b1d52c
    C3Vector center;                        // +0x10: what the visible map was drawn around
    C3Vector pending;                       // +0x1c: what the hidden one is being drawn around
    // The look-at's up vector. NOTHING EVER WRITES IT, in the reference either: the look-at sees a
    // zero up, gives up, and leaves the identity, so every cascade is a straight top-down
    // projection. Kept as a field because that is where the reference reads it from.
    C3Vector up;                            // +0x28
    uint32_t tile;                          // +0x34: the next sub-tile of the hidden map
    uint32_t visible;                       // +0x38: which of the two maps the shaders sample
};

typedef void (*ShadowBuildMatrixCallback)(const C3Vector& center, float extent, C44Matrix& out, const C3Vector& up, int32_t index);
typedef void (*ShadowSetupViewCallback)(CWFrustum& frustum, C44Matrix& projection, const C3Vector& up, ShadowView& view, int32_t index);
typedef int32_t (*ShadowCollectCallback)(ShadowView& view, int32_t frame);
typedef int32_t (*ShadowRenderCallback)(ShadowView& view, int32_t index, CGxTex* color, CGxTex* depth, const C3Vector& up);

// The extShadowQuality setting in effect (DAT_00d43154)
extern int32_t g_shadowMapQuality;
// A reallocation is pending, which masks the quality to 0 for the frame (DAT_00b1d51c)
extern int32_t g_shadowMapRealloc;
// FROZEN-ONLY: set for the one frame FROZEN_SHADOW_DUMP captures (ShadowMap.cpp).
extern int32_t g_shadowDumpFrame;
// The fog scale the terrain fog constant is multiplied by (DAT_00d4300c)
extern float g_shadowMapFogScale;
// The map edge in texels (DAT_00d43150), from the quality.
extern int32_t g_shadowMapSize;
// The PCF kernel (DAT_00d431d0): eight taps of one texel each, in texture units.
extern float g_shadowMapPcfTaps[8][4];
// The light direction as given (DAT_00d43180) and turned by the world matrix (DAT_00d4318c).
extern C3Vector g_shadowMapLightDir;
extern C3Vector g_shadowMapLightDirWorld;
// The intensity and flag the reference stores and never reads (DAT_00b1d518, DAT_00d43168).
extern float g_shadowMapIntensity;
extern int32_t g_shadowMapFlag;
// The shader level the last caster pass was drawn at (DAT_00d43010).
extern int32_t g_shadowMapCasterLevel;
// The cascades were drawn over map that has since been unloaded: clear and restart them on the
// next render step (DAT_00d4314c).
extern int32_t g_shadowMapCascadesStale;
// The cascades (DAT_00d43290) and the main map's centre, extent and up (DAT_00d43260,
// DAT_00d43258, DAT_00d43278).
extern ShadowCascade g_shadowCascades[3];
extern C3Vector g_shadowMainCenter;
extern float g_shadowMainExtent;
extern C3Vector g_shadowMainUp;
// The main pass's scissor, copied out of the view (DAT_00d43170).
extern float g_shadowMainScissor[4];
// The plane the map object binder hands its shaders, and the one it uses at quality 3 and up
// (DAT_00d4319c, DAT_00d431ac). MapShadowSetupPlane writes the first.
extern C4Plane g_mapShadowPlane;
extern C4Plane g_mapShadowPlaneCascade;

// Take the map size the current quality asks for. ref: the head of FUN_00875d30
void ShadowMapUpdateSize();
// Rebuild the kernel for the current map size. ref: FUN_008742e0
void ShadowMapBuildPcfTaps();
// The quality in effect: 0 while a reallocation is pending. ref: FUN_00873f80
int32_t ShadowMapGetQuality();
// The shader permutation for the quality in effect. ref: FUN_00873ff0
int32_t ShadowMapGetShaderLevel();
// The console description of a quality level. ref: FUN_00873f60
const char* ShadowMapQualityName(int32_t quality);
// Whether the device can do a quality level at all. ref: FUN_008740d0
int32_t ShadowMapQualitySupported(int32_t quality);
// Take a new quality if the device supports it, and ask for a target realloc. ref: FUN_00874210
int32_t ShadowMapSetQuality(int32_t quality);
// ref: FUN_00874240
void ShadowMapReleaseTargets();
// ref: FUN_00873fe0
void ShadowMapDeviceRestore();
// ref: FUN_00875d30
void ShadowMapEnsureTargets();
// ref: FUN_00876360
int32_t ShadowMapInitialize();
// ref: FUN_00875c10
void ShadowMapSetLightDirection(const C3Vector& dir);
// ref: FUN_00874010
void ShadowMapSetIntensity(float intensity, int32_t flag);
// ref: FUN_00874030
void ShadowMapSetDepthScale(float scale);

// The four callbacks. ref: FUN_00873fa0, FUN_00873fb0, FUN_00873fc0, FUN_00873fd0
void ShadowMapSetBuildMatrixCallback(ShadowBuildMatrixCallback callback);
void ShadowMapSetSetupViewCallback(ShadowSetupViewCallback callback);
void ShadowMapSetCollectCallback(ShadowCollectCallback callback);
void ShadowMapSetRenderCallback(ShadowRenderCallback callback);

// Draw the frame's maps around `focus`; `lit` adds the lit pass. ref: FUN_00875f80
int32_t ShadowMapRender(const C3Vector& focus, int32_t lit);
// The twelve light-matrix rows every shadowed shader samples with. ref: FUN_008750b0
void ShadowMapBuildLightMatrices();

// The binds, one per kind of shader. ref: FUN_00874660, FUN_008744e0, FUN_008745d0, FUN_00874760
void ShadowMapBindTerrain();
void ShadowMapBindScene();
void ShadowMapBindMapObj(int32_t lit);
void ShadowMapBindDetailDoodads();

#endif
