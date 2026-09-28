#ifndef WORLD_MAP_SHADOW_HPP
#define WORLD_MAP_SHADOW_HPP

#include <cstdint>
#include <tempest/Plane.hpp>
#include <tempest/Vector.hpp>

class C44Matrix;
class CGxTex;

// The map shadow map: the quality tier above blob shadows, where casters are rendered from the
// light's point of view into a depth map that the terrain then samples. See
// docs/ref/parity-shadowmap.md for the recovered mechanism.
//
// This is the light volume only -- the camera, the projection and the texture matrix. Rendering
// into a target and sampling it in the terrain shader come after.

// The constants that define the light volume, gathered where the comparison harness can read them.
//
// They were file-local `const` before, so the compiler folded them away and they appeared in no
// symbol table -- which meant the seven matching values in the reference's shadow cache could only
// be eyeballed, never actually compared. Exposing them costs nothing and turns an assumption into a
// check.
struct MapShadowConstants {
    int32_t size;        // map edge in texels
    float extent;        // half-size of the covered box, in yards
    float nearPlane;
    float farPlane;
    float back;          // how far back along the light the eye sits
    float bias;
    float depthScale;    // 1 / farPlane
    float upHint[3];
};

extern const MapShadowConstants g_mapShadowConstants;

// The focus the light volume was last built around (the player position).
extern C3Vector g_mapShadowFocus;

// The direction the shadow light travels, exaggerated and clamped the reference's way.
C3Vector MapShadowLightDirection();

// Where the map is centred: the camera, or whatever object the camera is tracking.
// ref: FUN_007bb3e0
C3Vector MapShadowFocus();

// Release both render targets so the next frame allocates them again. This is frozen's half of
// ShadowMapReleaseTargets: the reference frees three textures plus three filter pairs, and
// frozen only has these two.
void MapShadowReleaseTargets();

// Clear one shadow target pair to white through a viewport rectangle, restoring the colour
// target and the viewport afterwards. ref: FUN_007bb830
void MapShadowClearTarget(CGxTex* color, CGxTex* depth, const float* viewport);

// The per-frame driver: direction, focus, intensity, render. ref: FUN_007bb570
void MapShadowRender();

// Rebuild the light volume around a focus point (the player). Call once per frame before drawing.
void MapShadowSetup(const C3Vector& focus);

// The orthographic projection the map is rendered with. Plain: the depth written into the map is
// the light-space z the vertex shader passes down, not the projected z, so this stays ordinary.
const C44Matrix& MapShadowProjection();

// world -> light view. Casters whose vertices already carry the camera's view (frozen bakes it into
// M2 bone matrices) are rebased with inverse(cameraView) * this.
const C44Matrix& MapShadowLightView();

// world -> shadow texture: xy in [0,1] texture space, z the linear light depth in [0,1].
const C44Matrix& MapShadowTexMatrix();

// Size of the map in texels (square).
int32_t MapShadowSize();

// Allocate (once) the colour and depth targets, bind them, clear to white and leave the device
// ready for caster draws. Returns 0 if the targets could not be created, in which case nothing was
// bound and MapShadowEnd must not be called.
int32_t MapShadowBegin();

// Restore the previous colour/depth targets and viewport.
void MapShadowEnd();

// The rendered map, for sampling. Null until a successful MapShadowBegin/MapShadowEnd pair.
CGxTex* MapShadowTexture();

// Debug: write the map out as a greyscale TGA on the next frame that renders it.
void MapShadowRequestDump(const char* path);

// The plane and height the map object and interior shadow binders sample against. Built by
// MapShadowSetupPlane; the terrain path never reads them. ref: FUN_007bb670
extern C4Plane g_mapShadowPlane;
extern float g_mapShadowHeight;

void MapShadowSetupPlane(const C3Vector& playerPos);

#endif
