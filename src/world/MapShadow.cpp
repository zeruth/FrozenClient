#include "world/MapShadow.hpp"
#include "console/CVar.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/RenderTarget.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "gx/shader/CShaderEffectManager.hpp"
#include "gx/texture/CGxTex.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Types.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "world/CWFrustum.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "world/ShadowMap.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapStaticEntity.hpp"
#include "world/map/MapOcclusion.hpp"
#include <storm/Memory.hpp>
#include <tempest/Intersect.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Plane.hpp>
#include <tempest/Rect.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// The map's half of the shadow map (reference MapShadow.cpp, 0x007ba840..0x007bd450): the four
// callbacks the shadow map draws through -- the sampling matrix, a cascade's view, collecting what
// casts into it, and drawing that -- plus the per-frame driver and the plane the map object shaders
// read. The maps themselves, the cascades and the binds are ShadowMap.cpp.

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

// ------------------------------------------------------------------------------------------------
// What the map puts into the shadow maps
// ------------------------------------------------------------------------------------------------

// The far side of the map's coordinates, and the end of the last cell inside it.
static const float MAP_HALF_EXTENT = 17066.666015625f;      // DAT_009e2acc
static const float MAP_EXTENT = 34133.33203125f;            // DAT_009e2ac8
static const float MAP_EXTENT_LAST = 34132.33203125f;       // DAT_00a400f8

// How far back along the light every view's eye sits, and the depth range.
static const float LIGHT_BACK = 2000.0f;                    // DAT_00a400e4
static const float LIGHT_FAR = 4000.0f;                     // DAT_00a3fa3c

// Each cascade's casters: the map object groups with their placements, and the model batches in
// the two lists CM2Model::CollectShadowCasters fills (DAT_00d25320, 0x24 bytes per cascade).
struct MapShadowCasters {
    uint32_t mapObjCount = 0;
    CMapObjGroup** groups = nullptr;
    const C44Matrix** placements = nullptr;
    M2ShadowCasterList models[2];
};

static MapShadowCasters s_casters[3];
static const uint32_t MAX_MAP_OBJ_CASTERS = 0x800;
static const uint32_t MAX_MODEL_CASTERS = 0x2000;

// Bumped by every collect, never zero, so a stamp of zero always means "not this frame"
// (DAT_00d25300).
static uint8_t s_collectFrame;

// The most model casters either list has held (DAT_00d25390, DAT_00d2538c).
static uint32_t s_maxCasters[2];

// shadowCull, shadowScissor and shadowInstancing (DAT_00d25308, DAT_00d25310, DAT_00d25314).
static CVar* s_shadowCull;
static CVar* s_shadowScissor;
static CVar* s_shadowInstancing;

float g_mapShadowHeight = 0.0f;

// ref: FUN_007ba840
// Empty every cascade's lists, allocating them the first time.
static void MapShadowResetCasters() {
    for (auto& casters : s_casters) {
        casters.mapObjCount = 0;
        casters.models[0].count = 0;
        casters.models[0].capacity = MAX_MODEL_CASTERS;
        casters.models[1].count = 0;
        casters.models[1].capacity = MAX_MODEL_CASTERS;

        if (!casters.groups) {
            casters.groups = static_cast<CMapObjGroup**>(SMemAlloc(MAX_MAP_OBJ_CASTERS * sizeof(CMapObjGroup*), __FILE__, __LINE__, 0));
        }

        if (!casters.placements) {
            casters.placements = static_cast<const C44Matrix**>(SMemAlloc(MAX_MAP_OBJ_CASTERS * sizeof(C44Matrix*), __FILE__, __LINE__, 0));
        }

        if (!casters.models[0].data) {
            casters.models[0].data = static_cast<M2ShadowCaster*>(SMemAlloc(MAX_MODEL_CASTERS * sizeof(M2ShadowCaster), __FILE__, __LINE__, 0));
        }

        if (!casters.models[1].data) {
            casters.models[1].data = static_cast<M2ShadowCaster*>(SMemAlloc(MAX_MODEL_CASTERS * sizeof(M2ShadowCaster), __FILE__, __LINE__, 0));
        }
    }
}

