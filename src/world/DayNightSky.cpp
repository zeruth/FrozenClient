#include "world/DayNightLight.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Transform.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "model/Model2.hpp"
#include "client/Client.hpp"
#include "util/CStatus.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldParam.hpp"
#include "world/CWorldScene.hpp"
#include "world/CloudNoiseTable.hpp"
#include "ui/game/CGCamera.hpp"
#include "gx/Texture.hpp"
#include "console/CVar.hpp"
#include <common/Time.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Rect.hpp>
#include <cmath>
#include <cstring>

// The reference's DayNight module, sky half: the stars, the bodies, the dome, the clouds, the
// glares and the skybox models, drawn by CMap::Render into the world window (FUN_007f09b0) and by
// the world frame's glare pass (FUN_007f0870). Everything here draws through the fixed-function
// pipeline with no shader bound, as the reference does.

namespace {

// The depth range the sky draws in (DAT_00adeef0 / DAT_00adeef4).
const float SKY_MIN_Z = 0.9990234375f;
const float SKY_MAX_Z = 1.0f;

// The dome's ring angles over pi (0x00a41a90): the zenith, five rings down to 45 degrees, and the
// nadir.
const float s_domeRings[7] = { 0.0f, 0.17f, 0.2f, 0.23f, 0.24f, 0.25f, 1.0f };

// FUN_004c1cf0
bool FloatNear(float a, float b) {
    return fabsf(a - b) <= 9.5367431640625e-07f;
}

// The client's polynomial cosine of x * pi.
float CosPi(float x) {
    float fraction;
    int32_t whole;
    CameraSplitFloor(x, &fraction, &whole);

    float v = 1.0f - (6.0f - 4.0f * fraction) * fraction * fraction;

    return (whole & 1) ? -v : v;
}

// The sun, the moon and the second moon's quads (FUN_007edbe0's static tables at 0x00d39078 and
// 0x00d39048): a unit square in the plane facing the camera, with two spare vertices the horizon
// clip fills in.
const C3Vector s_bodyQuad[6] = {
    { 0.0f, -0.5f, 0.5f }, { 0.0f, 0.5f, 0.5f }, { 0.0f, -0.5f, -0.5f },
    { 0.0f, 0.5f, -0.5f }, { 0.0f, -0.5f, 99.0f }, { 0.0f, 0.5f, 99.0f }
};
const C2Vector s_bodyQuadUv[6] = {
    { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f },
    { 1.0f, 1.0f }, { 0.0f, 99.0f }, { 1.0f, 99.0f }
};
const uint16_t s_bodyIndices4[4] = { 0, 1, 2, 3 };          // 0x00af4dac
const uint16_t s_bodyIndices6[6] = { 0, 1, 4, 5, 2, 3 };    // 0x00af4db4

// The glare's quad (0x00af4cb0, 0x00af4ce0, 0x00af4b70).
const C3Vector s_glareQuad[4] = { { 0.0f, -0.5f, 0.5f }, { 0.0f, 0.5f, 0.5f }, { 0.0f, -0.5f, -0.5f }, { 0.0f, 0.5f, -0.5f } };
const C2Vector s_glareUv[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } };
const uint16_t s_glareIndices[4] = { 0, 2, 1, 3 };

}

// ref: FUN_009abb60
// A frame whose first axis is `forward`, the second level beside it, and the third their cross.
static void FacingBasis(C44Matrix& m, const C3Vector& forward) {
    float inv = 1.0f / sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    m.a0 = forward.x * inv;
    m.a1 = forward.y * inv;
    m.a2 = forward.z * inv;

    m.b0 = -(forward.y * inv);
    m.b1 = m.a0;
    m.b2 = 0.0f;

    if (fabsf(m.b0 * m.a0) <= 9.999999747378752e-06f) {
        m.b0 = 0.0f;
        m.b1 = 1.0f;
        m.b2 = 0.0f;
    } else {
        float s = 1.0f / sqrtf(m.b1 * m.b1 + m.b0 * m.b0);
        m.b0 = m.b0 * s;
        m.b1 = s * m.b1;
    }

    m.c0 = m.b2 * m.a1 - m.a2 * m.b1;
    m.c1 = m.a2 * m.b0 - m.b2 * m.a0;
    m.c2 = m.a0 * m.b1 - m.b0 * m.a1;
}

// The view that puts a camera-facing quad at `pos`: the facing frame, moved out to `pos` relative to
// the camera, in front of the current view. Returns the view it replaced.
static C44Matrix PushFacingView(const C3Vector& pos) {
    C44Matrix view;
    GxXformView(view);

    C44Matrix basis;
    FacingBasis(basis, { view.a2, view.b2, view.c2 });

    const C3Vector& camera = DayNightGetBlock()->cameraPos;
    basis.d0 = pos.x - camera.x;
    basis.d1 = pos.y - camera.y;
    basis.d2 = pos.z - camera.z;

    GxXformPush(GxXform_World);
    GxXformSet(GxXform_World, C44Matrix());
    GxXformSetView(basis * view);

    return view;
}

static void PopFacingView(const C44Matrix& view) {
    GxXformPop(GxXform_World);
    GxXformSetView(view);
}

static void DrawStrip(uint32_t indexCount, uint32_t vertexCount) {
    CGxBatch batch;
    batch.m_primType = GxPrim_TriangleStrip;
    batch.m_start = 0;
    batch.m_count = indexCount;
    batch.m_minIndex = 0;
    batch.m_maxIndex = static_cast<uint16_t>(vertexCount - 1);
    GxDraw(&batch, 1);
}

// ---------------------------------------------------------------------------------------- stars

// ref: FUN_009abb00
static void StarsInitialize(DNStars* stars) {
    stars->scene = M2CreateScene();
    stars->model = stars->scene->CreateModel("Environments\\Stars\\stars.mdl", 0);
    stars->time = static_cast<uint32_t>(OsGetAsyncTimeMs());
}

// ref: FUN_009abb30
static void StarsRelease(DNStars* stars) {
    if (stars->model) {
        stars->model->Release();
        stars->model = nullptr;
    }

    if (stars->scene) {
        stars->scene->Release();
        stars->scene = nullptr;
    }
}

// ref: FUN_009abd50
// The stars come out once their alpha passes 1.
static void StarsDraw(DNStars* stars) {
    if (stars->color.a <= 1 || !stars->model) {
        return;
    }

    stars->model->SetAnimating(1);

    if (!stars->model->m_attachParent) {
        stars->model->m_flag8 = 1;
        stars->model->m_flag10000 = 1;
    } else {
        stars->model->m_flag80 = 1;
        stars->model->m_flag20000 = 1;
    }

    stars->model->m_baseAlpha = stars->color.a * (1.0f / 255.0f);

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    uint32_t dt = now - stars->time;
    stars->scene->AdvanceTime(dt);
    stars->time += dt;

    C3Vector origin = { 0.0f, 0.0f, 0.0f };
    stars->scene->Animate(origin);
    stars->scene->Draw(M2PASS_0);
    stars->scene->Draw(M2PASS_1);
}

// ---------------------------------------------------------------------------------------- dome

