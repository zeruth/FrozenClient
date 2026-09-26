#include "world/Shadow.hpp"
#include <tempest/Vector.hpp>
#include <tempest/Matrix.hpp>
#include "world/CWorld.hpp"
#include "console/CVar.hpp"
#include "console/Console.hpp"
#include "gx/Texture.hpp"
#include "gx/texture/CGxTex.hpp"
#include "model/CM2Model.hpp"
#include <storm/String.hpp>
#include <tempest/Box.hpp>
#include <cmath>
#include <cstdio>

// ------------------------------------------------------------------------------------------------
// The blob shadow. See Shadow.hpp for what this module is and what of it is ported.
// ------------------------------------------------------------------------------------------------

namespace {

// DAT_00d38044, DAT_00d38040, DAT_00d3803c: the blob and the two generated fade ramps.
HTEXTURE s_blobTexture = nullptr;
HTEXTURE s_addTexture = nullptr;
HTEXTURE s_modTexture = nullptr;

// DAT_00d3804c and DAT_00d38048.
CVar* s_shadowLODVar = nullptr;
CVar* s_extShadowQualityVar = nullptr;

// The ramps are 64x8 and generated, not loaded.
const uint32_t RAMP_WIDTH = 64;
const uint32_t RAMP_HEIGHT = 8;

// DAT_00af3e24, with DAT_00af3e20 holding the pixel count the size query reported. One buffer
// serves both ramps because only one of them is ever being latched at a time.
uint32_t s_rampPixels[RAMP_WIDTH * RAMP_HEIGHT];
uint32_t s_rampCount = 0;

// DAT_009e1134: a decal whose box is thinner than a millimetre in x or y gets no transforms at all.
const float DECAL_MIN_EXTENT = 0.001f;

// DAT_009f1ff4, the quarter turn the footprint square is built with. The reference builds it as a
// rotation about (0, 0, 1) through an arbitrary-axis constructor; on an identity matrix that is
// exactly a rotation around z.
const float DECAL_TURN = -1.5707963705062866f;

// The trapezoid both ramps are built from. Along the row, t runs 0 .. RAMP_FULL; it fades up over
// the first RAMP_RISE, holds at 1 until RAMP_FALL and fades back down. The three constants were
// read out of the image on 2026-09-26: DAT_00a4040c is 2.0, DAT_009e30cc is 10.0 and DAT_00a1047c
// is 12.0, so with 64 texels the soft ends are about ten texels each.
const float RAMP_RISE = 2.0f;
const float RAMP_FALL = 10.0f;
const float RAMP_FULL = 12.0f;

float RampValue(uint32_t x, uint32_t width) {
    float t = (static_cast<float>(x) / static_cast<float>(width - 1)) * RAMP_FULL;

    if (t < RAMP_RISE) {
        return t * 0.5f;
    }

    if (t < RAMP_FALL) {
        return 1.0f;
    }

    float falling = (RAMP_FULL - t) * 0.5f;

    return falling > 0.0f ? falling : 0.0f;
}

// ref: FUN_007e36e0
// "ShadowAdd": white with the trapezoid in the alpha channel, for the additive stage.
void ShadowAddCallback(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Lock) {
        s_rampCount = width * height;
        return;
    }

    if (cmd != GxTex_Latch || mipLevel != 0) {
        return;
    }

    stride = width * 4;
    texels = s_rampPixels;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t alpha = static_cast<uint32_t>(lroundf(RampValue(x, width) * 255.0f));

            s_rampPixels[y * width + x] = (alpha << 24) | 0x00ffffff;
        }
    }
}

// ref: FUN_007e3820
// "ShadowMod": the INVERSE of the trapezoid as a grey, for the modulating stage. Its alpha is
// opaque except where the grey has gone fully white, which the reference writes as a compare
// against 0xff rather than a second ramp.
void ShadowModCallback(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Lock) {
        s_rampCount = width * height;
        return;
    }

    if (cmd != GxTex_Latch || mipLevel != 0) {
        return;
    }

    stride = width * 4;
    texels = s_rampPixels;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t grey = static_cast<uint32_t>(lroundf((1.0f - RampValue(x, width)) * 255.0f)) & 0xff;
            uint32_t alpha = grey == 0xff ? 0x00 : 0xff;

            s_rampPixels[y * width + x] = (alpha << 24) | (grey << 16) | (grey << 8) | grey;
        }
    }
}

// The ramps' update rect, {minY, minX, maxY, maxX}, which is the whole texture.
CiRect RampRect() {
    CiRect rect;
    rect.minY = 0;
    rect.minX = 0;
    rect.maxY = static_cast<int32_t>(RAMP_HEIGHT);
    rect.maxX = static_cast<int32_t>(RAMP_WIDTH);

    return rect;
}