// ref: FUN_007ba8f0
// The size limits on what casts into a cascade: nothing smaller than a quarter yard, two yards or
// ten, by cascade, and nothing above ten thousand -- except the main map's unit-only pass, which
// takes nothing above 25.
static void MapShadowRadiusLimits(const ShadowView& view, int32_t index, float* minRadius, float* maxRadius) {
    *maxRadius = 10000.0f;

    if (index == 0) {
        if ((view.mask[0] & 0xC) == 4) {
            *maxRadius = 25.0f;
        }

        *minRadius = 0.25f;
        return;
    }

    if (index > 0) {
        *minRadius = index < 2 ? 2.0f : 10.0f;
        return;
    }

    *minRadius = 0.25f;
}

// ref: FUN_007baba0
// How many of the model's bones face the camera (billboarded leaves and the like): those cast
// with the mask's 4 bit, everything else with its 8.
static uint16_t MapShadowBillboardBones(CM2Model* model) {
    return model->m_loaded ? model->m_shared->uint198 : 0;
}

// ref: FUN_007babc0
// Whether a doodad is further from the camera than its detail band draws.
static int32_t MapShadowBeyondDetail(const C3Vector& position, uint8_t detailLevel) {
    const C3Vector& camera = CWorldScene::s_cameraPos;
    float dx = position.x - camera.x;
    float dy = position.y - camera.y;
    float dz = position.z - camera.z;

    return CWorld::GetDetailBands().fadeStartSq[detailLevel] < dz * dz + dy * dy + dx * dx;
}

static bool BoxesOverlap(const CAaBox& a, const CAaBox& b) {
    return a.b.x <= b.t.x && a.b.y <= b.t.y && a.b.z <= b.t.z
        && b.b.x <= a.t.x && b.b.y <= a.t.y && b.b.z <= a.t.z;
}

// A box the cascade can see and its inner cascade does not already cover.
static bool MapShadowCascadeSees(const ShadowView& view, int32_t index, const CAaBox& box) {
    if (!AaBoxVsPlanes6(view.cullFrustum[index].planes, box)) {
        return false;
    }

    return !view.behind[index] || AaBoxBehindPlanes6(view.behindFrustum[index].planes, box) != 3;
}

// ref: FUN_007bac10
// A map's sampling matrix: from camera view space, through the light's view and the ortho box,
// with column 2 replaced by the light-view depth less a bias that grows with the cascade (and by
// half a yard more with hardware PCF).
static void MapShadowBuildMatrix(const C3Vector& center, float extent, C44Matrix& out, const C3Vector& up, int32_t index) {
    if (extent <= 0.0f) {
        return;
    }

    const C3Vector& camera = CWorld::GetCameraPos();
    const C3Vector& light = g_shadowMapLightDir;

    C3Vector eye = {
        (center.x - light.x * LIGHT_BACK) - camera.x,
        (center.y - light.y * LIGHT_BACK) - camera.y,
        (center.z - light.z * LIGHT_BACK) - camera.z
    };

    C3Vector target = { center.x - camera.x, center.y - camera.y, center.z - camera.z };

    C44Matrix view;
    GxXformView(view);
    C44Matrix inverseView = view.Inverse(view.Determinant());

    C44Matrix projection;
    projection.Identity();
    GxuXformCreateOrtho(-extent, extent, -extent, extent, 1.0f, LIGHT_FAR, projection);

    C44Matrix lightView;
    MatrixLookAtLH(eye, target, up, lightView);

    out = inverseView * lightView * projection;

    C44Matrix depth = inverseView * lightView;
    out.a2 = depth.a2;
    out.b2 = depth.b2;
    out.c2 = depth.c2;
    out.d2 = depth.d2;

    float pcf = CShaderEffect::s_usePcfFiltering ? 0.5f : 0.0f;
    float factor;

    switch (index) {
    case 0:
        factor = 2.0f;
        break;
    case 1:
        factor = 4.0f;
        break;
    case 2:
        factor = 8.0f;
        break;
    default:
        factor = 0.5f;
        break;
    }

    out.d2 = depth.d2 - (factor * 0.2f + pcf);
}

