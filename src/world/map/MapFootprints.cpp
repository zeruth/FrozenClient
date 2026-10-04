#include "world/map/MapFootprints.hpp"
#include "db/Db.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "world/CWorld.hpp"
#include "world/Shadow.hpp"
#include "world/WorldFacets.hpp"
#include "util/CStatus.hpp"
#include <common/Handle.hpp>
#include <storm/Array.hpp>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <cmath>

namespace {

// CGxVertexPT0T1: a footprint vertex as it is kept, with its two decal coordinates.
struct FootprintVertex {
    C3Vector position;
    C2Vector tex0;
    C2Vector tex1;
};

// One step (0x34 bytes).
struct Footprint {
    C3Vector position;                              // +0x00
    uint32_t alpha = 0;                             // +0x0c
    uint32_t texture = 0;                           // +0x10
    uint32_t pad14 = 0;
    uint32_t time = 0;                              // +0x18
    TSGrowableArray<FootprintVertex> vertices;      // +0x1c
};

// One texture (0x18 bytes): the steps drawn with it this frame, and their vertex total.
struct FootprintTexture {
    HTEXTURE texture = nullptr;
    TSGrowableArray<uint32_t> steps;
    uint32_t vertexCount = 0;
};

const uint32_t NUM_FOOTPRINTS = 0x240;
const uint32_t NUM_PLAYER_FOOTPRINTS = 0x40;

Footprint s_footprints[NUM_FOOTPRINTS];             // 0x00cf4960
uint32_t s_nextPlayer = 0;                          // 0x00cf4958
uint32_t s_nextOther = NUM_PLAYER_FOOTPRINTS;       // 0x00cf495c
TSGrowableArray<FootprintTexture> s_textures;       // 0x00cfbe60
CFacetList s_facets;                                // 0x00adfd04

// The vertex every footprint streams (format 6): position, the normal straight up, the colour with
// the fade in its alpha, and the two decal coordinates.
struct FootprintStreamVertex {
    C3Vector position;
    C3Vector normal;
    uint32_t color;
    C2Vector tex0;
    C2Vector tex1;
};

} // namespace

void FootprintsInitialize() {
    s_nextOther = NUM_PLAYER_FOOTPRINTS;
    s_nextPlayer = 0;

    // FUN_007a02f0: one slot per id, up to the largest.
    int32_t maxID = 0;

    for (int32_t i = 0; i < g_footprintTexturesDB.GetNumRecords(); i++) {
        auto rec = g_footprintTexturesDB.GetRecordByIndex(i);

        if (maxID < rec->m_ID) {
            maxID = rec->m_ID;
        }
    }

    s_textures.SetCount(static_cast<uint32_t>(maxID + 1));

    for (int32_t i = g_footprintTexturesDB.GetNumRecords(); i != 0;) {
        i--;

        auto rec = g_footprintTexturesDB.GetRecordByIndex(i);
        auto& slot = s_textures[static_cast<uint32_t>(rec->m_ID)];

        if (!slot.texture) {
            CStatus status;
            CGxTexFlags flags(static_cast<EGxTexFilter>(1), 0, 0, 0, 0, 0, 1);
            slot.texture = TextureCreate(rec->m_footstepFilePath, flags, &status, 1);
        }
    }
}

void FootprintsClear() {
    for (auto& footprint : s_footprints) {
        footprint.vertices.SetCount(0);
    }
}

void FootprintsDestroy() {
    for (auto& footprint : s_footprints) {
        footprint.vertices.SetCount(0);
    }

    for (uint32_t i = 0; i < s_textures.Count(); i++) {
        if (s_textures[i].texture) {
            HandleClose(s_textures[i].texture);
            s_textures[i].texture = nullptr;
        }
    }
}

void FootprintAdd(uint32_t texture, const C2Vector& size, const C3Vector& position, float facing, int32_t mirror,
                  int32_t terrainType, int32_t activePlayer) {
    if (s_textures.Count() <= texture) {
        return;
    }

    auto terrain = g_terrainTypeDB.GetRecord(terrainType);

    if (!terrain || !(terrain->m_flags & 0x1)) {
        return;
    }

    // FUN_0079f6f0: a box two thirds of the longer side round the step.
    float half = (size.y < size.x ? size.x : size.y) * 0.66f;
    CAaBox box;
    box.b = { position.x - half, position.y - half, position.z - half };
    box.t = { position.x + half, position.y + half, position.z + half };

    if (!CWorld::QueryFacets(box, s_facets, 0x200122, nullptr)) {
        return;
    }

    Footprint* footprint;

    if (!activePlayer) {
        footprint = &s_footprints[s_nextOther++];

        if (NUM_FOOTPRINTS - 1 < s_nextOther) {
            s_nextOther = NUM_PLAYER_FOOTPRINTS;
        }
    } else {
        footprint = &s_footprints[s_nextPlayer++];

        if (NUM_PLAYER_FOOTPRINTS - 1 < s_nextPlayer) {
            s_nextPlayer = 0;
        }
    }

    footprint->position = position;
    footprint->time = CWorld::GetCurTimeMs();
    footprint->texture = texture;

    C44Matrix stage0;
    C44Matrix stage1;
    C44Matrix shape;

    // FUN_0079f760: the decal mirrored for the other foot, stretched to the footprint's
    // proportions and turned to the step's facing.
    if (mirror) {
        C3Vector flip = { -1.0f, 1.0f, 1.0f };
        shape.Scale(flip);
    }

    if (size.x < size.y) {
        C3Vector stretch = { 1.0f, size.y / size.x, 1.0f };
        shape.Scale(stretch);
    } else if (size.y < size.x) {
        C3Vector stretch = { size.x / size.y, 1.0f, 1.0f };
        shape.Scale(stretch);
    }

    shape.RotateAroundZ(-facing);

    DecalBuildTransforms(stage0, stage1, box, &shape, 0.5f, 1);

    uint32_t count = s_facets.facets.Count();
    footprint->vertices.SetCount(count * 3);

    auto out = footprint->vertices.Ptr();

    for (uint32_t i = 0; i < count; i++) {
        const auto& facet = s_facets.facets[i];

        for (const auto& vertex : facet.vertices) {
            out->position = vertex;

            C3Vector t0 = vertex * stage0;
            out->tex0 = { t0.x, t0.y };

            C3Vector t1 = vertex * stage1;
            out->tex1 = { t1.x, t1.y };

            out++;
        }
    }
}