// ref: FUN_007f2470
// Seven rings of a unit sphere sunk by cos(45 degrees), so its 45-degree ring sits on the horizon.
// The zenith and the nadir are single points; the bands between rings are triangle strips.
void DomeBuild(DNDome* dome, float radius) {
    dome->segments = 24;

    float sink = static_cast<float>(cos(0.7853981633974483));
    uint32_t count = 0;
    uint16_t* index = dome->indices;
    uint32_t prevStart = 0;
    float prevAngle = 0.0f;

    for (int32_t ring = 0; ring < 7; ring++) {
        float angle = s_domeRings[ring] * 3.1415927f;
        float x = 0.31830987f * angle;
        float cosA = CosPi(x);
        float sinA = CosPi(x - 0.5f);

        uint32_t start = count;
        bool zenith = FloatNear(angle, 0.0f);

        for (int32_t j = 0; j < 24; j++) {
            double t = static_cast<double>(j) * 0.0416666679084301 * 6.2831854820251465;
            C3Vector& v = dome->verts[count++];
            v.x = static_cast<float>(sin(t) * sinA * radius);
            v.y = static_cast<float>(cos(t) * sinA * radius);
            v.z = cosA * radius - sink;

            if (zenith || FloatNear(angle, 3.1415927f)) {
                break;
            }
        }

        if (0 < ring) {
            bool prevPoint = FloatNear(prevAngle, 0.0f);
            bool curPoint = FloatNear(angle, 3.1415927f);

            for (uint32_t u = 0; u < 25; u++) {
                *index++ = static_cast<uint16_t>(prevStart + (prevPoint ? 0 : u % 24));
                *index++ = static_cast<uint16_t>((curPoint ? 0 : u % 24) + start);
            }
        }

        prevStart = start;
        prevAngle = angle;
    }

    dome->vertexCount = static_cast<uint16_t>(count);
    dome->indexCount = 300;
}

// ref: FUN_009acb00
static void DomeDraw(DNDome* dome) {
    GxXformPush(GxXform_World);
    C44Matrix scale;
    C3Vector s = { 6.6666665f, 6.6666665f, 6.6666665f };
    scale.Scale(s);
    GxXformSet(GxXform_World, scale);

    GxRsPush();
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Add);
    GxRsSetAlphaRef();

    GxPrimVertexPtr(dome->vertexCount, dome->verts, sizeof(C3Vector), nullptr, 0, dome->colors, sizeof(CImVector), nullptr, 0, nullptr, 0);
    GxPrimIndexPtr(dome->indexCount, dome->indices);
    DrawStrip(dome->indexCount, dome->vertexCount);

    GxXformPop(GxXform_World);
    GxRsPop();
}

// ---------------------------------------------------------------------------------------- bodies

// ref: FUN_009ad0b0
static void BodyLoadTexture(DNBody* body, const char* path) {
    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);
    CStatus status;
    body->texture = TextureCreate(path, flags, &status, 0);
}

// ref: FUN_007edbe0
// A body's quad at its size, all four corners in its colour.
static void BodyQuad(const DNBody* body, C3Vector* pos, C2Vector* uv, CImVector* colors, const uint16_t** indices, uint32_t* vertexCount, uint32_t* indexCount) {
    for (int32_t i = 0; i < 6; i++) {
        pos[i] = { s_bodyQuad[i].x * body->size, s_bodyQuad[i].y * body->size, s_bodyQuad[i].z * body->size };
        uv[i] = s_bodyQuadUv[i];
        colors[i] = body->color;
    }

    *vertexCount = 4;
    *indices = s_bodyIndices4;
    *indexCount = 4;
}

// ref: FUN_007edee0
// Cut the quad at the horizon, and fade the part of it within 0.4 of the horizon.
static void BodyClip(const DNBody* body, C3Vector* pos, C2Vector* uv, CImVector* colors, const uint16_t** indices, uint32_t* vertexCount, uint32_t* indexCount) {
    *vertexCount = 0;

    float height = body->pos.z - DayNightGetBlock()->cameraPos.z;
    float top = pos[0].z + height;
    float bottom = pos[2].z + height;

    if (top <= 0.0f || bottom <= 0.0f) {
        if (top < 0.0f && bottom < 0.0f) {
            return;
        }

        *vertexCount = 4;
        float t = top / (top - bottom);
        float z = (pos[2].z - pos[0].z) * t + pos[0].z;
        pos[2].z = z;
        pos[3].z = z;
        uv[2].y = t;
        uv[3].y = t;
    } else {
        *vertexCount = 4;
    }

    float fadeTop = (pos[0].z + height) - 0.4f;
    float fadeBottom = (pos[2].z + height) - 0.4f;

    if (0.001f < fadeTop && fadeBottom < 0.001f) {
        *vertexCount = 6;
        *indexCount = 6;
        *indices = s_bodyIndices6;

        float t = fadeTop / (fadeTop - fadeBottom);
        float z = (pos[2].z - pos[0].z) * t + pos[0].z;
        float v = (uv[2].y - uv[0].y) * t + uv[0].y;
        pos[4].z = z;
        pos[5].z = z;
        uv[4].y = v;
        uv[5].y = v;
    }

    for (uint32_t i = 0; i < *vertexCount; i++) {
        float d = (height + pos[i].z) - 0.4f;

        if (d < 0.001f) {
            float a = (0.4f + d) * 2.5f;
            float f = 0.0f;

            if (0.0f <= a) {
                f = a < 1.0f ? a : 1.0f;
            }

            colors[i].a = static_cast<uint8_t>(lrintf(f * 255.0f));
        }
    }
}

// ref: FUN_009ac660
static void BodyDraw(DNBody* body) {
    C3Vector pos[6];
    C2Vector uv[6];
    CImVector colors[6];
    const uint16_t* indices = nullptr;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    BodyQuad(body, pos, uv, colors, &indices, &vertexCount, &indexCount);
    BodyClip(body, pos, uv, colors, &indices, &vertexCount, &indexCount);

    if (!vertexCount) {
        return;
    }

    auto gxTex = body->texture ? TextureGetGxTex(body->texture, 0, nullptr) : nullptr;

    if (!gxTex) {
        return;
    }

    GxRsPush();
    C44Matrix view = PushFacingView(body->pos);

    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Texture0, gxTex);
    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);

    GxPrimVertexPtr(vertexCount, pos, sizeof(C3Vector), nullptr, 0, colors, sizeof(CImVector), uv, sizeof(C2Vector), nullptr, 0);
    GxPrimIndexPtr(indexCount, indices);
    DrawStrip(indexCount, vertexCount);

    PopFacingView(view);
    GxRsPop();
}


// ---------------------------------------------------------------------------------------- clouds

namespace {

DNClouds s_clouds;                          // DAT_00d38d90

float s_cloudValues[256];                   // DAT_00d38688: the noise values
float s_cloudEase[256];                     // DAT_00d38188: (1 - cos(i * pi / 256)) / 2
uint8_t s_cloudAlpha[256];                  // DAT_00d38588: coverage to alpha

// The cloud sheet sizes per detail level (0x00a41aac, 0x00a41ac0) and each octave's step in 8.8
// fixed point per level (0x00af4dc4).
const uint32_t s_cloudSizes[5] = { 128, 256, 512, 1024, 2048 };
const uint32_t s_cloudShifts[5] = { 7, 8, 9, 10, 11 };
const uint16_t s_cloudSteps[15] = { 16, 32, 64, 128, 256, 8, 16, 32, 64, 128, 4, 8, 16, 32, 64 };

// The cloud dome's rings over pi (0x00a41ad4) and the alpha each ring fades the sheet by
// (0x00a41b04).
const float s_cloudRings[12] = { 0.0f, 0.025f, 0.05f, 0.075f, 0.1f, 0.125f, 0.15f, 0.175f, 0.205f, 0.23f, 0.245f, 0.25f };
const uint8_t s_cloudRingAlpha[12] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 0, 0 };

