#include "world/MapShadow.hpp"
#include "model/CM2Scene.hpp"
#include "world/ShadowMap.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "world/CWorld.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/RenderTarget.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/texture/CGxTex.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Plane.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

// Light volume for the map shadow map. All constants recovered in docs/ref/parity-shadowmap.md:
//
//   direction : the outdoor light direction with z scaled by 5 and clamped to >= -1.2. Given the
//               solved zenith band (110-127 degrees) that clamp ALWAYS bites, so the shadow light
//               is effectively pinned near 52 degrees elevation and barely moves over the day.
//               That is what stops shadows stretching to the horizon at dawn and dusk.
//   camera    : eye = focus - dir * 2000, target = focus, up = world +X (world +Z is unusable at
//               that pitch).
//   projection: orthographic over a 40 x 40 yard box, near 1, far 4000.
//   depth     : column 2 of the projection is replaced by column 2 of the VIEW matrix, so the
//               stored depth is linear distance along the light rather than a projected z; the
//               texture matrix then scales it by 1/4000 into [0, 1]. The reader is handed the same
//               constant, so writer and reader agree.
//   bias      : -0.1 world units, baked into the matrix rather than set as a render state.

namespace {

const float SHADOW_EXTENT = 20.0f;   // half-size of the 40x40 yard box
const float SHADOW_NEAR = 1.0f;
const float SHADOW_FAR = 4000.0f;
const float SHADOW_BACK = 2000.0f;   // how far back along the light the eye sits
const float SHADOW_BIAS = -0.1f;
const float SHADOW_DEPTH_SCALE = 1.0f / SHADOW_FAR;
// The DEFAULT map edge. The size in force is g_shadowMapSize, which ShadowMapUpdateSize takes
// from the quality -- two of the seven levels ask for 2048. This constant is what the quality
// is what every tier but those two asks for, and what the compare harness
// reports as the nominal size.
const int32_t SHADOW_SIZE = 1024;

} // namespace (reopened below)

// The point the light volume was last built around. The reference caches the same thing, so
// exposing frozen's copy turns "we pass the player position, same as the reference" from a claim in a
// comment into a compared value.
C3Vector g_mapShadowFocus = { 0.0f, 0.0f, 0.0f };

const MapShadowConstants g_mapShadowConstants = {
    SHADOW_SIZE,
    SHADOW_EXTENT,
    SHADOW_NEAR,
    SHADOW_FAR,
    SHADOW_BACK,
    SHADOW_BIAS,
    SHADOW_DEPTH_SCALE,
    { 1.0f, 0.0f, 0.0f },
};

namespace {

C44Matrix s_lightView;
C44Matrix s_projection;
C44Matrix s_savedProjection;
C44Matrix s_texMatrix;
bool s_built = false;

CGxTex* s_colorTex = nullptr;
CGxTex* s_depthTex = nullptr;
CGxTex* s_savedColor = nullptr;
CGxTex* s_savedDepth = nullptr;
bool s_allocFailed = false;
bool s_rendered = false;
float s_savedViewport[6] = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

// A render target owns its texels -- the device writes them, nothing uploads them -- so there is
// nothing for an upload callback to do and both targets below were created without one. GxTexCreate
// asserts one is present anyway, and a build with assertions live takes that literally: this killed
// the client a few seconds into the world on Android, where assertions are not compiled out.
void ShadowTargetCallback(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel, void* userArg, uint32_t& stride, const void*& texels) {
}

// Allocate both targets, not just the colour one. The reference gets away with a lone R32F colour
// target because it keeps the default depth-stencil, but D3D9 requires the depth surface to be at
// least as large as the colour surface, and a 1024 map against a smaller window fails the bind.
// Taking the hardware-PCF layout unconditionally sidesteps that entirely.
bool EnsureTargets() {
    if (s_colorTex && s_depthTex) {
        return true;
    }

    if (s_allocFailed) {
        return false;
    }

    // No mips, no wrap, render target, no anisotropy: clamping matters because a lookup outside the
    // 40 yard box must read the border, not wrap around to the far side of the map.
    CGxTexFlags flags(GxTex_Linear, 1, 1, 0, 0, 1, 0);

    // The size the quality asks for, not a constant -- see ShadowMapUpdateSize.
    int32_t size = g_shadowMapSize;

    int32_t ok = GxTexCreate(
        GxTex_2d, size, size, 1, GxTex_R32F, GxTex_R32F,
        flags, nullptr, ShadowTargetCallback, "ShadowCache", s_colorTex);

    if (ok) {
        ok = GxTexCreate(
            GxTex_2d, size, size, 1, GxTex_D24X8, GxTex_D24X8,
            flags, nullptr, ShadowTargetCallback, "ShadowCacheDepth", s_depthTex);
    }

    if (!ok || !s_colorTex || !s_depthTex) {
        fprintf(stderr, "MapShadow: target allocation FAILED (%dx%d)\n", size, size);
        s_allocFailed = true;
        s_colorTex = nullptr;
        s_depthTex = nullptr;
        return false;
    }

    fprintf(stderr, "MapShadow: targets allocated %dx%d (R32F + D24X8)\n", size, size);
    return true;
}

} // namespace

