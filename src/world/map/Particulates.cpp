#include "world/map/Particulates.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "db/Db.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/RenderState.hpp"
#include "gx/Transform.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "ui/game/CGCamera.hpp"
#include "util/CStatus.hpp"
#include "util/Random.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/random/CRandom.hpp>
#include <cmath>
#include <cstring>

namespace {

// Each mote's quad: the four corners around its centre, in view space, scaled by its size.
// DAT_00adf808
const float s_moteCorners[4][3] = {
    { -0.5f, 0.5f, 0.0f },
    { -0.5f, -0.5f, 0.0f },
    { 0.5f, 0.5f, 0.0f },
    { 0.5f, -0.5f, 0.0f },
};

// The 25 cells of the mote texture, a 5x5 atlas: four (u, v) corners each. DAT_00adf838
const float s_moteUvs[25][8] = {
    { 0.0f, 0.0f, 0.0f, 0.19921875f, 0.19921875f, 0.0f, 0.19921875f, 0.19921875f },
    { 0.19921875f, 0.0f, 0.19921875f, 0.19921875f, 0.3984375f, 0.0f, 0.3984375f, 0.19921875f },
    { 0.3984375f, 0.0f, 0.3984375f, 0.19921875f, 0.59765625f, 0.0f, 0.59765625f, 0.19921875f },
    { 0.59765625f, 0.0f, 0.59765625f, 0.19921875f, 0.796875f, 0.0f, 0.796875f, 0.19921875f },
    { 0.0f, 0.19921875f, 0.0f, 0.3984375f, 0.19921875f, 0.19921875f, 0.19921875f, 0.3984375f },
    { 0.19921875f, 0.19921875f, 0.19921875f, 0.3984375f, 0.3984375f, 0.19921875f, 0.3984375f, 0.3984375f },
    { 0.3984375f, 0.19921875f, 0.3984375f, 0.3984375f, 0.59765625f, 0.19921875f, 0.59765625f, 0.3984375f },
    { 0.59765625f, 0.19921875f, 0.59765625f, 0.3984375f, 0.796875f, 0.19921875f, 0.796875f, 0.3984375f },
    { 0.0f, 0.3984375f, 0.0f, 0.59765625f, 0.19921875f, 0.3984375f, 0.19921875f, 0.59765625f },
    { 0.19921875f, 0.3984375f, 0.19921875f, 0.59765625f, 0.3984375f, 0.3984375f, 0.3984375f, 0.59765625f },
    { 0.3984375f, 0.3984375f, 0.3984375f, 0.59765625f, 0.59765625f, 0.3984375f, 0.59765625f, 0.59765625f },
    { 0.59765625f, 0.3984375f, 0.59765625f, 0.59765625f, 0.796875f, 0.3984375f, 0.796875f, 0.59765625f },
    { 0.796875f, 0.0f, 0.796875f, 0.19921875f, 0.99609375f, 0.0f, 0.99609375f, 0.19921875f },
    { 0.796875f, 0.19921875f, 0.796875f, 0.3984375f, 0.99609375f, 0.19921875f, 0.99609375f, 0.3984375f },
    { 0.796875f, 0.3984375f, 0.796875f, 0.59765625f, 0.99609375f, 0.3984375f, 0.99609375f, 0.59765625f },
    { 0.796875f, 0.59765625f, 0.796875f, 0.796875f, 0.99609375f, 0.59765625f, 0.99609375f, 0.796875f },
    { 0.796875f, 0.796875f, 0.796875f, 0.99609375f, 0.99609375f, 0.796875f, 0.99609375f, 0.99609375f },
    { 0.0f, 0.59765625f, 0.0f, 0.796875f, 0.19921875f, 0.59765625f, 0.19921875f, 0.796875f },
    { 0.19921875f, 0.59765625f, 0.19921875f, 0.796875f, 0.3984375f, 0.59765625f, 0.3984375f, 0.796875f },
    { 0.3984375f, 0.59765625f, 0.3984375f, 0.796875f, 0.59765625f, 0.59765625f, 0.59765625f, 0.796875f },
    { 0.59765625f, 0.59765625f, 0.59765625f, 0.796875f, 0.796875f, 0.59765625f, 0.796875f, 0.796875f },
    { 0.0f, 0.796875f, 0.0f, 0.99609375f, 0.19921875f, 0.796875f, 0.19921875f, 0.99609375f },
    { 0.19921875f, 0.796875f, 0.19921875f, 0.99609375f, 0.3984375f, 0.796875f, 0.3984375f, 0.99609375f },
    { 0.3984375f, 0.796875f, 0.3984375f, 0.99609375f, 0.59765625f, 0.796875f, 0.59765625f, 0.99609375f },
    { 0.59765625f, 0.796875f, 0.59765625f, 0.99609375f, 0.796875f, 0.796875f, 0.796875f, 0.99609375f },
};

// Which atlas cell each of eight consecutive motes takes, one row per LiquidType
// m_particleTexSlots value. DAT_00a3f970
const int32_t s_moteFrames[5][8] = {
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    { 8, 9, 10, 11, 8, 9, 10, 11 },
    { 12, 12, 12, 12, 12, 12, 12, 12 },
    { 13, 14, 15, 16, 13, 14, 15, 16 },
    { 17, 18, 19, 20, 21, 22, 23, 24 },
};

// A random float in [1, 2) from 23 random mantissa bits.
float RandomOneToTwo() {
    uint32_t bits = (CRandom::uint32(g_rndSeed) & 0x7fffff) | 0x3f800000;
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

// ref: FUN_006f7a10
// The polynomial sine the object effects use: sin(x) from the same cubic as CGCamera::SineEase,
// a quarter turn on. Shared: the lightning bolts (Lightning.cpp) wave by it too.
float PolySin(float x) {
    float fraction;
    int32_t whole;
    CameraSplitFloor(x * 0.31830987334251404f - 0.5f, &fraction, &whole);

    float value = 1.0f - (6.0f - 4.0f * fraction) * fraction * fraction;

    if (whole & 1) {
        value = -value;
    }

    return value;
}

// ref: FUN_0079e100
Particulates::Particulates(float sizeScale, float boxSize, const char* texture) {
    for (int32_t i = 0; i < 4000; i++) {
        this->m_motes[i].position = { 0.0f, 0.0f, 0.0f };
    }

    this->m_cameraPos = { 0.0f, 0.0f, 0.0f };
    this->m_active = 0;
    this->m_currentDir = { 0.0f, 0.0f, 0.0f };
    this->m_texture = nullptr;
    this->m_sizeScale = sizeScale;
    this->m_count = 4000;
    this->m_boxSize = boxSize;
    this->m_liquidType = 0;
    this->m_currentRate = 0.0f;
    this->m_currentPhase = 0.0f;
    this->m_currentStrength = 0.0f;

    this->SetTexture(texture);
    this->Respawn(2);
    this->NewCurrent();
}

// ref: FUN_0079b340
Particulates::~Particulates() {
    if (this->m_texture) {
        HandleClose(this->m_texture);
    }
}

// ref: FUN_0079dff0
void Particulates::SetTexture(const char* texture) {
    if (this->m_texture) {
        HandleClose(this->m_texture);
    }

    CStatus status;
    this->m_texture = TextureCreate(texture, CGxTexFlags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1), &status, 0);
}

// ref: FUN_0079b360
void Particulates::SetSizeScale(float scale) {
    this->m_sizeScale = scale;
}

// ref: FUN_0079b8e0
void Particulates::Respawn(uint32_t liquidType) {
    float minSize = this->m_sizeScale * 0.5f;
    float maxSize = this->m_sizeScale * 1.5f;
    float half = 0.5f * this->m_boxSize;

    for (uint32_t i = 0; i < this->m_count; i++) {
        Mote& mote = this->m_motes[i];

        float z = RandomOneToTwo();
        float y = RandomOneToTwo();
        float x = RandomOneToTwo();

        mote.position.x = (x - 1.0f) * this->m_boxSize - half;
        mote.position.y = (y - 1.0f) * this->m_boxSize - half;
        mote.position.z = (z - 1.0f) * this->m_boxSize - half;
        mote.size = (RandomOneToTwo() - 1.0f) * (maxSize - minSize) + minSize;
    }

    this->m_liquidType = liquidType;
}

// ref: FUN_0079bcc0
void Particulates::NewCurrent() {
    // Two angles in (-pi, pi): the sign bit of each random word picks the half.
    uint32_t bits = CRandom::uint32(g_rndSeed);
    float value;
    uint32_t mantissa = (bits & 0x7fffff) | 0x3f800000;
    std::memcpy(&value, &mantissa, sizeof(value));
    float pitch = (static_cast<int32_t>(bits) < 0 ? 2.0f - value : value - 2.0f) * 3.1415927410125732f;

    bits = CRandom::uint32(g_rndSeed);
    mantissa = (bits & 0x7fffff) | 0x3f800000;
    std::memcpy(&value, &mantissa, sizeof(value));
    float yaw = (static_cast<int32_t>(bits) < 0 ? 2.0f - value : value - 2.0f) * 3.1415927410125732f;

    float s = std::sin(pitch);
    this->m_currentDir.y = std::sin(yaw) * s;
    this->m_currentDir.x = std::cos(yaw) * s;
    this->m_currentDir.z = std::cos(pitch) * 0.25f;

    if (this->m_currentDir.z < 0.0f) {
        this->m_currentDir.z = -this->m_currentDir.z;
    }

    float inv = 1.0f / std::sqrt(
        this->m_currentDir.x * this->m_currentDir.x
        + this->m_currentDir.y * this->m_currentDir.y
        + this->m_currentDir.z * this->m_currentDir.z);

    this->m_currentDir.x = this->m_currentDir.x * inv;
    this->m_currentDir.y = this->m_currentDir.y * inv;
    this->m_currentDir.z = inv * this->m_currentDir.z;

    this->m_currentPhase = 0.0f;
    this->m_currentRate = ((RandomOneToTwo() - 1.0f) + 1.0f) * 0.012500000186264515f;
    this->m_currentStrength = ((RandomOneToTwo() - 1.0f) + 1.0f) * 0.004999999888241291f;
}

// ref: FUN_0079be50
C3Vector Particulates::Drift(float step) {
    auto rec = g_liquidTypeDB.GetRecord(static_cast<int32_t>(this->m_liquidType));

    // Frozen-only null check: the reference reads the row unguarded.
    int32_t movement = rec ? rec->m_particleMovement : 0;

    if (movement == 0) {
        // A current that swells and fades over half a cycle, then picks a new heading.
        float phase = step + this->m_currentPhase;
        this->m_currentPhase = phase;
        phase = phase * this->m_currentRate;

        if (0.5f < phase) {
            this->NewCurrent();
            phase = 0.0f;
        }

        float strength = PolySin(phase * 6.2831854820251465f) * this->m_currentStrength;

        return {
            this->m_currentDir.x * strength,
            this->m_currentDir.y * strength,
            strength * this->m_currentDir.z,
        };
    }

    if (movement == 1) {
        return { 0.0f, 0.0f, -0.019999999552965164f * step };
    }

    if (movement != 2) {
        return { 0.0f, 0.0f, 0.0f };
    }

    return { 0.0f, 0.0f, 0.019999999552965164f * step };
}

// ref: FUN_0079bf40
void Particulates::Update() {
    if (!this->m_active) {
        return;
    }

    float half = this->m_boxSize * 0.5f;
    const C3Vector& camera = CWorldScene::s_cameraPos;

    C3Vector moved = {
        this->m_cameraPos.x - camera.x,
        this->m_cameraPos.y - camera.y,
        this->m_cameraPos.z - camera.z,
    };

    this->m_cameraPos = camera;

    // A camera that jumped further than the cube is wide starts a fresh cloud.
    if (this->m_boxSize * this->m_boxSize < moved.z * moved.z + moved.y * moved.y + moved.x * moved.x) {
        this->Respawn(this->m_liquidType);
        moved = { 0.0f, 0.0f, 0.0f };
    }

    C3Vector drift = this->Drift(CWorld::GetTickTimeSec());

    for (uint32_t i = 0; i < this->m_count; i++) {
        C3Vector& p = this->m_motes[i].position;

        p.x = p.x + moved.x + drift.x;
        p.y = p.y + drift.y + moved.y;
        p.z = p.z + drift.z + moved.z;

        if (half < p.x) {
            p.x = p.x - this->m_boxSize;
        } else if (p.x < -half) {
            p.x = p.x + this->m_boxSize;
        }

        if (half < p.y) {
            p.y = p.y - this->m_boxSize;
        } else if (p.y < -half) {
            p.y = p.y + this->m_boxSize;
        }

        if (half < p.z) {
            p.z = p.z - this->m_boxSize;
        } else if (p.z < -half) {
            p.z = p.z + this->m_boxSize;
        }
    }
}

// ref: FUN_0079ca70
void Particulates::Render() {
    if (!this->m_active) {
        return;
    }

    CGxTex* gxTex = TextureGetGxTex(this->m_texture, 0, nullptr);

    if (!gxTex) {
        return;
    }

    CGxBuf* vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x18, 0xa68);
    auto vertex = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(vertexBuf));
    CGxBuf* indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, 0xf9c);
    auto index = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(indexBuf));

    // The motes are camera-relative, so only the view's rotation applies; the draw then runs
    // with an identity view and the saved one goes back afterwards.
    C44Matrix view;
    GxXformView(view);

    C44Matrix identity;
    g_theGxDevicePtr->XformSetView(identity);

    auto rec = g_liquidTypeDB.GetRecord(static_cast<int32_t>(this->m_liquidType));

    // Frozen-only null check: the reference reads the row's columns unguarded.
    int32_t slots = rec ? rec->m_particleTexSlots : 0;

    if (slots < 0 || slots > 4) {
        slots = 0;
    }

    uint32_t vertexCount = 0;
    int32_t frame = 8;

    for (uint32_t i = 0; i < this->m_count; i++) {
        const Mote& mote = this->m_motes[i];
        const C3Vector& p = mote.position;

        float vx = view.a0 * p.x + view.b0 * p.y + p.z * view.c0;
        float vy = view.b1 * p.y + view.a1 * p.x + p.z * view.c1;
        float vz = view.a2 * p.x + view.b2 * p.y + p.z * view.c2;

        if (0.0f < vz && vx < vz && -vz < vx && vy < vz && -vz < vy) {
            const float* uv = s_moteUvs[frame];

            for (int32_t corner = 0; corner < 4; corner++) {
                vertex[0] = s_moteCorners[corner][0] * mote.size + vx;
                vertex[1] = s_moteCorners[corner][1] * mote.size + vy;
                vertex[2] = vz;

                uint32_t white = 0xffffffff;
                std::memcpy(&vertex[3], &white, sizeof(white));

                vertex[4] = uv[corner * 2];
                vertex[5] = uv[corner * 2 + 1];
                vertex += 6;
            }

            uint16_t base = static_cast<uint16_t>(vertexCount);
            index[0] = base;
            index[3] = base + 3;
            vertexCount += 4;
            index[1] = base + 1;
            index[2] = base + 2;
            index[4] = base + 2;
            index[5] = base + 1;
            index += 6;

            if (0xa67 < vertexCount) {
                break;
            }
        }

        frame = s_moteFrames[slots][i & 7];
    }

    g_theGxDevicePtr->BufUnlock(indexBuf, 0);
    indexBuf->unk1C = 1;
    g_theGxDevicePtr->BufUnlock(vertexBuf, 0);
    vertexBuf->unk1C = 1;

    GxRsPush();

    GxRsSet(GxRs_Fog, rec ? (rec->m_flags >> 4) & 1 : 0);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Lighting, 0);

    GxPrimVertexPtr(vertexBuf, GxVBF_PCT);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_count = (vertexCount >> 2) * 6;
    batch.m_minIndex = 0;
    batch.m_maxIndex = static_cast<uint16_t>(vertexCount - 1);
    g_theGxDevicePtr->Draw(&batch, 1);

    GxRsPop();

    g_theGxDevicePtr->XformSetView(view);
}