// ref: FUN_007e3a20
bool ShadowLODCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    uint32_t lod = 0;

    if (sscanf(value, "%d", &lod) != 1) {
        lod = 0;
    }

    if (lod < 2) {
        ShadowSetLOD(static_cast<int32_t>(lod));

        char message[256];
        snprintf(message, sizeof(message), "Shadow LOD set to %d", g_shadowLOD);
        ConsoleWrite(message, DEFAULT_COLOR);

        return true;
    }

    ConsoleWrite("Shadow LOD must be in the range (0, 1)", DEFAULT_COLOR);

    return false;
}

} // namespace

int32_t g_shadowLOD = 0;

HTEXTURE ShadowBlobTexture() {
    return s_blobTexture;
}

// ref: FUN_007e2c40
CGxTex* ShadowModGxTex() {
    return TextureGetGxTex(s_modTexture, 1, nullptr);
}

// ref: FUN_007e2c60
CGxTex* ShadowAddGxTex() {
    return TextureGetGxTex(s_addTexture, 1, nullptr);
}

// ref: FUN_007e4a40
// Called from the client's init right after CWorld::Initialize, where the reference calls it.
void ShadowInit() {
    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1);

    s_blobTexture = TextureCreate("Textures\\ShadowBlob.blp", flags, nullptr, 1);

    s_addTexture = TextureCreate(
        RAMP_WIDTH, RAMP_HEIGHT, GxTex_Argb8888, GxTex_Argb8888,
        CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), nullptr, ShadowAddCallback, "ShadowAdd", 0);

    s_modTexture = TextureCreate(
        RAMP_WIDTH, RAMP_HEIGHT, GxTex_Argb8888, GxTex_Argb8888,
        CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), nullptr, ShadowModCallback, "ShadowMod", 0);

    // The LOD CVar owns the ramps: its callback is what generates them, and its default of "1"
    // means blobs are on unless something turns them off.
    s_shadowLODVar = CVar::Register(
        "shadowLOD", "Unit shadow LOD", 0x1, "1", &ShadowLODCallback, GRAPHICS, false, nullptr, false);

    s_extShadowQualityVar = CVar::Lookup("extShadowQuality");
}

// ref: FUN_007e2c80
void ShadowDestroy() {
    if (s_blobTexture) {
        HandleClose(s_blobTexture);
    }

    if (s_modTexture) {
        HandleClose(s_modTexture);
    }

    if (s_addTexture) {
        HandleClose(s_addTexture);
    }

    s_blobTexture = nullptr;
    s_modTexture = nullptr;
    s_addTexture = nullptr;
}

// ref: FUN_007e3980
// Turning blobs on is what generates the ramps: the reference re-installs each generator on its
// texture and then latches both, rather than relying on the create-time callback firing.
//
// DIVERGED: the re-install (FUN_004b5430) has no counterpart here, because frozen's TextureCreate
// keeps the callback it was given and nothing ever replaces it. The latch itself is reproduced.
void ShadowSetLOD(int32_t lod) {
    g_shadowLOD = lod;

    if (lod != 1) {
        return;
    }

    CiRect rect = RampRect();

    auto add = TextureGetGxTex(s_addTexture, 1, nullptr);

    if (add) {
        GxTexUpdate(add, rect, 0);
    }

    auto mod = TextureGetGxTex(s_modTexture, 1, nullptr);

    if (mod) {
        GxTexUpdate(mod, rect, 0);
    }
}