// frozen's half of ShadowMapReleaseTargets (ref FUN_00874240), which the realloc latch calls
// before rebuilding. The reference closes three textures -- the map and the lit and unlit
// variants -- plus two handles in each of three filter ring entries; frozen renders one map
// with an explicit depth surface and builds no filter chain, so these two are all of it.
//
// s_allocFailed is cleared as well. It is a latch that stops a failed allocation being retried
// every frame, and a device reset is exactly the event that can make the allocation start
// working, so keeping it set across a release would turn one bad frame into a dead feature.
void MapShadowReleaseTargets() {
    if (s_colorTex) {
        GxTexDestroy(s_colorTex);
        s_colorTex = nullptr;
    }

    if (s_depthTex) {
        GxTexDestroy(s_depthTex);
        s_depthTex = nullptr;
    }

    s_allocFailed = false;
    s_rendered = false;
}

// ref: FUN_007bb830
// Clear one shadow target pair to white through a viewport rectangle.
//
// The order is the reference's and it matters: DEPTH is bound first and COLOUR second, because
// binding a colour target resets the viewport, so the rectangle has to be set after the last
// bind rather than before the first. Then the viewport is restored from what the device had,
// and only the COLOUR target is put back -- the reference never restores the depth one here,
// which is a real asymmetry and not a transcription slip: its caller binds a depth surface per
// map and the colour target is the one shared with the frame.
//
// White is 'nothing casts here': a sampled depth of 1.0 is further than any real surface.
void MapShadowClearTarget(CGxTex* color, CGxTex* depth, const float* viewport) {
    CGxTex* savedColor = nullptr;
    GxRenderTargetGet(GxBuffers_Color, savedColor);

    // The device's current viewport, to be put back after the clear.
    float saved[6];
    GxXformViewport(saved[0], saved[1], saved[2], saved[3], saved[4], saved[5]);

    GxRenderTargetSet(GxBuffers_Depth, depth, 0);
    GxRenderTargetSet(GxBuffers_Color, color, 0);

    GxXformSetViewport(viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f);

    CImVector white = { 0xFF, 0xFF, 0xFF, 0xFF };
    GxSceneClear(0x3, white);

    GxXformSetViewport(saved[0], saved[1], saved[2], saved[3], saved[4], saved[5]);

    GxRenderTargetSet(GxBuffers_Color, savedColor, 0);
}

namespace {

C3Vector Normalize(const C3Vector& v) {
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);

    if (len < 1e-6f) {
        return { 0.0f, 0.0f, 1.0f };
    }

    return { v.x / len, v.y / len, v.z / len };
}

