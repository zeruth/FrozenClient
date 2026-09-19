#include "world/MapShadow.hpp"
#include "world/CWorld.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/RenderTarget.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/texture/CGxTex.hpp"
#include <tempest/Matrix.hpp>
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
char s_dumpPath[260] = { 0 };

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

    int32_t ok = GxTexCreate(
        GxTex_2d, SHADOW_SIZE, SHADOW_SIZE, 1, GxTex_R32F, GxTex_R32F,
        flags, nullptr, nullptr, "ShadowCache", s_colorTex);

    if (ok) {
        ok = GxTexCreate(
            GxTex_2d, SHADOW_SIZE, SHADOW_SIZE, 1, GxTex_D24X8, GxTex_D24X8,
            flags, nullptr, nullptr, "ShadowCacheDepth", s_depthTex);
    }

    if (!ok || !s_colorTex || !s_depthTex) {
        fprintf(stderr, "MapShadow: target allocation FAILED (%dx%d)\n", SHADOW_SIZE, SHADOW_SIZE);
        s_allocFailed = true;
        s_colorTex = nullptr;
        s_depthTex = nullptr;
        return false;
    }

    fprintf(stderr, "MapShadow: targets allocated %dx%d (R32F + D24X8)\n", SHADOW_SIZE, SHADOW_SIZE);
    return true;
}

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

void MapShadowSetup(const C3Vector& focus) {
    g_mapShadowFocus = focus;

    // The reference's exaggeration and clamp operate on the direction the light TRAVELS, which
    // points downward, so the vector has to be flipped into that convention BEFORE the rule is
    // applied. frozen stores the direction TOWARD the light (see CWorld::s_outdoorDirection), whose z
    // is positive, and the clamp is one-sided: applied to a positive z it never engages at all.
    //
    // That was the bug. The clamp is what holds the light near 52 degrees of elevation; without it
    // frozen produced 0.963 where the reference holds 0.829, a far steeper light and correspondingly
    // wrong shadow length. Confirmed against the value the reference stores at 0x00D43180:
    // reference -0.3956 -0.3956 -0.8288, this code -0.3970 -0.3970 -0.8275.
    C3Vector lit = CWorld::GetOutdoorDirection();
    C3Vector dir = { -lit.x, -lit.y, -lit.z * 5.0f };

    if (dir.z < -1.2f) {
        dir.z = -1.2f;
    }

    dir = Normalize(dir);

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
    remap.d0 = 0.5f + 0.5f / static_cast<float>(SHADOW_SIZE);
    remap.d1 = 0.5f + 0.5f / static_cast<float>(SHADOW_SIZE);
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

        float halfTexel = 0.5f / static_cast<float>(SHADOW_SIZE);
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
    return SHADOW_SIZE;
}


int32_t MapShadowBegin() {
    if (!s_built || !EnsureTargets()) {
        return 0;
    }

    // Set FROZEN_SHADOW_DUMP to a file path to have the map written out once, a hundred frames in so
    // the world has finished streaming. Reading the map back is the only way to tell an empty pass
    // from a broken bind while nothing samples it yet.
    static int32_t frame = 0;

    // Frame 30, not 100: the client currently dies about 15 seconds after entering the world, and
    // waiting 100 frames was leaving no dump at all when the run ended early.
    if (++frame == 30) {
        const char* want = getenv("FROZEN_SHADOW_DUMP");

        fprintf(stderr, "MapShadow: dump requested? %s\n", want ? want : "(FROZEN_SHADOW_DUMP unset)");

        if (want) {
            MapShadowRequestDump(want);
        }
    }

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


    // Read back only AFTER the target has been unbound. GetRenderTargetData refuses a surface
    // that is still the device's active render target, which is why the dump reported FAILED
    // while it sat at the top of this function.
    if (s_dumpPath[0]) {
        fprintf(stderr, "MapShadow: dumping tex %p handle %p\n",
                static_cast<void*>(s_colorTex),
                s_colorTex ? s_colorTex->m_apiSpecificData : nullptr);

        int32_t ok = GxRenderTargetDump(s_colorTex, s_dumpPath);
        fprintf(stderr, "MapShadow: dump to %s %s\n", s_dumpPath, ok ? "OK" : "FAILED");
        s_dumpPath[0] = 0;
    }

    s_rendered = true;
}

CGxTex* MapShadowTexture() {
    return s_rendered ? s_colorTex : nullptr;
}

void MapShadowRequestDump(const char* path) {
    if (!path) {
        s_dumpPath[0] = 0;
        return;
    }

    strncpy(s_dumpPath, path, sizeof(s_dumpPath) - 1);
    s_dumpPath[sizeof(s_dumpPath) - 1] = 0;
}