// ref: FUN_007bafd0
// A cascade's view: the ortho box around its centre seen from up the light, its frustum turned
// inside out (the ortho's handedness is the opposite of SetCorners'), and -- unless this is a
// progressive tile -- a cull frustum narrowed to what of it the camera can actually see, with the
// matching scissor rectangle.
static void MapShadowSetupView(CWFrustum& frustum, C44Matrix& projection, const C3Vector& up, ShadowView& view, int32_t index) {
    const C3Vector& center = view.center[index];
    const C3Vector& light = g_shadowMapLightDir;

    C3Vector eye = {
        center.x - light.x * LIGHT_BACK,
        center.y - light.y * LIGHT_BACK,
        center.z - light.z * LIGHT_BACK
    };

    GxuXformCreateOrtho(view.ortho[index][0], view.ortho[index][1], view.ortho[index][2], view.ortho[index][3], 1.0f, LIGHT_FAR, projection);

    C44Matrix lightView;
    MatrixLookAtLH(eye, center, up, lightView);

    C3Vector corners[8];
    FrustumCorners(lightView, projection, corners);
    frustum.SetCorners(corners);
    frustum.NegatePlanes();
    frustum.MirrorCorners();

    float* scissor = view.scissor[index];
    scissor[0] = -1.0f;
    scissor[1] = -1.0f;
    scissor[2] = 1.0f;
    scissor[3] = 1.0f;

    view.cullFrustum[index] = frustum;

    if (view.mask[index] == 8 || !s_shadowCull || !s_shadowCull->m_intValue) {
        return;
    }

    // The camera's frustum in the light's clip space, boxed and clamped to it.
    CWFrustum camera = CWorldScene::s_clipFrustum;
    camera.Transform(lightView * projection);

    CAaBox box;
    BoundsFromPoints(box, camera.corners, 8);

    box.b.x = std::min(std::max(box.b.x, -1.0f), 1.0f);
    box.b.y = std::min(std::max(box.b.y, -1.0f), 1.0f);
    box.b.z = std::min(std::max(box.b.z, -1.0f), 1.0f);
    box.t.x = std::min(std::max(box.t.x, -1.0f), 1.0f);
    box.t.y = std::min(std::max(box.t.y, -1.0f), 1.0f);
    box.t.z = std::min(std::max(box.t.z, -1.0f), 1.0f);

    if (!(box.b.x < box.t.x) || !(box.b.y < box.t.y) || !(box.b.z < box.t.z)) {
        return;
    }

    const float* ortho = view.ortho[index];

    C44Matrix narrow;
    narrow.Identity();
    GxuXformCreateOrtho(-(ortho[0] * box.b.x), ortho[1] * box.t.x, -(ortho[2] * box.b.y), box.t.y * ortho[3], 1.0f, LIGHT_FAR, narrow);

    FrustumCorners(lightView, narrow, corners);
    view.cullFrustum[index].SetCorners(corners);
    view.cullFrustum[index].NegatePlanes();
    view.cullFrustum[index].MirrorCorners();

    scissor[1] = box.b.x;
    scissor[3] = box.t.x;
    scissor[0] = -box.t.y;
    scissor[2] = -box.b.y;
}

// ref: FUN_007bb460
// The chunk range a box covers: [0] and [2] along the map's first axis (from x), [1] and [3]
// along its second (from y), each a chunk index counted from the far edge.
static void MapShadowCellRange(const CAaBox& box, int32_t* range) {
    float fromTopX = -(box.t.x - MAP_HALF_EXTENT);
    float fromTopY = -(box.t.y - MAP_HALF_EXTENT);
    float fromBottomX = -(box.b.x - MAP_HALF_EXTENT);
    float fromBottomY = -(box.b.y - MAP_HALF_EXTENT);

    if (fromTopY < 0.0f) {
        fromTopY = 0.0f;
    }

    if (fromTopX < 0.0f) {
        fromTopX = 0.0f;
    }

    if (MAP_EXTENT <= fromBottomY) {
        fromBottomY = MAP_EXTENT_LAST;
    }

    if (MAP_EXTENT <= fromBottomX) {
        fromBottomX = MAP_EXTENT_LAST;
    }

    // 0.24 is one over an eighth of a chunk: the cell index, shifted down to the chunk's.
    range[0] = static_cast<int32_t>(lrintf(fromTopX * 0.24f - 0.5f)) >> 3;
    range[1] = static_cast<int32_t>(lrintf(fromTopY * 0.24f - 0.5f)) >> 3;
    range[2] = static_cast<int32_t>(lrintf(fromBottomX * 0.24f - 0.5f)) >> 3;
    range[3] = static_cast<int32_t>(lrintf(fromBottomY * 0.24f - 0.5f)) >> 3;
}

