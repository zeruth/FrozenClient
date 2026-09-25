#include "world/map/CMap.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include <tempest/Matrix.hpp>
#include <cmath>

// Turning a tile's placement records into map objects (reference MapLoad.cpp).

TSHashTable<CMapObjDef, HASHKEY_NONE> CMapObjDef::s_uniqueIds;

// Distance from the map's NW corner to its centre: the placement records are corner-relative
static const float MAP_HALF_EXTENT = 17066.666015625f;

static const float DEG2RAD = 0.0174532924f;
static const float PI = 3.14159274f;

// ref: FUN_007bf460
// One WMO placement. With dedup set, a building another tile already placed is handed back
// instead of placed twice. The record's axes are the ADT's: its x is the world's -y, its y the
// world's z, and its z the world's -x, all off the map's corner.
CMapObjDef* CMap::CreateMapObjDef(const char* name, const SMODF* modf, const C3Vector& origin, int32_t dedup) {
    if (dedup) {
        auto existing = CMapObjDef::s_uniqueIds.Ptr(modf->uniqueId, HASHKEY_NONE());

        if (existing) {
            return existing;
        }
    }

    auto def = CMap::AllocMapObjDef();

    if (dedup) {
        CMapObjDef::s_uniqueIds.Insert(def, modf->uniqueId, HASHKEY_NONE());
    }

    def->m_position.x = origin.x - modf->position.z;
    def->m_position.y = origin.y - modf->position.x;
    def->m_position.z = origin.z + modf->position.y;

    float rotZ = modf->rotation.z * DEG2RAD;
    float rotX = modf->rotation.x * DEG2RAD;
    float rotY = modf->rotation.y * DEG2RAD + PI;

    def->m_flags = 0;
    def->m_nameId = modf->nameId;
    def->m_doodadSet = modf->doodadSet;
    def->m_nameSet = modf->nameSet;
    def->m_unk138 = 0;

    C44Matrix placement;
    placement.d0 = def->m_position.x;
    placement.d1 = def->m_position.y;
    placement.d2 = def->m_position.z;

    placement.RotateAroundZ(rotY);
    placement.RotateAroundY(rotX);
    placement.RotateAroundX(rotZ);

    def->m_placement = placement;
    def->m_inversePlacement = placement.AffineInverse();

    def->m_bounds.b.x = origin.x - modf->extentsMax.z;
    def->m_bounds.b.y = origin.y - modf->extentsMax.x;
    def->m_bounds.b.z = origin.z + modf->extentsMin.y;
    def->m_bounds.t.x = origin.x - modf->extentsMin.z;
    def->m_bounds.t.y = origin.y - modf->extentsMin.x;
    def->m_bounds.t.z = origin.z + modf->extentsMax.y;

    def->m_center.x = (def->m_bounds.b.x + def->m_bounds.t.x) * 0.5f;
    def->m_center.y = (def->m_bounds.t.y + def->m_bounds.b.y) * 0.5f;
    def->m_center.z = (def->m_bounds.t.z + def->m_bounds.b.z) * 0.5f;

    float dx = def->m_bounds.t.x - def->m_center.x;
    float dy = def->m_bounds.t.y - def->m_center.y;
    float dz = def->m_bounds.t.z - def->m_center.z;
    def->m_radius = sqrtf(dz * dz + dy * dy + dx * dx);

    def->m_mapObj = CMapObj::Create(name);

    return def;
}

