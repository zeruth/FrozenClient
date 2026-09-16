#ifndef WORLD_MAP_SHADOW_HPP
#define WORLD_MAP_SHADOW_HPP

#include <cstdint>
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

// Rebuild the light volume around a focus point (the player). Call once per frame before drawing.
void MapShadowSetup(const C3Vector& focus);

// The orthographic projection the map is rendered with. Plain: the depth written into the map is
// the light-space z the vertex shader passes down, not the projected z, so this stays ordinary.
const C44Matrix& MapShadowProjection();

// world -> light view. Casters whose vertices already carry the camera's view (whoa bakes it into
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

#endif