static CMapChunk* MapShadowChunkAt(int32_t row, int32_t col) {
    CMapArea* area = CMap::s_areaGrid[((row >> 4) & 0x3F) * 64 + ((col >> 4) & 0x3F)];

    if (!area || area->m_asyncObject) {
        return nullptr;
    }

    return area->m_chunks[(row & 0xF) * 16 + (col & 0xF)];
}

// ref: FUN_007bb9d0
// Units and the like: which cascades from `first` to `last` each entity casts into.
static void MapShadowCollectEntities(ShadowView& view, int32_t first, int32_t last) {
    float maxRadius[3] = { 10000.0f, 10000.0f, 10000.0f };
    const float minRadius[3] = { 0.25f, 2.0f, 10.0f };

    if ((view.mask[0] & 0xC) == 4) {
        maxRadius[0] = 25.0f;
    }

    for (auto object = CMap::s_entityList.Head(); object; object = CMap::s_entityList.Next(object)) {
        auto entity = static_cast<CMapStaticEntity*>(object);

        if ((entity->m_flags7c & 0x4) || !entity->m_model) {
            continue;
        }

        int32_t unit = (entity->m_type & CMapBaseObj::Type_Entity) && !(entity->m_flags7c & 0x2000);
        uint16_t billboard = MapShadowBillboardBones(entity->m_model);
        float radius = entity->m_sphere.r;

        for (int32_t i = first; i <= last; i++) {
            uint32_t mask = view.mask[i];
            uint32_t flags = entity->m_flags;
            bool heightCheck;

            if (!(mask & 2)) {
                if (!(flags & CMapBaseObj::Flag_Interior)) {
                    heightCheck = false;
                } else if (entity->m_flags7c & 0x1000) {
                    heightCheck = true;
                } else {
                    continue;
                }
            } else {
                if (!unit || (flags & CMapBaseObj::Flag_Exterior)) {
                    continue;
                }

                heightCheck = true;
            }

            if (heightCheck && !(entity->m_position.z <= g_mapShadowHeight)) {
                continue;
            }

            uint32_t take = unit ? (mask & 1) : billboard ? (mask & 4) : (mask & 8);

            if (!take || !BoxesOverlap(entity->m_bounds, view.bounds[i])) {
                continue;
            }

            if (!(minRadius[i] <= radius)) {
                continue;
            }

            if (!(flags & 0x20000) && !(radius <= maxRadius[i])) {
                continue;
            }

            if (MapShadowCascadeSees(view, i, entity->m_bounds)) {
                entity->m_model->CollectShadowCasters(s_casters[i].models);
            }
        }
    }

}

// Every group of a placed building, with its index into the root.
template <class Fn>
static void ForEachDefGroup(CMapObjDef* def, Fn fn) {
    for (uint32_t j = 0; j < def->m_defGroups.Count(); j++) {
        CMapObjDefGroup* defGroup = def->m_defGroups[j];
        CMapObjGroup* group = def->m_mapObj ? def->m_mapObj->GetGroup(j, 0) : nullptr;

        if (!defGroup || !group || !(group->m_flags & 0x48)) {
            continue;
        }

        if (!fn(defGroup, group)) {
            break;
        }
    }
}

static bool MapShadowAddMapObj(int32_t index, CMapObjGroup* group, CMapObjDef* def) {
    auto& casters = s_casters[index];

    if (casters.mapObjCount >= MAX_MAP_OBJ_CASTERS) {
        return false;
    }

    casters.placements[casters.mapObjCount] = &def->m_placement;
    casters.groups[casters.mapObjCount] = group;
    casters.mapObjCount++;

    return true;
}