// The cloud light's strength over the day (0x00af4be0).
const float s_cloudLightBand[16] = {
    0.16666667f, 1.0f, 0.19444445f, 1.0f, 0.20138890f, 1.0f, 0.22916667f, 1.0f,
    0.89583331f, 1.0f, 0.92361110f, 1.0f, 0.88888890f, 1.0f, 0.91666669f, 1.0f
};

// The client CRT's rand(), from its initial seed.
uint32_t s_randSeed = 1;

int32_t CrtRand() {
    s_randSeed = s_randSeed * 214013 + 2531011;
    return static_cast<int32_t>((s_randSeed >> 16) & 0x7FFF);
}

// The quick reciprocal square root the generator uses, without a refining step.
float FastInverseSqrt(float x) {
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    bits = 0x5F3997BB - ((bits >> 1) & 0x3FFFFFFF);
    float y;
    memcpy(&y, &bits, sizeof(y));
    return y;
}

// One octave's walk through the noise lattice (0x54 bytes on the reference's stack).
struct CloudOctave {
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t stepX;
    uint16_t stepY;
    uint16_t zi;
    uint16_t yi;
    uint16_t zi2;
    uint16_t end;
    uint16_t yi1;
    uint16_t zi1;
    float amplitude;
    uint32_t p00, p01, p10, p11;
    float v00, d00, v01, d01, v10, d10, v11, d11;
    uint32_t xi;
    uint32_t cachedXi;
};

}

DNClouds* DayNightGetClouds() {
    return &s_clouds;
}

// ref: FUN_007ed250
// The noise values (the reference takes them from rand(), whose state there depends on everything
// that ran before; frozen starts the CRT's generator from its initial seed) and the cosine ease.
static void CloudTables() {
    for (float& v : s_cloudValues) {
        int32_t r = CrtRand();
        v = 1.0f - (static_cast<float>(r) * 3.0518509e-05f + static_cast<float>(r) * 3.0518509e-05f);
    }

    for (int32_t i = 0; i < 256; i++) {
        s_cloudEase[i] = static_cast<float>((1.0 - cos(static_cast<double>(i) * 0.012271846644580364)) * 0.5);
    }
}

// ref: FUN_007f04b0
static void CloudsConstruct(DNClouds* clouds) {
    CloudTables();

    clouds->clock = 0.0f;
    clouds->current = 0;
    clouds->restart = 0;
    clouds->speed = 2.0f;
    clouds->octaves = 4;
    clouds->format = 2;
    clouds->rows = 8;
}

// ref: FUN_007ece10
static void CloudTextureCallback(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mip, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Latch && mip == 0) {
        stride = w * 4;
        texels = userArg;
    }
}

// ref: FUN_007f1b10
// The sheet at a detail level: its two textures, its texels (all white), its coverage and the row
// of values the shading reads back.
static void CloudsSetDetail(DNClouds* clouds, uint8_t lod, uint32_t rows) {
    if (clouds->textures[0] || clouds->textures[1]) {
        if (clouds->textures[0]) {
            HandleClose(clouds->textures[0]);
        }

        if (clouds->textures[1]) {
            HandleClose(clouds->textures[1]);
        }

        clouds->textures[0] = nullptr;
        clouds->textures[1] = nullptr;

        delete[] clouds->coverage;
        clouds->coverage = nullptr;
        delete[] clouds->texels;
        clouds->texels = nullptr;
        delete[] clouds->rowValues;
        clouds->rowValues = nullptr;
    }

    clouds->lod = lod;
    clouds->size = s_cloudSizes[lod];
    clouds->rows = rows ? rows : 8;
    clouds->sizeMask = clouds->size - 1;
    clouds->sizeShift = s_cloudShifts[lod];

    uint32_t count = clouds->size * clouds->size;
    clouds->texels = new CImVector[count];
    memset(clouds->texels, 0xFF, count * sizeof(CImVector));
    clouds->coverage = new uint8_t[count];
    clouds->rowValues = new float[clouds->size];
    memset(clouds->rowValues, 0, clouds->size * sizeof(float));

    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);
    clouds->textures[0] = TextureCreate(clouds->size, clouds->size, static_cast<EGxTexFormat>(clouds->format), GxTex_Argb8888, flags, clouds->texels, CloudTextureCallback, "DNClouds0", 0);
    clouds->textures[1] = TextureCreate(clouds->size, clouds->size, static_cast<EGxTexFormat>(clouds->format), GxTex_Argb8888, flags, clouds->texels, CloudTextureCallback, "DNClouds1", 0);

    clouds->rowStart = 0;
    clouds->restart = 1;
}

// ref: FUN_007f20e0
// Twelve rings of the sunk sphere from the zenith to the horizon, sixteen around, the sheet mapped
// on as a disc and faded out over the last rings.
static void CloudsBuildMesh(DNClouds* clouds, float radius) {
    float sink = static_cast<float>(cos(0.7853981633974483));
    uint32_t count = 0;
    uint16_t* index = clouds->indices;
    uint32_t prevStart = 0;
    float prevAngle = 0.0f;

    for (uint32_t ring = 0; ring < 12; ring++) {
        float angle = s_cloudRings[ring] * 3.1415927f;
        double cosA = cos(static_cast<double>(angle));
        double sinA = sin(static_cast<double>(angle));
        float uvRadius = static_cast<float>(ring) * 0.09090909f * 0.5f;

        uint32_t start = count;
        bool zenith = FloatNear(angle, 0.0f);
        double az = 0.0;

        for (int32_t j = 0; j < 16; j++) {
            double sAz = sin(az);
            double cAz = cos(az);

            clouds->verts[count] = {
                static_cast<float>(sAz * sinA * radius),
                static_cast<float>(sinA * cAz * radius),
                static_cast<float>(cosA * radius + -sink)
            };
            clouds->colors[count].value = (static_cast<uint32_t>(s_cloudRingAlpha[ring]) << 24) | 0xFFFFFF;
            clouds->uvs[count] = {
                static_cast<float>(sAz * uvRadius + 0.5),
                static_cast<float>(uvRadius * cAz + 0.5)
            };
            count++;

            if (zenith || FloatNear(angle, 6.2831855f)) {
                break;
            }

            az = az + 0.39269909262657166;
        }

        if (ring != 0) {
            bool prevPoint = FloatNear(prevAngle, 0.0f);
            bool curPoint = FloatNear(angle, 6.2831855f);

            for (uint32_t u = 0; u < 17; u++) {
                *index++ = static_cast<uint16_t>(prevStart + (prevPoint ? 0 : (u & 0xF)));
                *index++ = static_cast<uint16_t>(start + (curPoint ? 0 : (u & 0xF)));
            }
        }

        prevStart = start;
        prevAngle = angle;
    }

    clouds->vertexCount = static_cast<uint16_t>(count);
    clouds->indexCount = 0x176;
}

// ref: FUN_007edb50
// The coverage-to-alpha curve: 255 * (1 - exponent^x) over the coverage above the threshold.
static void CloudsSetExponent(DNClouds* clouds, float exponent) {
    clouds->exponent = exponent;

    float step = static_cast<float>(255 - clouds->threshold) * 0.00390625f;
    float x = 0.0f;

    for (int32_t i = 0; i < 256; i++) {
        float v = 255.0f - static_cast<float>(pow(clouds->exponent, x)) * 255.0f;
        s_cloudAlpha[i] = static_cast<uint8_t>(static_cast<int32_t>(v));
        x = step + x;
    }
}