C3Vector Cross(const C3Vector& a, const C3Vector& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

float Dot(const C3Vector& a, const C3Vector& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

} // namespace

// The reference's exaggeration and clamp operate on the direction the light TRAVELS, which
// points downward, so the vector has to be flipped into that convention BEFORE the rule is
// applied. frozen stores the direction TOWARD the light (see CWorld::s_outdoorDirection), whose z
// is positive, and the clamp is one-sided: applied to a positive z it never engages at all.
//
// That was the bug. The clamp is what holds the light near 52 degrees of elevation; without it
// frozen produced 0.963 where the reference holds 0.829, a far steeper light and correspondingly
// wrong shadow length. Confirmed against the value the reference stores at 0x00D43180:
// reference -0.3956 -0.3956 -0.8288, this code -0.3970 -0.3970 -0.8275.
//
// BOTH NUMBERS ARE NOW READ RATHER THAN MATCHED. The reference applies them in FUN_007bb570,
// which multiplies the light direction's z by DAT_009ebf34 and clamps it against
// DAT_00a400fc before normalizing. Read out of the image on 2026-09-25 those are exactly
// 5.0 and -1.2, so the 5.0f and -1.2f below are the reference's own constants and not a fit
// to an observed vector.
//
// It lives here rather than inside MapShadowSetup because the reference stores the direction into
// the shadow map module BEFORE it picks a focus, and the order is worth keeping.
C3Vector MapShadowLightDirection() {
    C3Vector lit = CWorld::GetOutdoorDirection();
    C3Vector dir = { -lit.x, -lit.y, -lit.z * 5.0f };

    if (dir.z < -1.2f) {
        dir.z = -1.2f;
    }

    dir = Normalize(dir);

    return dir;
}

void MapShadowSetup(const C3Vector& focus) {
    g_mapShadowFocus = focus;

    C3Vector dir = MapShadowLightDirection();

    // `dir` now points the way the light travels, i.e. downward, so the eye is the focus displaced
    // BACK along it and ends up in the sky, exactly as the reference does it
    // (eye = centre - lightDirWorld * 2000). The forward axis is then `dir` itself.
    C3Vector eye = { focus.x - dir.x * SHADOW_BACK, focus.y - dir.y * SHADOW_BACK, focus.z - dir.z * SHADOW_BACK };
    C3Vector zAxis = Normalize({ focus.x - eye.x, focus.y - eye.y, focus.z - eye.z });
    C3Vector up = { 1.0f, 0.0f, 0.0f };
    C3Vector xAxis = Normalize(Cross(up, zAxis));
    C3Vector yAxis = Cross(zAxis, xAxis);

    C44Matrix view;
    view.Identity();
    view.a0 = xAxis.x; view.a1 = yAxis.x; view.a2 = zAxis.x;
    view.b0 = xAxis.y; view.b1 = yAxis.y; view.b2 = zAxis.y;
    view.c0 = xAxis.z; view.c1 = yAxis.z; view.c2 = zAxis.z;
    view.d0 = -Dot(xAxis, eye);
    view.d1 = -Dot(yAxis, eye);
    view.d2 = -Dot(zAxis, eye);

    // Orthographic over the box, then swap in linear light depth for column 2.
    C44Matrix proj;
    proj.Identity();
    proj.a0 = 1.0f / SHADOW_EXTENT;
    proj.b1 = 1.0f / SHADOW_EXTENT;
    proj.c2 = 1.0f / (SHADOW_FAR - SHADOW_NEAR);
    proj.d2 = -SHADOW_NEAR / (SHADOW_FAR - SHADOW_NEAR);

    C44Matrix vp = view * proj;

    // Column 2 = the view's column 2, i.e. distance along the light, plus the depth bias. This
    // applies to the SAMPLING matrix only. The map is rendered with a plain orthographic
    // projection: the reference's shadow map pixel shader writes the light-space z it receives in
    // oT1, not the projected depth, so the two only agree if the projection stays ordinary and the
    // comparison value is built from the view's column 2.
    vp.a2 = view.a2;
    vp.b2 = view.b2;
    vp.c2 = view.c2;
    vp.d2 = view.d2 + SHADOW_BIAS;

    s_lightView = view;

    // The rendering projection, in the [-1, 1] depth convention CGxDeviceD3d::IXformSetProjection
    // expects; it does the remap to D3D's [0, 1] itself, and handing it an already-remapped matrix
    // would push every caster outside the clip volume.
    s_projection.Identity();
    s_projection.a0 = 1.0f / SHADOW_EXTENT;
    s_projection.b1 = 1.0f / SHADOW_EXTENT;
    s_projection.c2 = 2.0f / (SHADOW_FAR - SHADOW_NEAR);
    s_projection.d2 = -(SHADOW_FAR + SHADOW_NEAR) / (SHADOW_FAR - SHADOW_NEAR);

    // world -> texture: NDC xy [-1,1] to [0,1] with y flipped, depth scaled into [0,1], plus the
    // half-texel offset the reference applies.
    C44Matrix remap;
    remap.Identity();
    remap.a0 = 0.5f;
    remap.b1 = -0.5f;
    remap.c2 = SHADOW_DEPTH_SCALE;
    remap.d0 = 0.5f + 0.5f / static_cast<float>(g_shadowMapSize);
    remap.d1 = 0.5f + 0.5f / static_cast<float>(g_shadowMapSize);
    remap.d2 = 0.0f;

    s_texMatrix = vp * remap;

    // One-shot self-check, per docs/ref/parity-shadowmap.md: the focus point is the centre of the
    // light volume by construction, so it must land at the middle of the map at half depth. If the
    // axis choice, the column-2 swap or the remap were wrong this is where it shows, before any
    // render target exists to confuse the picture.
    if (!s_built) {
        float u = focus.x * s_texMatrix.a0 + focus.y * s_texMatrix.b0 + focus.z * s_texMatrix.c0 + s_texMatrix.d0;
        float v = focus.x * s_texMatrix.a1 + focus.y * s_texMatrix.b1 + focus.z * s_texMatrix.c1 + s_texMatrix.d1;
        float d = focus.x * s_texMatrix.a2 + focus.y * s_texMatrix.b2 + focus.z * s_texMatrix.c2 + s_texMatrix.d2;

        float halfTexel = 0.5f / static_cast<float>(g_shadowMapSize);
        bool ok = fabsf(u - 0.5f) < halfTexel * 2.0f
               && fabsf(v - 0.5f) < halfTexel * 2.0f
               && fabsf(d - 0.5f) < 0.001f;

        fprintf(stderr, "MapShadow: focus -> uv(%.4f %.4f) depth %.4f  light(%.3f %.3f %.3f)  %s\n",
            u, v, d, dir.x, dir.y, dir.z, ok ? "OK" : "MISMATCH");
    }

    s_built = true;
}

const C44Matrix& MapShadowProjection() {
    return s_projection;
}

const C44Matrix& MapShadowLightView() {
    return s_lightView;
}

const C44Matrix& MapShadowTexMatrix() {
    return s_texMatrix;
}

int32_t MapShadowSize() {
    return g_shadowMapSize;
}


int32_t MapShadowBegin() {
    if (!s_built || !EnsureTargets()) {
        return 0;
    }

    // Set FROZEN_SHADOW_DUMP to a file path to have the map written out once, a hundred frames in so
    // the world has finished streaming. Reading the map back is the only way to tell an empty pass
    // from a broken bind while nothing samples it yet.
    GxRenderTargetGet(GxBuffers_Color, s_savedColor);
    GxRenderTargetGet(GxBuffers_Depth, s_savedDepth);
    GxXformViewport(
        s_savedViewport[0], s_savedViewport[1], s_savedViewport[2],
        s_savedViewport[3], s_savedViewport[4], s_savedViewport[5]);

    GxRsPush();

    GxRenderTargetSet(GxBuffers_Depth, s_depthTex, 0);
    GxRenderTargetSet(GxBuffers_Color, s_colorTex, 0);

    // SetRenderTarget resets the viewport to the whole surface, so the viewport must be set AFTER
    // the bind, never before. Getting this backwards leaves the map rendered at back-buffer scale
    // into one corner, which looks like a broken projection and is not.
    GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

    // White is "nothing casts here": the sampled depth is 1.0, further than any real surface.
    CImVector white = { 0xFF, 0xFF, 0xFF, 0xFF };
    GxSceneClear(0x3, white);

    // Casters are drawn double-sided and unfogged; a shadow only cares about the nearest surface
    // along the light, so a back face is as good a caster as a front one.
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Fog, 0);

    // Save the camera's projection before replacing it. MapShadowEnd restores it.
    //
    // Leaving it set was the cause of entities drawing at a fixed size over everything: terrain
    // survives because it builds its own view-projection earlier in the frame, but the model pass
    // calls CShaderEffect::UpdateProjMatrix, which reads the DEVICE projection -- so every entity
    // was drawn through this orthographic matrix. Orthographic means no perspective divide, hence
    // no change with camera distance, and its depth does not match the perspective depth buffer,
    // hence always in front.
    GxXformProjection(s_savedProjection);
    GxXformSetProjection(s_projection);

    return 1;
}