// ref: FUN_007e2d60
// The two TEXTURE transforms a projected decal draws through, and the reason the decal never has to
// reproduce anything about the receiver: the receiver's own vertices are drawn as they are, and
// these two matrices turn their positions into the decal's texture coordinates. FUN_007e4370 pushes
// them as GxXform_Tex0 and GxXform_Tex1, so stage 0 gets the blob and stage 1 gets a fade ramp.
//
// It was linked to Liquid::IMaterial::Draw by the matcher and it is nothing of the kind -- it sits
// inside the blob shadow's own module. Three reference decal kinds call it.
//
//   stage 0  = Translate(offset) * diag(1/width, 1/height, 1, 1) * RotateZ(-pi/2)
//              then *= `extra` if one is given, then +0.5 on the translation's x and y
//   stage 1  = Translate(offset) * a matrix whose only non-zero terms are
//              c0 = c2 = 1/depth, d0 = d2 = `bias`, d1 = d3 = 1
//
// stage 0 is the footprint: the box's own width and height become the 0..1 texture square, turned a
// quarter turn, and the +0.5 puts the box centre at the middle of the texture. stage 1 is the fade
// ALONG the projection axis -- it maps the box's depth into u, leaves v at 1, and so reads one row
// of the 64x8 ShadowAdd / ShadowMod ramp. That is what those two textures are for.
//
// `absolute` picks the space: zero makes the offset camera-relative (camera - centre), which is what
// the decal pass wants because FUN_007e4370 draws with world and view set to identity; non-zero just
// negates the centre.
//
// Does nothing at all when the box is thinner than a millimetre in x or y (DAT_009e1134 is 0.001).
void DecalBuildTransforms(C44Matrix& stage0, C44Matrix& stage1, const CAaBox& box, const C44Matrix* extra, float bias, int32_t absolute) {
    const C3Vector& cameraPos = CWorld::GetCameraPos();

    C3Vector centre = {
        (box.t.x + box.b.x) * 0.5f,
        (box.t.y + box.b.y) * 0.5f,
        (box.t.z + box.b.z) * 0.5f
    };

    C3Vector offset;

    if (absolute == 0) {
        offset.x = cameraPos.x - centre.x;
        offset.y = cameraPos.y - centre.y;
        offset.z = cameraPos.z - centre.z;
    } else {
        offset.x = -centre.x;
        offset.y = -centre.y;
        offset.z = -centre.z;
    }

    C44Matrix translate;
    translate.Identity();
    translate.Translate(offset);

    float width = box.t.x - box.b.x;
    float height = box.t.y - box.b.y;
    float depth = box.t.z - box.b.z;

    if (width < DECAL_MIN_EXTENT || height < DECAL_MIN_EXTENT) {
        return;
    }

    // The footprint square, a quarter turn, and the half-texel centring.
    C44Matrix scale;
    scale.Identity();
    scale.a0 = 1.0f / width;
    scale.b1 = 1.0f / height;

    stage0 = translate * scale * C44Matrix::RotationAroundZ(DECAL_TURN);

    if (extra) {
        stage0 *= *extra;
    }

    stage0.d0 += 0.5f;
    stage0.d1 += 0.5f;

    // The ramp lookup along the projection axis. Every term the reference does not write is zero,
    // including the whole first two rows, so this is built from scratch rather than from identity.
    C44Matrix ramp;
    ramp.a0 = 0.0f; ramp.a1 = 0.0f; ramp.a2 = 0.0f; ramp.a3 = 0.0f;
    ramp.b0 = 0.0f; ramp.b1 = 0.0f; ramp.b2 = 0.0f; ramp.b3 = 0.0f;
    ramp.c0 = 1.0f / depth; ramp.c1 = 0.0f; ramp.c2 = 1.0f / depth; ramp.c3 = 0.0f;
    ramp.d0 = bias; ramp.d1 = 1.0f; ramp.d2 = bias; ramp.d3 = 1.0f;

    stage1 = translate * ramp;
}

// ref: FUN_007e49e0
// The per-caster gate, and the five conditions are all the reference's:
//
//   the model exists and is drawable,
//   its per-frame flag 0x4000 is clear,
//   its bounding box is not degenerate,
//   the extShadowQuality CVar reads BELOW 1, and
//   the shadow LOD is exactly 1.
//
// The fourth one is worth stating plainly because it is not obvious and it decides what a player
// sees: BLOB SHADOWS AND THE SHADOW MAP ARE MUTUALLY EXCLUSIVE. Raising extShadowQuality turns
// every blob off, because the map shadow is then drawing the same shadows properly. Frozen's
// shadow quality gate landed in the same cycle as this, so the two now agree.
void ShadowDrawBlob(CM2Model* model) {
    if (!model || !model->IsDrawable(0, 0)) {
        return;
    }

    // m_flag4000, NOT a bit in m_flags: the reference reads the +0x10 state word, and the two
    // storages are not interchangeable (see the note on CM2Model::m_flags).
    if (model->m_flag4000) {
        return;
    }

    CAaBox bounds;
    model->GetBoundingBox(bounds);

    if (AaBoxIsDegenerate(bounds)) {
        return;
    }

    if (s_extShadowQualityVar && s_extShadowQualityVar->GetInt() >= 1) {
        return;
    }

    if (g_shadowLOD != 1) {
        return;
    }

    // TODO FUN_007e4480, 1370 bytes: the projector. It turns the caster's box into a texture
    // projection matrix (seeding the accumulator at +-5, scaling the box by the model matrix's
    // first-row length, and pushing the near and far planes out by 5/3 and 1 of the half-height),
    // sets the decal state -- blend 4, no lighting, no fog, no depth write, the blob on stage 0,
    // ColorOp0 5, AlphaOp0 3, and DepthFunc EQUAL when its third argument is zero -- takes the
    // strength from the model's own field at +0x178 clamped to [0, 1], and hands off to
    // FUN_007e4370. The strength at the call site is the constant 0.4 at 0x009f98d8.
    //
    // FUN_007e4370, 268 bytes, then sets world and view to IDENTITY and calls FUN_007e3e80 (the
    // fixed-function receiver walk) or FUN_007e3aa0 (the same walk through CShaderEffect), which
    // between them gather the receiver's triangles through FUN_007e2fd0, FUN_007e32f0,
    // FUN_007e3580 and FUN_007e35f0 and draw them with the blob on one stage and one of these two
    // ramps on the next.
    //
    // That chain is 6.8 KB and none of it is ported. frozen's own blob pass was deleted in
    // f4e53209 and cf7768ff because it re-drew receiver triangles and selected with a depth-EQUAL
    // test, which stopped working the moment the receiver's base pass moved to CMapRenderChunk's
    // own vertex program; re-porting it means doing it this way instead.
}