static void CloudsInitialize(DNClouds* clouds) {
    CloudsConstruct(clouds);
    CloudsSetDetail(clouds, 0, 0);
    CloudsBuildMesh(clouds, 1.0f);

    float density = clouds->density != 0.0f ? clouds->density : 0.6f;
    clouds->threshold = static_cast<uint8_t>(lrintf((1.0f - density) * 255.0f));
    CloudsSetExponent(clouds, 0.96f);
    clouds->enabled = 1;
}

static void CloudsShutdown(DNClouds* clouds) {
    for (auto& texture : clouds->textures) {
        if (texture) {
            HandleClose(texture);
            texture = nullptr;
        }
    }

    delete[] clouds->coverage;
    clouds->coverage = nullptr;
    delete[] clouds->texels;
    clouds->texels = nullptr;
    delete[] clouds->rowValues;
    clouds->rowValues = nullptr;
}

// FUN_004c5000: the roots of a x^2 + b x + c, smaller first.
static bool SolveQuadratic(float a, float b, float c, float* r0, float* r1) {
    float ac4 = a * c * 4.0f;

    if (b * b <= ac4) {
        return false;
    }

    float root = sqrtf(b * b - ac4);
    float q = (b <= 0.0f ? b - root : root + b) * -0.5f;
    float inv = 1.0f / (q * a);
    float x0 = inv * q * q;
    float x1 = inv * a * c;

    if (x0 < x1) {
        *r0 = x0;
        *r1 = x1;
    } else {
        *r1 = x0;
        *r0 = x1;
    }

    return true;
}

// ref: FUN_007eda90
// Where the line from the camera along `dir` leaves the cloud dome.
static C3Vector CloudDomeHit(const C3Vector& origin, const C3Vector& dir) {
    float k = -static_cast<float>(cos(0.7853981633974483));
    float near0 = 0.0f;
    float t = 0.0f;
    SolveQuadratic(dir.z * dir.z + dir.y * dir.y + dir.x * dir.x, -k * dir.z + -k * dir.z, k * k - 1.0f, &near0, &t);

    const C3Vector& camera = DayNightGetBlock()->cameraPos;

    return {
        camera.x + origin.x + dir.x * t,
        camera.y + dir.y * t + origin.y,
        origin.z + dir.z * t + camera.z
    };
}

static void CloudSheetCoords(const DNClouds* clouds, const C3Vector& pos, float* u, float* v);

// ref: FUN_007efae0
// The light the sheet is shaded by: the sun by day, the moon by night, as a point over the sheet;
// the cloud colours from the light's bands; how strong it is, dimmed by a storm.
static void CloudsLight(DNClouds* clouds, C3Vector* base, C3Vector* highlight, C3Vector* ambient, C3Vector* light, float* intensity) {
    auto block = DayNightGetBlock();
    auto bodies = DayNightGetBodies();
    float t = block->timeOfDay;

    C3Vector body = (t < 0.20138890f || !(t < 0.92361110f)) ? bodies->moon.pos : bodies->sun.pos;
    C3Vector dir = { body.x - block->cameraPos.x, body.y - block->cameraPos.y, body.z - block->cameraPos.z };
    C3Vector origin = { 0.0f, 0.0f, 0.0f };
    C3Vector hit = CloudDomeHit(origin, dir);

    float u = 0.0f;
    float v = 0.0f;
    CloudSheetCoords(clouds, hit, &u, &v);

    float inv = 1.0f / static_cast<float>(clouds->size);
    clouds->lightU = inv * u;
    clouds->lightV = inv * v;

    *light = { u, v, 64.0f };

    const float k = 1.0f / 255.0f;
    const CImVector& c11 = block->info.color[11];
    const CImVector& c10 = block->info.color[10];
    const CImVector& c12 = block->info.color[12];
    *base = { c11.r * k, c11.g * k, c11.b * k };
    *highlight = { c10.r * k, c10.g * k, c10.b * k };
    *ambient = { c12.r * k, c12.g * k, c12.b * k };

    *intensity = InterpBodyBand(s_cloudLightBand, 8, t) * *intensity;
    light->z = block->storm * 192.0f + light->z;
    *intensity = (1.0f - block->storm * 0.75f) * *intensity;
}

