#include "world/DayNight.hpp"
#include "world/Terrain.hpp"
#include "world/map/CMap.hpp"
#include "world/CWorldScene.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldParam.hpp"
#include "world/Clouds.hpp"
#include "db/Db.hpp"
#include "console/CVar.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Model.hpp"
#include "model/Model2.hpp"
#include "model/M2Types.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "model/CM2Lighting.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/shader/CGxShader.hpp"
#include "util/SFile.hpp"
#include <storm/String.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

// ---------------------------------------------------------------------------------------------
// Mirrors of the four symbols this code used from Terrain.cpp. Keeping them here, fed by
// SkySetCameraState, is what makes the move a pure relocation rather than a rewrite: every
// expression below is unchanged from what shipped.
C3Vector s_cameraPos = { 0.0f, 0.0f, 0.0f };
int32_t s_cameraLiquidKind = -1;

// The UI shader pair the sky draws through. Terrain.cpp owns their creation, and its public
// accessor ensures them, so this stands in for the file-local pair plus EnsureShaders().
CGxShader* s_uiVertexShader[1] = { nullptr };
CGxShader* s_uiPixelShader = nullptr;

void EnsureShaders() {
    TerrainUiShaders(s_uiVertexShader[0], s_uiPixelShader);
}

// --- Sky dome ---------------------------------------------------------------------------------
// A camera-centred dome coloured by the LightIntBand sky gradient (horizon .. zenith). Drawn first
// with depth off so terrain and objects paint over it, matching the reference's sky backdrop.
// Sky dome rings, as zenith angles in turns of pi (0 = straight up, 0.5 = horizon, 1 = nadir).
//
// The paragraph that used to stand here described the dome BEFORE the table was read out of the
// binary, flagged the zenith-angle reading as uncertain, and argued for carrying the gradient down
// to the horizon and blending into fog. It was left in place when the reference's own table landed
// and then contradicted the paragraph below it and the code under both. Removed 2026-09-23; the
// uncertainty it flagged is settled.
//
// One thing in it was a measurement and is kept, because it predicts what a run will show. Colouring
// the reference's table literally puts a hard edge at 45 degrees wherever the fog band differs from
// the horizon band, and in some zones it differs badly -- the Death Knight start reads sky
// 62,154,197 against fog 0,62,85 at noon, which will look like a blue ball with a dark skirt. That
// is what the reference does, so it is the expected appearance rather than a defect to tune away.
// If it looks wrong on screen, check the band mapping before changing the geometry.
//
// The reference's dome, read out of the binary rather than tuned: FUN_007f2470 builds 24 segments
// and 7 rings whose zenith angles are the table at 0x00a41a90 times pi, and the azimuth step at
// 0x00a41cec is exactly 1/24. Both were checked against WoW.exe directly (2026-09-23), as was the
// vertex count the colour writer implies: 1 + 5*24 + 1 = 122.
//
// Note how little of the sphere the gradient occupies. Every band sits between the zenith and 45
// degrees elevation; from there down it is one flat sheet of the fog band, which is why the dome
// meets the fogged terrain horizon with no seam and no blending -- they are the same colour.
// frozen previously spread 12 rings evenly and lerped a z gradient across them, which put the
// gradient far too low and made the horizon band far too thin.
const int32_t SKY_RINGS = 6;
const int32_t SKY_SEGS = 24;
const float SKY_RING_ZENITH[SKY_RINGS + 1] = {
    0.0f, 0.17f, 0.20f, 0.23f, 0.24f, 0.25f, 1.0f
};
const int32_t SKY_VERTS = (SKY_RINGS + 1) * (SKY_SEGS + 1);
const float SKY_RADIUS = 150.0f; // inside the minimum far clip (183) so the dome is never clipped

C3Vector s_skyPos[SKY_VERTS];
C2Vector s_skyUv[SKY_VERTS];
CImVector s_skyCol[SKY_VERTS];
uint16_t s_skyIdx[SKY_RINGS * SKY_SEGS * 6];
int32_t s_skyIdxCount = 0;
bool s_skyBuilt = false;
HTEXTURE s_skyWhite = nullptr;

// GxTexCreate asserts width >= 8, so the dome cannot have the 2x2 white sheet it wants --
// the sheet is uniform, so widening it to the smallest legal size costs 256 bytes and
// changes nothing on screen. On Windows the assertion is compiled out and a 2x2 texture
// went through unnoticed; on Android assertions are live and it killed the client a few
// seconds into the world, the moment the sky first drew.
const int32_t SKY_WHITE_DIM = 8;
CImVector s_skyWhitePixels[SKY_WHITE_DIM * SKY_WHITE_DIM];

void SkyWhiteCallback(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mip, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Latch) {
        stride = 4 * w;
        texels = s_skyWhitePixels;
    }
}