// ref: FUN_007c6150
// The chunk's MCRF references turned into map objects: the WMO placements first, then the
// doodads. Each def the chunk reaches gets a link the chunk holds and the def counts.
//
// Two parts of the reference are not here. A MODF flagged destructible (bit 0) goes to the game
// object system instead of the map, through a path that is not ported; and the doodad half needs
// CMapDoodadDef, which is the next subsystem. Both are marked where they belong.
void CMapChunk::CreateRefs(CMapArea* area, const uint32_t* refs, uint32_t doodadCount, uint32_t mapObjCount) {
    C3Vector origin = { MAP_HALF_EXTENT, MAP_HALF_EXTENT, 0.0f };
    auto mapObjRefs = refs + doodadCount;

    for (uint32_t i = 0; i < mapObjCount; i++) {
        auto modf = &reinterpret_cast<const SMODF*>(area->m_mapObjDefs)[mapObjRefs[i]];

        if (modf->flags & 0x1) {
            // TODO the destructible building: the reference checks the def table for the sentinel
            // uniqueId 0x5476ed and the list at DAT_00aeede8 for this one, then builds a game
            // object from the MODF extents (FUN_0077f290 -> FUN_0079eff0) and warns
            // "Destructible building WMO(%s) has invalid geobox" when they are degenerate
            continue;
        }

        auto name = area->m_mapObjNames + area->m_mapObjNameOffsets[modf->nameId];
        auto def = CMap::CreateMapObjDef(name, modf, origin, 1);

        auto link = CMap::AllocBaseObjLink(def);
        link->ref = this;
        this->m_mapObjDefLinkList.LinkToTail(link);
    }

    // TODO the doodad half: for each of the first doodadCount refs, CMap::CreateDoodadDef
    // (FUN_007becd0) off the tile's MDDF, a link into m_entityLinkList, and FUN_007b4fa0 when the
    // def is flagged to join a map object def group
}

// ----------------------------------------------------------------------------------------------
// Streaming the buildings the camera can reach (reference Map.cpp)

// ref: FUN_007b48c0
// The squared distance from a point to a box: zero inside it, otherwise to the nearest face.
static float BoxDistanceSq(const CAaBox& box, const C3Vector& point) {
    float x = box.b.x <= point.x ? (point.x <= box.t.x ? point.x : box.t.x) : box.b.x;
    float y = box.b.y <= point.y ? (point.y <= box.t.y ? point.y : box.t.y) : box.b.y;
    float z = box.b.z <= point.z ? (point.z <= box.t.z ? point.z : box.t.z) : box.b.z;

    return (z - point.z) * (z - point.z) + (x - point.x) * (x - point.x) + (y - point.y) * (y - point.y);
}

// ref: FUN_007bde50
// One CMapObjDefGroup per group of the root, each holding the group's own box and sphere brought
// into world space by the def's placement, and flagged interior or exterior by its MOGI flags.
void CMap::CreateDefGroups(CMapObj* mapObj, CMapObjDef* def) {
    def->m_defGroups.SetCount(mapObj->m_groupCount);

    for (uint32_t i = 0; i < mapObj->m_groupCount; i++) {
        auto defGroup = CMap::AllocMapObjDefGroup();

        auto link = CMap::AllocBaseObjLink(defGroup);
        link->ref = def;
        def->m_defGroupLinkList.LinkToTail(link);

        def->m_defGroups[i] = defGroup;

        C3Vector center;
        float radius;
        mapObj->GroupBoundingSphere(i, &center, &radius);
        TransformPointInPlace(defGroup->m_center, center, def->m_placement);
        defGroup->m_radius = radius;

        CAaBox bounds;
        mapObj->GroupBounds(i, &bounds);
        defGroup->m_bounds = TransformBox(bounds, def->m_placement);

        defGroup->m_groupIndex = i;
        defGroup->m_ambientColor = def->m_ambientColor;
        defGroup->m_flags = 0;

        // MOGI bit 3 (indoor) or bit 6 (unreachable) makes the group interior; anything else is
        // outside weather and outdoor light
        if (mapObj->GroupFlags(i) & 0x48) {
            defGroup->m_flags |= 0x4;
        } else {
            defGroup->m_flags |= 0x2;
        }
    }
}