// ref: FUN_007efd00
// The next rows of the sheet: four octaves of 3D value noise through the time-scrolled lattice,
// thresholded into coverage, shaded by the light against the slope of the first three octaves, and
// uploaded; a finished sheet swaps the textures and moves time on.
static void CloudsGenerate(DNClouds* clouds) {
    if (!clouds->enabled) {
        clouds->restart = 0;
        return;
    }

    auto block = DayNightGetBlock();

    if (block->overrideSky && 0.99f < block->overrideSkyWeight) {
        clouds->restart = 0;
        return;
    }

    for (int32_t i = 0; i < 3; i++) {
        if (block->sky[i] && 0.99f < block->skyWeight[i] && block->skyFlag[i] == 0) {
            clouds->restart = 0;
            return;
        }
    }

    uint32_t savedRows = clouds->rows;

    if (clouds->restart) {
        clouds->rowStart = 0;
        clouds->rows = clouds->size;
    }

    clouds->clock = clouds->clock + block->frameDelta;

    float density = block->info.floatBand[1];

    if (clouds->density != 0.0f) {
        density = clouds->density;
    }

    clouds->threshold = static_cast<uint8_t>(static_cast<int32_t>((1.0f - density) * 255.0f));

    uint32_t zi = clouds->time >> 8;
    uint32_t p0 = s_cloudPermutation[zi];
    uint32_t p1 = s_cloudPermutation[(zi + 1) & 0xFF];

    CloudOctave octaves[8];
    const uint16_t* steps = &s_cloudSteps[clouds->lod * 5];

    for (uint32_t k = 0; k < clouds->octaves; k++) {
        CloudOctave& o = octaves[k];
        uint16_t step = steps[k];

        o.stepY = step;
        o.stepX = step;
        o.y = static_cast<uint16_t>(step * static_cast<uint16_t>(clouds->rowStart));
        o.zi = static_cast<uint16_t>(zi);
        o.end = static_cast<uint16_t>(((static_cast<uint32_t>(step) << clouds->sizeShift) >> 8) + 2 + zi);
        o.zi1 = static_cast<uint16_t>(zi + 1);
        o.zi2 = static_cast<uint16_t>(zi);
        o.z = clouds->time;
        o.amplitude = 1.0f / static_cast<float>(1 << k);
    }

    C3Vector base = { 0.0f, 0.0f, 0.0f };
    C3Vector highlight = { 0.0f, 0.0f, 0.0f };
    C3Vector ambient = { 0.0f, 0.0f, 0.0f };
    C3Vector light = { 0.0f, 0.0f, 0.0f };
    float intensity = 1.0f;
    CloudsLight(clouds, &base, &highlight, &ambient, &light, &intensity);

    float gx = 0.0f;
    float gy = 0.0f;

    for (uint32_t r = 0; r < clouds->rows; r++) {
        uint32_t row = clouds->rowStart + r;
        uint32_t offset = row << clouds->sizeShift;
        uint8_t* coverRow = clouds->coverage + offset;
        uint8_t* texRow = reinterpret_cast<uint8_t*>(clouds->texels + offset);

        for (uint32_t k = 0; k < clouds->octaves; k++) {
            CloudOctave& o = octaves[k];
            uint32_t yi = o.y >> 8;
            o.yi = static_cast<uint16_t>(yi);
            o.yi1 = static_cast<uint16_t>((yi + 1) & 0xFF);
            o.p00 = s_cloudPermutation[(p0 + yi) & 0xFF];
            o.p01 = s_cloudPermutation[(p0 + ((yi + 1) & 0xFF)) & 0xFF];
            o.p10 = s_cloudPermutation[(p1 + yi) & 0xFF];
            o.p11 = s_cloudPermutation[(p1 + ((yi + 1) & 0xFF)) & 0xFF];
            o.cachedXi = 0xFFFFFFFF;
            o.x = clouds->time;
        }

        float rowF = static_cast<float>(row);
        float prev = 0.0f;

        for (uint32_t x = 0; x < clouds->size; x++) {
            float value = 0.0f;

            for (uint32_t k = 0; k < clouds->octaves; k++) {
                CloudOctave& o = octaves[k];
                uint32_t xi = o.x >> 8;
                o.xi = xi;

                if (xi != o.cachedXi) {
                    o.cachedXi = xi;

                    float a = s_cloudValues[s_cloudPermutation[(o.p00 + xi) & 0xFF]];
                    o.v00 = a;
                    o.d00 = s_cloudValues[s_cloudPermutation[(o.p00 + xi + 1) & 0xFF]] - a;

                    a = s_cloudValues[s_cloudPermutation[(o.p01 + xi) & 0xFF]];
                    o.v01 = a;
                    o.d01 = s_cloudValues[s_cloudPermutation[(o.p01 + xi + 1) & 0xFF]] - a;

                    a = s_cloudValues[s_cloudPermutation[(o.p10 + xi) & 0xFF]];
                    o.v10 = a;
                    o.d10 = s_cloudValues[s_cloudPermutation[(o.p10 + xi + 1) & 0xFF]] - a;

                    a = s_cloudValues[s_cloudPermutation[(xi + o.p11) & 0xFF]];
                    o.v11 = a;
                    o.d11 = s_cloudValues[s_cloudPermutation[(xi + o.p11 + 1) & 0xFF]] - a;
                }

                float fx = s_cloudEase[o.x & 0xFF];
                float n00 = o.d00 * fx + o.v00;
                float n10 = o.d10 * fx + o.v10;
                float fy = s_cloudEase[o.y & 0xFF];
                float lower = ((o.d01 * fx + o.v01) - n00) * fy + n00;
                float fz = s_cloudEase[o.z & 0xFF];

                o.x = static_cast<uint16_t>(o.stepX + o.x);

                float upper = ((o.d11 * fx + o.v11) - n10) * fy + n10;
                value = ((upper - lower) * fz + lower) * o.amplitude + value;

                if (k == 2) {
                    float scale = static_cast<float>(1 << (clouds->sizeShift - 7));
                    gx = (prev - value) * scale;
                    gy = (clouds->rowValues[x] - value) * scale;
                    clouds->rowValues[x] = value;
                    prev = value;
                }
            }

            int32_t n = static_cast<int32_t>(lrintf(value * 64.0f + 128.0f));
            int32_t idx = (n & 0xFF) - clouds->threshold;
            uint8_t alpha = 0 <= idx ? s_cloudAlpha[idx] : 0;
            coverRow[x] = alpha;

            uint8_t* texel = texRow + x * 4;

            if (alpha == 0) {
                if (x != 0) {
                    memcpy(texel, texel - 4, 4);
                    texel[3] = 0;
                }

                continue;
            }

            float lx = light.x - static_cast<float>(x);
            float ly = light.y - rowF;
            float shade = static_cast<float>(static_cast<uint8_t>((static_cast<uint8_t>(-static_cast<int32_t>(alpha) - 1) >> 1) + 0x40)) * (1.0f / 255.0f);
            float cr = base.x * shade + ambient.x;
            float cg = base.y * shade + ambient.y;
            float cb = base.z * shade + ambient.z;

            float il = FastInverseSqrt(lx * lx + ly * ly + light.z * light.z);
            float in = FastInverseSqrt(gx * gx + gy * gy + 1.0f);
            float d = in * il * (ly * gy + lx * gx + light.z);

            if (0.0f < d) {
                d = d * intensity;
                cr = highlight.x * d + cr;
                cg = highlight.y * d + cg;
                cb = d * highlight.z + cb;
            }

            if (1.0f < cr) {
                cr = 1.0f;
            }

            if (1.0f < cg) {
                cg = 1.0f;
            }

            if (1.0f < cb) {
                cb = 1.0f;
            }

            texel[2] = static_cast<uint8_t>(lrintf(cr * 255.0f));
            texel[1] = static_cast<uint8_t>(lrintf(cg * 255.0f));
            texel[0] = static_cast<uint8_t>(lrintf(cb * 255.0f));
            texel[3] = alpha;
        }

        for (uint32_t k = 0; k < clouds->octaves; k++) {
            octaves[k].y = static_cast<uint16_t>(octaves[k].y + octaves[k].stepY);
        }
    }

    auto gxTex = TextureGetGxTex(clouds->textures[(clouds->current - 1) & 1], 1, nullptr);

    if (gxTex) {
        GxTexUpdate(gxTex, 0, clouds->rowStart, clouds->size, clouds->rows + clouds->rowStart, 1);
    }

    clouds->rowStart += clouds->rows;

    if (clouds->size <= clouds->rowStart) {
        uint16_t time = static_cast<uint16_t>(lrintf(clouds->speed * clouds->clock));

        if (time == clouds->time) {
            if (clouds->restart) {
                clouds->current = (clouds->current - 1) & 1;
            }
        } else {
            clouds->time = time;
            clouds->current = (clouds->current - 1) & 1;
        }

        clouds->rowStart = 0;
    }

    if (!clouds->restart) {
        return;
    }

    clouds->rows = savedRows;
    clouds->restart = 0;
}

// ref: FUN_007f1010
void DayNightUpdateClouds() {
    CloudsGenerate(&s_clouds);
}

// ref: FUN_009acd40
static void CloudsDraw(DNClouds* clouds) {
    if (!clouds->enabled) {
        return;
    }

    auto gxTex = clouds->textures[clouds->current] ? TextureGetGxTex(clouds->textures[clouds->current], 0, nullptr) : nullptr;

    if (!gxTex) {
        return;
    }

    GxXformPush(GxXform_World);
    C44Matrix scale;
    C3Vector s = { 6.6666665f, 6.6666665f, 6.6666665f };
    scale.Scale(s);
    GxXformSet(GxXform_World, scale);

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Texture0, gxTex);

    // During a timed fade the whole sheet takes one colour, white at the fade's alpha.
    CImVector fade;
    const CImVector* colors = clouds->colors;
    uint32_t colorStride = sizeof(CImVector);

    if (DayNightTimedFadeActive()) {
        fade.b = 0xFF;
        fade.g = 0xFF;
        fade.r = 0xFF;
        fade.a = static_cast<uint8_t>(lrintf(DayNightTimedFadeAmount() * 255.0f));
        colors = &fade;
        colorStride = 0;
    }

    GxPrimVertexPtr(clouds->vertexCount, clouds->verts, sizeof(C3Vector), nullptr, 0, colors, colorStride, clouds->uvs, sizeof(C2Vector), nullptr, 0);
    GxPrimIndexPtr(clouds->indexCount, clouds->indices);
    DrawStrip(clouds->indexCount, clouds->vertexCount);

    GxRsPop();
    GxXformPop(GxXform_World);
}

// ---------------------------------------------------------------------------------------- glare