// Depth range of the sky viewport in the reference (DAT_00adeef0 / DAT_00adeef4)
// The sky's depth range. Both ends are the far value on purpose.
//
// The reference constant for the minimum is 0.9990234375, and squeezing the dome into
// [0.9990234375, 1.0] is meant to put every sky pixel behind everything else. With this client's
// near and far planes it does not: depth is z_buf = (f/(f-n)) * (1 - n/z), so with n = 0.2 and
// f = 727 the buffer already reads 0.9990234 at **159 yards**. Every terrain pixel beyond that has a
// LARGER depth than the dome's nearest ring, so the dome passed the less-equal test and, being
// GxBlend_Add, was added on top of ground that was already fogged -- a bright ring at the onset
// distance, which is what the user reported seeing.
//
// Pinning both ends to 1.0 makes every sky pixel land at exactly the cleared depth, so less-equal
// admits it only where nothing else has drawn. That is the property the squeezed range was reaching
// for. Recorded as a deliberate divergence from the reference constant rather than a port.
const float SKY_VIEWPORT_MIN_Z = 1.0f;
const float SKY_VIEWPORT_MAX_Z = 1.0f;

// The sky's own scenes, separate from the world M2 scene. File scope so SkyRelease can free them
// on unload; as function-local statics they survived every map change and leaked.
CM2Scene* s_starsScene = nullptr;
CM2Model* s_starsModel = nullptr;
bool s_starsTried = false;
CM2Scene* s_skyboxScene = nullptr;
CM2Model* s_skyboxModel = nullptr;
char s_skyboxLoaded[260] = { 0 };

void BuildSkyDome() {
    const float PI = 3.14159265f;
    int32_t v = 0;

    for (int32_t ring = 0; ring <= SKY_RINGS; ring++) {
        float theta = SKY_RING_ZENITH[ring] * PI; // measured from the zenith
        float ct = cosf(theta);
        float st = sinf(theta);

        for (int32_t seg = 0; seg <= SKY_SEGS; seg++) {
            float phi = static_cast<float>(seg) / SKY_SEGS * 2.0f * PI;
            s_skyPos[v].x = st * cosf(phi) * SKY_RADIUS;
            s_skyPos[v].y = st * sinf(phi) * SKY_RADIUS;
            s_skyPos[v].z = ct * SKY_RADIUS;
            s_skyUv[v].x = 0.0f;
            s_skyUv[v].y = 0.0f;
            v++;
        }
    }

    int32_t n = 0;

    for (int32_t ring = 0; ring < SKY_RINGS; ring++) {
        for (int32_t seg = 0; seg < SKY_SEGS; seg++) {
            uint16_t i0 = static_cast<uint16_t>(ring * (SKY_SEGS + 1) + seg);
            uint16_t i1 = static_cast<uint16_t>(i0 + 1);
            uint16_t i2 = static_cast<uint16_t>(i0 + (SKY_SEGS + 1));
            uint16_t i3 = static_cast<uint16_t>(i2 + 1);

            s_skyIdx[n++] = i0; s_skyIdx[n++] = i2; s_skyIdx[n++] = i1;
            s_skyIdx[n++] = i1; s_skyIdx[n++] = i2; s_skyIdx[n++] = i3;
        }
    }

    s_skyIdxCount = n;
    s_skyBuilt = true;
}

// The skybox model is shown at full brightness (its own texture/colours carry the sky's look); it
// never takes distance fog.
void SkyboxLightingCallback(CM2Model* model, CM2Lighting* lighting, void* arg) {
    C3Vector white = { 1.0f, 1.0f, 1.0f };
    lighting->AddAmbient(white);
}

} // namespace

// Release the sky's private scenes (stars and the zone skybox). They live outside the world M2
// scene and were never freed, so every map change leaked one of each and left the previous zone's
// skybox model resident.
// Release the sky's own models on unload. The stars and skybox live in their own CM2Scene objects
// rather than the world scene, and as function-local statics they used to survive every map change,
// leaving the previous zone's skybox resident. The scenes themselves are kept and reused: the port
// has M2CreateScene but no matching destroy, so inventing one here would be guesswork.
void SkyRelease() {
    CloudsRelease();

    if (s_starsModel) {
        s_starsModel->DetachFromScene();
        s_starsModel->Release();
        s_starsModel = nullptr;
    }

    s_starsTried = false;

    if (s_skyboxModel) {
        s_skyboxModel->DetachFromScene();
        s_skyboxModel->Release();
        s_skyboxModel = nullptr;
    }

    s_skyboxLoaded[0] = 0;
}

// ------------------------------------------------------------------------------------------------
// Sun and moons (reference FUN_007eecc0 positions them, FUN_009ac660 draws each as one screen-
// aligned quad). Each body is a direction, not an orbit: the position is the camera plus a fixed
// radius of 12 along a direction built from two day-driven angle bands. Both angle tables and the
// size tables were recovered by disassembly; see docs/ref/parity-sky-bodies.md.
//
// Note the azimuth is CONSTANT for the sun and the first moon (45 degrees), so they rise and set in
// the same compass direction -- which agrees with the outdoor light direction, whose azimuth is 225
// degrees pointing away from the light. The second moon drifts in azimuth on a 1.7-day cycle.
// ------------------------------------------------------------------------------------------------

const float SKY_BODY_RADIUS = 12.0f;

struct SkyBodyKey { float time; float value; };