// ref: FUN_007bc490
// The buildings and the doodads inside them, for every cascade at once (quality 5).
static void MapShadowCollectMapObjsWhole(ShadowView& view, int32_t first, int32_t last) {
    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        if (def->m_flags & 0x20) {
            continue;
        }

        if (!BoxesOverlap(def->m_bounds, view.bounds[last]) || !AaBoxVsPlanes6(view.cullFrustum[last].planes, def->m_bounds)) {
            continue;
        }

        ForEachDefGroup(def, [&](CMapObjDefGroup* defGroup, CMapObjGroup* group) {
            if (!view.bounds[last].Intersects(defGroup->m_bounds) || !AaBoxVsPlanes6(view.cullFrustum[last].planes, defGroup->m_bounds)) {
                return true;
            }

            for (int32_t k = first; k <= last; k++) {
                if (view.bounds[k].Intersects(defGroup->m_bounds) && MapShadowCascadeSees(view, k, defGroup->m_bounds)) {
                    if (!MapShadowAddMapObj(k, group, def)) {
                        break;
                    }
                }
            }

            for (auto link = defGroup->m_doodadDefLinkList.Head(); link; link = defGroup->m_doodadDefLinkList.Next(link)) {
                auto doodad = static_cast<CMapStaticEntity*>(link->owner);

                if (!doodad->m_model || !(doodad->m_flags & 0x80)) {
                    continue;
                }

                for (int32_t k = first; k <= last; k++) {
                    if (doodad->m_shadowFrame[k] == s_collectFrame) {
                        continue;
                    }

                    doodad->m_shadowFrame[k] = s_collectFrame;

                    if (MapShadowBeyondDetail(doodad->m_sphere.c, doodad->m_detailLevel)) {
                        continue;
                    }

                    if (view.bounds[k].Intersects(doodad->m_bounds) && MapShadowCascadeSees(view, k, doodad->m_bounds)) {
                        doodad->m_model->CollectShadowCasters(s_casters[k].models);
                    }
                }
            }

            return true;
        });
    }
}

// ref: FUN_007bc890
// The buildings and the doodads inside them, for one cascade. A building flagged 0x400 only casts
// into a pass that takes the 4 bit, and then without the size limit.
static void MapShadowCollectMapObjs(ShadowView& view, int32_t index) {
    float minRadius;
    float maxRadius;
    MapShadowRadiusLimits(view, index, &minRadius, &maxRadius);

    uint32_t mask = view.mask[index];

    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        if (def->m_flags & 0x20) {
            continue;
        }

        float radiusLimit = maxRadius;
        int32_t onlyBillboardMask = 0;
        uint32_t billboardOk = 0;
        uint32_t normalOk;

        if (!(def->m_flags & 0x400)) {
            normalOk = (mask & 8) != 0;
            billboardOk = (mask & 4) != 0;
        } else {
            if (!(mask & 4)) {
                continue;
            }

            radiusLimit = 100000.0f;
            billboardOk = 1;
            normalOk = 1;
            onlyBillboardMask = 1;
        }

        if (!view.bounds[index].Intersects(def->m_bounds) || !MapShadowCascadeSees(view, index, def->m_bounds)) {
            continue;
        }

        ForEachDefGroup(def, [&](CMapObjDefGroup* defGroup, CMapObjGroup* group) {
            if (!view.bounds[index].Intersects(defGroup->m_bounds) || !MapShadowCascadeSees(view, index, defGroup->m_bounds)) {
                return true;
            }

            if (onlyBillboardMask ? (mask & 4) : (mask & 8)) {
                if (!MapShadowAddMapObj(index, group, def)) {
                    return false;
                }
            }

            for (auto link = defGroup->m_doodadDefLinkList.Head(); link; link = defGroup->m_doodadDefLinkList.Next(link)) {
                auto doodad = static_cast<CMapStaticEntity*>(link->owner);

                if (doodad->m_shadowFrame[index] == s_collectFrame) {
                    continue;
                }

                doodad->m_shadowFrame[index] = s_collectFrame;

                if (!doodad->m_model || !(doodad->m_flags & 0x80)) {
                    continue;
                }

                if (MapShadowBeyondDetail(doodad->m_sphere.c, doodad->m_detailLevel) || !(doodad->m_sphere.r <= radiusLimit)) {
                    continue;
                }

                uint32_t ok = MapShadowBillboardBones(doodad->m_model) ? billboardOk : normalOk;

                if (ok && view.bounds[index].Intersects(doodad->m_bounds) && MapShadowCascadeSees(view, index, doodad->m_bounds)) {
                    doodad->m_model->CollectShadowCasters(s_casters[index].models);
                }
            }

            return true;
        });
    }
}