// ref: FUN_009ad000
static void GlareLoadTexture(DNGlare* glare, const char* path) {
    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);
    CStatus status;
    glare->texture = TextureCreate(path, flags, &status, 1);

    if (g_theGxDevicePtr->Caps().m_occlusionQuery) {
        g_theGxDevicePtr->QueryCreate(glare->query, 0);
    }

    glare->lastPixels = 0;
}

// FUN_006bfb60: a quad's corners projected into window pixels.
static bool ProjectToWindow(uint32_t count, const C3Vector* verts, C3Vector* out) {
    CRect window;
    g_theGxDevicePtr->CapsWindowSize(window);

    C44Matrix viewProj;
    GxXformViewProj(viewProj);

    C44Matrix world;
    GxXformWorld(world);
    C44Matrix m = world * viewProj;

    for (uint32_t i = 0; i < count; i++) {
        const C3Vector& v = verts[i];
        float px = v.x * m.a0 + v.y * m.b0 + v.z * m.c0 + m.d0;
        float py = v.x * m.a1 + v.y * m.b1 + v.z * m.c1 + m.d1;
        float pz = v.x * m.a2 + v.y * m.b2 + v.z * m.c2 + m.d2;
        float pw = v.x * m.a3 + v.y * m.b3 + v.z * m.c3 + m.d3;

        if (fabsf(pw - 0.0f) < 0.001f) {
            return false;
        }

        float inv = 1.0f / pw;
        out[i].x = (px * inv * 0.5f + 0.5f) * (window.maxX - window.minX) + window.minX;
        out[i].y = (py * inv * 0.5f + 0.5f) * (window.maxY - window.minY) + window.minY;
        out[i].z = pz * inv;
    }

    return true;
}

// ref: FUN_009abe00
// How much of the glare's body the hardware saw last frame: the pixels the occlusion query counted
// over the quad's area on screen. The quad is drawn invisibly to count this frame's.
static float GlareOcclusion(DNGlare* glare) {
    uint32_t pixels = 0;
    uint32_t available = 0;

    g_theGxDevicePtr->QueryGetParam(glare->query, 1, available);

    if (!available || !g_theGxDevicePtr->QueryGetData(glare->query, &pixels)) {
        pixels = glare->lastPixels;
    }

    glare->lastPixels = pixels;

    if (available) {
        g_theGxDevicePtr->QueryBegin(glare->query);
    }

    GxRsPush();
    GxRsSet(GxRs_ColorWrite, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_Lighting, 0);

    C3Vector pos[6];
    C2Vector uv[6];
    CImVector colors[6];
    const uint16_t* indices = nullptr;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    BodyQuad(glare->body, pos, uv, colors, &indices, &vertexCount, &indexCount);

    float minX, maxX, minY, maxY, minZ, maxZ;
    GxXformViewport(minX, maxX, minY, maxY, minZ, maxZ);
    GxXformSetViewport(minX, maxX, minY, maxY, SKY_MIN_Z, SKY_MAX_Z);

    C44Matrix view = PushFacingView(glare->pos);

    if (available) {
        GxPrimVertexPtr(vertexCount, pos, sizeof(C3Vector), nullptr, 0, colors, sizeof(CImVector), nullptr, 0, nullptr, 0);
        GxPrimIndexPtr(indexCount, indices);
        DrawStrip(indexCount, vertexCount);
    }

    C3Vector screen[6];
    float area = 0.0f;

    if (ProjectToWindow(vertexCount, pos, screen)) {
        area = fabsf(screen[2].y - screen[0].y) * fabsf(screen[1].x - screen[0].x);
    }

    PopFacingView(view);
    GxXformSetViewport(minX, maxX, minY, maxY, minZ, maxZ);
    GxRsPop();

    if (available) {
        g_theGxDevicePtr->QueryEnd(glare->query);
    }

    float result = 0.0f;

    if (glare->skipQuery == 0) {
        if (1.0f < area) {
            result = static_cast<float>(pixels) / area;

            if (1.0f < result) {
                return 1.0f;
            }
        }

        return result;
    }

    glare->skipQuery = 0;

    return result;
}

// ref: FUN_009abc60
// Without occlusion queries: whether anything in the world lies between the camera and the glare.
static float GlareRaycast(DNGlare* glare) {
    auto block = DayNightGetBlock();
    C3Vector d = { glare->pos.x - block->cameraPos.x, glare->pos.y - block->cameraPos.y, glare->pos.z - block->cameraPos.z };
    float inv = 1.0f / sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);

    C3Vector start = { block->cameraPos.x + d.x * inv * 0.5f, block->cameraPos.y + d.y * inv * 0.5f, block->cameraPos.z + d.z * inv * 0.5f };
    C3Vector end = { block->cameraPos.x + block->farClip * d.x * inv, block->cameraPos.y + d.y * inv * block->farClip, block->farClip * d.z * inv + block->cameraPos.z };
    C3Vector hit = { 0.0f, 0.0f, 0.0f };
    float t = 1.0f;

    if (WorldQuerySegment(start, end, &hit, &t, 0x200112, nullptr)) {
        return 0.0f;
    }

    // TODO FUN_004f9410: the world frame's own ray test against the models on its list
    // (CGWorldFrame +0x2a4), not carried by frozen's world frame. Only this fallback reaches it,
    // and the fallback is only taken with the occlusion CVar off or no query support.
    return 1.0f;
}

// ref: FUN_009ac3c0
static float GlareVisibility(DNGlare* glare) {
    if (!DayNightSkyReady() || !glare->enabled || !glare->color.a) {
        return 0.0f;
    }

    if (glare->query && CWorldParam::cvar_occlusion && CWorldParam::cvar_occlusion->GetInt()) {
        return GlareOcclusion(glare);
    }

    return GlareRaycast(glare);
}

// ref: FUN_007ef920
// Where on the cloud sheet the line to `pos` crosses: its angle from the zenith of the sunk dome,
// out to 45 degrees, mapped to the sheet's radius.
static void CloudSheetCoords(const DNClouds* clouds, const C3Vector& pos, float* u, float* v) {
    auto block = DayNightGetBlock();
    float dx = pos.x - block->cameraPos.x;
    float dy = pos.y - block->cameraPos.y;
    float dz = (pos.z - block->cameraPos.z) + static_cast<float>(cos(0.7853981633974483));

    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    float angle = static_cast<float>(acos(dz / len));

    if (0.78539819f < angle) {
        angle = 0.78539819f;
    }

    float r = angle * 1.2732395f * 0.5f;
    float flat = sqrtf(dy * dy + dx * dx);
    float nx = 0.0f;
    float ny = 0.0f;

    if (1e-05f < flat) {
        nx = dx / flat;
        ny = dy / flat;
    }

    float size = static_cast<float>(clouds->size);
    *u = size * (nx * r + 0.5f);
    *v = size * (0.5f + ny * r);
}

// ref: FUN_007efa30
// The cloud cover in front of `pos`, from the sheet's coverage map.
static float CloudDensityAt(const DNClouds* clouds, const C3Vector& pos) {
    if (!clouds->enabled || !clouds->coverage) {
        return 0.0f;
    }

    float u;
    float v;
    CloudSheetCoords(clouds, pos, &u, &v);

    uint32_t last = clouds->size - 1;
    int32_t iu = static_cast<int32_t>(llrintf(u));
    int32_t iv = static_cast<int32_t>(llrintf(v));
    uint32_t cu = static_cast<uint32_t>(iu) > last ? (iu < 0 ? 0 : last) : static_cast<uint32_t>(iu);
    uint32_t cv = static_cast<uint32_t>(iv) > last ? (iv < 0 ? 0 : last) : static_cast<uint32_t>(iv);

    return clouds->coverage[(cv << clouds->sizeShift) + cu] * (1.0f / 255.0f);
}

