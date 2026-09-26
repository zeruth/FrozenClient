#include "world/map/MapOcclusion.hpp"
#include "world/map/MapOcclusionTable.hpp"
#include "world/map/CMap.hpp"
#include "world/CWorldScene.hpp"
#include <tempest/Box.hpp>
#include <tempest/Intersect.hpp>
#include <cmath>

namespace MapOcclusion {

// The volumes' boxes, computed once from their vertices. The reference keeps them in the table
// itself, zeroed in the image and filled on first use (FUN_007ccd20 behind a done flag).
static CAaBox s_boxes[sizeof(VOLUMES) / sizeof(VOLUMES[0])];
static bool s_boxesBuilt = false;

// Part of ref: FUN_007ccd20
static void BuildBoxes() {
    if (s_boxesBuilt) {
        return;
    }

    s_boxesBuilt = true;

    for (uint32_t i = 0; i < sizeof(VOLUMES) / sizeof(VOLUMES[0]); i++) {
        const SVolume& volume = VOLUMES[i];

        CAaBox& box = s_boxes[i];

        box.b.x = box.b.y = box.b.z = 1e30f;
        box.t.x = box.t.y = box.t.z = -1e30f;

        for (uint32_t k = 0; k < volume.vertexCount; k++) {
            const float* v = VERTICES[volume.firstVertex + k];

            if (v[0] < box.b.x) { box.b.x = v[0]; }
            if (v[1] < box.b.y) { box.b.y = v[1]; }
            if (v[2] < box.b.z) { box.b.z = v[2]; }

            if (v[0] > box.t.x) { box.t.x = v[0]; }
            if (v[1] > box.t.y) { box.t.y = v[1]; }
            if (v[2] > box.t.z) { box.t.z = v[2]; }
        }
    }
}

// Part of ref: FUN_007cd4e0
// One volume becomes a silhouette pyramid: a plane through the camera and each edge of the
// polygon, so anything inside all of them is behind the polygon from here. There is no cap
// plane -- the reference adds one per edge and no more, and the frustum test upstream is what
// keeps things behind the camera out.
//
// The table's polygons do NOT share a winding -- fifteen wind one way, fifteen the other and
// thirty-two are vertical -- so the sign of a plane depends on the volume it came from. The
// order below is the reference's own, taken off the call site's instructions.
static void AddVolume(const C3Vector& camera, const float* vertices, uint32_t count) {
    CWorldScene::OcclusionVolume volume;

    volume.firstPlane = static_cast<int32_t>(CWorldScene::s_occlusionPlanes.Count());
    volume.planeCount = static_cast<int32_t>(count);

    for (uint32_t i = 0; i < count; i++) {
        const C3Vector& a = *reinterpret_cast<const C3Vector*>(vertices + i * 3);
        const C3Vector& b = *reinterpret_cast<const C3Vector*>(vertices + ((i + 1) % count) * 3);

        C4Plane plane;

        // The order is the call site's own: the edge's two vertices, then the camera. Ghidra
        // drops it because the out-plane arrives in ECX, so it was read off the instructions.
        PlaneFromPoints(&plane, a, b, camera);

        CWorldScene::s_occlusionPlanes.Add(1, &plane);
    }

    CWorldScene::s_occlusionVolumes.Add(1, &volume);
}

// ref: FUN_007cd850
void BuildVolumes(const C3Vector& camera, const C3Vector* corners, int32_t restricted) {
    CWorldScene::s_occlusionPlanes.SetCount(0);
    CWorldScene::s_occlusionVolumes.SetCount(0);

    BuildBoxes();

    // The reference builds its own frustum here from the corners it was handed, rather than
    // taking one ready-made. That is what makes this per-traversal.
    CWFrustum frustum;

    frustum.SetCorners(corners);

    for (uint32_t i = 0; i < sizeof(VOLUMES) / sizeof(VOLUMES[0]); i++) {
        const SVolume& volume = VOLUMES[i];

        if (volume.mapId != CMap::s_mapID) {
            continue;
        }

        // Bit 1 marks the volumes the restricted pass keeps; without it every one is considered.
        if (restricted && !(volume.flags & 0x2)) {
            continue;
        }

        if (!AaBoxVsPlanes6(frustum.planes, s_boxes[i])) {
            continue;
        }

        // TODO the reference has a second path here, taken when the per-frame extrude distance
        // (a float CMap::Render seeds to -1.0 and only sometimes overwrites) rises above 1e-6:
        // it clips the polygon against a plane pushed along the view direction and builds the
        // pyramid from the clipped outline instead. The seed value selects THIS path, so it is
        // the usual one; the other needs FUN_007f9650, the polygon clipper.
        AddVolume(camera, VERTICES[volume.firstVertex], volume.vertexCount);

    }
}

}