// Wrap-around linear interpolation over (time, value) pairs. The reference passes the key count
// in ESI and the table in EDI, clamps the parameter to [0, 1], and wraps the last key round to
// the first across the end of the day. Every band in the sky bodies, the glare and the sky
// highlight is read through it.
// ref: FUN_007ed3b0
float InterpBodyBand(const SkyBodyKey* keys, int32_t count, float t) {
    if (count <= 0) {
        return 0.0f;
    }

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    int32_t hi = 0;

    while (hi < count && keys[hi].time <= t) {
        hi++;
    }

    int32_t lo;

    if (hi == count) {
        hi = 0;
        lo = count - 1;
    } else if (hi == 0) {
        lo = count - 1;
    } else {
        lo = hi - 1;
    }

    float span = keys[hi].time - keys[lo].time;

    if (span < 0.0f) {
        span += 1.0f;
    }

    if (span < 0.001f) {
        return keys[lo].value;
    }

    float f = t - keys[lo].time;

    if (f < 0.0f) {
        f += 1.0f;
    }

    return keys[lo].value + (keys[hi].value - keys[lo].value) * (f / span);
}

const SkyBodyKey SUN_THETA[5] = {
    { 0.2291667f, 1.7453293f }, { 0.4965278f, 0.0872665f }, { 0.5000000f, 0.0872665f },
    { 0.5034722f, 0.0872665f }, { 0.8958333f, 1.7453293f },
};
const SkyBodyKey SUN_PHI[3] = { { 0.2291667f, 0.7853982f }, { 0.5f, 0.7853982f }, { 0.8958333f, 0.7853982f } };
const SkyBodyKey SUN_SIZE[4] = { { 0.25f, 2.0f }, { 0.28125f, 1.0f }, { 0.84375f, 1.0f }, { 0.875f, 2.0f } };

const SkyBodyKey MOON_THETA[5] = {
    { 0.0000000f, 0.6108652f }, { 0.0034722f, 0.6108652f }, { 0.1666667f, 1.7453293f },
    { 0.9166667f, 1.7453293f }, { 0.9965278f, 0.6108652f },
};
const SkyBodyKey MOON1_PHI[3] = { { 0.0f, 0.7853982f }, { 0.1666667f, 0.7853982f }, { 0.9166667f, 0.7853982f } };
const SkyBodyKey MOON2_PHI[3] = { { 0.0000000f, 2.3561945f }, { 0.1666667f, 2.6179938f }, { 0.9166667f, 2.8797934f } };
const SkyBodyKey MOON_SIZE[4] = { { 0.0416667f, 1.0f }, { 0.1666667f, 1.5f }, { 0.9166667f, 1.5f }, { 0.9993056f, 1.0f } };

HTEXTURE s_bodyTexture[3] = { nullptr, nullptr, nullptr };
HTEXTURE s_glareTexture[2] = { nullptr, nullptr }; // sun, moon 1 (moon 2 has no glare)
bool s_bodyTried = false;

// Glare visibility over the day, from the glare objects at 0x00d38ea8 (sun) and 0x00d38f58 (moon);
// base sizes 1.0 and 2.0 respectively.
const SkyBodyKey SUN_GLARE_VIS[4] = { { 0.2708333f, 0.0f }, { 0.3125f, 1.0f }, { 0.8125f, 1.0f }, { 0.875f, 0.0f } };
const SkyBodyKey MOON_GLARE_VIS[4] = { { 0.0833333f, 1.0f }, { 0.1354167f, 0.0f }, { 0.9479167f, 0.0f }, { 0.9993056f, 1.0f } };