// ref: FUN_007b5d00
// The def's own setup, once its root has parsed: its world bounds from the root's, room for one
// light per MOLT entry, the root's ambient colour, and a group object per group.
void CMap::SetupMapObjDef(CMapObjDef* def, CMapObj* mapObj) {
    def->m_flags |= 0x80;

    C3Vector center;
    float radius;
    mapObj->BoundingSphere(&center, &radius);
    TransformPointInPlace(def->m_center, center, def->m_placement);
    def->m_radius = radius;

    CAaBox bounds;
    mapObj->Bounds(&bounds);
    def->m_bounds = TransformBox(bounds, def->m_placement);

    def->m_ambientColor = mapObj->m_ambientColor;

    def->m_lights.SetCount(mapObj->m_lightCount);

    for (uint32_t i = 0; i < mapObj->m_lightCount; i++) {
        def->m_lights[i] = nullptr;
    }

    CMap::CreateDefGroups(mapObj, def);
}

// ref: FUN_007b6110
// Every placed building, once per map update. A def whose box reaches the world's far box gets
// its root waited for and set up; each of its groups that reaches the far box starts its own
// file read, and one that reaches the near box is waited for rather than left to arrive.
//
// Not ported, and marked where they belong: the doodads and lights a loaded group places
// (FUN_007bdd70, FUN_007bf740, FUN_007c1f20), and the collision and portal work behind the
// def's own 0x80000000 flag.
void CMap::UpdateMapObjDefs(int32_t update) {
    int32_t canWait = CMap::s_streamingMode == 0 && CMap::s_loading == 0;

    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        auto mapObj = def->m_mapObj;

        if (CWorld::s_farBox.Intersects(def->m_bounds)) {
            if (canWait && !mapObj->m_rootLoaded) {
                mapObj->WaitForRoot();
            }

            if (!(def->m_flags & 0x80) && mapObj->m_rootLoaded) {
                CMap::SetupMapObjDef(def, mapObj);
            }
        }

        float distance = BoxDistanceSq(def->m_bounds, CWorld::s_targetPos);

        if (distance < mapObj->m_nearestDistanceSq) {
            mapObj->m_nearestDistanceSq = distance;
        }

        for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
            auto defGroup = static_cast<CMapObjDefGroup*>(link->owner);
            auto group = mapObj->GetGroup(defGroup->m_groupIndex, 1);

            if (group && CWorld::s_farBox.Intersects(defGroup->m_bounds)) {
                if (!(group->m_state & 0x1)) {
                    if (!group->m_asyncObject) {
                        mapObj->ReadGroup(defGroup->m_groupIndex);
                    }

                    if (canWait && CWorld::s_nearBox.Intersects(defGroup->m_bounds)) {
                        mapObj->WaitForGroup(defGroup->m_groupIndex);
                    }
                }

                group->m_unk190 = 0;

                if (group->m_state & 0x1) {
                    if (!(defGroup->m_flags & 0x10)) {
                        // TODO FUN_007bdd70(mapObj, group, def, defGroup): the group's doodads
                    }

                    if (!(defGroup->m_flags & 0x8)) {
                        // TODO FUN_007bf740(mapObj, group, def, defGroup): the group's lights,
                        // then FUN_007c1f20(defGroup->m_bounds)
                    }
                }
            }

            if (group) {
                float groupDistance = BoxDistanceSq(defGroup->m_bounds, CWorld::s_targetPos);

                if (groupDistance < group->m_nearestDistanceSq) {
                    group->m_nearestDistanceSq = groupDistance;
                }
            }

            // A set-up def hands each of its groups to the frame, once the group's file is in
            // and the group reaches what the frustum covers
            if (update && (def->m_flags & 0x80)) {
                if (!group || !(group->m_state & 0x1)) {
                    // TODO FUN_00794ad0(defGroup, FLT_MAX): the group leaves the collision grid
                    // while its file is still coming
                } else if (!(def->m_flags & 0x20)) {
                    if (CWorldScene::s_frustumBounds.Intersects(defGroup->m_bounds)) {
                        CWorldScene::BucketMapObjDefGroup(def, defGroup);
                    }

                    // TODO the group's portals, walked from here into the neighbours they open
                }
            }
        }

        if (!(def->m_flags & 0x80)) {
            // TODO FUN_007946d0(def->m_bounds, FLT_MAX): the def's place in the collision grid
        }
    }
}