// ref: FUN_007bcc00
// The doodads standing on open ground, for every cascade at once (quality 5): the chunks under the
// last cascade that are in its frustum and not hidden by an occluder.
static void MapShadowCollectChunksWhole(ShadowView& view, int32_t first, int32_t last) {
    int32_t range[4];
    MapShadowCellRange(view.bounds[last], range);

    for (int32_t row = range[0]; row <= range[2]; row++) {
        for (int32_t col = range[1]; col <= range[3]; col++) {
            CMapChunk* chunk = MapShadowChunkAt(row, col);

            if (!chunk || !BoxesOverlap(chunk->m_bounds, view.bounds[last])) {
                continue;
            }

            if (!AaBoxVsPlanes6(view.cullFrustum[last].planes, chunk->m_bounds)) {
                continue;
            }

            CAaSphere sphere = { chunk->m_center, chunk->m_radius };

            if (CWorldScene::SphereOccludedByVolumes(sphere)) {
                continue;
            }

            for (auto link = chunk->m_entityLinkList.Head(); link; link = chunk->m_entityLinkList.Next(link)) {
                auto doodad = static_cast<CMapStaticEntity*>(link->owner);

                if (!doodad->m_model || !(doodad->m_flags & 0x80)) {
                    continue;
                }

                uint16_t billboard = MapShadowBillboardBones(doodad->m_model);

                for (int32_t k = first; k <= last; k++) {
                    if (doodad->m_shadowFrame[k] == s_collectFrame) {
                        continue;
                    }

                    doodad->m_shadowFrame[k] = s_collectFrame;

                    if (MapShadowBeyondDetail(doodad->m_sphere.c, doodad->m_detailLevel)) {
                        continue;
                    }

                    uint32_t take = billboard ? (view.mask[k] & 4) : (view.mask[k] & 8);

                    if (take && view.bounds[k].Intersects(doodad->m_bounds) && MapShadowCascadeSees(view, k, doodad->m_bounds)) {
                        doodad->m_model->CollectShadowCasters(s_casters[k].models);
                    }
                }
            }
        }
    }
}

// ref: FUN_007bcf20
// The doodads standing on open ground, for one cascade.
static void MapShadowCollectChunks(ShadowView& view, int32_t index) {
    float minRadius;
    float maxRadius;
    MapShadowRadiusLimits(view, index, &minRadius, &maxRadius);

    int32_t range[4];
    MapShadowCellRange(view.bounds[index], range);

    for (int32_t row = range[0]; row <= range[2]; row++) {
        for (int32_t col = range[1]; col <= range[3]; col++) {
            CMapChunk* chunk = MapShadowChunkAt(row, col);

            if (!chunk || !BoxesOverlap(chunk->m_bounds, view.bounds[index])) {
                continue;
            }

            if (!MapShadowCascadeSees(view, index, chunk->m_bounds)) {
                continue;
            }

            for (auto link = chunk->m_entityLinkList.Head(); link; link = chunk->m_entityLinkList.Next(link)) {
                auto doodad = static_cast<CMapStaticEntity*>(link->owner);

                if (doodad->m_shadowFrame[index] == s_collectFrame) {
                    continue;
                }

                doodad->m_shadowFrame[index] = s_collectFrame;

                if (!doodad->m_model || !(doodad->m_flags & 0x80)) {
                    continue;
                }

                if (MapShadowBeyondDetail(doodad->m_sphere.c, doodad->m_detailLevel) || !(doodad->m_sphere.r <= maxRadius)) {
                    continue;
                }

                uint32_t take = MapShadowBillboardBones(doodad->m_model) ? (view.mask[index] & 4) : (view.mask[index] & 8);

                if (take && view.bounds[index].Intersects(doodad->m_bounds) && MapShadowCascadeSees(view, index, doodad->m_bounds)) {
                    doodad->m_model->CollectShadowCasters(s_casters[index].models);
                }
            }
        }
    }
}