void MapShadowEnd() {
    GxRsPop();

    GxXformSetProjection(s_savedProjection);

    GxRenderTargetSet(GxBuffers_Color, s_savedColor, 0);
    GxRenderTargetSet(GxBuffers_Depth, s_savedDepth, 0);
    GxXformSetViewport(
        s_savedViewport[0], s_savedViewport[1], s_savedViewport[2],
        s_savedViewport[3], s_savedViewport[4], s_savedViewport[5]);


    s_rendered = true;
}

CGxTex* MapShadowTexture() {
    return s_rendered ? s_colorTex : nullptr;
}

// The plane and height the MAP OBJECT and interior shadow binders read, and the last thing item 10
// names. Only those binders consume it -- the terrain path never reads a plane -- which is why a
// terrain-only port could skip this.
C4Plane g_mapShadowPlane = { { 0.0f, 0.0f, 1.0f }, 0.0f };
float g_mapShadowHeight = 0.0f;

// ref: FUN_007bb670
// Writes exactly two things: a plane through the player, and the player's height plus two.
//
// The normal is the WORLD matrix's third row, normalised -- (0, 0, 1) whenever that matrix is
// identity, which it is at the reference's only call site. The point is the player position made
// camera-relative and then pushed through world * view.
//
// UNCERTAIN, and the reference is what it is: this mixes a world-space normal with a view-space
// point, which only produces a meaningful plane because the world matrix is identity there. Its
// intended space was not resolved, so this reproduces the arithmetic rather than a cleaned-up
// version of it. See docs/ref/parity-shadowmap.md section 6c.
void MapShadowSetupPlane(const C3Vector& playerPos) {
    C44Matrix world;
    GxXformWorld(world);

    C44Matrix view;
    GxXformView(view);

    C44Matrix worldView = world * view;

    C3Vector normal = { world.c0, world.c1, world.c2 };
    float length = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);

    if (length > 0.0f) {
        float inv = 1.0f / length;
        normal.x *= inv;
        normal.y *= inv;
        normal.z *= inv;
    }

    const C3Vector& cameraPos = CWorld::GetCameraPos();
    C3Vector relative = {
        playerPos.x - cameraPos.x,
        playerPos.y - cameraPos.y,
        playerPos.z - cameraPos.z
    };

    C3Vector point = relative * worldView;

    g_mapShadowPlane.n = normal;
    g_mapShadowPlane.d = -(normal.x * point.x + normal.y * point.y + normal.z * point.z);

    // DAT_00a4040c.
    g_mapShadowHeight = playerPos.z + 2.0f;
}

