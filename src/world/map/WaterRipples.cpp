#include "world/map/WaterRipples.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldParam.hpp"
#include "world/CWorldScene.hpp"
#include "world/Shadow.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "console/CVar.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Transform.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/shader/CGxShader.hpp"
#include "util/CStatus.hpp"
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <cmath>
#include <cstring>

bool MapQueryBox(const CAaBox& box, void* object, uint32_t queryMask);

namespace WaterRipples {

HTEXTURE s_textures[2] = { nullptr, nullptr };
TSGrowableArray<Water0Ripple> s_pool;
STORM_EXPLICIT_LIST(Water0Ripple, link) s_live;
uint32_t s_nextSlot = 0x20;
uint32_t s_nextReservedSlot = 0;
CGxShader* s_vertexShader = nullptr;
CGxShader* s_pixelShader = nullptr;
int32_t s_useShaders = 0;

}

// The share of a ripple's life its alpha spends rising, by kind (DAT_00a3fac0).
static const float RIPPLE_RISE_SHARE[2] = { 0.4000000059604645f, 0.4000000059604645f };

// ref: FUN_0079d220
Water0Ripple::Water0Ripple(const Water0Ripple& other) {
    this->position = other.position;
    this->angle = other.angle;
    this->radius = other.radius;
    this->radiusRate = other.radiusRate;
    this->alpha = other.alpha;
    this->alphaPeak = other.alphaPeak;
    this->alphaRise = other.alphaRise;
    this->alphaFall = other.alphaFall;
    this->endTime = other.endTime;
    this->kind = other.kind;
    this->vertices.SetCount(other.vertices.Count());

    for (uint32_t i = 0; i < other.vertices.Count(); i++) {
        this->vertices[i] = other.vertices[i];
    }
}

// ref: FUN_0079c110
Water0Ripple::~Water0Ripple() {
    this->drawLink.Unlink();
    this->link.Unlink();
}

// ref: FUN_0079cf40
void Water0Ripple::Init(const C3Vector& position, float angle, float radius, float alphaPeak, float duration, float radiusRate, int32_t kind) {
    this->kind = kind != 0;
    this->position = position;
    this->angle = angle;
    this->radius = radius;

    float reach = radiusRate * duration + radius;

    this->radiusRate = (reach - radius) / duration;
    this->alpha = 0.0f;
    this->alphaPeak = alphaPeak;
    this->alphaRise = alphaPeak / (RIPPLE_RISE_SHARE[this->kind] * duration);
    this->alphaFall = -(alphaPeak / ((1.0f - RIPPLE_RISE_SHARE[this->kind]) * duration));
    this->endTime = CWorld::GetGameTimeSec() + duration;

    // The water under the ripple's whole reach, as world-space triangles.
    CAaBox box;
    box.b = { position.x - reach, position.y - reach, position.z - 1.0f };
    box.t = { position.x + reach, position.y + reach, position.z + 1.0f };

    this->vertices.SetCount(0);

    CMapObjGroup::s_hitFlags = 0;
    CMapObjGroup::s_hitRecordCount = 0;
    CMapObjGroup::s_hitFacePoolCount = 0;
    CMapObjGroup::s_hitIndexPoolCount = 0;
    CMapObjGroup::s_hitPlacementCount = 0;

    // The reference passes the address of a local byte as the owner; the liquid collectors never
    // read it.
    uint8_t owner;

    if (!MapQueryBox(box, &owner, 0x20000)) {
        return;
    }

    for (uint32_t r = 0; r < CMapObjGroup::s_hitRecordCount; r++) {
        const CMapObjHitRecord& record = CMapObjGroup::s_hitRecords[r];
        uint32_t first = this->vertices.Count();

        this->vertices.SetCount(first + record.faceCount * 3);

        C3Vector* out = &this->vertices[first];
        const uint16_t* tri = record.indices;

        for (uint32_t f = 0; f < record.faceCount; f++, tri += 3, out += 3) {
            out[0] = record.vertices[tri[0]] * *record.placement;
            out[1] = record.vertices[tri[1]] * *record.placement;
            out[2] = record.vertices[tri[2]] * *record.placement;
        }
    }
}