void DrawSkyBody(const SkyBodyKey* theta, const SkyBodyKey* phi, int32_t phiCount,
                 const SkyBodyKey* sizeBand, float baseSize, float t, HTEXTURE texture,
                 const C3Vector& right, const C3Vector& up) {
    if (!texture) {
        return;
    }

    float th = InterpBodyBand(theta, 5, t);
    float ph = InterpBodyBand(phi, phiCount, t);
    float size = InterpBodyBand(sizeBand, 4, t) * baseSize;

    float st = sinf(th);
    C3Vector dir = { cosf(ph) * st, sinf(ph) * st, cosf(th) };

    // Below the eye-level plane the reference clips the quad away entirely; a body parked ten
    // degrees under the horizon is simply not drawn.
    if (dir.z <= 0.0f) {
        return;
    }

    // CAMERA-RELATIVE: the sky pass builds its matrix from the untranslated view (see SkyRender),
    // so everything it draws is centred on the origin, like the dome and the cloud sheet. Adding
    // the camera's world position here would have flung the body thousands of yards off screen.
    C3Vector c = { dir.x * SKY_BODY_RADIUS, dir.y * SKY_BODY_RADIUS, dir.z * SKY_BODY_RADIUS };

    float h = size * 0.5f;
    C3Vector pos[4] = {
        { c.x - right.x * h + up.x * h, c.y - right.y * h + up.y * h, c.z - right.z * h + up.z * h },
        { c.x + right.x * h + up.x * h, c.y + right.y * h + up.y * h, c.z + right.z * h + up.z * h },
        { c.x + right.x * h - up.x * h, c.y + right.y * h - up.y * h, c.z + right.z * h - up.z * h },
        { c.x - right.x * h - up.x * h, c.y - right.y * h - up.y * h, c.z - right.z * h - up.z * h },
    };
    C2Vector uv[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

    // Tint from LightIntBand band 9, as the reference does.
    const C3Vector& tint = CWorld::GetBodyTint();
    CImVector col[4];

    for (int32_t i = 0; i < 4; i++) {
        col[i].b = static_cast<uint8_t>((tint.z > 1.0f ? 1.0f : tint.z) * 255.0f);
        col[i].g = static_cast<uint8_t>((tint.y > 1.0f ? 1.0f : tint.y) * 255.0f);
        col[i].r = static_cast<uint8_t>((tint.x > 1.0f ? 1.0f : tint.x) * 255.0f);
        col[i].a = 0xFF;
    }

    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

    GxRsSet(GxRs_Texture0, TextureGetGxTex(texture, 0, nullptr));
    GxPrimLockVertexPtrs(4, pos, sizeof(C3Vector), nullptr, 0, col, sizeof(CImVector), nullptr, 0, uv, sizeof(C2Vector), nullptr, 0);
    GxDrawLockedElements(GxPrim_Triangles, 6, idx);
    GxPrimUnlockVertexPtrs();
}

// Glare halo around a body, drawn additively at the body's position and brightest when the body is
// near the centre of view.
//
// The visibility band and the base sizes are recovered constants. What is NOT recovered is the
// reference's size law for the quad -- FUN_007ef6e0 builds it and was not transcribed instruction
// by instruction -- so the base size is used directly, in the same world units as the disc sizes.
// If the halo reads too small or too large, that is the first constant to revisit.
void DrawGlare(const SkyBodyKey* theta, const SkyBodyKey* phi, int32_t phiCount,
               const SkyBodyKey* visBand, float baseSize, float t, HTEXTURE texture,
               const C3Vector& right, const C3Vector& up, const C3Vector& fwd) {
    if (!texture) {
        return;
    }

    float vis = InterpBodyBand(visBand, 4, t);

    if (vis <= 0.01f) {
        return;
    }

    float th = InterpBodyBand(theta, 5, t);
    float ph = InterpBodyBand(phi, phiCount, t);
    float st = sinf(th);
    C3Vector dir = { cosf(ph) * st, sinf(ph) * st, cosf(th) };

    if (dir.z <= 0.0f) {
        return;
    }

    // Brightest looking straight at it, gone once it is well off to the side
    float facing = dir.x * fwd.x + dir.y * fwd.y + dir.z * fwd.z;

    if (facing <= 0.0f) {
        return;
    }

    float intensity = vis * facing * facing;

    // CAMERA-RELATIVE: the sky pass builds its matrix from the untranslated view (see SkyRender),
    // so everything it draws is centred on the origin, like the dome and the cloud sheet. Adding
    // the camera's world position here would have flung the body thousands of yards off screen.
    C3Vector c = { dir.x * SKY_BODY_RADIUS, dir.y * SKY_BODY_RADIUS, dir.z * SKY_BODY_RADIUS };

    float h = baseSize * 0.5f;
    C3Vector pos[4] = {
        { c.x - right.x * h + up.x * h, c.y - right.y * h + up.y * h, c.z - right.z * h + up.z * h },
        { c.x + right.x * h + up.x * h, c.y + right.y * h + up.y * h, c.z + right.z * h + up.z * h },
        { c.x + right.x * h - up.x * h, c.y + right.y * h - up.y * h, c.z + right.z * h - up.z * h },
        { c.x - right.x * h - up.x * h, c.y - right.y * h - up.y * h, c.z - right.z * h - up.z * h },
    };
    C2Vector uv[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

    const C3Vector& tint = CWorld::GetBodyTint();
    CImVector col[4];

    for (int32_t i = 0; i < 4; i++) {
        float r = tint.x * intensity;
        float g = tint.y * intensity;
        float b = tint.z * intensity;
        col[i].b = static_cast<uint8_t>((b > 1.0f ? 1.0f : b) * 255.0f);
        col[i].g = static_cast<uint8_t>((g > 1.0f ? 1.0f : g) * 255.0f);
        col[i].r = static_cast<uint8_t>((r > 1.0f ? 1.0f : r) * 255.0f);
        col[i].a = 0xFF;
    }

    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

    GxRsSet(GxRs_Texture0, TextureGetGxTex(texture, 0, nullptr));
    GxPrimLockVertexPtrs(4, pos, sizeof(C3Vector), nullptr, 0, col, sizeof(CImVector), nullptr, 0, uv, sizeof(C2Vector), nullptr, 0);
    GxDrawLockedElements(GxPrim_Triangles, 6, idx);
    GxPrimUnlockVertexPtrs();
}

void SkyBodiesRender() {
    if (!s_bodyTried) {
        s_bodyTried = true;
        CStatus status;
        auto flags = CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1);
        s_bodyTexture[0] = TextureCreate("Textures\\sunCenter.blp", flags, &status, 0);
        s_bodyTexture[1] = TextureCreate("Textures\\moon.blp", flags, &status, 0);
        s_bodyTexture[2] = TextureCreate("Textures\\moon02.blp", flags, &status, 0);
        s_glareTexture[0] = TextureCreate("Textures\\sunGlare.blp", flags, &status, 0);
        s_glareTexture[1] = TextureCreate("Textures\\moonGlare.blp", flags, &status, 0);
    }

    // One-shot report: whether the sky bodies are even reachable, and whether their textures
    // loaded. A missing sun was reported from a real session, and "the pass runs but the texture is
    // null" looks identical on screen to "the pass never runs".
    static bool reported = false;

    if (!reported) {
        reported = true;
        fprintf(stderr, "SkyBodies: sun %s moon %s moon02 %s sunGlare %s moonGlare %s\n",
                s_bodyTexture[0] ? "ok" : "MISSING", s_bodyTexture[1] ? "ok" : "MISSING",
                s_bodyTexture[2] ? "ok" : "MISSING", s_glareTexture[0] ? "ok" : "MISSING",
                s_glareTexture[1] ? "ok" : "MISSING");
    }

    float t = CWorld::GetDayProgress();

    // Screen-aligned billboards
    const C3Vector& fwd = CWorld::GetCameraDir();
    C3Vector right = { fwd.y, -fwd.x, 0.0f };
    float rl = sqrtf(right.x * right.x + right.y * right.y);

    if (rl < 1e-4f) {
        right = { 1.0f, 0.0f, 0.0f };
    } else {
        right.x /= rl; right.y /= rl;
    }

    C3Vector up = { right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y - right.y * fwd.x };

    GxRsSet(GxRs_BlendingMode, GxBlend_Add);

    DrawSkyBody(SUN_THETA, SUN_PHI, 3, SUN_SIZE, 1.0f, t, s_bodyTexture[0], right, up);
    DrawSkyBody(MOON_THETA, MOON1_PHI, 3, MOON_SIZE, 1.75f, t, s_bodyTexture[1], right, up);

    // The second moon runs on its own 1.7-day cycle, so its phase is not the day fraction
    float day = t; // frozen has no absolute day counter yet; phase folds back to the day fraction
    float t2 = day / 1.7f;
    t2 -= static_cast<float>(static_cast<int32_t>(t2));

    DrawSkyBody(MOON_THETA, MOON2_PHI, 3, MOON_SIZE, 1.0f, t2, s_bodyTexture[2], right, up);

    // Glare over the discs; moon 2 has none.
    DrawGlare(SUN_THETA, SUN_PHI, 3, SUN_GLARE_VIS, 1.0f, t, s_glareTexture[0], right, up, fwd);
    DrawGlare(MOON_THETA, MOON1_PHI, 3, MOON_GLARE_VIS, 2.0f, t, s_glareTexture[1], right, up, fwd);
}

// ------------------------------------------------------------------------------------------------
// The sky highlight -- the dome's azimuthal colour variation, reference FUN_007f0530.
//
// The four rings between the zenith and 45 degrees are not painted flat. Their colour is pushed per
// segment by a profile band sampled at the segment's azimuth relative to the camera, so one half of
// the dome brightens and the opposite half darkens toward the zenith colour. It is the sunrise and
// sunset glow, and it is off for most of the day.
//
// Every number below was read out of WoW.exe, not inferred:
//   * strength = StrengthBand(dayFraction) * LightParams.highlightSky. The band is at 0x00af4b7c
//     and peaks only around 06:30 and 21:30; the scale is DNInfo+0x128, which FUN_007ebff0 fills
//     with `fildl 0x4(%edi)` at 0x007ec1cd -- LightParams column 1, highlightSky, 0 or 1. A zone
//     whose row carries 0 therefore gets no highlight at all, which is most of them.
//   * the segment parameter is wrap01(yaw / (2pi) + 0.25 + seg * (-1 / segCount)). The two
//     constants are 0.159155 at 0x00a41ca8 and 0.25 at 0x00a41b00, and the -1 is at 0x009e2ef4.
//     yaw is atan2(forward.y, forward.x) normalised to [0, 2pi), computed at 0x007f3920 from the
//     camera forward vector the DayNight block keeps at +0x30.
//   * profile = ProfileBand(that parameter), the band at 0x00af4bac. It is positive across one
//     half of the dome and negative across the other.
//
// The two branches, taken verbatim from 0x007f06b1 and 0x007f070b (the sign of the profile picks
// between them, with zero going to the first):
//
//     local = lerp(ringColor, topRingColor, strength)
//     profile >= 0:  out = lerp(ringColor, local, (profile - 1) * strength)
//     profile <  0:  out = lerp(local, lerp(local, zenithColor, strength * 0.7), -profile * strength)
//
// One deliberate divergence. The reference lerps 0-255 bytes and casts the result back to a byte
// with no clamp (FUN_007ed2d0), so an out-of-range channel would wrap; frozen keeps floats and
// clamps to [0, 255]. Both branches can leave the 0..1 range because their factors extrapolate,
// and a wrapped channel would be a garish artefact rather than a faithful colour.
const SkyBodyKey SKY_HIGHLIGHT_STRENGTH[6] = {
    { 0.125000f, 0.0f }, { 0.270833f, 1.0f }, { 0.291667f, 0.0f },
    { 0.854167f, 0.0f }, { 0.895833f, 1.0f }, { 0.999306f, 0.0f },
};

const SkyBodyKey SKY_HIGHLIGHT_PROFILE[6] = {
    { 0.125f, 1.0f }, { 0.375f, 0.0f }, { 0.500f, -0.5f },
    { 0.625f, -0.7f }, { 0.750f, -0.5f }, { 0.875f, 0.0f },
};

// Per-channel linear interpolation, matching FUN_007ed2d0 apart from the clamp noted above.
C3Vector SkyLerp(const C3Vector& a, const C3Vector& b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

void SkyRender() {
    // The reference skips the whole sky pass while the camera is under liquid; the clear colour
    // (the underwater fog) is the backdrop instead.
    if (s_cameraLiquidKind >= 0) {
        return;
    }

    EnsureShaders();

    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    if (!s_skyBuilt) {
        BuildSkyDome();
    }

    if (!s_skyWhite) {
        for (int32_t i = 0; i < SKY_WHITE_DIM * SKY_WHITE_DIM; i++) {
            s_skyWhitePixels[i] = CImVector { 0xFF, 0xFF, 0xFF, 0xFF };
        }

        // Last argument 0: a non-zero value replaces the filter above with the global mipmapped
        // one, which makes the device ask this callback for mip levels it cannot supply.
        s_skyWhite = TextureCreate(SKY_WHITE_DIM, SKY_WHITE_DIM, GxTex_Argb8888, GxTex_Argb8888, CGxTexFlags(GxTex_Linear, 1, 1, 0, 0, 0, 1), s_skyWhitePixels, SkyWhiteCallback, __FILE__, 0);
    }

    // One band per ring, straight across -- no gradient maths. Ring i takes sky band i, and the
    // bottom two rings both take band 7, which is the fog colour. That flat assignment IS the
    // reference's shading (FUN_007f0530 writes 1 zenith colour, then 4 rings of 24 from successive
    // bands, then 24 + 1 of the fog band).
    //
    // On top of that the four middle rings carry the sky highlight, which varies their colour per
    // segment with the camera azimuth; see the block above SkyRender.
    C3Vector ringColor[SKY_RINGS + 1];

    for (int32_t ring = 0; ring <= SKY_RINGS; ring++) {
        ringColor[ring] = CWorld::GetSkyColor(ring < 5 ? ring : 5);
    }

    float highlight = InterpBodyBand(SKY_HIGHLIGHT_STRENGTH, 6, CWorld::GetDayProgress())
        * CWorld::GetSkyHighlight();

    // The reference rebuilds this per ring; the yaw cannot change inside a frame, so it is hoisted.
    const C3Vector& camDir = CWorld::GetCameraDir();
    float yaw = atan2f(camDir.y, camDir.x);

    if (yaw < 0.0f) {
        yaw += 6.2831855f;
    }

    float segParam0 = yaw * 0.159155f + 0.25f;

    if (segParam0 > 1.0f) {
        segParam0 -= 1.0f;
    }

    const float segStep = -1.0f / SKY_SEGS;

    int32_t v = 0;

    for (int32_t ring = 0; ring <= SKY_RINGS; ring++) {
        const C3Vector& base = ringColor[ring];

        // Rings 1..4 are the reference's four highlighted rings: the zenith vertex above them and
        // the two fog-band rings below are flat there too.
        bool highlighted = highlight > 0.0f && ring >= 1 && ring <= 4;
        C3Vector local = highlighted ? SkyLerp(base, ringColor[1], highlight) : base;
        float p = segParam0;

        for (int32_t seg = 0; seg <= SKY_SEGS; seg++) {
            C3Vector c = base;

            if (highlighted) {
                if (p < 0.0f) {
                    p += 1.0f;
                }

                float profile = InterpBodyBand(SKY_HIGHLIGHT_PROFILE, 6, p);

                if (profile >= 0.0f) {
                    c = SkyLerp(base, local, (profile - 1.0f) * highlight);
                } else {
                    C3Vector toward = SkyLerp(local, ringColor[0], highlight * 0.7f);
                    c = SkyLerp(local, toward, -profile * highlight);
                }

                p += segStep;
            }

            float r = c.x * 255.0f;
            float g = c.y * 255.0f;
            float b = c.z * 255.0f;
            r = r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r);
            g = g < 0.0f ? 0.0f : (g > 255.0f ? 255.0f : g);
            b = b < 0.0f ? 0.0f : (b > 255.0f ? 255.0f : b);

            s_skyCol[v].r = static_cast<uint8_t>(r);
            s_skyCol[v].g = static_cast<uint8_t>(g);
            s_skyCol[v].b = static_cast<uint8_t>(b);
            s_skyCol[v].a = 0xFF;
            v++;
        }
    }

    // The dome is centred on the camera: use the eye-at-origin view (no camera translation)
    C44Matrix view;
    GxXformView(view);
    C44Matrix proj;
    GxXformProjNative(proj);
    C44Matrix viewProj = view * proj;
    C44Matrix viewProjT = viewProj.Transpose();

    // The reference (FUN_007f09b0) draws the sky AFTER the opaque world, through a viewport whose
    // depth range is squeezed to [0.999, 1.0] (DAT_00adeef0/DAT_00adeef4). Every sky pixel then
    // lands at the far end of the depth buffer and the ordinary less-equal test lets it through
    // only where the cleared depth (1.0) is still untouched, so the sky can never draw over
    // terrain, buildings or models, and the skybox model needs no depth clear afterwards.
    float vpMinX, vpMaxX, vpMinY, vpMaxY, vpMinZ, vpMaxZ;
    GxXformViewport(vpMinX, vpMaxX, vpMinY, vpMaxY, vpMinZ, vpMaxZ);
    GxXformSetViewport(vpMinX, vpMaxX, vpMinY, vpMaxY, SKY_VIEWPORT_MIN_Z, SKY_VIEWPORT_MAX_Z);

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 0); // less-equal
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_VertexShader, s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, s_uiPixelShader);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<float*>(&viewProjT), 4);

    // The bodies go down first and the dome is ADDED over them, which is how the reference layers
    // the sky (the dome is GxBlend_Add there). That only works because the scene clears to black
    // under an open sky -- see CGWorldFrame::OnWorldRender -- so the dome's colour IS the sky.
    SkyBodiesRender();

    GxRsSet(GxRs_BlendingMode, GxBlend_Add);
    GxRsSet(GxRs_Texture0, s_skyWhite ? TextureGetGxTex(s_skyWhite, 0, nullptr) : nullptr);

    // A zone whose light names a skybox (LightParams -> LightSkybox) draws that sky model as the
    // real sky -- e.g. the Death Knight start's IceCrownSky. The reference (FUN_007f09b0) skips the
    // stars, the dome layers and the clouds entirely while such a skybox is up at full alpha, so
    // the gradient dome is only drawn for zones without one.
    const char* skyboxPath = CWorld::GetSkyboxPath();

    // Only suppress the gradient once the zone's skybox model is actually up. A zone can name a
    // skybox that fails to load, or is still streaming on the first frames after a map change, and
    // skipping the dome on the strength of the name alone left the sky black.
    // ...and only once it is actually SUBMITTING GEOMETRY. Checking m_m2DataLoaded alone was too
    // weak: on map 609 the model loads, the dome switches itself off on that evidence, and the
    // skybox then contributes nothing -- measured as array54[M2PASS_0].Count() == 0 every frame --
    // leaving the sky black with the sun, moon and clouds hidden behind nothing at all.
    //
    // The element count is from the previous frame (this runs before the skybox is animated below),
    // which is exactly what is wanted: the dome keeps drawing until the skybox has demonstrably
    // taken over, and resumes the moment it stops.
    bool skyboxDrawing = s_skyboxScene
        && (s_skyboxScene->array54[M2PASS_0].Count() > 0 || s_skyboxScene->array44.Count() > 0);

    bool skyboxUp = skyboxPath && s_skyboxModel && s_skyboxModel->m_shared
        && s_skyboxModel->m_shared->m_m2DataLoaded && skyboxDrawing;

    if (!skyboxUp) {
        GxPrimLockVertexPtrs(
            SKY_VERTS,
            s_skyPos, sizeof(C3Vector),
            nullptr, 0,
            s_skyCol, sizeof(CImVector),
            nullptr, 0,
            s_skyUv, sizeof(C2Vector),
            nullptr, 0
        );
        GxDrawLockedElements(GxPrim_Triangles, s_skyIdxCount, s_skyIdx);
        GxPrimUnlockVertexPtrs();
    }

    GxRsPop();

    // Clouds over the gradient, under the stars and the skybox model.
    {
        uint32_t nowMs = CWorld::GetM2Scene() ? CWorld::GetM2Scene()->m_time : 0;
        static uint32_t s_cloudLast = 0;
        float dt = s_cloudLast ? static_cast<float>(nowMs - s_cloudLast) * 0.001f : 0.0f;
        s_cloudLast = nowMs;

        if (dt > 0.25f) {
            dt = 0.25f;
        }

        CloudsUpdate(dt);
        CloudsRender(viewProjT, s_cameraPos, s_uiVertexShader[0], s_uiPixelShader);
    }

    // Stars (reference FUN_009abd50: Environments\Stars\stars.mdl in its own scene, passes 0/1).
    // The model's animation spans the whole day, so it is seeked to the time of day like the
    // skybox; its materials fade the stars in only at night. Drawn through the same far-depth
    // viewport, over the gradient. Suppressed only once the zone's own skybox model is really up,
    // for the same reason as the dome above.
    if (!skyboxUp) {

        if (!s_starsTried) {
            s_starsTried = true;
            s_starsScene = M2CreateScene();

            if (s_starsScene) {
                s_starsModel = s_starsScene->CreateModel("Environments\\Stars\\stars.mdl", 0);

                if (s_starsModel) {
                    s_starsModel->SetLightingCallback(&SkyboxLightingCallback, nullptr);
                    s_starsModel->SetBoneSequence(-1, 0, -1, 0, 1.0f, 0, 1);
                    s_starsModel->SetAnimating(1);
                    s_starsModel->SetVisible(1);
                    s_starsModel->m_flag10000 = 1;
                }
            }
        }

        if (s_starsScene && s_starsModel) {
            s_starsModel->SetWorldTransform(s_cameraPos, 0.0f, 1.0f);

            uint32_t animDur = 0;

            if (s_starsModel->m_shared && s_starsModel->m_shared->m_m2DataLoaded && s_starsModel->m_shared->m_data->sequences.Count() > 0) {
                animDur = s_starsModel->m_shared->m_data->sequences[0].duration;
            }

            if (animDur > 0) {
                uint32_t target = static_cast<uint32_t>(CWorld::GetDayProgress() * animDur);
                uint32_t cur = s_starsScene->m_time % animDur;
                uint32_t delta = (target >= cur) ? (target - cur) : (target + animDur - cur);
                s_starsScene->AdvanceTime(delta);
            }

            s_starsScene->Animate(s_cameraPos);
            s_starsScene->Draw(M2PASS_0);
            s_starsScene->Draw(M2PASS_1);
        }
    }

    if (skyboxPath) {

        if (!s_skyboxScene) {
            s_skyboxScene = M2CreateScene();
        }

        if (s_skyboxScene && SStrCmpI(skyboxPath, s_skyboxLoaded, 0x7FFFFFFF) != 0) {
            if (s_skyboxModel) {
                s_skyboxModel->DetachFromScene();
                s_skyboxModel->Release();
                s_skyboxModel = nullptr;
            }

            s_skyboxModel = s_skyboxScene->CreateModel(skyboxPath, 0);

            if (s_skyboxModel) {
                s_skyboxModel->SetLightingCallback(&SkyboxLightingCallback, nullptr);
                s_skyboxModel->SetBoneSequence(-1, 0, -1, 0, 1.0f, 0, 1);
                s_skyboxModel->SetAnimating(1);
                s_skyboxModel->SetVisible(1);
                s_skyboxModel->m_flag10000 = 1;
            }

            SStrCopy(s_skyboxLoaded, skyboxPath, sizeof(s_skyboxLoaded));
        }

        if (s_skyboxModel) {
            // Centre it on the camera so it sits at infinity; the scene's Animate re-centres to the eye.
            // EVERY frame, not just at creation. CM2Scene::Animate clears m_flag8 and unlinks the
            // model from m_animateList as it walks them, and AnimateMT enqueues for drawing only
            // `if (m_flag8)`. A one-shot SetVisible/SetAnimating therefore draws for exactly one
            // frame -- the skybox loaded, resolved all its textures, reported drawable, and still
            // submitted zero elements every frame after the first.
            s_skyboxModel->SetVisible(1);
            s_skyboxModel->SetAnimating(1);

            s_skyboxModel->SetWorldTransform(s_cameraPos, 0.0f, 1.0f);

            // Advance the skybox in REAL TIME.
            //
            // Seeking the scene clock to the time of day starves GLOBAL SEQUENCES: a track with a
            // loopIndex other than 0xFFFF is timed by m_loops[i] = (m_scene->m_time - uint74) %
            // loopLength -- free-running loops that make IceCrownSky swirl and flicker. Pinning the
            // clock left the delta near zero each frame, so the sky was static.
            uint32_t nowMs = CWorld::GetM2Scene() ? CWorld::GetM2Scene()->m_time : 0;
            static uint32_t s_skyLastMs = 0;
            uint32_t skyDelta = s_skyLastMs && nowMs > s_skyLastMs ? nowMs - s_skyLastMs : 0;
            s_skyLastMs = nowMs;

            if (skyDelta > 250) {
                skyDelta = 250;
            }

            s_skyboxScene->AdvanceTime(skyDelta);

            // Drawn through the same far-depth viewport as the dome: the model's own depth writes
            // land at ~1.0, behind everything the world has already drawn.
            s_skyboxScene->Animate(s_cameraPos);

            // The gradient dome stops drawing once this model is up, so if it submits nothing the
            // sky is simply black. Count what each pass actually draws.
            s_skyboxScene->Draw(M2PASS_0);
            s_skyboxScene->Draw(M2PASS_1);
        }
    }

    GxXformSetViewport(vpMinX, vpMaxX, vpMinY, vpMaxY, vpMinZ, vpMaxZ);
}

// ---------------------------------------------------------------------------------------------
// The module's own surface.

HTEXTURE SkyWhiteTexture() {
    return s_skyWhite;
}

void SkySetCameraState(const C3Vector& cameraPos, int32_t cameraLiquidKind) {
    s_cameraPos = cameraPos;
    s_cameraLiquidKind = cameraLiquidKind;
}