// ref: FUN_007bd200
// Everything that casts into the views the shadow map set up.
static int32_t MapShadowCollect(ShadowView& view, int32_t frame) {
    (void)frame;

    MapShadowResetCasters();

    s_collectFrame++;

    if (s_collectFrame == 0) {
        s_collectFrame = 1;
    }

    uint32_t masks = 0;

    for (int32_t i = 0; i <= view.last; i++) {
        masks |= view.mask[i];
    }

    MapShadowCollectEntities(view, 0, view.last);

    if (!view.whole) {
        for (int32_t i = 0; i <= view.last; i++) {
            if (masks & 0xC) {
                MapOcclusion::ClearVolumes();
                MapShadowCollectMapObjs(view, i);
                MapShadowCollectChunks(view, i);
            }
        }
    } else if (masks & 0xC) {
        // The occluders, built from the middle of the last cascade's near face.
        const C3Vector* corners = view.frustum[view.last].corners;
        C3Vector center = {
            (corners[2].x + corners[1].x + corners[0].x + corners[3].x) * 0.25f,
            (corners[2].y + corners[1].y + corners[3].y + corners[0].y) * 0.25f,
            0.25f * (corners[2].z + corners[1].z + corners[3].z + corners[0].z)
        };

        MapOcclusion::BuildVolumes(center, corners, 1);

        MapShadowCollectMapObjsWhole(view, 0, view.last);
        MapShadowCollectChunksWhole(view, 0, view.last);
    }

    return 1;
}

// ref: FUN_007bbc50
// Draw one view's casters into its map: an empty view is just cleared; otherwise the light's view
// and the cascade's projection go in, the map is cleared to white inside the scissor, and the
// buildings and then the model batches are drawn with the depth-writing ShadowMapRenderSL effect.
static int32_t MapShadowRenderView(ShadowView& view, int32_t index, CGxTex* color, CGxTex* depth, const C3Vector& up) {
    auto& casters = s_casters[index];

    if (casters.mapObjCount == 0 && casters.models[0].count == 0 && casters.models[1].count == 0) {
        MapShadowClearTarget(color, depth, view.viewport[index]);
        return 1;
    }

    if (depth) {
        GxRenderTargetSet(GxBuffers_Depth, depth, 0);
    }

    GxRsPush();
    GxRsSet(GxRs_Culling, 0);
    CShaderEffect::SetTexMtx_Identity(0);

    CGxTex* savedColor = nullptr;
    GxRenderTargetGet(GxBuffers_Color, savedColor);

    GxRsSet(GxRs_Fog, 0);

    CShaderEffect* effect = CShaderEffectManager::GetEffect("ShadowMapRenderSL");

    const C3Vector& camera = CWorld::GetCameraPos();
    const C3Vector& center = view.center[index];
    const C3Vector& light = g_shadowMapLightDir;

    C3Vector eye = {
        (center.x - light.x * LIGHT_BACK) - camera.x,
        (center.y - light.y * LIGHT_BACK) - camera.y,
        (center.z - light.z * LIGHT_BACK) - camera.z
    };

    C3Vector target = { center.x - camera.x, center.y - camera.y, center.z - camera.z };

    C44Matrix toCamera;
    toCamera.Identity();
    C3Vector back = { -camera.x, -camera.y, -camera.z };
    toCamera.Translate(back);

    C44Matrix savedView;
    GxXformView(savedView);

    float savedViewport[6];
    GxXformViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3], savedViewport[4], savedViewport[5]);

    const float* viewport = view.viewport[index];
    GxXformSetViewport(viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f);

    C44Matrix savedProjection;
    GxXformProjection(savedProjection);

    C44Matrix inverseView = savedView.Inverse(savedView.Determinant());

    if (effect) {
        effect->SetCurrent();
    }

    // The caster pixel shader writes the light depth times c0.w; the light matrices read it back
    // with the same scale.
    float depthScale[4] = { 0.0f, 0.0f, 0.0f, 0.00025000001f };
    ShadowMapSetDepthScale(depthScale[3]);
    GxShaderConstantsSet(GxSh_Pixel, 0, depthScale, 1);

    g_shadowMapCasterLevel = ShadowMapGetShaderLevel();

    GxXformSetProjection(view.projection[index]);
    CShaderEffect::UpdateProjMatrix();

    GxRsSet(GxRs_ScissorTest, 0);
    GxRenderTargetSet(GxBuffers_Color, color, 0);

    CImVector white = { 0xFF, 0xFF, 0xFF, 0xFF };
    GxSceneClear(3, white);

    if (s_shadowScissor && s_shadowScissor->m_intValue) {
        GxRsSet(GxRs_ScissorTest, 1);

        const float* scissor = view.scissor[index];
        CRect rect;
        rect.minY = (scissor[0] + 1.0f) * 0.5f;
        rect.minX = (scissor[1] + 1.0f) * 0.5f;
        rect.maxY = (scissor[2] + 1.0f) * 0.5f;
        rect.maxX = (scissor[3] + 1.0f) * 0.5f;

        g_theGxDevicePtr->ScissorSet(&rect);
    }

    C44Matrix lightView;
    MatrixLookAtLH(eye, target, up, lightView);
    GxXformSetView(lightView);

    // The model batches carry the camera's view in their bones, so the light's view reaches them
    // through the inverse of it: three rows at vertex c14.
    C44Matrix rebase = (inverseView * lightView).Transpose();
    GxShaderConstantsSet(GxSh_Vertex, 0xE, reinterpret_cast<const float*>(&rebase), 3);

    if (casters.mapObjCount) {
        MapObjDrawShadowCasters(casters.groups, casters.mapObjCount, casters.placements, toCamera, view.frustum[index]);
    }

    if (casters.models[0].count || casters.models[1].count) {
        if (s_shadowInstancing && s_shadowInstancing->m_intValue) {
            casters.models[0].MergeRuns();
            casters.models[1].MergeRuns();
        }

        s_maxCasters[0] = std::max(s_maxCasters[0], casters.models[0].count);
        s_maxCasters[1] = std::max(s_maxCasters[1], casters.models[1].count);

        CM2Model::DrawShadowCasterLists(&casters.models[0], &casters.models[1]);
    }

    GxRsPop();

    GxXformSetViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3], savedViewport[4], savedViewport[5]);
    GxXformSetView(savedView);

    GxRsSet(GxRs_ScissorTest, 0);
    GxRenderTargetSet(GxBuffers_Color, savedColor, 0);

    GxXformSetProjection(savedProjection);
    CShaderEffect::UpdateProjMatrix();

    return 1;
}