// ref: FUN_007f1020 (the sun glare's slot 3)
// ref: FUN_007f1040 (the moon glare's slot 3)
static float GlareCloudFactor(const DNGlare* glare) {
    float density = CloudDensityAt(DayNightGetClouds(), glare->pos);

    if (glare->kind == DNGlare::Kind_Sun) {
        return 1.0f - density;
    }

    return 1.0f - fabsf((density - 0.5f) * 2.0f);
}

// ref: FUN_007eda30
// Under liquid the glare fades out over ten yards of depth.
static float GlareLiquidFactor() {
    if (!CWorldScene::s_cameraLiquidType) {
        return 1.0f;
    }

    float f = CWorldScene::s_cameraLiquidDepth * 0.1f;

    if (f < 0.0f) {
        return 1.0f;
    }

    return 1.0f - (1.0f <= f ? 1.0f : f);
}

// ref: FUN_007ef6e0
// The glare's target strength (clouds, liquid, an opaque skybox, the day band, what the hardware
// saw), the current strength eased toward it, and its size, alpha and dimming of the scene from how
// squarely the camera faces it.
static void GlareUpdate(DNGlare* glare, float dt) {
    auto block = DayNightGetBlock();

    glare->target = 1.0f;

    float dx = glare->pos.x - block->cameraPos.x;
    float dy = glare->pos.y - block->cameraPos.y;
    float dz = glare->pos.z - block->cameraPos.z;
    float inv = 1.0f / sqrtf(dx * dx + dz * dz + dy * dy);

    glare->target = GlareCloudFactor(glare) * glare->target;
    float base = glare->target;
    float liquid = GlareLiquidFactor();
    glare->target = liquid * base;

    float sky = 0.0f;

    if (!block->overrideSky || !(0.0f < block->overrideSkyWeight)) {
        sky = 0.0f;

        if (block->sky[0] && 0.0f < block->skyWeight[0]) {
            sky = block->skyWeight[0];
        }

        if (block->sky[1] && sky < block->skyWeight[1]) {
            sky = block->skyWeight[1];
        }

        if (block->sky[2] && sky < block->skyWeight[2]) {
            sky = block->skyWeight[2];
        }
    } else {
        sky = block->overrideSkyWeight;
    }

    glare->target = (1.0f - sky) * liquid * base;
    glare->target = InterpBodyBand(glare->band, 4, block->timeOfDay) * glare->target;

    if (glare->target != 0.0f) {
        glare->target = GlareVisibility(glare) * glare->target;
    }

    if (glare->current < glare->target) {
        float x = glare->rise * dt + glare->current;
        glare->current = x < glare->target ? x : glare->target;
    } else if (glare->target < glare->current) {
        float x = glare->current - glare->fall * dt;
        glare->current = glare->target < x ? x : glare->target;
    }

    float dot = block->cameraDir.y * dy * inv + block->cameraDir.z * dz * inv + block->cameraDir.x * dx * inv;

    if (dot < glare->dotMin) {
        dot = glare->dotMin;
    }

    float f = (dot - glare->dotMin) / (1.0f - glare->dotMin);
    glare->size = ((glare->sizeMax - glare->sizeMin) * f + glare->sizeMin) * glare->sizeBase;

    float alpha = ((glare->alphaMax - glare->alphaMin) * f + glare->alphaMin) * glare->current
                * (glare->color.a * (1.0f / 255.0f)) * 255.0f;
    glare->color.a = static_cast<uint8_t>(lrintf(alpha));

    glare->darken = static_cast<float>(pow(dot, 10.0)) * glare->current;
}

// ref: FUN_009ac400
static void GlareDraw(DNGlare* glare) {
    if (!DayNightSkyReady() || !glare->enabled || !glare->color.a) {
        return;
    }

    auto gxTex = glare->texture ? TextureGetGxTex(glare->texture, 0, nullptr) : nullptr;

    if (!gxTex) {
        return;
    }

    auto block = DayNightGetBlock();

    C44Matrix view;
    GxXformView(view);

    C44Matrix world;
    FacingBasis(world, { view.a2, view.b2, view.c2 });
    world.d0 = glare->pos.x - block->cameraPos.x;
    world.d1 = glare->pos.y - block->cameraPos.y;
    world.d2 = glare->pos.z - block->cameraPos.z;
    C3Vector s = { glare->size, glare->size, glare->size };
    world.Scale(s);

    GxXformPush(GxXform_World);
    GxXformSet(GxXform_World, world);

    float minX, maxX, minY, maxY, minZ, maxZ;
    GxXformViewport(minX, maxX, minY, maxY, minZ, maxZ);
    GxXformSetViewport(minX, maxX, minY, maxY, SKY_MIN_Z, SKY_MAX_Z);

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Add);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_DepthTest, 0);
    GxRsSet(GxRs_Texture0, gxTex);
    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);

    GxPrimVertexPtr(4, s_glareQuad, sizeof(C3Vector), nullptr, 0, &glare->color, 0, s_glareUv, sizeof(C2Vector), nullptr, 0);
    GxPrimIndexPtr(4, s_glareIndices);
    DrawStrip(4, 4);

    GxXformSetViewport(minX, maxX, minY, maxY, minZ, maxZ);
    GxXformPop(GxXform_World);
    GxRsPop();
}

// ref: FUN_007f0870
// The world frame's glare pass: each glare updated for the frame and drawn over the scene.
void DayNightGlareRender() {
    if (!DayNightGetBlock()->drawSky) {
        return;
    }

    auto bodies = DayNightGetBodies();
    float dt = DayNightGetBlock()->frameDelta;

    GlareUpdate(&bodies->sunGlare, dt);
    GlareDraw(&bodies->sunGlare);
    GlareUpdate(&bodies->moonGlare, dt);
    GlareDraw(&bodies->moonGlare);
}

// ---------------------------------------------------------------------------------------- skyboxes

// ref: FUN_007ecf20
// Keep a day-animated skybox at the time of day.
static void SkyModelAnimate(DNSkyModel* sky, uint32_t minutes) {
    if (!sky || !sky->m_model || !sky->m_model->IsDrawable(0, 0)) {
        return;
    }

    if (!sky->m_duration) {
        M2BoneSequenceState state;
        sky->m_model->GetBoneSequenceState(0xFFFFFFFF, &state);
        sky->m_duration = state.endTime - state.startTime;
    }

    if (!sky->m_duration) {
        return;
    }

    int32_t delta = static_cast<int32_t>(minutes - sky->m_lastTime);

    if (1 < (delta < 0 ? -delta : delta) && (sky->m_flags & 1)) {
        float duration = static_cast<float>(sky->m_duration);
        uint32_t time = static_cast<uint32_t>(llrintf(static_cast<float>(minutes) * 0.00069444446f * duration));
        sky->m_model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, time, 1.1574074e-08f * duration, 1, 1);
    }

    sky->m_lastTime = minutes;
}