void FootprintsRender() {
    if (!(CWorld::s_enables & CWorld::Enable_Footprints)) {
        return;
    }

    uint32_t now = CWorld::GetCurTimeMs();

    // Every step fades over six seconds and joins its texture's batch; a faded one is dropped.
    for (uint32_t i = 0; i < NUM_FOOTPRINTS; i++) {
        auto& footprint = s_footprints[i];

        if (!footprint.vertices.Count()) {
            continue;
        }

        float left = 1.0f - static_cast<float>(now - footprint.time) * 0.00016666666f;

        if (0.0f <= left) {
            int32_t alpha = static_cast<int32_t>(std::lround(left * 255.0f - 0.5f));
            footprint.alpha = alpha < 0x7f ? static_cast<uint32_t>(alpha) : 0x7f;

            auto& texture = s_textures[footprint.texture];
            *texture.steps.New() = i;
            texture.vertexCount += footprint.vertices.Count();
        } else {
            footprint.vertices.SetCount(0);
        }
    }

    GxRsPush();
    GxRsSet(GxRs_Texture1, ShadowModGxTex());
    GxRsSet(GxRs_BlendingMode, 2);
    GxRsSet(GxRs_AlphaRef, CGxDevice::s_alphaRef[2]);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_PolygonOffset, CWorld::GetFarClip() * 0.0039063101f);

    // The vertices are in world space; the view is taken from the camera.
    const C3Vector& camera = CWorld::GetCameraPos();
    C44Matrix toCamera;
    C3Vector back = { -camera.x, -camera.y, -camera.z };
    toCamera.Translate(back);
    g_theGxDevicePtr->XformPush(GxXform_World, toCamera);

    for (uint32_t t = 0; t < s_textures.Count(); t++) {
        auto& texture = s_textures[t];
        auto gxTex = texture.vertexCount ? TextureGetGxTex(texture.texture, 0, nullptr) : nullptr;

        if (gxTex) {
            auto vertexBuf = GxBufStream(GxPoolTarget_Vertex, sizeof(FootprintStreamVertex), texture.vertexCount);
            auto indexBuf = GxBufStream(GxPoolTarget_Index, 2, texture.vertexCount);
            auto out = reinterpret_cast<FootprintStreamVertex*>(GxBufLock(vertexBuf));
            auto indices = reinterpret_cast<uint16_t*>(GxBufLock(indexBuf));

            uint32_t index = 0;

            if (out && indices) {
                for (uint32_t s = 0; s < texture.steps.Count(); s++) {
                    const auto& footprint = s_footprints[texture.steps[s]];
                    uint32_t color = (footprint.alpha << 24) | 0xffffff;

                    for (uint32_t v = 0; v < footprint.vertices.Count(); v++) {
                        const auto& vertex = footprint.vertices[v];

                        out->position = vertex.position;
                        out->normal = { 0.0f, 0.0f, 1.0f };
                        out->color = color;
                        out->tex0 = vertex.tex0;
                        out->tex1 = vertex.tex1;
                        out++;

                        *indices++ = static_cast<uint16_t>(index++);
                    }
                }
            }

            GxBufUnlock(vertexBuf, 0);
            GxBufUnlock(indexBuf, 0);
            GxPrimVertexPtr(vertexBuf, GxVBF_PNCT2);
            g_theGxDevicePtr->PrimIndexPtr(indexBuf);
            GxRsSet(GxRs_Texture0, gxTex);

            CGxBatch batch;
            batch.m_primType = GxPrim_Triangles;
            batch.m_start = 0;
            batch.m_count = texture.vertexCount;
            batch.m_minIndex = 0;
            batch.m_maxIndex = static_cast<uint16_t>(texture.vertexCount - 1);
            GxDraw(&batch, 1);
        }

        texture.steps.SetCount(0);
        texture.vertexCount = 0;
    }

    GxXformPop(GxXform_World);
    GxRsPop();
}