// ref: FUN_007bd3a0
// Hand the shadow map the map's four callbacks, register the three switches they read, and make
// the maps.
void MapShadowInitialize() {
    ShadowMapSetBuildMatrixCallback(&MapShadowBuildMatrix);
    ShadowMapSetSetupViewCallback(&MapShadowSetupView);
    ShadowMapSetCollectCallback(&MapShadowCollect);
    ShadowMapSetRenderCallback(&MapShadowRenderView);

    s_shadowCull = CVar::Register("shadowCull", "enable shadow frustum culling", 0x0, "1", nullptr, DEFAULT);
    s_shadowScissor = CVar::Register("shadowScissor", "enable scissoring when rendering shadowmaps", 0x0, "1", nullptr, DEFAULT);
    s_shadowInstancing = CVar::Register("shadowInstancing", "enable instancing when rendering shadowmaps", 0x0, "1", nullptr, DEFAULT);

    ShadowMapInitialize();
}

// ref: FUN_007bb570
// The per-frame map shadow driver: the light direction, the focus, the intensity, the maps --
// with the lit pass when the view looks through a portal or the map has buildings -- and the light
// matrices the shaders will sample them with.
void MapShadowRender() {
    int32_t lit = 0.0f <= CWorldScene::s_portalWindow.depth || CWorldScene::s_hasMapObjs != 0;

    ShadowMapSetLightDirection(MapShadowLightDirection());

    C3Vector focus = MapShadowFocus();

    // 1.0 and 0 are the constants at the reference's call site, not a choice.
    ShadowMapSetIntensity(1.0f, 0);

    ShadowMapRender(focus, lit);
    ShadowMapBuildLightMatrices();
}