// ref: FUN_007f08c0
static void SkyModelDraw(DNSkyModel* sky, float weight) {
    if (!sky || !sky->m_model || !sky->m_model->IsDrawable(0, 0) || !(0.0f < weight)) {
        return;
    }

    CM2Model* model = sky->m_model;
    model->SetAnimating(1);

    if (!model->m_attachParent) {
        model->m_flag8 = 1;
        model->m_flag10000 = 1;
    } else {
        model->m_flag80 = 1;
        model->m_flag20000 = 1;
    }

    model->m_baseAlpha = weight;

    CM2Scene* scene = DayNightGetSkyScene();
    C3Vector origin = { 0.0f, 0.0f, 0.0f };
    scene->Animate(origin);
    scene->Draw(M2PASS_0);
    scene->Draw(M2PASS_1);

    model->SetAnimating(0);

    if (model->m_attachParent) {
        model->m_flag80 = 0;
        model->m_flag20000 = 0;
    } else {
        model->m_flag8 = 0;
        model->m_flag10000 = 0;
    }
}

// ref: FUN_007f31c0
DNSkyModel* DayNightSetSkyModel(uint32_t slot, int32_t enable, const char* name, uint32_t flags, float weight) {
    if (3 < slot) {
        return nullptr;
    }

    auto block = DayNightGetBlock();

    if (enable) {
        auto sky = DayNightGetSkyModel(name, flags);
        block->sky[slot] = sky;
        block->skyWeight[slot] = weight;
        block->skyFlag[slot] = 0;

        return sky;
    }

    block->sky[slot] = nullptr;
    block->skyWeight[slot] = 0.0f;
    block->skyFlag[slot] = 0;

    return nullptr;
}

// ---------------------------------------------------------------------------------------- sky

// ref: FUN_007f09b0
// The sky inside `window`: the stars, the three bodies, the dome and the clouds unless an opaque
// skybox covers them, then the skyboxes themselves, all at the back of the depth range.
void DayNightSkyRender(const CRect& window) {
    auto block = DayNightGetBlock();

    if (!block->drawSky) {
        return;
    }

    float minX, maxX, minY, maxY, minZ, maxZ;
    GxXformViewport(minX, maxX, minY, maxY, minZ, maxZ);

    CRect viewport(minY, minX, maxY, maxX);
    CRect visible = CRect::Intersection(window, viewport);

    if (!(visible.minY < visible.maxY && visible.minX < visible.maxX)) {
        return;
    }

    GxXformSetViewport(minX, maxX, minY, maxY, SKY_MIN_Z, SKY_MAX_Z);
    GxRsSet(GxRs_ScissorTest, 1);
    g_theGxDevicePtr->ScissorSet(&visible);

    bool drawSky = true;

    if (block->overrideSky && block->overrideSky->m_model && block->overrideSky->m_model->IsDrawable(0, 0)
        && 0.99f < block->overrideSkyWeight) {
        drawSky = false;
    }

    for (int32_t i = 0; i < 3; i++) {
        if (block->sky[i] && block->sky[i]->m_model && block->sky[i]->m_model->IsDrawable(0, 0)
            && 0.99f < block->skyWeight[i] && block->skyFlag[i] == 0) {
            drawSky = false;
        }
    }

    if (drawSky) {
        auto bodies = DayNightGetBodies();

        StarsDraw(DayNightGetStars());
        BodyDraw(&bodies->sun);
        BodyDraw(&bodies->moon);
        BodyDraw(&bodies->moon2);
        DomeDraw(DayNightGetDome());
        CloudsDraw(DayNightGetClouds());
    }

    CM2Scene* scene = DayNightGetSkyScene();

    if (scene) {
        uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
        uint32_t dt = now - DayNightGetSkySceneTime();
        scene->AdvanceTime(dt);
        DayNightSetSkySceneTime(DayNightGetSkySceneTime() + dt);

        uint32_t minutes = static_cast<uint32_t>(g_clientGameTime.GetHourAndMinutes());

        for (int32_t i = 0; i < 3; i++) {
            SkyModelAnimate(block->sky[i], minutes);
        }

        SkyModelAnimate(block->overrideSky, minutes);

        if (!block->overrideSky || block->overrideSkyWeight < 1.0f) {
            for (int32_t i = 0; i < 3; i++) {
                SkyModelDraw(block->sky[i], block->skyWeight[i]);
            }
        }

        if (block->overrideSky && 0.0f < block->overrideSkyWeight) {
            SkyModelDraw(block->overrideSky, block->overrideSkyWeight);
        }
    }

    GxRsSet(GxRs_ScissorTest, 0);
    GxXformSetViewport(minX, maxX, minY, maxY, minZ, maxZ);
}

// ---------------------------------------------------------------------------------------- setup

// ref: FUN_007ee150
static void SunGlareInitialize(DNGlare* glare, DNBody* sun) {
    GlareLoadTexture(glare, "Textures\\sunGlare.blp");
    glare->kind = DNGlare::Kind_Sun;
    glare->sizeBase = 1.0f;
    glare->body = sun;
    glare->fall = 1.5151515f;
    glare->enabled = 1;
    glare->skipQuery = 1;
    glare->rise = 4.0f;
    glare->alphaMin = 0.5f;
    glare->alphaMax = 1.0f;
    glare->sizeMin = 3.0f;
    glare->sizeMax = 20.0f;
    glare->dotMin = 0.7f;
    glare->darken = 0.0f;

    const float band[8] = { 0.2708333433f, 0.0f, 0.3125f, 1.0f, 0.8125f, 1.0f, 0.875f, 0.0f };
    memcpy(glare->band, band, sizeof(band));
}

// ref: FUN_007ee230
static void MoonGlareInitialize(DNGlare* glare, DNBody* moon) {
    GlareLoadTexture(glare, "Textures\\moonGlare.blp");
    glare->kind = DNGlare::Kind_Moon;
    glare->sizeBase = 2.0f;
    glare->body = moon;
    glare->fall = 1.5151515f;
    glare->enabled = 1;
    glare->skipQuery = 1;
    glare->rise = 3.030303f;
    glare->alphaMin = 0.1f;
    glare->alphaMax = 1.0f;
    glare->sizeMin = 1.0f;
    glare->sizeMax = 1.0f;
    glare->dotMin = 0.7f;
    glare->darken = 0.0f;

    const float band[8] = { 0.0833333358f, 1.0f, 0.1354166716f, 0.0f, 0.9479166865f, 0.0f, 0.9993056059f, 1.0f };
    memcpy(glare->band, band, sizeof(band));
}

// The sky half of FUN_007f2790.
void DayNightSkyInitialize() {
    auto bodies = DayNightGetBodies();

    DomeBuild(DayNightGetDome(), 1.0f);
    CloudsInitialize(DayNightGetClouds());

    SunGlareInitialize(&bodies->sunGlare, &bodies->sun);
    MoonGlareInitialize(&bodies->moonGlare, &bodies->moon);

    BodyLoadTexture(&bodies->sun, "Textures\\sunCenter.blp");
    BodyLoadTexture(&bodies->moon, "Textures\\moon.blp");
    BodyLoadTexture(&bodies->moon2, "Textures\\moon02.blp");

    StarsInitialize(DayNightGetStars());
}

// The sky half of FUN_007f1d30.
void DayNightSkyShutdown() {
    auto bodies = DayNightGetBodies();

    for (DNGlare* glare : { &bodies->sunGlare, &bodies->moonGlare }) {
        if (glare->texture) {
            HandleClose(glare->texture);
            glare->texture = nullptr;
        }

        if (glare->query) {
            g_theGxDevicePtr->QueryDestroy(glare->query);
        }
    }

    for (DNBody* body : { &bodies->sun, &bodies->moon, &bodies->moon2 }) {
        if (body->texture) {
            HandleClose(body->texture);
            body->texture = nullptr;
        }
    }

    CloudsShutdown(DayNightGetClouds());
    StarsRelease(DayNightGetStars());
}