// ref: FUN_007bb3e0
// Where the shadow map is centred: the camera position, unless the active camera is tracking an
// object, in which case that object's position wins. The reference resolves the camera's target
// GUID through the object manager with a TYPE_OBJECT mask -- any object, not just units -- and
// falls back to the camera when the lookup misses, which is what happens while a target is loading.
C3Vector MapShadowFocus() {
    C3Vector focus = CWorld::GetCameraPos();

    auto camera = CGWorldFrame::GetActiveCamera();

    if (camera) {
        auto target = ClntObjMgrObjectPtr(camera->GetTarget(), TYPE_OBJECT, __FILE__, __LINE__);

        if (target) {
            focus = target->GetPosition();
        }
    }

    return focus;
}

// ref: FUN_007bb570
// The per-frame map shadow driver, in the reference's order: store the light direction, pick the
// focus, set the intensity, render the map, filter it.
//
// DIVERGENCE, and it is one of decomposition rather than behaviour. The reference's render step is
// FUN_00875f80, which builds the light volume from the focus AND draws the casters through three
// function pointers the map registers; frozen splits that into MapShadowSetup (the volume) and
// MapShadowBegin / DrawShadowCasters / MapShadowEnd (the draw), so this calls four things where the
// reference calls one. The filter chain after it (FUN_008750b0) has no counterpart at all.
void MapShadowRender() {
    // THE REALLOC LATCH, and the reason every line below it was dead code until 2026-09-27.
    //
    // ShadowMapSetQuality raises g_shadowMapRealloc to say 'the targets are the wrong size now',
    // and ShadowMapGetQuality reports 0 while it is up so the frame that changes the setting
    // draws unshadowed instead of sampling a stale map. Nothing in frozen ever lowered it again.
    // So the first time anything set the quality the latch went up and stayed up, the getter
    // returned 0 for the rest of the process, and this function, ShadowMapBindTerrain,
    // ShadowMapBindMapObj and ShadowMapBindScene all returned at their first line forever.
    //
    // The reference clears it here, at the top of its render step (FUN_00875f80), after doing the
    // reallocation the latch was asking for: free the targets, allocate them again, drop the
    // flag. This is that, and it has to stay ABOVE the quality check -- the check is the thing
    // the latch suppresses, so a clear underneath it would never run.
    if (g_shadowMapRealloc) {
        MapShadowReleaseTargets();

        // Both in the reference's order and for the reference's reason: the size comes from the
        // quality and the PCF kernel is expressed in units of it, so the kernel has to be rebuilt
        // whenever the size can have moved. EnsureTargets does the first two before it allocates.
        ShadowMapUpdateSize();
        ShadowMapBuildPcfTaps();

        EnsureTargets();

        g_shadowMapRealloc = 0;
    }

    if (ShadowMapGetQuality() <= 0) {
        return;
    }

    ShadowMapSetLightDirection(MapShadowLightDirection());

    C3Vector focus = MapShadowFocus();

    // 1.0 and 0 are the constants at the reference's call site, not a choice.
    ShadowMapSetIntensity(1.0f, 0);

    auto scene = CWorld::GetM2Scene();

    if (!scene) {
        return;
    }

    MapShadowSetup(focus);

    if (MapShadowBegin()) {
        scene->DrawShadowCasters(MapShadowLightView());
        MapShadowEnd();
    }

    // TODO FUN_008750b0, 822 bytes: the blur chain that fills the three filter textures
    // ShadowMapBindScene binds at quality > 2. Nothing in frozen stands in for it.
}