namespace WaterRipples {

// ref: FUN_0079e1a0
void Initialize() {
    s_nextReservedSlot = 0;
    s_nextSlot = 0x20;

    static const char* const s_textureNames[2] = {
        "XTextures\\splash\\splash.blp",
        "XTextures\\splash\\wake.blp",
    };

    CStatus status;

    for (uint32_t i = 0; i < 2; i++) {
        s_textures[i] = TextureCreate(s_textureNames[i], CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 1);
    }

    SetPoolSize(0x80);

    s_useShaders = 0;

    g_theGxDevicePtr->ShaderCreate(&s_vertexShader, GxSh_Vertex, "Shaders\\Vertex", "WaterRipples", 1);
    g_theGxDevicePtr->ShaderCreate(&s_pixelShader, GxSh_Pixel, "Shaders\\Pixel", "WaterRipples", 1);

    if (s_vertexShader && s_vertexShader->Valid() && s_pixelShader && s_pixelShader->Valid()) {
        s_useShaders = 1;
    }
}

// ref: FUN_0079e2d0
void Destroy() {
    for (uint32_t i = 0; i < 2; i++) {
        HandleClose(s_textures[i]);
    }

    s_pool.SetCount(0);

    while (auto ripple = s_live.Head()) {
        ripple->link.Unlink();
    }

    if (s_vertexShader) {
        g_theGxDevicePtr->ShaderDestroy(&s_vertexShader);
    }

    if (s_pixelShader) {
        g_theGxDevicePtr->ShaderDestroy(&s_pixelShader);
    }
}

// ref: FUN_0079e080
void SetPoolSize(uint32_t count) {
    if (count == s_pool.Count()) {
        return;
    }

    s_pool.SetCount(count);
}

// ref: FUN_0079d460
void Add(const C3Vector& position, float angle, float radius, float alphaPeak, float life, float radiusRate, int32_t kind, int32_t reserved) {
    if (!CWorld::s_waterRipples || CWorldParam::s_waterLOD) {
        return;
    }

    float duration = 1.0f;

    if (life <= 0.1666666716337204f) {
        duration = life * 6.0f;
    }

    Spawn(position, angle, radius, alphaPeak, duration, radiusRate, kind, reserved);
}

// ref: FUN_0079d180
void Spawn(const C3Vector& position, float angle, float radius, float alphaPeak, float duration, float radiusRate, int32_t kind, int32_t reserved) {
    uint32_t slot;

    if (reserved) {
        slot = s_nextReservedSlot;
        s_nextReservedSlot++;

        if (s_nextReservedSlot >= 0x20) {
            s_nextReservedSlot = 0;
        }
    } else {
        slot = s_nextSlot;
        s_nextSlot++;

        if (s_nextSlot >= 0x80) {
            s_nextSlot = 0x20;
        }
    }

    // Frozen-only guard: the reference indexes the pool unchecked.
    if (slot >= s_pool.Count()) {
        return;
    }

    Water0Ripple* ripple = &s_pool[slot];
    ripple->Init(position, -angle, radius, alphaPeak, duration, radiusRate, kind);
    s_live.LinkToTail(ripple);
}

// ref: FUN_0079d5e0
void Draw() {
    if (!s_live.Head()) {
        return;
    }

    STORM_EXPLICIT_LIST(Water0Ripple, drawLink) byKind[2];
    uint32_t vertexCount[2] = { 0, 0 };

    float now = CWorld::GetGameTimeSec();
    float dt = CWorld::GetTickTimeSec();

    for (auto ripple = s_live.Head(); ripple; ) {
        auto next = s_live.Next(ripple);
        bool alive = false;

        if (now < ripple->endTime) {
            ripple->radius = ripple->radiusRate * dt + ripple->radius;

            float step = dt;

            if (ripple->alphaRise != 0.0f) {
                float alpha = dt * ripple->alphaRise + ripple->alpha;
                ripple->alpha = alpha;

                if (ripple->alphaPeak < alpha) {
                    step = step - (alpha - ripple->alphaPeak) / ripple->alphaRise;
                    ripple->alpha = ripple->alphaPeak;
                    ripple->alphaRise = 0.0f;
                }
            }

            if (ripple->alphaRise == 0.0f) {
                ripple->alpha = step * ripple->alphaFall + ripple->alpha;
            }

            alive = 0.0f < ripple->alpha;
        }

        if (alive) {
            vertexCount[ripple->kind] += ripple->vertices.Count();
            byKind[ripple->kind].LinkToTail(ripple);
        } else {
            ripple->link.Unlink();
        }

        ripple = next;
    }

    if (!vertexCount[0] && !vertexCount[1]) {
        for (auto& list : byKind) {
            while (auto ripple = list.Head()) {
                ripple->drawLink.Unlink();
            }
        }

        return;
    }

    // The ripples sit on the water's own triangles, lifted off them by the footstep bias.
    float bias = CWorldParam::cvar_footstepBias ? CWorldParam::cvar_footstepBias->GetFloat() * 0.0009765774011611938f : 0.0f;

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Add);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_PolygonOffset, bias);

    // The scene is drawn camera-relative: the world transform takes the camera back out.
    C44Matrix toCamera;
    const C3Vector& camera = CWorldScene::s_cameraPos;
    toCamera.d0 = -camera.x;
    toCamera.d1 = -camera.y;
    toCamera.d2 = -camera.z;

    if (!s_useShaders) {
        g_theGxDevicePtr->XformPush(GxXform_World, toCamera);
    } else {
        g_theGxDevicePtr->RsSet(GxRs_VertexShader, s_vertexShader);
        g_theGxDevicePtr->RsSet(GxRs_PixelShader, s_pixelShader);

        C44Matrix view;
        GxXformView(view);

        // The native projection, as the reference reads it (+0xfc8 at 0x0079da50): the
        // application one has its depth on [-1, 1] and put the ripples at the wrong depth.
        C44Matrix projection;
        g_theGxDevicePtr->XformProjNative(projection);

        // OpenGL's depth runs the other way: its projection's third row is turned round.
        if (g_theGxDevicePtr->m_api == GxApi_OpenGl) {
            projection.c0 = projection.c0 * -1.0f;
            projection.c1 = projection.c1 * -1.0f;
            projection.c2 = projection.c2 * -1.0f;
            projection.c3 = -1.0f * projection.c3;
        }

        C44Matrix mvp = (toCamera * view) * projection;
        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&mvp), 4);

        const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 3, zero, 1);
    }

    C44Matrix texture;
    C44Matrix decal;

    for (int32_t kind = 0; kind < 2; kind++) {
        uint32_t count = vertexCount[kind];

        if (!count) {
            continue;
        }

        CGxTex* gxTex = TextureGetGxTex(s_textures[kind], 0, nullptr);

        if (!gxTex) {
            continue;
        }

        CGxBuf* vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x18, count);
        auto vertex = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(vertexBuf));
        CGxBuf* indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, count);
        auto index = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(indexBuf));

        uint32_t written = 0;

        for (auto ripple = byKind[kind].Head(); ripple; ripple = byKind[kind].Next(ripple)) {
            if (!ripple->vertices.Count()) {
                continue;
            }

            C44Matrix rotation = C44Matrix::RotationAroundZ(ripple->angle);

            CAaBox box;
            box.b = { ripple->position.x - ripple->radius, ripple->position.y - ripple->radius, ripple->position.z - 1.0f };
            box.t = { ripple->radius + ripple->position.x, ripple->radius + ripple->position.y, ripple->position.z + 1.0f };

            DecalBuildTransforms(texture, decal, box, &rotation, 0.5f, 1);

            // White, at the ripple's alpha. The byte order is the same either way round.
            uint32_t a = static_cast<uint32_t>(static_cast<int32_t>(std::nearbyint(ripple->alpha * 255.0f))) & 0xff;
            uint32_t color = (a << 24) | 0xffffff;

            for (uint32_t i = 0; i < ripple->vertices.Count(); i++) {
                const C3Vector& p = ripple->vertices[i];

                vertex[0] = p.x;
                vertex[1] = p.y;
                vertex[2] = p.z;
                std::memcpy(&vertex[3], &color, sizeof(color));

                C3Vector uv = p * texture;
                vertex[4] = uv.x;
                vertex[5] = uv.y;
                vertex += 6;

                *index++ = static_cast<uint16_t>(written);
                written++;
            }
        }

        g_theGxDevicePtr->BufUnlock(indexBuf, 0);
        indexBuf->unk1C = 1;
        g_theGxDevicePtr->BufUnlock(vertexBuf, 0);
        vertexBuf->unk1C = 1;

        if (static_cast<uint16_t>(written)) {
            GxPrimVertexPtr(vertexBuf, GxVBF_PCT);
            g_theGxDevicePtr->PrimIndexPtr(indexBuf);
            g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);

            CGxBatch batch;
            batch.m_primType = GxPrim_Triangles;
            batch.m_start = 0;
            batch.m_count = written & 0xffff;
            batch.m_minIndex = 0;
            batch.m_maxIndex = static_cast<uint16_t>(written - 1);
            g_theGxDevicePtr->Draw(&batch, 1);
        }
    }

    GxRsPop();

    if (!s_useShaders) {
        g_theGxDevicePtr->XformPop(GxXform_World);
    }

    for (auto& list : byKind) {
        while (auto ripple = list.Head()) {
            ripple->drawLink.Unlink();
        }
    }
}

}
