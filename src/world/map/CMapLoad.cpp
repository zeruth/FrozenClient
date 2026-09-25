#include "world/map/CMap.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
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
