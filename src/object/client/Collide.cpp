// The movement collision (Collide.cpp, and the hull geometry the reference keeps beside it in
// VehicleCamera_C.cpp, 0x0075b480 .. 0x0075ef00).
//
// A moving unit is swept as a hull: a square column (the collision radius out from the position,
// the collision height up) with a pyramid under it whose tip is the position itself, so the unit
// rides over small bumps instead of catching on them. The world's triangles inside the step's box
// are gathered once per step (FUN_0075ff90) into one list, the hull is swept along the move against
// them (FUN_0075f9d0), and the walking, falling, hovering and swimming paths decide from the
// first contact whether to slide along it, step up onto it, fall off it or land on it.

#include "object/client/CMovementData_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/client/CVehicleCamera_C.hpp"
#include "object/movement/CMoveSpline.hpp"
#include "world/CWorld.hpp"
#include "world/WorldFacets.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/box/CAaBox.hpp>
#include <cmath>
#include <cstring>

namespace {

typedef CVehicleCamera_C::PointSet CollidePolygon;

// ref: DAT_00adba34, DAT_00adba54
// The step's triangles, and the water surfaces a swimmer collides with from below.
CFacetList s_facets;
CFacetList s_waterFacets;

// ref: DAT_00ca1660
// The box s_facets was gathered for; a sweep inside it reuses the list.
CAaBox s_facetBox;

const float COLLIDE_EPSILON = 0.0013888889225199819f;      // 0x00a37f14, 1/720
const float COLLIDE_NEG_EPSILON = -0.0013888889225199819f; // 0x00a37f18
const float COLLIDE_FACING = -9.999999747378752e-06f;       // 0x00a34eec
const float COLLIDE_WALKABLE = 0.6427876353263855f;         // 0x00a37f0c, cos 50 degrees
const float COLLIDE_WALKABLE_NPC = 0.1736481785774231f;     // 0x00a37f10, cos 80 degrees
const float COLLIDE_TOUCH = -0.02777777798473835f;          // 0x00a37f20, -1/36
const float COLLIDE_BEHIND = -9.5367431640625e-07f;         // 0x00a37f24
const float COLLIDE_REACH = 0.02777777798473835f;           // 0x00a3f854, 1/36
const float COLLIDE_SLOPE_X = 0.8796418905258179f;          // 0x00a37f28
const float COLLIDE_SLOPE_Z = 0.4756366014480591f;          // 0x00a37f2c
const float COLLIDE_FOOT = 1.8493989706039429f;             // 0x00a32830, the pyramid's height over the radius
const float COLLIDE_MARGIN = 0.1666666716337204f;           // 0x00a33b7c
const float COLLIDE_STEP_REACH = 1.191753625869751f;        // 0x00a37f78
const float COLLIDE_SQRT2 = 1.4142135381698608f;            // 0x00a37f7c
const float COLLIDE_SLIDE = -1.1866661310195923f;           // 0x00a37f80
const float COLLIDE_SLIDE_BACK = 1.1866661310195923f;       // 0x00a37f84
const float COLLIDE_STEP_DOT = 0.9848077297210693f;         // 0x00a37f88, cos 10 degrees
const float COLLIDE_FALL_FAR = 0.1111111119389534f;         // 0x00a1ea9c
const float COLLIDE_TIME_EPSILON = 0.0005000000237487257f;  // 0x00a25098
const float COLLIDE_HOVER_RATE = 3.5f;                      // 0x00a37f90
const float COLLIDE_NEAR_EDGE = 0.0833333358168602f;        // 0x00a37f38
const float COLLIDE_BIAS = 0.6385560035705566f;             // 0x00a37f5c
const float COLLIDE_UP_FACET = 0.017452405765652657f;       // 0x00a37f64, sin 1 degree
const float MOVE_TURN_EPSILON = 9.5367431640625e-07f;       // 0x009f1224
const float MOVE_SPEED_EPSILON = 2.384185791015625e-07f;    // 0x009ea27c
const float MOVE_SECONDS = 0.0010000000474974513f;          // 0x009e1134

inline float MsToSeconds(uint32_t ms) {
    return static_cast<float>(ms) * MOVE_SECONDS;
}

inline float Dot(const C3Vector& a, const C3Vector& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float PlaneDistance(const C4Plane& plane, const C3Vector& p) {
    return plane.n.x * p.x + plane.n.y * p.y + plane.n.z * p.z + plane.d;
}

// ref: FUN_006e9320
// Two values further apart than 2^-20.
inline bool Differs(float a, float b) {
    return MOVE_TURN_EPSILON <= std::fabs(a - b);
}

// ref: FUN_0075b560
void SwapPlanes(C4Plane& a, C4Plane& b) {
    C4Plane t = a;
    a = b;
    b = t;
}

// ref: FUN_0075b710
// Clip a polygon by a plane, keeping what is in front of it (distance -(n.p + d) >= 0); an edge
// the plane cuts gets a new point marked with the plane's `id`. Fewer than three points left
// empties it.
void ClipPolygon(CollidePolygon& polygon, const C4Plane& plane, uint32_t id) {
    uint32_t count = polygon.count;
    float lo = 3.4028234663852886e+38f;
    float hi = -3.4028234663852886e+38f;
    float dist[15];

    for (uint32_t i = 0; i < count; i++) {
        float d = -(plane.n.x * polygon.points[i].x + plane.n.y * polygon.points[i].y + plane.n.z * polygon.points[i].z + plane.d);

        if (d < lo) {
            lo = d;
        }

        if (hi < d) {
            hi = d;
        }

        dist[i] = d;
    }

    if (COLLIDE_NEG_EPSILON < lo) {
        return;
    }

    if (COLLIDE_EPSILON <= hi) {
        CollidePolygon source(polygon);
        polygon.count = 0;

        uint32_t prev = count - 1;

        for (uint32_t i = 0; i < count; i++) {
            float curDist = dist[i];
            float prevDist = dist[prev];
            const C3Vector& cur = source.points[i];
            const C3Vector& before = source.points[prev];

            auto emit = [&](const C3Vector& p, uint32_t pointId) {
                polygon.points[polygon.count] = p;
                std::memcpy(&polygon.values[polygon.count], &pointId, sizeof(pointId));
                polygon.count++;
            };

            uint32_t curId;
            std::memcpy(&curId, &source.values[i], sizeof(curId));

            if (0.0f <= prevDist) {
                if (0.0f <= curDist) {
                    emit(cur, curId);
                } else if (COLLIDE_EPSILON < prevDist) {
                    float t = prevDist / (curDist - prevDist);
                    emit({
                        before.x - (cur.x - before.x) * t,
                        before.y - (cur.y - before.y) * t,
                        before.z - (cur.z - before.z) * t
                    }, id);
                }
            } else if (0.0f <= curDist) {
                if (COLLIDE_EPSILON < curDist) {
                    float t = prevDist / (curDist - prevDist);
                    emit({
                        before.x - (cur.x - before.x) * t,
                        before.y - (cur.y - before.y) * t,
                        before.z - (cur.z - before.z) * t
                    }, id);
                }

                emit(cur, curId);
            }

            prev = i;
        }

        if (2 < polygon.count) {
            return;
        }
    }

    polygon.count = 0;
}

// The polygon a facet starts as: its three corners, none from a clip.
void FacetPolygon(const M2CollisionTriangle& facet, CollidePolygon& polygon) {
    polygon.points[0] = facet.vertices[0];
    polygon.points[1] = facet.vertices[1];
    polygon.points[2] = facet.vertices[2];

    uint32_t none = 0xffffffff;
    std::memcpy(&polygon.values[0], &none, sizeof(none));
    std::memcpy(&polygon.values[1], &none, sizeof(none));
    std::memcpy(&polygon.values[2], &none, sizeof(none));

    polygon.count = 3;
}

// The plane through an edge and the sweep direction, turned so the face's other corner is behind
// it (the shared body of FUN_0075bc50 and FUN_0075be80).
bool EdgeSweepPlane(const C3Vector& a, const C3Vector& b, const C3Vector& opposite, const C3Vector& dir, C4Plane& out) {
    float dx = (a.x + dir.x) - a.x;
    float dy = (a.y + dir.y) - a.y;
    float dz = (a.z + dir.z) - a.z;

    float ex = b.x - a.x;
    float ey = b.y - a.y;
    float ez = b.z - a.z;

    float nx = ey * dz - ez * dy;
    float ny = ez * dx - dz * ex;
    float nz = ex * dy - ey * dx;

    float length = nx * nx + ny * ny + nz * nz;

    if (length < MOVE_TURN_EPSILON) {
        return false;
    }

    float inv = 1.0f / std::sqrt(length);
    out.n = { nx * inv, ny * inv, nz * inv };
    out.d = -(out.n.x * a.x + out.n.y * a.y + out.n.z * a.z);

    if (0.0f < out.n.x * (opposite.x - a.x) + out.n.z * (opposite.z - a.z) + (opposite.y - a.y) * out.n.y) {
        out.n = { -out.n.x, -out.n.y, -out.n.z };
        out.d = -out.d;
    }

    return true;
}

// ref: FUN_0075bc50
// The volume a triangle face of the hull sweeps along `dir`: its three edge planes, then the
// face's plane moved to the end of the sweep.
bool TriangleSweepPlanes(const C3Vector* points, const uint8_t* face, const C4Plane& plane, const C3Vector& dir, C4Plane* out) {
    static const uint32_t s_next[3] = { 1, 2, 0 };
    static const uint32_t s_prev[3] = { 2, 0, 1 };

    for (uint32_t k = 0; k < 3; k++) {
        if (!EdgeSweepPlane(points[face[k]], points[face[s_next[k]]], points[face[s_prev[k]]], dir, out[k])) {
            return false;
        }
    }

    const C3Vector& a = points[face[0]];
    out[3].n = plane.n;
    out[3].d = -((a.z + dir.z) * plane.n.z + plane.n.y * (a.y + dir.y) + plane.n.x * (a.x + dir.x));

    return true;
}

// ref: FUN_0075be80
// The same for a quad face: four edge planes and the moved face.
bool QuadSweepPlanes(const C3Vector* points, const uint8_t* face, const C4Plane& plane, const C3Vector& dir, C4Plane* out) {
    for (uint32_t k = 0; k < 4; k++) {
        if (!EdgeSweepPlane(points[face[k]], points[face[(k + 1) & 3]], points[face[(k - 1) & 3]], dir, out[k])) {
            return false;
        }
    }

    const C3Vector& a = points[face[0]];
    out[4].n = plane.n;
    out[4].d = -((a.z + dir.z) * plane.n.z + plane.n.y * (a.y + dir.y) + plane.n.x * (a.x + dir.x));

    return true;
}

// ref: FUN_0075c0b0
// When the hull plane `index` moving along `dir` first touches the polygon: the smallest positive
// distance of its points, along `dir`, to the plane. A polygon wholly behind the plane is clipped
// by the hull's other planes first, and only counts if what is left reaches past the touch slack.
bool PolygonTouchTime(CollidePolygon& polygon, const C4Plane* hull, uint32_t index, const C3Vector& dir, float* t) {
    float best = *t;
    bool behind = true;
    const C4Plane& plane = hull[index];
    float denom = Dot(plane.n, dir);
    float absDenom = std::fabs(denom);

    for (uint32_t i = 0; i < polygon.count; i++) {
        float d = PlaneDistance(plane, polygon.points[i]);

        if (MOVE_SPEED_EPSILON <= absDenom) {
            d /= denom;
        }

        if (d < best) {
            best = 0.0f;

            if (0.0f < d) {
                best = d;
            }
        }

        if (COLLIDE_BEHIND < d) {
            behind = false;
        }
    }

    if (behind) {
        for (uint32_t i = 0; i < 9; i++) {
            if (i != index) {
                ClipPolygon(polygon, hull[i], i);

                if (!polygon.count) {
                    return false;
                }
            }
        }

        bool stillBehind = true;

        for (uint32_t i = 0; i < polygon.count; i++) {
            float d = PlaneDistance(plane, polygon.points[i]);

            if (MOVE_SPEED_EPSILON <= std::fabs(denom)) {
                d /= denom;
            }

            if (COLLIDE_TOUCH < d) {
                stillBehind = false;
            }
        }

        if (stillBehind) {
            return false;
        }
    }

    if (*t <= best) {
        return false;
    }

    *t = best;

    return true;
}

// ref: FUN_0075c5a0
// The first facet a hull face sweeps into: each facet facing the move is clipped to the face's
// swept volume and timed against the face.
bool SweepFacets(const CFacetList& list, const C3Vector& dir, const C4Plane* clip, uint32_t clipCount,
                 const C4Plane* hull, uint32_t index, float* t, uint32_t* hit) {
    bool found = false;

    for (uint32_t i = 0; i < list.facets.Count(); i++) {
        const M2CollisionTriangle& facet = list.facets[i];

        if (COLLIDE_FACING < Dot(facet.plane.n, dir)) {
            continue;
        }

        CollidePolygon polygon;
        FacetPolygon(facet, polygon);

        bool empty = false;

        for (uint32_t c = 0; c < clipCount; c++) {
            ClipPolygon(polygon, clip[c], c);

            if (!polygon.count) {
                empty = true;
                break;
            }
        }

        if (empty) {
            continue;
        }

        float touch = 3.4028234663852886e+38f;

        if (PolygonTouchTime(polygon, hull, index, dir, &touch) && touch <= *t) {
            *t = touch;
            *hit = i;
            found = true;
        }
    }

    return found;
}

// ref: FUN_0075c8f0
// The hull's nine planes: four sides, the top, and the four slopes of the pyramid under it.
void HullPlanes(const C3Vector& p, float height, float radius, C4Plane* planes) {
    planes[0] = { { -1.0f, 0.0f, 0.0f }, p.x - radius };
    planes[1] = { { 1.0f, 0.0f, 0.0f }, -p.x - radius };
    planes[2] = { { 0.0f, 1.0f, 0.0f }, -p.y - radius };
    planes[3] = { { 0.0f, -1.0f, 0.0f }, p.y - radius };
    planes[4] = { { 0.0f, 0.0f, 1.0f }, -p.z - height };
    planes[5] = { { -COLLIDE_SLOPE_X, 0.0f, -COLLIDE_SLOPE_Z }, p.z * COLLIDE_SLOPE_Z + p.x * COLLIDE_SLOPE_X + p.y * 0.0f };
    planes[6] = { { COLLIDE_SLOPE_X, 0.0f, -COLLIDE_SLOPE_Z }, p.y * 0.0f + (p.z * COLLIDE_SLOPE_Z - p.x * COLLIDE_SLOPE_X) };
    planes[7] = { { 0.0f, COLLIDE_SLOPE_X, -COLLIDE_SLOPE_Z }, (p.x * 0.0f + p.z * COLLIDE_SLOPE_Z) - p.y * COLLIDE_SLOPE_X };
    planes[8] = { { 0.0f, -COLLIDE_SLOPE_X, -COLLIDE_SLOPE_Z }, p.z * COLLIDE_SLOPE_Z + p.x * 0.0f + p.y * COLLIDE_SLOPE_X };
}

// ref: FUN_0075ca80 (the faces)
// The hull's five quad faces (sides then top), then its four pyramid triangles, as point indices.
const uint8_t s_hullFaces[32] = {
    1, 2, 6, 5,  3, 4, 8, 7,  2, 3, 7, 6,  4, 1, 5, 8,  5, 6, 7, 8,
    0, 1, 2,  0, 3, 4,  0, 2, 3,  0, 4, 1,
};

} // namespace

// ref: FUN_006e8fc0
// How far up the unit may step: 2 for a creature, the movement's own scale for a player.
static float CollideStepHeight(CMovementData_C* move) {
    if (!move->m_owner->IsPlayerControlled()) {
        return 2.0f;
    }

    return move->m_stepHeightScale;
}

// ref: FUN_006e9520
// What is left of the step, less what a step-up in progress (move flag 0x4000000, from the
// elevation at +0x88) has climbed already; never negative.
static float CollideStepRemaining(CMovementData_C* move) {
    float step = move->m_owner->IsPlayerControlled() ? move->m_stepHeightScale : 2.0f;

    if (move->m_moveFlags & 0x4000000) {
        step -= move->m_position.z - move->m_splineElevation;
    }

    return step <= 0.0f ? 0.0f : step;
}

// ref: FUN_0075ca80
// The hull at `position`: its planes, its nine points (the tip, the pyramid's top corners, the
// column's top corners), and the face table.
static void CollideHull(const CMovementData_C* move, const C3Vector& position, float height, C4Plane* planes, C3Vector* points) {
    float r = move->m_collisionRadius;
    float foot = r * COLLIDE_FOOT;

    HullPlanes(position, height, r, planes);

    points[0] = position;
    points[1] = { -r + position.x, -r + position.y, foot + position.z };
    points[2] = { position.x + -r, r + position.y, foot + position.z };
    points[3] = { position.x + r, r + position.y, foot + position.z };
    points[4] = { position.x + r, -r + position.y, foot + position.z };
    points[5] = { -r + position.x, -r + position.y, height + position.z };
    points[6] = { position.x + -r, r + position.y, height + position.z };
    points[7] = { position.x + r, r + position.y, height + position.z };
    points[8] = { position.x + r, -r + position.y, height + position.z };
}

// ref: FUN_0075cd00
// The unit's box: the radius out from the position, the height up.
static CAaBox CollideUnitBox(const CMovementData_C* move, const C3Vector& position) {
    CAaBox box;
    box.b = { position.x - move->m_collisionRadius, position.y - move->m_collisionRadius, position.z };
    box.t = { position.x + move->m_collisionRadius, position.y + move->m_collisionRadius, move->m_collisionHeight + position.z };

    return box;
}

// ref: FUN_0075e3d0
// The query mask for this unit: terrain, buildings, doodads and objects (players skip the
// 0x2000 group), water to walk on, the low-detail heights when flying, and ...
static uint32_t CollideQueryFlags(CMovementData_C* move) {
    uint32_t flags = move->m_owner->IsPlayerControlled() ? 0x100111 : 0x102111;

    // FUN_00714ac0: unit state 0x400 walls off what is not loaded instead of stopping.
    if (move->m_owner->m_stateFlags & 0x400) {
        flags |= 0x80000000;
    }

    uint32_t moveFlags = move->m_moveFlags;

    if ((moveFlags & 0x10000000) && !(moveFlags & 0x200000)
        && (-0.6457718014717102f < move->m_pitch || (move->m_moveFlags2 & 0x200))) {
        flags |= 0x10000;
    }

    if (moveFlags & 0x2000000) {
        flags |= (move->m_moveFlags2 & 0x4000) ? 0x200 : 0x20200;
    }

    // A ghost (player flags 0x10) passes through what only the living collide with.
    if (move->m_owner->IsA(TYPE_PLAYER) && (static_cast<CGPlayer_C*>(move->m_owner)->Player()->flags & 0x10)) {
        flags |= 0x8000;
    }

    return flags;
}

// ref: FUN_0075f0a0
// The triangles near a sweep of `delta`: the step's list when the swept box is inside the box it
// was gathered for, a fresh gather (grown by a sixth of a yard) otherwise. A passenger's triangles
// are carried into its transport's space.
static int32_t CollideGatherNear(CMovementData_C* move, const C3Vector& delta) {
    C3Vector position = move->m_position;
    C3Vector d = delta;
    C44Matrix transport;

    if (move->m_transportGUID) {
        MovementGetTransportMatrixChecked(move->m_transportGUID, transport, move->m_guid, ".\\Collide.cpp", 0x2ef);
        position = position * transport;
        d = {
            transport.a0 * delta.x + transport.b0 * delta.y + transport.c0 * delta.z,
            transport.a1 * delta.x + delta.y * transport.b1 + delta.z * transport.c1,
            delta.x * transport.a2 + delta.y * transport.b2 + delta.z * transport.c2
        };
    }

    CAaBox unit = CollideUnitBox(move, position);
    CAaBox moved;
    moved.b = { unit.b.x + d.x, unit.b.y + d.y, unit.b.z + d.z };
    moved.t = { unit.t.x + d.x, d.y + unit.t.y, d.z + unit.t.z };

    if (s_facetBox.IsPointInside(moved.b) && s_facetBox.IsPointInside(moved.t)) {
        return 1;
    }

    CAaBox query;
    query.b = { moved.b.x - COLLIDE_MARGIN, moved.b.y - COLLIDE_MARGIN, moved.b.z - COLLIDE_MARGIN };
    query.t = { moved.t.x + COLLIDE_MARGIN, moved.t.y + COLLIDE_MARGIN, COLLIDE_MARGIN + moved.t.z };
    query.GrowToInclude(s_facetBox);

    if (!CWorld::QueryFacets(unit, query, s_facets, CollideQueryFlags(move), nullptr)) {
        return 0;
    }

    if (!(move->m_moveFlags & 0x200000)) {
        s_waterFacets.facets.SetCount(0);
        s_waterFacets.owners.SetCount(0);
    } else {
        // A swimmer meets the water's surface from below: its triangles face down.
        CWorld::QueryFacets(unit, query, s_waterFacets, 0x20000, nullptr);

        for (uint32_t i = 0; i < s_waterFacets.facets.Count(); i++) {
            auto& plane = s_waterFacets.facets[i].plane;
            plane.n = { -plane.n.x, -plane.n.y, -plane.n.z };
            plane.d = -plane.d;
        }
    }

    s_facetBox = query;

    if (move->m_transportGUID) {
        C44Matrix inverse = transport.AffineInverse();

        for (uint32_t i = 0; i < s_facets.facets.Count(); i++) {
            auto& facet = s_facets.facets[i];
            facet.vertices[0] = facet.vertices[0] * inverse;
            facet.vertices[1] = facet.vertices[1] * inverse;
            facet.vertices[2] = facet.vertices[2] * inverse;

            C3Vector n = facet.plane.n;
            facet.plane.n = {
                n.x * inverse.a0 + inverse.c0 * n.z + inverse.b0 * n.y,
                inverse.a1 * n.x + inverse.c1 * n.z + inverse.b1 * n.y,
                inverse.c2 * n.z + inverse.b2 * n.y + n.x * inverse.a2
            };
            facet.plane.d = -(facet.plane.n.x * facet.vertices[0].x + facet.plane.n.z * facet.vertices[0].z
                + facet.vertices[0].y * facet.plane.n.y);
        }
    }

    return 1;
}

// ref: FUN_0075f9d0
// Sweep the hull `dist` along unit `dir` against `list`: how far it gets (`*moved`, 0 under a
// 720th), the facet it stops on (`*hit`, the list's count when it stops on nothing), and the hull
// planes that made contact (`contact`, up to four, the first being the one the move stops on).
// `from` sweeps from another position than the unit's, without re-gathering.
static int32_t CollideSweep(CMovementData_C* move, const CFacetList& list, const C3Vector& dir, float dist,
                            uint32_t* hit, C4Plane* contact, uint32_t* contactCount, float* moved,
                            const C3Vector* from) {
    *contactCount = 0;

    if (std::fabs(dist) < MOVE_TURN_EPSILON) {
        *moved = 0.0f;
        *hit = list.facets.Count();
        return 1;
    }

    float height = move->m_collisionHeight;
    C3Vector start = from ? *from : move->m_position;

    if ((move->m_moveFlags & 0x200000) && &list == &s_waterFacets) {
        height *= 0.75f;
    }

    float reach = dist < COLLIDE_REACH ? COLLIDE_REACH : dist;
    C3Vector swept = { reach * dir.x, dir.y * reach, dir.z * reach };

    C4Plane hull[9];
    C3Vector points[9];
    CollideHull(move, start, height, hull, points);

    if (!from && !CollideGatherNear(move, swept)) {
        *moved = 0.0f;
        return 0;
    }

    *hit = list.facets.Count();
    float t = dist;

    auto record = [&](const C4Plane& plane, float touch) {
        if (t - COLLIDE_EPSILON <= touch) {
            if (touch < COLLIDE_EPSILON + t) {
                contact[*contactCount] = plane;
                (*contactCount)++;
            }
        } else {
            contact[0] = plane;
            *contactCount = 1;
        }

        if (touch < t) {
            t = touch;

            if (1 < *contactCount) {
                SwapPlanes(contact[*contactCount - 1], contact[0]);
            }
        }
    };

    // ref: FUN_0075cd70: the pyramid's four faces.
    for (uint32_t k = 0; k < 4; k++) {
        const C4Plane& plane = hull[5 + k];
        float facing = plane.n.y * swept.y + plane.n.x * swept.x + plane.n.z * swept.z;
        C4Plane volume[4];

        if (0.0f < facing && TriangleSweepPlanes(points, &s_hullFaces[20 + k * 3], plane, swept, volume)) {
            float touch = dist;

            if (SweepFacets(list, dir, volume, 3, hull, 5 + k, &touch, hit)) {
                record(plane, touch);
            }
        }
    }

    // The column's five faces.
    for (uint32_t k = 0; k < 5; k++) {
        const C4Plane& plane = hull[k];
        float facing = swept.x * plane.n.x + swept.z * plane.n.z + plane.n.y * swept.y;
        C4Plane volume[5];

        if (0.0f < facing && QuadSweepPlanes(points, &s_hullFaces[k * 4], plane, swept, volume)) {
            float touch = dist;

            if (SweepFacets(list, dir, volume, 4, hull, k, &touch, hit)) {
                record(plane, touch);
            }
        }
    }

    *moved = COLLIDE_EPSILON <= t ? t : 0.0f;

    return 1;
}

// ref: FUN_0075ff90
// Gather the triangles for a whole collide step: the unit's box, grown along the step's
// displacement -- by the fall still to come when falling, by the step-up reach and a slope below
// when walking -- and by a 720th all round.
static int32_t CollideGather(CMovementData_C* move, float dist, uint32_t ms, const C3Vector& dir) {
    C3Vector position = move->m_position;
    C3Vector d = dir;
    C44Matrix transport;

    if (move->m_transportGUID) {
        MovementGetTransportMatrixChecked(move->m_transportGUID, transport, move->m_guid, ".\\Collide.cpp", 0x557);
        position = position * transport;
        d = {
            transport.a0 * dir.x + transport.b0 * dir.y + transport.c0 * dir.z,
            transport.a1 * dir.x + dir.y * transport.b1 + dir.z * transport.c1,
            dir.x * transport.a2 + dir.y * transport.b2 + dir.z * transport.c2
        };
    }

    CAaBox unit = CollideUnitBox(move, position);
    s_facetBox = unit;

    C3Vector step = { d.x * dist, d.y * dist, d.z * dist };

    if (move->m_moveFlags & 0x1000) {
        step.z -= move->GetFallDistanceFromHere(static_cast<int32_t>(move->m_fallTime + ms));
    }

    if (!(move->m_moveFlags & 0x1000)) {
        if (!(move->m_moveFlags & 0x2200000)) {
            float stepReach = move->m_collisionRadius + COLLIDE_EPSILON;

            if (stepReach < CollideStepHeight(move) * COLLIDE_STEP_REACH) {
                stepReach = CollideStepHeight(move) * COLLIDE_STEP_REACH;
            }

            stepReach += dist;

            CAaBox ahead;
            ahead.b = { d.x * stepReach + s_facetBox.b.x, s_facetBox.b.y + d.y * stepReach, s_facetBox.b.z + d.z * stepReach };
            ahead.t = { s_facetBox.t.x + d.x * stepReach, d.y * stepReach + s_facetBox.t.y, d.z * stepReach + s_facetBox.t.z };
            s_facetBox.GrowToInclude(ahead);

            float half = dist * 0.5f;
            C3Vector mid = { d.x * half + position.x, d.y * half + position.y, d.z * half + position.z };
            float spread = move->m_collisionRadius * COLLIDE_SQRT2 + half;

            CAaBox around;
            around.b = { mid.x - spread, mid.y - spread, mid.z };
            around.t = { mid.x + spread, spread + mid.y, mid.z };
            s_facetBox.GrowToInclude(around);

            float up = dist;

            if (up < CollideStepHeight(move) + CollideStepHeight(move)) {
                up = CollideStepHeight(move) + CollideStepHeight(move);
            }

            s_facetBox.t.z = up + s_facetBox.t.z;
            s_facetBox.b.z = s_facetBox.b.z - (dist * COLLIDE_STEP_REACH + CollideStepHeight(move));
        } else {
            float half = dist * 0.5f;
            C3Vector mid = { d.x * half + position.x, d.y * half + position.y, d.z * half + position.z };
            float wide = move->m_collisionRadius * COLLIDE_SQRT2;

            CAaBox around;
            around.b = { (mid.x - half) - wide, (mid.y - half) - wide, mid.z - half };
            around.t = { mid.x + half + wide, mid.y + half + wide, 0.0f };

            float top = wide;

            if (top <= move->m_collisionHeight) {
                top = move->m_collisionHeight;
            }

            around.t.z = top + mid.z + half;
            s_facetBox.GrowToInclude(around);
        }
    } else {
        CAaBox moved;
        moved.b = { step.x + s_facetBox.b.x, s_facetBox.b.y + step.y, s_facetBox.b.z + step.z };
        moved.t = { s_facetBox.t.x + step.x, step.y + s_facetBox.t.y, step.z + s_facetBox.t.z };
        s_facetBox.GrowToInclude(moved);

        float spread = step.z * COLLIDE_SLIDE;

        if (0.0f < spread) {
            s_facetBox.b.x -= spread;
            s_facetBox.t.x += spread;
            s_facetBox.b.y -= spread;
            s_facetBox.t.y = spread + s_facetBox.t.y;
        }
    }

    s_facetBox.b.x -= COLLIDE_EPSILON;
    s_facetBox.b.y -= COLLIDE_EPSILON;
    s_facetBox.b.z -= COLLIDE_EPSILON;
    s_facetBox.t.x += COLLIDE_EPSILON;
    s_facetBox.t.y += COLLIDE_EPSILON;
    s_facetBox.t.z = COLLIDE_EPSILON + s_facetBox.t.z;

    if (!CWorld::QueryFacets(unit, s_facetBox, s_facets, CollideQueryFlags(move), nullptr)) {
        return 0;
    }

    if (!(move->m_moveFlags & 0x200000)) {
        s_waterFacets.facets.SetCount(0);
        s_waterFacets.owners.SetCount(0);
    } else {
        CWorld::QueryFacets(unit, s_facetBox, s_waterFacets, 0x20000, nullptr);

        for (uint32_t i = 0; i < s_waterFacets.facets.Count(); i++) {
            auto& plane = s_waterFacets.facets[i].plane;
            plane.n = { -plane.n.x, -plane.n.y, -plane.n.z };
            plane.d = -plane.d;
        }
    }

    if (move->m_transportGUID) {
        C44Matrix inverse = transport.AffineInverse();

        for (uint32_t i = 0; i < s_facets.facets.Count(); i++) {
            auto& facet = s_facets.facets[i];
            facet.vertices[0] = facet.vertices[0] * inverse;
            facet.vertices[1] = facet.vertices[1] * inverse;
            facet.vertices[2] = facet.vertices[2] * inverse;

            C3Vector n = facet.plane.n;
            facet.plane.n = {
                inverse.a0 * n.x + inverse.c0 * n.z + inverse.b0 * n.y,
                inverse.a1 * n.x + inverse.c1 * n.z + inverse.b1 * n.y,
                inverse.c2 * n.z + inverse.b2 * n.y + n.x * inverse.a2
            };
            facet.plane.d = -(facet.plane.n.x * facet.vertices[0].x + facet.plane.n.z * facet.vertices[0].z
                + facet.vertices[0].y * facet.plane.n.y);
        }
    }

    return 1;
}

// ---- facet tests --------------------------------------------------------------------------------

// ref: FUN_0075b690
// The facet is too steep to stand on (normal z at most cos 50 degrees).
static bool CollideFacetSteep(uint32_t facet) {
    return s_facets.facets[facet].plane.n.z <= COLLIDE_WALKABLE;
}

// ref: FUN_0075b6c0
// The facet is too steep for this unit: a player stands up to 50 degrees, a creature to 80.
static bool CollideFacetSteepFor(CMovementData_C* move, uint32_t facet) {
    float limit = move->m_owner->IsPlayerControlled() ? COLLIDE_WALKABLE : COLLIDE_WALKABLE_NPC;

    return !(limit < s_facets.facets[facet].plane.n.z);
}

// ref: FUN_0075cfe0
// The facet's plane passes within a 720th of one of the column's four top corners.
static bool CollideFacetAtTop(CMovementData_C* move, uint32_t facet) {
    const C4Plane& plane = s_facets.facets[facet].plane;
    float r = move->m_collisionRadius;
    float top = move->m_collisionHeight + move->m_position.z;

    C3Vector corners[4] = {
        { r + move->m_position.x, move->m_position.y - r, top },
        { r + move->m_position.x, r + move->m_position.y, top },
        { move->m_position.x - r, r + move->m_position.y, top },
        { move->m_position.x - r, move->m_position.y - r, top },
    };

    for (const auto& corner : corners) {
        if (std::fabs(PlaneDistance(plane, corner)) < COLLIDE_EPSILON) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0075d0a0
// The point lies inside the facet's three edges (within a twelfth of a yard).
static bool CollidePointOverFacet(uint32_t facet, const C3Vector& point) {
    const M2CollisionTriangle& tri = s_facets.facets[facet];
    static const uint8_t s_corners[3] = { 0, 1, 2 };
    C4Plane planes[4];

    TriangleSweepPlanes(tri.vertices, s_corners, tri.plane, tri.plane.n, planes);

    for (uint32_t i = 0; i < 3; i++) {
        if (COLLIDE_NEAR_EDGE < PlaneDistance(planes[i], point)) {
            return false;
        }
    }

    return true;
}

// ref: FUN_0075d1c0
// Whether the facet the walk stopped on is ground: walkable, or -- when it faces down from the
// top of the column -- whether the step-up in progress has to end there.
static uint32_t CollideFacetIsGround(CMovementData_C* move, uint32_t facet, int32_t* ended) {
    if (ended) {
        *ended = 0;
    }

    float limit = move->m_owner->IsPlayerControlled() ? COLLIDE_WALKABLE : COLLIDE_WALKABLE_NPC;
    float nz = s_facets.facets[facet].plane.n.z;

    if (limit < nz) {
        if (CollidePointOverFacet(facet, move->m_position)) {
            move->m_moveFlags &= 0xfbffffff;
        }

        return 1;
    }

    if (0.0f < nz || !CollideFacetAtTop(move, facet)) {
        return move->m_moveFlags & 0x4000000;
    }

    if (move->m_moveFlags & 0x4000000) {
        if (ended) {
            *ended = 1;
        }

        move->m_moveFlags &= 0xfbffffff;
    }

    return COLLIDE_WALKABLE <= -s_facets.facets[facet].plane.n.z ? 1 : 0;
}

// ref: FUN_0075d2d0
// The facet is a wall: neither ground nor ceiling.
static bool CollideFacetIsWall(uint32_t facet) {
    float nz = s_facets.facets[facet].plane.n.z;

    if (COLLIDE_WALKABLE < nz) {
        return false;
    }

    if (nz <= 0.0f && COLLIDE_WALKABLE <= -nz) {
        return false;
    }

    return true;
}

// ref: FUN_0075d340
// A falling unit lands on the facet: it is ground for this unit and the point after the move lies
// over it.
static bool CollideLandsOn(CMovementData_C* move, uint32_t facet, const C3Vector& delta) {
    C3Vector point = { move->m_position.x + delta.x, move->m_position.y + delta.y, move->m_position.z + delta.z };
    float limit = move->m_owner->IsPlayerControlled() ? COLLIDE_WALKABLE : COLLIDE_WALKABLE_NPC;

    return limit < s_facets.facets[facet].plane.n.z && CollidePointOverFacet(facet, point);
}

// ref: FUN_0075de40
// One of the contact planes is the column's top.
static bool CollideContactIsTop(const C4Plane* contact, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (std::fabs(contact[i].n.z - 1.0f) < MOVE_TURN_EPSILON) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0075de80
// When every contact is one of the pyramid's slopes, the direction the slopes push the hull up
// along: the one slope, the mean of two, the slope without a horizontal axis of three, nothing for
// four. False when a contact is not a slope.
static bool CollideSlopePush(const C4Plane* contact, uint32_t count, C3Vector* out) {
    if (count == 0 || 4 < count) {
        return false;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (MOVE_TURN_EPSILON <= std::fabs(contact[i].n.z - -COLLIDE_SLOPE_Z)) {
            return false;
        }
    }

    switch (count) {
        case 1:
            *out = contact[0].n;
            break;

        case 2:
            *out = {
                (contact[1].n.x + contact[0].n.x) * COLLIDE_BIAS,
                (contact[1].n.y + contact[0].n.y) * COLLIDE_BIAS,
                (contact[1].n.z + contact[0].n.z) * COLLIDE_BIAS
            };
            break;

        case 3: {
            int32_t onX[3];
            int32_t onY[3];
            int32_t xCount = 0;
            int32_t yCount = 0;

            for (int32_t i = 0; i < 3; i++) {
                if (std::fabs(contact[i].n.y) < MOVE_TURN_EPSILON) {
                    onY[yCount++] = i;
                } else if (std::fabs(contact[i].n.x) < MOVE_TURN_EPSILON) {
                    onX[xCount++] = i;
                }
            }

            int32_t pick = xCount ? onX[0] : 0;

            if (yCount == 1) {
                pick = onY[0];
            }

            *out = contact[pick].n;
            break;
        }

        case 4:
            *out = { 0.0f, 0.0f, 0.0f };
            break;
    }

    return true;
}

// ref: FUN_0075e0c0
// The vertical correction for walking onto a slope: how far up (or down) the slope at `normal`
// lifts the hull over the step's length, capped at the step height.
static C3Vector CollideSlopeLift(CMovementData_C* move, const C3Vector& dir, float* dist, C3Vector normal,
                                 int32_t pushed, const C3Vector& push) {
    if (pushed && normal.z <= COLLIDE_WALKABLE
        && MOVE_SPEED_EPSILON <= std::fabs(push.x * push.x + push.y * push.y + push.z * push.z)) {
        normal = { -push.x, -push.y, -push.z };
    }

    float rise = (-normal.z * dir.z + dir.y * -normal.y + dir.x * -normal.x) * *dist;
    float lift;

    if (std::fabs(normal.z) < MOVE_SPEED_EPSILON) {
        lift = rise < 0.0f ? -3.4028234663852886e+38f : 3.4028234663852886e+38f;
    } else {
        lift = rise / normal.z;
    }

    float z;

    if ((move->m_moveFlags & 0x4000000) && lift < 0.0f) {
        *dist = 0.0f;
        z = CollideStepRemaining(move);
    } else {
        float step = CollideStepRemaining(move);

        if (lift <= step) {
            z = lift;

            if (lift < -CollideStepHeight(move)) {
                *dist = -((*dist / lift) * CollideStepHeight(move));
                z = -CollideStepHeight(move);
            }
        } else {
            *dist = (*dist / lift) * CollideStepRemaining(move);
            z = CollideStepRemaining(move);
        }
    }

    return { 0.0f * z, 0.0f * z, z };
}

// ref: FUN_0075e250
// Sliding along a wall: the horizontal correction that keeps the move off the facet, limited so
// the slide is no longer than `limit`.
static C3Vector CollideWallSlide(const C3Vector& dir, float dist, float limit, const C3Vector& normal) {
    if (normal.z < 0.0f && COLLIDE_WALKABLE <= -normal.z) {
        return { 0.0f, 0.0f, 0.0f };
    }

    C2Vector flat = { normal.x, normal.y };
    float length = flat.x * flat.x + flat.y * flat.y;

    if (MOVE_SPEED_EPSILON < length) {
        float inv = 1.0f / std::sqrt(length);
        flat.x *= inv;
        flat.y *= inv;
    }

    float push = (-normal.z * dir.z + dir.y * -normal.y + dir.x * -normal.x) * dist;
    float facing = normal.z * 0.0f + normal.x * flat.x + normal.y * flat.y;

    if (MOVE_SPEED_EPSILON <= std::fabs(facing)) {
        push /= facing;
    }

    C2Vector slide = { flat.x * (push + MOVE_SECONDS), (push + MOVE_SECONDS) * flat.y };

    float x = dir.x * dist + slide.x;
    float y = dist * dir.y + slide.y;
    float total = x * x + y * y;

    if (limit * limit < total) {
        float scale = limit / std::sqrt(total);
        slide.x = x * scale - dir.x * dist;
        slide.y = scale * y - dist * dir.y;
    }

    return { slide.x, slide.y, 0.0f };
}

// ---- the edge a fall slides along ---------------------------------------------------------------

// ref: FUN_0075d680
// Where three planes meet.
static C3Vector CollidePlanesMeet(const C4Plane& a, const C4Plane& b, const C4Plane& c) {
    C33Matrix m;
    m.a0 = a.n.x; m.a1 = a.n.y; m.a2 = a.n.z;
    m.b0 = b.n.x; m.b1 = b.n.y; m.b2 = b.n.z;
    m.c0 = c.n.x; m.c1 = c.n.y; m.c2 = c.n.z;

    C33Matrix inv = m.Inverse(m.Determinant());

    float ra = -a.d;
    float rb = -b.d;
    float rc = -c.d;

    return {
        inv.a2 * rc + inv.a1 * rb + inv.a0 * ra,
        inv.b0 * ra + inv.b1 * rb + inv.b2 * rc,
        ra * inv.c0 + inv.c2 * rc + inv.c1 * rb
    };
}

// ref: FUN_0075d4b0
// The facet edge nearest the line through `point` along `dir`, as a unit direction.
static C3Vector CollideNearestEdge(uint32_t facet, const C3Vector& dir, const C3Vector& point) {
    const M2CollisionTriangle& tri = s_facets.facets[facet];
    static const int32_t s_from[3] = { 1, 2, 0 };
    float best = 3.4028234663852886e+38f;
    C3Vector result = { 0.0f, 0.0f, 0.0f };

    for (int32_t k = 0; k < 3; k++) {
        const C3Vector& to = tri.vertices[k];
        const C3Vector& from = tri.vertices[s_from[k]];

        float ex = to.x - from.x;
        float ey = to.y - from.y;
        float ez = to.z - from.z;
        float length = std::sqrt(ex * ex + ey * ey + ez * ez);

        if (std::fabs(length) < MOVE_SPEED_EPSILON) {
            continue;
        }

        float inv = 1.0f / length;
        ex *= inv;
        ey *= inv;
        ez *= inv;

        float distance;

        if (MOVE_TURN_EPSILON <= std::fabs((dir.x * ex + dir.y * ey + dir.z * ez) - 1.0f)) {
            float cx = dir.z * ey - dir.y * ez;
            float cy = dir.x * ez - dir.z * ex;
            float cz = dir.y * ex - dir.x * ey;
            distance = -(point.x * cx + point.y * cy + point.z * cz) + cy * to.y + to.x * cx + to.z * cz;
            distance = distance * distance;
        } else {
            float along = (to.z - point.z) * dir.z + dir.y * (to.y - point.y) + dir.x * (to.x - point.x);
            float px = to.x - (point.x + dir.x * along);
            float py = to.y - (dir.y * along + point.y);
            float pz = to.z - (point.z + dir.z * along);
            distance = px * px + py * py + pz * pz;
        }

        if (distance < best) {
            result = { ex, ey, ez };
            best = distance;
        }
    }

    return result;
}

// ref: FUN_0075d740
// One of the pyramid's four top corners lies on the plane.
static bool CollideFootOnPlane(CMovementData_C* move, const C4Plane& plane) {
    float r = move->m_collisionRadius;
    float foot = r * COLLIDE_FOOT + move->m_position.z;
    C3Vector points[5] = {
        { move->m_position.x, move->m_position.y, move->m_position.z },
        { move->m_position.x + r, r + move->m_position.y, foot },
        { move->m_position.x + -r, r + move->m_position.y, foot },
        { move->m_position.x + r, -r + move->m_position.y, foot },
        { -r + move->m_position.x, -r + move->m_position.y, foot },
    };

    for (const auto& p : points) {
        if (std::fabs(std::fabs(PlaneDistance(plane, p))) < COLLIDE_EPSILON) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0075d890
// The plane cuts one of the hull's edges (DAT_00a37f3c, sixteen point pairs).
static bool CollidePlaneCutsHull(CMovementData_C* move, const C4Plane& plane) {
    static const uint8_t s_edges[32] = {
        0, 1, 0, 2, 0, 3, 0, 4, 1, 2, 2, 4, 4, 3, 3, 1,
        1, 5, 2, 6, 4, 7, 3, 8, 5, 6, 6, 7, 7, 8, 8, 5,
    };

    float r = move->m_collisionRadius;
    float foot = r * COLLIDE_FOOT + move->m_position.z;
    float top = move->m_collisionHeight + move->m_position.z;
    const C3Vector& p = move->m_position;

    C3Vector points[9] = {
        { p.x, p.y, p.z },
        { p.x + r, r + p.y, foot },
        { -r + p.x, r + p.y, foot },
        { r + p.x, -r + p.y, foot },
        { -r + p.x, -r + p.y, foot },
        { r + p.x, r + p.y, top },
        { p.x + -r, r + p.y, top },
        { p.x + r, -r + p.y, top },
        { p.x + -r, p.y + -r, top },
    };

    for (uint32_t i = 0; i < 32; i += 2) {
        float a = PlaneDistance(plane, points[s_edges[i]]);
        float b = PlaneDistance(plane, points[s_edges[i + 1]]);

        if (std::fabs(b) * std::fabs(a) < 0.0f) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0075e760
// The direction a fall slides off a facet: along the edge two contacts meet in, when they meet on
// the facet; the facet's own normal otherwise.
static C3Vector CollideFallNormal(CMovementData_C* move, uint32_t facet, const C4Plane* contact, uint32_t count) {
    const C4Plane& plane = s_facets.facets[facet].plane;

    if (count == 1) {
        if (CollidePlaneCutsHull(move, plane)) {
            return { -contact[0].n.x, -contact[0].n.y, -contact[0].n.z };
        }
    } else if (count == 2) {
        float ex = contact[1].n.z * contact[0].n.y - contact[0].n.z * contact[1].n.y;
        float ey = contact[1].n.x * contact[0].n.z - contact[0].n.x * contact[1].n.z;
        float ez = contact[0].n.x * contact[1].n.y - contact[1].n.x * contact[0].n.y;
        float inv = 1.0f / std::sqrt(ez * ez + ex * ex + ey * ey);
        C3Vector edge = { ex * inv, ey * inv, inv * ez };

        if (MOVE_TURN_EPSILON <= std::fabs(edge.z)
            && Differs(edge.x * plane.n.x + edge.z * plane.n.z + plane.n.y * edge.y, 0.0f)
            && (std::fabs(edge.z - 1.0f) < MOVE_TURN_EPSILON || !CollideFootOnPlane(move, plane))) {
            C3Vector point = CollidePlanesMeet(plane, contact[0], contact[1]);
            C3Vector along = CollideNearestEdge(facet, edge, point);

            C3Vector slide = {
                edge.y * along.z - edge.z * along.y,
                edge.z * along.x - edge.x * along.z,
                edge.x * along.y - edge.y * along.x
            };

            float length = std::sqrt(slide.x * slide.x + slide.z * slide.z + slide.y * slide.y);

            if (length == 0.0f) {
                return { -contact[0].n.x, -contact[0].n.y, -contact[0].n.z };
            }

            slide.x /= length;
            slide.y /= length;
            slide.z /= length;

            if (slide.y * contact[0].n.y + contact[0].n.x * slide.x + contact[0].n.z * slide.z <= 0.0f) {
                return slide;
            }

            return { -slide.x, -slide.y, -slide.z };
        }
    }

    return plane.n;
}

// ref: FUN_0075db00
// The normal a fall slides along against a ledge: the facet edge nearest the point, crossed
// with the facet's normal, turned against the move.
static C3Vector CollideLedgeNormal(const C3Vector& point, const C3Vector& dir, uint32_t facet) {
    const M2CollisionTriangle& tri = s_facets.facets[facet];

    auto nearest = [&](const C3Vector& a, const C3Vector& b, C3Vector* edge) {
        float ex = b.x - a.x;
        float ey = b.y - a.y;
        float ez = b.z - a.z;
        float length = ex * ex + ez * ez + ey * ey;

        if (MOVE_SPEED_EPSILON < length) {
            float inv = 1.0f / std::sqrt(length);
            ex *= inv;
            ey *= inv;
            ez *= inv;
        }

        float along = (point.x - a.x) * ex + (point.y - a.y) * ey + (point.z - a.z) * ez;
        float px = point.x - (along * ex + a.x);
        float py = point.y - (a.y + ey * along);
        float pz = point.z - (a.z + ez * along);

        *edge = { ex, ey, ez };

        return pz * pz + py * py + px * px;
    };

    C3Vector edge;
    float best = nearest(tri.vertices[0], tri.vertices[1], &edge);

    C3Vector second;
    float distance = nearest(tri.vertices[1], tri.vertices[2], &second);

    if (distance < best) {
        best = distance;
        edge = second;
    }

    C3Vector third;
    distance = nearest(tri.vertices[2], tri.vertices[0], &third);

    if (distance < best) {
        edge = third;
    }

    C3Vector n = {
        tri.plane.n.z * edge.y - tri.plane.n.y * edge.z,
        edge.z * tri.plane.n.x - tri.plane.n.z * edge.x,
        tri.plane.n.y * edge.x - edge.y * tri.plane.n.x
    };

    if (0.0f <= n.x * dir.x + dir.y * n.y + dir.z * n.z) {
        return n;
    }

    return { -n.x, -n.y, -n.z };
}

// ref: FUN_0075e9c0
// A fall's horizontal slide off what it hit, `remaining` of `dist` left to go.
static C2Vector CollideFallSlide(CMovementData_C* move, const C3Vector& dir, float moved, float dist, uint32_t facet,
                                 const C4Plane* contact, uint32_t count) {
    C3Vector n;

    if (!CollideFacetIsWall(facet)) {
        C3Vector point = {
            move->m_position.x + dir.x * moved,
            dir.y * moved + move->m_position.y,
            dir.z * moved + move->m_position.z
        };

        n = CollideLedgeNormal(point, dir, facet);
    } else {
        n = CollideFallNormal(move, facet, contact, count);
    }

    C2Vector flat = { n.x, n.y };
    float length = flat.x * flat.x + flat.y * flat.y;

    if (MOVE_SPEED_EPSILON < length) {
        float inv = 1.0f / std::sqrt(length);
        flat.x *= inv;
        flat.y *= inv;
    }

    float push = (dist - moved) * (-n.z * dir.z + dir.y * -n.y + dir.x * -n.x);
    float facing = n.z * 0.0f + n.y * flat.y + flat.x * n.x;

    if (MOVE_SPEED_EPSILON <= std::fabs(facing)) {
        push /= facing;
    }

    push += MOVE_SECONDS;

    return { flat.x * push, push * flat.y };
}

// ---- falling ------------------------------------------------------------------------------------

// ref: FUN_0075e040
// Still rising, or the rise outlasts the time the fall has: no landing yet.
static int32_t CollideStillRising(CMovementData_C* move, float elapsed, float span, const C3Vector& delta, int32_t landed) {
    if (landed) {
        return 1;
    }

    if (0.0f < move->m_jumpVelocity) {
        return 0;
    }

    float lift = move->GetJumpLiftTime();

    if (lift <= span + elapsed) {
        if (lift < elapsed) {
            return 0;
        }

        float rise = (lift - elapsed) * move->m_currentSpeed;

        if (rise * rise <= delta.x * delta.x + delta.y * delta.y) {
            return 0;
        }
    }

    return 1;
}

// ref: FUN_00988280
// How long the fall takes to cover `distance` from its current vertical speed; with `rising`, only
// while the unit is still going up.
static float CollideFallTime(CMovementData_C* move, float distance, int32_t rising) {
    uint32_t safeFall = move->m_moveFlags & 0x20000000;
    float terminal = safeFall ? 7.0f : 60.14800262451172f;
    float v = move->m_jumpVelocity;

    if (terminal < v) {
        v = terminal;
    }

    if (std::fabs(v) < MOVE_SPEED_EPSILON) {
        // FUN_00988220: from rest.
        float reach = 0.051837362349033356f * terminal * terminal * 0.5f;

        if (reach <= distance) {
            return 0.051837362349033356f * terminal + (distance - reach) / terminal;
        }

        if (distance <= 0.0f) {
            return 0.0f;
        }

        return std::sqrt(distance * 0.10367472469806671f);
    }

    float s = 38.582210540771484f * distance + v * v;
    float root = 0.0f < s ? std::sqrt(s) : 0.0f;

    if (rising && 0.0f <= (-v - root) * 0.051837362349033356f) {
        return (-v - root) * 0.051837362349033356f;
    }

    return (root - v) * 0.051837362349033356f;
}

// ref: FUN_0075eb00
// Trim a fall step that lands: the time left after reaching the ground, and the step cut to it.
static float CollideFallLanding(CMovementData_C* move, float span, float elapsed, C3Vector* delta, float* moved,
                                int32_t landed, float horizontal) {
    int32_t rising = CollideStillRising(move, elapsed, span, *delta, landed);
    float ratio = 0.0f;

    if (MOVE_TURN_EPSILON < horizontal) {
        ratio = (std::sqrt(delta->x * delta->x + delta->y * delta->y) / horizontal) * span;
    }

    float t = CollideFallTime(move, (move->m_fallStartElevation - move->m_position.z) - delta->z, rising);

    if (t <= elapsed) {
        *delta = { 0.0f, 0.0f, 0.0f };
        *moved = 0.0f;
        return 0.0f;
    }

    t -= elapsed;

    if (span < t) {
        return span;
    }

    if (t < ratio) {
        float scale = t / ratio;
        delta->x *= scale;
        delta->y *= scale;
        *moved = std::sqrt(delta->z * delta->z + delta->y * delta->y + delta->x * delta->x);
    }

    return t;
}

// ref: FUN_00760fc0
// One sweep of a fall: 0 when it met nothing, 1 when it hit something (landed or deflected),
// 2 when the world is not there to sweep.
static int32_t CollideFallSweep(CMovementData_C* move, float elapsed, C3Vector* delta, float* dist,
                                C2Vector* slide, WOWGUID* floor, int32_t* landed, int32_t* topHit) {
    *topHit = 0;
    *landed = 0;

    float length = *dist;

    if (std::fabs(length) < MOVE_SPEED_EPSILON) {
        *floor = 0;
        return 0;
    }

    float inv = 1.0f / length;
    C3Vector dir = { delta->x * inv, delta->y * inv, inv * delta->z };

    uint32_t hit;
    C4Plane contact[4];
    uint32_t contactCount;

    if (!CollideSweep(move, s_facets, dir, length, &hit, contact, &contactCount, dist, nullptr)) {
        *delta = { 0.0f, 0.0f, 0.0f };
        return 2;
    }

    if (hit == s_facets.facets.Count()) {
        *floor = 0;
        return 0;
    }

    float moved = *dist;
    *delta = { dir.x * moved, dir.y * moved, moved * dir.z };

    auto& owner = s_facets.owners[hit];
    *floor = (static_cast<WOWGUID>(owner.b) << 32) | owner.a;

    if (CollideLandsOn(move, hit, *delta)) {
        *landed = 1;
        return 1;
    }

    if (0.0f < dir.z) {
        float rise = move->GetJumpLiftTime() - elapsed;

        if (0.0f < rise && *dist <= rise * move->m_currentSpeed && CollideContactIsTop(contact, contactCount)) {
            *topHit = 1;
            *floor = 0;
            return 1;
        }
    }

    *slide = CollideFallSlide(move, dir, *dist, length, hit, contact, contactCount);

    return 1;
}

// ref: FUN_0075cf80
// A fall becomes a long fall (0x2000): from a jump once it has dropped a ninth of a yard below
// where it started, from a step off a ledge after half a second.
static void CollideCheckFallingFar(CMovementData_C* move) {
    uint32_t moveFlags = move->m_moveFlags;

    if (moveFlags & 0x20002000) {
        return;
    }

    if (!(moveFlags & 0x1000) || move->m_jumpVelocity == 0.0f) {
        if (499 < move->m_fallTime) {
            move->m_moveFlags = moveFlags | 0x2000;
        }
    } else if (move->m_position.z <= move->m_fallStartElevation - COLLIDE_FALL_FAR) {
        move->m_moveFlags = moveFlags | 0x2000;
    }
}

// ref: FUN_007612b0
// Fall `delta` over `ms`, sliding off what it hits, until it lands or the time is used; the time
// the step took. With `report`, a landing is reported and the moves deferred in the air applied.
static uint32_t CollideFallStep(CMovementData_C* move, int32_t time, uint32_t ms, C3Vector delta, int32_t report) {
    float span = MsToSeconds(ms);
    float start = MsToSeconds(move->m_fallTime);
    float elapsed = 0.0f;
    uint32_t stalls = 0;
    WOWGUID floor = 0;
    C2Vector slide = { 0.0f, 0.0f };
    int32_t reanchor = 0;
    int32_t dropped = 0;
    int32_t landed = 0;
    int32_t headHit = 0;

    float savedSpeed = move->m_currentSpeed;
    C2Vector savedDirection2d = move->m_direction2d;
    C3Vector savedDirection = move->m_direction;

    float length = std::sqrt(delta.z * delta.z + delta.x * delta.x + delta.y * delta.y);
    float horizontal = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    float vertical = std::fabs(delta.z);
    C2Vector startXY = { move->m_position.x, move->m_position.y };
    float startHorizontal = horizontal;
    int32_t pass = 0;
    float total = span;

    while (MOVE_TURN_EPSILON <= std::fabs(length)) {
        float inv = 1.0f / length;
        C3Vector dir = { inv * delta.x, inv * delta.y, inv * delta.z };
        float at = start + elapsed;
        float dist = length;

        int32_t result = CollideFallSweep(move, at, &delta, &dist, &slide, &floor, &landed, &headHit);

        if (result == 2) {
            // The world is not there: the time passes without the fall.
            int32_t skipped = static_cast<int32_t>(std::nearbyint((span - elapsed) * 1000.0f));
            move->m_anchorTime -= skipped;
            move->m_fallTime -= skipped;
            move->SkipTime(skipped);
            total = span;
            break;
        }

        float used;

        if (result == 0) {
            used = span - elapsed;
        } else {
            used = CollideFallLanding(move, span - elapsed, at, &delta, &dist, headHit, horizontal);
        }

        move->m_position.x += delta.x;
        move->m_position.y += delta.y;
        move->m_position.z += delta.z;

        elapsed += used;

        if (headHit) {
            // The jump hit its head: the fall starts over from here, falling far.
            move->m_moveFlags |= 0x2000;
            move->m_fallTime = 0;
            move->m_jumpVelocity = 0.0f;
            move->m_fallStartElevation = move->m_position.z;
            total = elapsed;
            break;
        }

        if (landed) {
            move->StopFall();
            total = elapsed;
            break;
        }

        if (span <= elapsed + COLLIDE_TIME_EPSILON) {
            total = span;
            break;
        }

        reanchor = 1;
        length -= dist;

        bool deflect = true;

        if (used <= COLLIDE_TIME_EPSILON) {
            stalls++;

            if (6 <= stalls) {
                if (dropped || !Differs(horizontal, 0.0f)) {
                    move->StopFall();
                    total = elapsed;
                    break;
                }

                // Stuck against something: give up the horizontal motion and drop straight down.
                stalls = 0;
                move->m_currentSpeed = 0.0f;
                dropped = 1;
                delta = { 0.0f, 0.0f, dir.z * length };
                length = std::fabs(delta.z);
                deflect = false;
            }
        } else {
            stalls = 1;
        }

        if (deflect) {
            delta.z = dir.z * length;
            delta.x = dir.x * length + slide.x;
            delta.y = dir.y * length + slide.y;

            // A slide that has carried the unit further than the fall's own reach, and well past
            // what the drop allows, stops the fall where it is.
            if (pass) {
                float fx = (move->m_position.x - startXY.x) + delta.x;
                float fy = (move->m_position.y - startXY.y) + delta.y;
                float wander = fy * fy + fx * fx;
                float reach = startHorizontal + MOVE_TURN_EPSILON;
                float drop = vertical * COLLIDE_SLIDE_BACK;

                if (reach * reach < wander && drop * drop < wander) {
                    move->StopFall();
                    total = elapsed;
                    break;
                }
            }

            C2Vector flat = { delta.x, delta.y };
            horizontal = std::sqrt(flat.x * flat.x + flat.y * flat.y);

            if (MOVE_TURN_EPSILON <= std::fabs(horizontal)) {
                flat.x *= 1.0f / horizontal;
                flat.y *= 1.0f / horizontal;

                float speed = (flat.y * move->m_direction2d.y + move->m_direction2d.x * flat.x) * move->m_currentSpeed;
                move->m_currentSpeed = speed < 0.0f ? 0.0f : speed;
            } else {
                move->m_currentSpeed = 0.0f;
            }

            move->m_direction2d = flat;
            length = std::sqrt(delta.z * delta.z + delta.x * delta.x + delta.y * delta.y);
            move->m_direction = { move->m_direction2d.x, move->m_direction2d.y, 0.0f };
        }

        pass++;
    }

    int32_t usedMs = static_cast<int32_t>(std::nearbyint(total * 1000.0f));

    if (!headHit) {
        move->m_fallTime += usedMs;
    }

    if (report) {
        // A head hit's report (FUN_006e9270 -> FUN_0071f2c0) goes with the remote-movement
        // acknowledgements.

        if (!(move->m_moveFlags & 0x1000)) {
            if (floor) {
                move->SetFloorObject(floor);
            }

            if (move->ApplyDeferredMoves()) {
                return usedMs;
            }
        } else {
            CollideCheckFallingFar(move);

            // A jump keeps the run it was jumped with.
            if ((move->m_moveFlags & 0x1000) && move->m_jumpVelocity != 0.0f && (move->m_moveFlags & 0xf)) {
                move->m_currentSpeed = savedSpeed;
                move->m_direction2d = savedDirection2d;
                move->m_direction = savedDirection;
            }

            if (floor) {
                move->SetFloorObject(floor);
            }
        }
    }

    if (reanchor) {
        move->ResetAnchor(0);
    }

    (void)time;

    return usedMs;
}

// ref: FUN_007618b0
// Fall for `ms`: the fall's own displacement plus the horizontal motion, swept against the world.
static uint32_t CollideFall(CMovementData_C* move, int32_t time, uint32_t ms, float dist, const C2Vector& dir) {
    if (move->m_transportGUID && move->SetFloorObject(0)) {
        return 0;
    }

    if (ms == 0) {
        return 0;
    }

    float fall = move->GetFallDistanceFromHere(static_cast<int32_t>(move->m_fallTime + ms));
    float x = dir.x * dist;
    float y = dir.y * dist;
    float z = -fall;

    if (MOVE_SPEED_EPSILON <= std::fabs(std::sqrt(x * x + z * z + y * y))) {
        if (!s_facets.facets.Count()) {
            move->m_position.x += x;
            move->m_position.y += y;
            move->m_position.z += z;
            move->m_fallTime += ms;
            CollideCheckFallingFar(move);
        } else {
            ms = CollideFallStep(move, time, ms, { x, y, z }, 1);
        }

        move->m_up = { 0.0f, 0.0f, 1.0f };

        return ms;
    }

    move->m_fallTime += ms;

    return ms;
}

// ---- the rest of the movement's state a trial step disturbs -------------------------------------

namespace {

// ref: FUN_0075b480, FUN_0075b4f0
struct CollideSavedState {
    C3Vector anchorPosition;
    float anchorFacing;
    float anchorPitch;
    uint32_t anchorTime;
    uint32_t fallTime;
    C3Vector direction;
    C2Vector direction2d;
    uint32_t moveFlags;
    float currentSpeed;
};

void CollideSave(const CMovementData_C* move, CollideSavedState& state) {
    state.anchorPosition = move->m_anchorPosition;
    state.anchorFacing = move->m_anchorFacing;
    state.anchorPitch = move->m_anchorPitch;
    state.anchorTime = move->m_anchorTime;
    state.fallTime = move->m_fallTime;
    state.direction = move->m_direction;
    state.direction2d = move->m_direction2d;
    state.moveFlags = move->m_moveFlags;
    state.currentSpeed = move->m_currentSpeed;
}

void CollideRestore(CMovementData_C* move, const CollideSavedState& state) {
    move->m_anchorPosition = state.anchorPosition;
    move->m_anchorFacing = state.anchorFacing;
    move->m_anchorPitch = state.anchorPitch;
    move->m_anchorTime = state.anchorTime;
    move->m_fallTime = state.fallTime;
    move->m_direction = state.direction;
    move->m_direction2d = state.direction2d;
    move->m_moveFlags = state.moveFlags;
    move->m_currentSpeed = state.currentSpeed;
}

} // namespace

// ref: FUN_006ec7b0
// The local player stands on `floor`: a transport's guid joins it, zero leaves the one it rides
// unless it is still over it. NOT PORTED: the join and leave themselves (FUN_006ec400,
// CMovementData_C::ForceSetTransportInt) and the over-the-transport test (FUN_0074b5e0) are the
// transport port's; frozen has no transports for a unit to stand on, so nothing changes.
int32_t CMovementData_C::SetFloorObject(WOWGUID floor) {
    if (!this->IsActivePlayer() || this->IsSplineActive()) {
        return 0;
    }

    (void)floor;

    return 0;
}

// ref: FUN_0075eda0
// Held off the ground (by a spline that carries it, move-flags-2 0x4, or gravity off), or
// swimming or flying.
static bool CollideIsAirborneKind(const CMovementData_C* move) {
    auto spline = move->m_spline;

    if (spline) {
        if (!(spline->flags & 0x400) && (spline->flags & 0x200)) {
            return (move->m_moveFlags & 0x2200000) != 0;
        }

        if (!(spline->flags & 0x400) && (spline->flags & 0x2000)) {
            return true;
        }
    }

    if ((move->m_moveFlags2 & 0x4) || (move->m_moveFlags & 0x400)) {
        return true;
    }

    return (move->m_moveFlags & 0x2200000) != 0;
}

// ref: FUN_0075ee00
// The unit moves freely in three dimensions: airborne in the sense above, or on a spline that
// flies it (0x800 under move-flags-2 0x80) or drops it (0x200).
static bool CollideMovesFreely(const CMovementData_C* move) {
    if (CollideIsAirborneKind(move)) {
        return true;
    }

    auto spline = move->m_spline;

    if ((move->m_moveFlags2 & 0x80) && spline && !(spline->flags & 0x400) && (spline->flags & 0x800)) {
        return true;
    }

    return spline && !(spline->flags & 0x400) && (spline->flags & 0x200);
}

// ref: FUN_0075ec10
// The mean normal of the facets that face up (by more than a degree) and reach into the planes.
static C3Vector CollideMeanUp(const C4Plane* planes, uint32_t count) {
    C3Vector sum = { 0.0f, 0.0f, 0.0f };
    uint32_t taken = 0;

    for (uint32_t i = 0; i < s_facets.facets.Count(); i++) {
        const M2CollisionTriangle& facet = s_facets.facets[i];

        if (!(COLLIDE_UP_FACET < facet.plane.n.z)) {
            continue;
        }

        CollidePolygon polygon;
        FacetPolygon(facet, polygon);

        bool empty = false;

        for (uint32_t p = 0; p < count; p++) {
            ClipPolygon(polygon, planes[p], p);

            if (!polygon.count) {
                empty = true;
                break;
            }
        }

        if (empty) {
            continue;
        }

        taken++;
        sum.x += facet.plane.n.x;
        sum.y += facet.plane.n.y;
        sum.z += facet.plane.n.z;
    }

    if (!taken) {
        return { 0.0f, 0.0f, 1.0f };
    }

    float inv = 1.0f / static_cast<float>(taken);
    sum.x *= inv;
    sum.y *= inv;
    sum.z *= inv;

    float length = 1.0f / std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);

    return { length * sum.x, sum.y * length, length * sum.z };
}

// ref: FUN_0075ee60
// The up vector: the mean of the ground facets under the unit's box.
static void CollideUpdateUp(CMovementData_C* move) {
    float r = move->m_collisionRadius;
    const C3Vector& p = move->m_position;

    C4Plane planes[6] = {
        { { 1.0f, 0.0f, 0.0f }, -p.x - r },
        { { -1.0f, 0.0f, 0.0f }, p.x - r },
        { { 0.0f, 1.0f, 0.0f }, -p.y - r },
        { { 0.0f, -1.0f, 0.0f }, p.y - r },
        { { 0.0f, 0.0f, 1.0f }, -p.z - move->m_collisionHeight },
        { { 0.0f, 0.0f, -1.0f }, p.z },
    };

    move->m_up = CollideMeanUp(planes, 6);
}

// ref: FUN_0075d3c0
// A unit carried by a spline into a place the world has not loaded goes there anyway: it is put at
// `target`, tilted along its way, and the collision's aftermath runs as if it had collided.
static void CollidePlaceOnSpline(CMovementData_C* move, const C3Vector& target, int32_t time, uint32_t ms) {
    float fx = target.x - move->m_position.x;
    float fy = target.y - move->m_position.y;
    float fz = target.z - move->m_position.z;
    float length = fx * fx + fy * fy + fz * fz;

    if (MOVE_SPEED_EPSILON < length) {
        float inv = 1.0f / std::sqrt(length);
        fx *= inv;
        fy *= inv;
        fz *= inv;
    }

    float sx = -fx;
    float sy = fy;
    float side = sy * sy + sx * sx;

    if (MOVE_SPEED_EPSILON < side) {
        float inv = 1.0f / std::sqrt(side);
        sx *= inv;
        sy *= inv;
    }

    move->m_up = { sx * fz, -(fz * sy), sy * fy - sx * fx };
    move->m_position = target;

    move->OnCollided(time + static_cast<int32_t>(ms), 0, move->m_moveFlags, move->m_moveFlags2, 0, 0);
}

// ---- the pyramid sweep alone, for the hover probe -----------------------------------------------

// ref: FUN_0075cd70
static void CollideSweepPyramid(const CFacetList& list, const C3Vector& dir, float dist, const C3Vector& swept,
                                const C4Plane* hull, const C3Vector* points, uint32_t* hit, C4Plane* contact,
                                uint32_t* contactCount, float* t) {
    for (uint32_t k = 0; k < 4; k++) {
        const C4Plane& plane = hull[5 + k];
        float facing = plane.n.y * swept.y + plane.n.x * swept.x + plane.n.z * swept.z;
        C4Plane volume[4];

        if (0.0f < facing && TriangleSweepPlanes(points, &s_hullFaces[20 + k * 3], plane, swept, volume)) {
            float touch = dist;

            if (SweepFacets(list, dir, volume, 3, hull, 5 + k, &touch, hit)) {
                if (*t - COLLIDE_EPSILON <= touch) {
                    if (touch < COLLIDE_EPSILON + *t) {
                        contact[*contactCount] = plane;
                        (*contactCount)++;
                    }
                } else {
                    contact[0] = plane;
                    *contactCount = 1;
                }

                if (touch < *t) {
                    *t = touch;

                    if (1 < *contactCount) {
                        SwapPlanes(contact[*contactCount - 1], contact[0]);
                    }
                }
            }
        }
    }
}

// ref: FUN_0075f520
int32_t CMovementData_C::QueryHoverHeight(float* height, int32_t* noFloor, WOWGUID* floorObject) {
    if (noFloor) {
        *noFloor = 0;
    }

    if (floorObject) {
        *floorObject = 0;
    }

    float probe = this->m_hoverHeight + this->m_hoverHeight;
    *height = probe;

    C3Vector down = { 0.0f, 0.0f, -1.0f };
    C3Vector swept = { 0.0f, 0.0f, -probe };

    C4Plane hull[9];
    C3Vector points[9];
    CollideHull(this, this->m_position, this->m_collisionHeight, hull, points);

    if (!CollideGatherNear(this, swept)) {
        *height = 0.0f;
        return 0;
    }

    uint32_t hit = s_facets.facets.Count();
    C4Plane contact[4];
    uint32_t contactCount = 0;

    CollideSweepPyramid(s_facets, down, probe, swept, hull, points, &hit, contact, &contactCount, height);

    if (hit < s_facets.facets.Count()) {
        if (floorObject) {
            auto& owner = s_facets.owners[hit];
            *floorObject = (static_cast<WOWGUID>(owner.b) << 32) | owner.a;
        }

        if (*height < this->m_hoverHeight && CollideFacetSteepFor(this, hit)) {
            *height = this->m_hoverHeight;
        }
    } else if (noFloor) {
        *noFloor = 1;
    }

    if (*height < COLLIDE_EPSILON) {
        *height = 0.0f;
    }

    return 1;
}

// ---- walking ------------------------------------------------------------------------------------

// ref: FUN_007619c0
// Would a step down ahead carry the unit on in the direction it walks? A trial fall of `height`
// along the walk, from `start`, with the movement put back afterwards.
static bool CollideStepsDownAhead(CMovementData_C* move, const C2Vector& dir, const C2Vector& start, float height) {
    uint32_t ms = static_cast<uint32_t>(std::nearbyint(std::sqrt((height + height) * 0.051837362349033356f) * 1000.0f));

    CollideSavedState saved;
    CollideSave(move, saved);

    move->StartFall(0.0f);

    float fall = move->GetFallDistanceFromHere(static_cast<int32_t>(ms));
    C3Vector delta = { move->m_direction2d.x, move->m_direction2d.y, -fall };

    for (uint32_t done = 0; done < ms;) {
        if (!(move->m_moveFlags & 0x1000)) {
            break;
        }

        done += CollideFallStep(move, 0, ms - done, delta, 0);
    }

    CollideRestore(move, saved);

    float mx = move->m_position.x - start.x;
    float my = move->m_position.y - start.y;
    float length = std::sqrt(mx * mx + my * my);

    if (move->m_collisionRadius <= length) {
        float inv = 1.0f / length;

        if (COLLIDE_STEP_DOT < dir.y * inv * my + dir.x * inv * mx) {
            return true;
        }
    }

    return false;
}

// ref: FUN_00761b00
// Against a facet the walk cannot cross: whether the unit can step up onto what is there. Nothing
// moves -- the position is put back either way -- but a step it can make leaves move flag
// 0x4000000 set with the elevation it started from (+0x88), and the walk climbs it from there.
static int32_t CollideStepUp(CMovementData_C* move, C2Vector* dir, const C3Vector& normal) {
    C3Vector start = move->m_position;
    C2Vector walk = *dir;

    float reach = move->m_collisionRadius + COLLIDE_EPSILON;

    if (reach < CollideStepHeight(move) * COLLIDE_STEP_REACH) {
        reach = CollideStepHeight(move) * COLLIDE_STEP_REACH;
    }

    uint32_t hit;
    C4Plane contact[4];
    uint32_t contactCount;
    float moved;

    if (normal.z <= COLLIDE_WALKABLE && 0.0f < normal.z) {
        // A slope too steep to walk: try straight away from it.
        float inv = 1.0f / std::sqrt(normal.y * normal.y + normal.x * normal.x);
        C3Vector away = { -(inv * normal.x), -(inv * normal.y), 0.0f };
        walk = { away.x, away.y };

        if (!CollideSweep(move, s_facets, away, reach, &hit, contact, &contactCount, &moved, nullptr)) {
            return 0;
        }

        if (hit == s_facets.facets.Count() || s_facets.facets[hit].plane.n.x != normal.x
            || s_facets.facets[hit].plane.n.y != normal.y || s_facets.facets[hit].plane.n.z != normal.z) {
            walk = *dir;
        }
    }

    C3Vector up = { 0.0f, 0.0f, 1.0f };
    float lift;

    if (!CollideSweep(move, s_facets, up, CollideStepRemaining(move), &hit, contact, &contactCount, &lift, nullptr)) {
        return 0;
    }

    float climbed = lift;

    if (move->m_moveFlags & 0x4000000) {
        climbed = (move->m_position.z - move->m_splineElevation) + lift;
    }

    if (std::fabs(climbed) < MOVE_SPEED_EPSILON) {
        move->m_moveFlags &= 0xfbffffff;
        return 1;
    }

    move->m_position.z += lift;

    C3Vector across = { walk.x, walk.y, 0.0f };
    float forward;

    if (!CollideSweep(move, s_facets, across, reach, &hit, contact, &contactCount, &forward, nullptr)) {
        return 0;
    }

    move->m_position.x += walk.x * forward;
    move->m_position.y += walk.y * forward;

    int32_t ended;

    if (Differs(forward, reach) && CollideFacetIsGround(move, hit, &ended)) {
        float rest = reach - forward;
        C3Vector push = { 0.0f, 0.0f, 0.0f };
        int32_t pushed = CollideSlopePush(contact, contactCount, &push) ? 1 : 0;

        C3Vector flat = { walk.x, walk.y, 0.0f };
        C3Vector rise = CollideSlopeLift(move, flat, &rest, s_facets.facets[hit].plane.n, pushed, push);
        C3Vector v = { rise.x + walk.x * rest, rise.y + rest * walk.y, rise.z };
        float length = std::sqrt(v.z * v.z + v.y * v.y + v.x * v.x);

        if (MOVE_SPEED_EPSILON <= std::fabs(length - 0.0f)) {
            v.x /= length;
            v.y /= length;
            v.z /= length;

            float along;

            if (!CollideSweep(move, s_facets, v, length, &hit, contact, &contactCount, &along, nullptr)) {
                return 0;
            }

            v.x *= along;
            v.y *= along;
            v.z *= along;

            forward = std::sqrt(v.x * v.x + v.y * v.y) + forward;
            move->m_position.x += v.x;
            move->m_position.y += v.y;
            move->m_position.z += v.z;
            lift = v.z + lift;
        }
    }

    if (hit == s_facets.facets.Count() || move->m_collisionRadius < forward) {
        C3Vector downward = { 0.0f, 0.0f, -1.0f };
        float drop;

        if (!CollideSweep(move, s_facets, downward, lift, &hit, contact, &contactCount, &drop, nullptr)) {
            return 0;
        }

        move->m_position.z -= drop;

        if (hit == s_facets.facets.Count() || !CollideFacetSteep(hit)
            || CollideStepsDownAhead(move, walk, { start.x, start.y }, lift - drop)) {
            move->m_position = start;

            if (!(move->m_moveFlags & 0x4000000)) {
                move->m_splineElevation = start.z;
            }

            move->m_moveFlags |= 0x4000000;

            return 1;
        }
    }

    move->m_moveFlags &= 0xfbffffff;
    move->m_position = start;

    return 1;
}

// ref: FUN_007620f0
// Walk `dist` along `dir` over `ms`: slide along walls, climb slopes, step up what the step
// height allows, and fall off what it does not stand on.
static uint32_t CollideWalk(CMovementData_C* move, int32_t time, uint32_t ms, float dist, const C2Vector& dir) {
    (void)time;

    if (std::fabs(dist) < MOVE_TURN_EPSILON) {
        return ms;
    }

    if (!s_facets.facets.Count()) {
        return move->StartFall(0.0f) ? 0 : ms;
    }

    C4Plane contact[4];
    uint32_t contactCount;
    float total = static_cast<float>(ms);
    float msLeft = total;
    float msUsed = 0.0f;
    C2Vector walk = dir;
    C3Vector sweep = { dir.x, dir.y, 0.0f };
    float distLeft = dist;
    float horizontal = dist;
    uint32_t stalls = 0;
    int32_t reanchor = 0;
    int32_t ended = 0;
    float climb = dist / move->m_collisionRadius;
    WOWGUID floor = 0;
    uint32_t hit;
    uint32_t lastHit;
    float moved;

    if (!CollideSweep(move, s_facets, sweep, dist, &hit, contact, &contactCount, &moved, nullptr)) {
        return ms;
    }

    for (;;) {
        lastHit = hit;

        C3Vector step = { sweep.x * moved, sweep.y * moved, sweep.z * moved };

        if (step.z <= climb) {
            climb -= step.z;
        } else {
            climb /= step.z;
            reanchor = 1;
            step.x *= climb;
            step.y *= climb;
            step.z = climb * step.z;
            moved = climb * moved;
            climb = 0.0f;
        }

        move->m_position.x += step.x;
        move->m_position.y += step.y;
        move->m_position.z += step.z;

        if (hit < s_facets.facets.Count()) {
            // Stopped against a facet.
            auto& owner = s_facets.owners[hit];
            floor = (static_cast<WOWGUID>(owner.b) << 32) | owner.a;

            float spent = (moved / distLeft) * msLeft;
            distLeft -= moved;
            horizontal -= std::sqrt(step.x * step.x + step.y * step.y);

            if (spent < 1.0f) {
                stalls++;

                if (5 < stalls) {
                    reanchor = 1;
                    hit = lastHit;
                    goto finish;
                }
            } else {
                stalls = 1;
            }

            msUsed += spent;
            msLeft -= spent;

            if (!(1.0f <= total - msUsed)) {
                if (Differs(total, msUsed)) {
                    reanchor = 1;
                }

                hit = lastHit;
                goto finish;
            }

            C3Vector normal = s_facets.facets[lastHit].plane.n;
            C3Vector correction;

            if (!CollideFacetIsGround(move, lastHit, &ended)) {
                if (ended) {
                    goto fall;
                }

                C3Vector push = { 0.0f, 0.0f, 0.0f };
                reanchor = 1;
                int32_t pushed = CollideSlopePush(contact, contactCount, &push) ? 1 : 0;
                uint32_t before = move->m_moveFlags;

                if (!CollideStepUp(move, &walk, normal)) {
                    return ms;
                }

                if (move->m_moveFlags & 0x4000000) {
                    float wanted = distLeft;
                    correction = CollideSlopeLift(move, sweep, &distLeft, normal, pushed, push);
                    msLeft = (distLeft / wanted) * msLeft;
                } else {
                    if (before & 0x4000000) {
                        goto fall;
                    }

                    correction = CollideWallSlide(sweep, distLeft, horizontal, normal);
                }
            } else {
                float wanted = distLeft;
                C3Vector push = { 0.0f, 0.0f, 0.0f };
                int32_t pushed = CollideSlopePush(contact, contactCount, &push) ? 1 : 0;

                correction = CollideSlopeLift(move, sweep, &distLeft, normal, pushed, push);
                msLeft = (distLeft / wanted) * msLeft;
            }

            C3Vector v = {
                sweep.x * distLeft + correction.x,
                sweep.y * distLeft + correction.y,
                sweep.z * distLeft + correction.z
            };

            distLeft = std::sqrt(v.x * v.x + v.z * v.z + v.y * v.y);
            sweep = v;

            if (!(MOVE_SECONDS <= distLeft)) {
                reanchor = 1;
                goto finish;
            }

            walk = { sweep.x, sweep.y };
            float inv = 1.0f / distLeft;
            sweep.x *= inv;
            sweep.y *= inv;
            sweep.z = inv * sweep.z;

            float flat = std::sqrt(walk.x * walk.x + walk.y * walk.y);

            if (MOVE_SPEED_EPSILON <= std::fabs(flat)) {
                walk.x = (1.0f / flat) * walk.x;
                walk.y = (1.0f / flat) * walk.y;
            }

            if (MOVE_TURN_EPSILON < horizontal - flat) {
                reanchor = 1;
            }

            horizontal = flat;

            if ((move->m_moveFlags & 0x4000000) && MOVE_TURN_EPSILON < v.z) {
                // A step-up climbs no higher than the step allows above where it began.
                float top = v.z + move->m_position.z;

                if (CollideStepHeight(move) + move->m_splineElevation < top) {
                    float allowed = (CollideStepHeight(move) + move->m_splineElevation) - move->m_position.z;
                    distLeft = (allowed / v.z) * distLeft;

                    if (Differs(allowed, v.z)) {
                        reanchor = 1;
                    }

                    if (distLeft < MOVE_SECONDS) {
                        goto fall;
                    }
                }
            }
        } else {
            // The whole sweep went through.
            msUsed += msLeft;
            float rest = total - msUsed;

            if (!(1.0f < rest)) {
                // Done: look for the ground under the end of the walk.
                C3Vector downward = { 0.0f, 0.0f, -1.0f };
                float drop;

                if (!CollideSweep(move, s_facets, downward, dist * COLLIDE_FOOT, &hit, contact, &contactCount, &drop, nullptr)) {
                    return ms;
                }

                move->m_position.z -= drop;

                if (hit == s_facets.facets.Count()) {
                    floor = 0;

                    if (!(move->m_moveFlags & 0x4000000)) {
                        goto fall;
                    }
                } else {
                    auto& owner = s_facets.owners[hit];
                    floor = (static_cast<WOWGUID>(owner.b) << 32) | owner.a;

                    if (!CollideFacetSteep(hit)) {
                        move->m_moveFlags &= 0xfbffffff;
                    } else if (!(move->m_moveFlags & 0x4000000)
                               || COLLIDE_FACING < s_facets.facets[hit].plane.n.x * walk.x + s_facets.facets[hit].plane.n.y * walk.y) {
                        goto fall;
                    }
                }

                goto finish;
            }

            walk = dir;
            distLeft = (rest / total) * dist;
            sweep = { dir.x, dir.y, 0.0f };
            msLeft = rest;
        }

        if (!CollideSweep(move, s_facets, sweep, distLeft, &hit, contact, &contactCount, &moved, nullptr)) {
            return ms;
        }
    }

finish:
    if (ended) {
        goto fall;
    }

    if (hit < s_facets.facets.Count()) {
        if (reanchor) {
            move->ResetAnchor(0);
        }

        move->SetFloorObject(floor);
    } else if (!(move->m_moveFlags & 0x4000000)) {
        goto fall;
    }

    return ms;

fall:
    move->StartFall(0.0f);

    return ms;
}

// ---- hovering and swimming ----------------------------------------------------------------------

// ref: FUN_00762980
// Hover: rise or sink toward the hover height at three and a half yards a second, sliding along
// what the move meets.
static uint32_t CollideHover(CMovementData_C* move, int32_t time, uint32_t ms, float dist, const C3Vector& dir) {
    if (ms == 0) {
        return 0;
    }

    float height;
    int32_t noFloor;
    WOWGUID floor;

    if (!move->QueryHoverHeight(&height, &noFloor, &floor)) {
        return ms;
    }

    if (noFloor && move->FallIfUnsupported()) {
        uint32_t fell = CollideFall(move, time, ms, dist, { dir.x, dir.y });

        if (fell) {
            return fell;
        }
    }

    float msTotal = static_cast<float>(ms);
    float maxRise = msTotal * MOVE_SECONDS * COLLIDE_HOVER_RATE;
    float x = dir.x * dist;
    float y = dir.y * dist;
    float toHover = move->m_hoverHeight - height;
    float z = -maxRise;

    if (-maxRise <= toHover) {
        z = toHover < maxRise ? toHover : maxRise;
    }

    z += dir.z * dist;

    float length = std::sqrt(z * z + x * x + y * y);
    C3Vector way = { x, y, z };

    if (MOVE_SPEED_EPSILON <= std::fabs(length)) {
        float inv = 1.0f / length;
        way = { inv * x, inv * y, inv * z };
    }

    if (std::fabs(length) < MOVE_TURN_EPSILON) {
        return ms;
    }

    C4Plane contact[7];
    uint32_t contactCount;
    uint32_t stalls = 0;
    float used = 0.0f;
    int32_t deflected = 0;
    uint32_t hit;
    float moved;

    if (!CollideSweep(move, s_facets, way, length, &hit, contact, &contactCount, &moved, nullptr)) {
        return ms;
    }

    for (;;) {
        if (s_facets.facets.Count() <= hit) {
            move->m_position.x += way.x * moved;
            move->m_position.y += way.y * moved;
            move->m_position.z += way.z * moved;

            if (deflected) {
                move->ResetAnchor(0);
            }

            move->SetFloorObject(floor);

            return ms;
        }

        auto& owner = s_facets.owners[hit];

        if (owner.a || owner.b) {
            floor = (static_cast<WOWGUID>(owner.b) << 32) | owner.a;
        }

        move->m_position.x += way.x * moved;
        move->m_position.y += way.y * moved;
        move->m_position.z += way.z * moved;

        float spent = (moved / length) * (msTotal - used);
        length -= moved;

        if (spent + MOVE_TURN_EPSILON <= 1.0f) {
            stalls++;

            if (6 <= stalls) {
                break;
            }
        } else {
            stalls = 1;
            used += spent;
        }

        if (msTotal - used < 1.0f) {
            if (!Differs(msTotal, used) && !deflected) {
                move->SetFloorObject(floor);
                return ms;
            }

            break;
        }

        C3Vector normal = s_facets.facets[hit].plane.n;
        C3Vector step = { way.x * length, way.y * length, way.z * length };
        C3Vector correction;

        if (!CollideFacetIsGround(move, hit, nullptr)) {
            correction = CollideWallSlide(way, length, std::sqrt(step.x * step.x + step.y * step.y), normal);
        } else {
            float s = (-normal.x * way.x + -normal.z * way.z + -normal.y * way.y) * length + MOVE_SECONDS;
            correction = { normal.x * s, normal.y * s, s * normal.z };
        }

        C3Vector v = { step.x + correction.x, step.y + correction.y, step.z + correction.z };
        deflected = 1;
        length = std::sqrt(v.x * v.x + v.z * v.z + v.y * v.y);

        if (std::fabs(length) < MOVE_TURN_EPSILON) {
            break;
        }

        float inv = 1.0f / length;
        way = { inv * v.x, inv * v.y, inv * v.z };

        if (!CollideSweep(move, s_facets, way, length, &hit, contact, &contactCount, &moved, nullptr)) {
            return ms;
        }
    }

    move->ResetAnchor(0);
    move->SetFloorObject(floor);

    return ms;
}

// ref: FUN_00760b40
// Swim or fly: slide along the world and, swimming, along the water's surface from below; a
// swimmer ascending into the surface jumps out of it.
static uint32_t CollideSwim(CMovementData_C* move, int32_t time, uint32_t ms, float dist, C3Vector dir) {
    (void)time;

    if ((move->m_transportGUID && move->SetFloorObject(0)) || ms == 0) {
        return 0;
    }

    if (std::fabs(dist) < MOVE_TURN_EPSILON) {
        return ms;
    }

    C4Plane contact[4];
    uint32_t contactCount;
    float msTotal = static_cast<float>(ms);
    float used = 0.0f;
    uint32_t stalls = 0;
    int32_t hitSolid = 0;
    int32_t reanchor = 0;
    float length = dist;
    uint32_t hit;
    float moved;

    int32_t swept = CollideSweep(move, s_facets, dir, length, &hit, contact, &contactCount, &moved, nullptr);

    for (;;) {
        if (!swept) {
            return ms;
        }

        const CFacetList* list = &s_facets;

        if (s_facets.facets.Count() <= hit) {
            if (!hitSolid && !move->IsHeldOffGround()) {
                if (!CollideSweep(move, s_waterFacets, dir, length, &hit, contact, &contactCount, &moved, nullptr)) {
                    return ms;
                }

                list = &s_waterFacets;
            }

            if (hit == list->facets.Count()) {
                move->m_position.x += dir.x * moved;
                move->m_position.y += dir.y * moved;
                move->m_position.z += dir.z * moved;

                if (reanchor) {
                    move->ResetAnchor(0);
                }

                return ms;
            }
        } else {
            hitSolid = 1;
        }

        move->m_position.x += dir.x * moved;
        move->m_position.y += dir.y * moved;
        move->m_position.z += dir.z * moved;

        float spent = (moved / length) * (msTotal - used);
        float rest = length - moved;

        if (spent + MOVE_TURN_EPSILON <= 1.0f) {
            stalls++;

            if (5 < stalls) {
                move->ResetAnchor(0);
                return ms;
            }
        } else {
            stalls = 1;
            used += spent;
        }

        if (!hitSolid && (move->m_moveFlags & 0x400000)) {
            // Ascending into the surface: out of the water.
            if (Differs(msTotal, used)) {
                reanchor = 1;
            }

            move->Jump(1);

            if (reanchor) {
                move->ResetAnchor(0);
            }

            return ms;
        }

        if (msTotal - used < 1.0f) {
            if (Differs(msTotal, used) || reanchor) {
                move->ResetAnchor(0);
            }

            return ms;
        }

        const C3Vector& normal = list->facets[hit].plane.n;
        reanchor = 1;

        float s = (-normal.x * dir.x + -normal.z * dir.z + -normal.y * dir.y) * rest + MOVE_SECONDS;
        C3Vector v = { normal.x * s + dir.x * rest, dir.y * rest + normal.y * s, dir.z * rest + normal.z * s };

        length = std::sqrt(v.x * v.x + v.z * v.z + v.y * v.y);

        if (std::fabs(length) < MOVE_TURN_EPSILON) {
            move->ResetAnchor(0);
            return ms;
        }

        float inv = 1.0f / length;
        dir = { inv * v.x, inv * v.y, inv * v.z };

        swept = CollideSweep(move, s_facets, dir, length, &hit, contact, &contactCount, &moved, nullptr);
    }
}

// ---- the entry ----------------------------------------------------------------------------------

// ref: FUN_00762e00
uint32_t CMovementData_C::Collide(int32_t time, uint32_t ms, const C3Vector& delta) {
    if (ms == 0) {
        return 0;
    }

    C3Vector target = {
        this->m_position.x + delta.x,
        this->m_position.y + delta.y,
        this->m_position.z + delta.z
    };

    // 0x00a37f94: how far inside the map's edge a mover has to stay.
    float margin = this->m_collisionRadius + 71.37559509277344f;
    float span = 34133.33203125f;
    float fromY = -(target.y - 17066.666f);
    float fromX = -(target.x - 17066.666f);

    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)
        || fromY <= margin || !(fromY < span - margin && margin < fromX && fromX < span - margin)) {
        return ms;
    }

    float dz = CollideMovesFreely(this) ? delta.z : 0.0f;
    float dist = std::sqrt(dz * dz + delta.x * delta.x + delta.y * delta.y);
    float speed = dist / (static_cast<float>(ms) * MOVE_SECONDS);
    C3Vector dir = { 0.0f, 0.0f, 0.0f };

    if (MOVE_TURN_EPSILON <= std::fabs(dist)) {
        float inv = 1.0f / dist;
        dir = { delta.x * inv, delta.y * inv, inv * dz };
    }

    uint32_t used = 0;
    int32_t placed = 0;

    while (used < ms) {
        if (!(this->m_moveFlags & 0x40c0100f) || (this->m_moveFlags & 0x800)) {
            break;
        }

        uint32_t remaining = ms - used;
        int32_t stepTime = static_cast<int32_t>(used) + time;
        float stepDist = static_cast<float>(remaining) * MOVE_SECONDS * speed;

        if (!CollideGather(this, stepDist, remaining, dir)) {
            // The world under the step is not loaded: a spline carries the unit there anyway,
            // anything else waits, telling the server it skipped the time.
            if (!this->IsSplineActive()) {
                this->SkipTime(remaining);
                this->m_anchorTime -= remaining;
            } else {
                CollidePlaceOnSpline(this, target, time, ms);
                placed = 1;
            }

            used = ms;
            break;
        }

        uint16_t oldFlags2 = this->m_moveFlags2;
        uint32_t oldFlags = this->m_moveFlags;
        uint32_t jumping = this->IsJumping();
        WOWGUID oldTransport = this->m_transportGUID;
        float oldSpeed = this->m_currentSpeed;
        uint32_t consumed;

        if (!CollideMovesFreely(this)) {
            if (!(this->m_moveFlags & 0x1000)) {
                if (!(this->m_moveFlags & 0x40000000)) {
                    consumed = CollideWalk(this, stepTime, remaining, stepDist, { dir.x, dir.y });
                } else {
                    consumed = CollideHover(this, stepTime, remaining, stepDist, dir);
                }
            } else {
                consumed = CollideFall(this, stepTime, remaining, stepDist, { dir.x, dir.y });
            }
        } else {
            consumed = CollideSwim(this, stepTime, remaining, stepDist, dir);
        }

        used += consumed;

        int32_t transportChanged = this->m_transportGUID != oldTransport ? 1 : 0;
        this->OnCollided(static_cast<int32_t>(used) + time, static_cast<int32_t>(ms - used), oldFlags, oldFlags2, jumping, transportChanged);

        if (transportChanged || (!(this->m_moveFlags & 0x1000) && (oldFlags & 0x1000))) {
            // Landed, or onto another transport: the time the step did not use is not walked on.
            uint32_t lost = remaining - consumed;

            if (this->m_anchorTime < lost) {
                this->m_anchorTime = 0;
            } else {
                this->m_anchorTime -= lost;
            }

            break;
        }

        if (!this->IsSplineActive() && this->m_currentSpeed != oldSpeed) {
            this->UpdateCurrentSpeed(0);
            break;
        }

        if (this->m_anchorTime == 0 && (oldFlags & 0xf)) {
            this->m_anchorTime = ms - used;
        }
    }

    if (!placed) {
        auto spline = this->m_spline;

        if (!spline || (spline->flags & 0x400) || !(spline->flags & 0x2000)) {
            CollideUpdateUp(this);
        }
    }

    return used;
}
