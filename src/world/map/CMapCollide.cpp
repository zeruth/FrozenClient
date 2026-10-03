// The world's triangles inside a box, for the movement collision (MapObj.cpp / World.cpp in the
// reference: FUN_00783910 and the FUN_007a5f20 family). The camera's frustum query
// (CMap::QueryFrustumFacets) is the sibling of this one, and the two share the facet list, the
// per-chunk cell walk and the building hit records.

#include "world/map/CMapCollide.hpp"
#include "db/Db.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Shared.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "world/map/CChunkLiquid.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapDoodadDef.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapStaticEntity.hpp"
#include <tempest/Intersect.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Rect.hpp>
#include <tempest/matrix/C33Matrix.hpp>
#include <algorithm>
#include <cmath>

namespace {

const float MAP_HALF_EXTENT = 17066.666015625f; // DAT_009e2acc
const float CELLS_PER_YARD = 0.2399999946355819f; // DAT_00a3fd6c

// ref: DAT_00ce04c4
// A second stamp beside CMap::s_queryStamp: the doodads and the dynamic objects a box query has
// already taken, so a doodad linked into several cells is added once.
int32_t s_boxQueryStamp = 0;

// The owners run beside the facets; whatever added facets without owners is padded with zeros so
// the two stay index for index (the tail of FUN_007a5a60 and FUN_007a3e40).
void PadOwners(CFacetList& list) {
    uint32_t count = list.facets.Count();
    uint32_t before = list.owners.Count();

    if (before < count) {
        list.owners.SetCount(count);

        for (uint32_t i = before; i < count; i++) {
            list.owners[i] = { 0, 0 };
        }
    }
}

// The triangle record FUN_007a3db0 appends: its plane, then its three corners.
void AppendFacet(CFacetList& list, const C4Plane& plane, const C3Vector& a, const C3Vector& b, const C3Vector& c) {
    auto facet = list.facets.New();
    facet->plane = plane;
    facet->vertices[0] = a;
    facet->vertices[1] = b;
    facet->vertices[2] = c;
}

} // namespace

// ref: FUN_007a3e40
// A box as twelve triangles facing out, for what is there but not loaded: a tile still streaming
// in, or a doodad without a collision model.
void MapAddBoxFacets(const CAaBox& box, CFacetList& list) {
    if (!(box.b.x < box.t.x && box.b.y < box.t.y && box.b.z < box.t.z)) {
        return;
    }

    C3Vector p[8] = {
        { box.b.x, box.t.y, box.b.z },
        { box.t.x, box.t.y, box.b.z },
        { box.t.x, box.b.y, box.b.z },
        { box.b.x, box.b.y, box.b.z },
        { box.b.x, box.t.y, box.t.z },
        { box.t.x, box.t.y, box.t.z },
        { box.t.x, box.b.y, box.t.z },
        { box.b.x, box.b.y, box.t.z },
    };

    C4Plane planes[6];
    PlaneFromPoints(&planes[0], p[0], p[2], p[3]);
    PlaneFromPoints(&planes[1], p[1], p[6], p[2]);
    PlaneFromPoints(&planes[2], p[4], p[6], p[5]);
    PlaneFromPoints(&planes[3], p[0], p[7], p[4]);
    PlaneFromPoints(&planes[4], p[0], p[5], p[1]);
    PlaneFromPoints(&planes[5], p[2], p[7], p[3]);

    static const int32_t s_triangles[36] = {
        0, 2, 3,  0, 1, 2,
        1, 6, 2,  1, 5, 6,
        4, 6, 5,  4, 7, 6,
        0, 7, 4,  0, 3, 7,
        0, 5, 1,  0, 4, 5,
        2, 7, 3,  2, 6, 7,
    };

    for (int32_t t = 0; t < 36; t += 3) {
        AppendFacet(list, planes[t / 6], p[s_triangles[t]], p[s_triangles[t + 1]], p[s_triangles[t + 2]]);
    }

    PadOwners(list);
}

// ref: FUN_007a4270
// Two triangles making a wall quad from a corner and its two edges, facing `normal`.
static void MapAddWallFacets(CFacetList& list, const C3Vector& normal, const C3Vector& corner,
                             const C3Vector& edgeA, const C3Vector& edgeB) {
    C4Plane plane;
    plane.n = normal;
    plane.d = -(normal.y * corner.y + corner.x * normal.x + normal.z * corner.z);

    AppendFacet(list, plane, corner, corner + edgeA, edgeB + corner + edgeA);
    AppendFacet(list, plane, corner, edgeB + corner + edgeA, corner + edgeB);
}

// ref: FUN_007a43d0
// A chunk at the edge of the map (chunk flag 0x40) walls off the sides the box reaches past.
static void MapAddEdgeWalls(CMapChunk* chunk, const CAaBox& box, CFacetList& list) {
    // 0x00a3e9d8: the wall's height.
    const C3Vector up = { 0.0f, 0.0f, 32000.0f };
    const CAaBox& bounds = chunk->m_bounds;

    if (box.b.y < bounds.b.y) {
        MapAddWallFacets(list, { 0.0f, -1.0f, 0.0f }, { bounds.b.x, bounds.b.y, bounds.b.z },
                         { bounds.t.x - bounds.b.x, 0.0f, 0.0f }, up);
    }

    if (bounds.t.y < box.t.y) {
        MapAddWallFacets(list, { 0.0f, 1.0f, 0.0f }, { bounds.t.x, bounds.t.y, bounds.b.z },
                         { bounds.b.x - bounds.t.x, 0.0f, 0.0f }, up);
    }

    if (box.b.x < bounds.b.x) {
        MapAddWallFacets(list, { -1.0f, 0.0f, 0.0f }, { bounds.b.x, bounds.t.y, bounds.b.z },
                         { 0.0f, bounds.b.y - bounds.t.y, 0.0f }, up);
    }

    if (bounds.t.x < box.t.x) {
        MapAddWallFacets(list, { 1.0f, 0.0f, 0.0f }, { bounds.t.x, bounds.b.y, bounds.b.z },
                         { 0.0f, bounds.t.y - bounds.b.y, 0.0f }, up);
    }
}

// ref: FUN_007d8840
// The terrain triangles of a chunk's cells that the box reaches: the frustum gather's sibling,
// testing each corner against the box (ClassifyCornerZ) instead of six planes.
void CMapChunk::GatherBoxFacets(const CiRect& cells, const CAaBox& box, CFacetList& list) {
    static const int32_t s_cellPoints[5] = { 0, 9, 17, 1, 18 };                                  // DAT_00a40618
    static const int32_t s_cellFacets[4][3] = { { 17, 9, 0 }, { 9, 1, 0 }, { 9, 17, 18 }, { 9, 18, 1 } }; // DAT_00a405e8

    auto table = reinterpret_cast<C3Vector*>(CMapChunk::s_vertexTable);

    for (int32_t row = cells.minY; row <= cells.maxY; row++) {
        for (int32_t col = cells.minX; col <= cells.maxX; col++) {
            if (CMapChunk::s_holeMask[(col >> 1) + (row >> 1) * 4] & this->m_header->holes) {
                continue;
            }

            int32_t base = row * 17 + col;
            uint8_t outcodes[20];

            for (int32_t point : s_cellPoints) {
                table[base + point].z = this->m_heights[base + point];
                outcodes[point] = ClassifyCornerZ(box, table[base + point]);
            }

            for (auto& tri : s_cellFacets) {
                if (outcodes[tri[0]] & outcodes[tri[1]] & outcodes[tri[2]]) {
                    continue;
                }

                M2CollisionTriangle* facet = list.facets.New();

                facet->plane.n = { 0.0f, 0.0f, 1.0f };
                facet->plane.d = 0.0f;

                for (int32_t k = 0; k < 3; k++) {
                    const C3Vector& local = table[base + tri[k]];
                    facet->vertices[k] = {
                        local.x + this->m_position.x,
                        local.y + this->m_position.y,
                        local.z + this->m_position.z,
                    };
                }

                const C3Vector& p0 = facet->vertices[0];
                const C3Vector& p1 = facet->vertices[1];
                const C3Vector& p2 = facet->vertices[2];

                if (!CMap::s_useSse) {
                    PlaneFromPoints(&facet->plane, p0, p1, p2);
                } else {
                    float nx = (p1.y - p0.y) * (p2.z - p0.z) - (p1.z - p0.z) * (p2.y - p0.y);
                    float ny = (p1.z - p0.z) * (p2.x - p0.x) - (p2.z - p0.z) * (p1.x - p0.x);
                    float nz = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
                    float inv = FacetRsqrt(nx * nx + ny * ny + nz * nz);

                    facet->plane.n = { nx * inv, ny * inv, nz * inv };
                    facet->plane.d = -(facet->plane.n.x * p0.x + facet->plane.n.z * p0.z + p0.y * facet->plane.n.y);
                }
            }
        }
    }
}

// ref: FUN_007a4af0
// A doodad's collision triangles inside the box, owned by the doodad's object (zero for a placed
// doodad).
static void MapAddDoodadFacets(CMapDoodadDef* doodad, const CAaBox& box, CFacetList& list) {
    uint32_t before = list.facets.Count();

    doodad->m_model->GetCollisionTriangles(box, doodad->m_placement, list.facets);

    uint32_t count = list.facets.Count();
    list.owners.SetCount(count);

    for (uint32_t i = before; i < count; i++) {
        list.owners[i] = { 0, 0 };
    }
}

// ref: FUN_007a50c0
// The doodads of a link list the box reaches: collision triangles from a doodad with a
// collision model (0x80), its model box as a placeholder for one without.
static bool MapQueryDoodadFacets(CMapBaseObjRefList& links, const CAaBox& box, CFacetList& list, uint32_t flags) {
    if (!(flags & 0xf0000f)) {
        return true;
    }

    for (auto link = links.Head(); link; link = links.Next(link)) {
        auto entity = static_cast<CMapStaticEntity*>(link->owner);

        if ((entity->m_flags & 0x100) || entity->m_queryStamp == s_boxQueryStamp || !entity->m_model) {
            continue;
        }

        if (!(entity->m_type & CMapBaseObj::Type_DoodadDef)) {
            continue;
        }

        auto doodad = static_cast<CMapDoodadDef*>(entity);

        // A doodad owned by an object answers to the dynamic-object mask instead.
        if (!(flags & 0xf)) {
            continue;
        }

        CAaBox bounds = entity->m_collisionBounds;

        if (!(entity->m_flags & 0x80)) {
            entity->m_flags7c |= 0x10000;
            bounds = TransformBox(entity->m_model->m_shared->aaBox154, doodad->m_placement);
        }

        if (box.Intersects(bounds)) {
            if (!(entity->m_flags & 0x80)) {
                MapAddBoxFacets(bounds, list);
            } else {
                MapAddDoodadFacets(doodad, box, list);
            }
        }

        entity->m_queryStamp = s_boxQueryStamp;
    }

    return true;
}

// ref: FUN_007a3cf0
static void MapQueryLiquidFacets(CChunkLiquid* liquid, const CAaBox& box, const CiRect& cells, CFacetList& list) {
    CMapObjGroup::s_hitFlags = 0;
    CMapObjGroup::s_hitRecordCount = 0;
    CMapObjGroup::s_hitFacePoolCount = 0;
    CMapObjGroup::s_hitIndexPoolCount = 0;
    CMapObjGroup::s_hitPlacementCount = 0;

    uint8_t owner;

    if (liquid->QueryBox(&owner, box, cells)) {
        CWorld::AddHitFacets(list, 0, 0);
    }
}

// ref: FUN_007a5a60
// One chunk of the box query. A tile that should be there but is still loading fails the query,
// which holds the mover still until it arrives.
static bool MapQueryCellFacets(int32_t col, int32_t row, const CiRect& cells, const CAaBox& sweep,
                               const CAaBox& box, CFacetList& list, uint32_t flags) {
    uint32_t before = list.facets.Count();
    int32_t areaIndex = ((row >> 4) & 0x3f) * 64 + ((col >> 4) & 0x3f);
    CMapArea* area = CMap::s_areaGrid[areaIndex];

    if (!area || area->m_asyncObject) {
        if (!(CMap::s_areaInfo[areaIndex][0] & 0x1)) {
            return true;
        }

        // NOT PORTED: with query flag 0x80000000 the reference walls an unloaded tile off with a
        // box around its cells (0x007a5ab0 .. 0x007a5b4f) instead of failing; the flag is a unit
        // state (+0xa30 bit 0x400) nothing ported sets.
        (void)sweep;

        return false;
    }

    CMapChunk* chunk = area->m_chunks[(row & 0xf) * 16 + (col & 0xf)];

    if (!chunk) {
        return false;
    }

    if (chunk->m_flags & 0x40) {
        MapAddEdgeWalls(chunk, box, list);
    }

    CiRect local;
    local.minY = std::max(cells.minY - row * 8, 0);
    local.minX = std::max(cells.minX - col * 8, 0);
    local.maxY = std::min(cells.maxY - row * 8, 7);
    local.maxX = std::min(cells.maxX - col * 8, 7);

    CAaBox moved;
    moved.b = { box.b.x - chunk->m_position.x, box.b.y - chunk->m_position.y, box.b.z - chunk->m_position.z };
    moved.t = { box.t.x - chunk->m_position.x, box.t.y - chunk->m_position.y, box.t.z - chunk->m_position.z };

    if (flags & 0x100) {
        chunk->GatherBoxFacets(local, moved, list);
    }

    if (flags & 0x30000) {
        bool waterOnly = (flags & 0x10000) && !(flags & 0x20000);

        for (auto liquid = chunk->m_liquidList.Head(); liquid; liquid = chunk->m_liquidList.Next(liquid)) {
            if (waterOnly) {
                auto rec = g_liquidTypeDB.GetRecord(static_cast<int32_t>(liquid->m_liquidType));

                // Frozen-only null check: the reference reads the row's flags unguarded.
                if (!rec || !(rec->m_flags & 0x4)) {
                    continue;
                }
            }

            MapQueryLiquidFacets(liquid, moved, local, list);
        }
    }

    (void)before;
    PadOwners(list);

    if ((flags & 0xf0000f) && !MapQueryDoodadFacets(chunk->m_entityLinkList, box, list, flags)) {
        return false;
    }

    // FUN_007a5240: the dynamic objects linked to the chunk (game objects with collision), through
    // a callback the game object module installs (DAT_00ce04b0). Frozen's game objects do not
    // install one yet, so there is nothing to ask.

    return true;
}

// ref: FUN_007a55e0
// The buildings the box reaches: each in its own space, then its groups' doodads.
static bool MapQueryMapObjFacets(const CAaBox& sweep, const CAaBox& box, CFacetList& list, uint32_t flags, uint32_t* hitFlags) {
    C3Vector center = {
        (box.t.x + box.b.x) * 0.5f,
        (box.t.y + box.b.y) * 0.5f,
        (box.t.z + box.b.z) * 0.5f
    };

    CAaBox extents;
    extents.b = { box.b.x + -center.x, box.b.y + -center.y, box.b.z + -center.z };
    extents.t = { box.t.x + -center.x, -center.y + box.t.y, -center.z + box.t.z };

    // The destructible-building proxies (DAT_00aeede8) fail the query where they stand; frozen
    // does not build them (CMapChunk::CreateRefs skips destructible MODFs), so the list is empty.

    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        if ((def->m_flags & 0x100) || !def->m_mapObj || !box.Intersects(def->m_bounds)) {
            continue;
        }

        if (!def->m_mapObj->m_rootLoaded || !def->m_defGroupLinkList.Head()) {
            if (!(flags & 0x80000000) || sweep.Intersects(def->m_bounds)) {
                return false;
            }

            MapAddBoxFacets(def->m_bounds, list);
            continue;
        }

        C3Vector localCenter = center * def->m_inversePlacement;

        CAaBox local;
        TransformBoxExtents(C33Matrix(def->m_inversePlacement), extents, local);

        local.b.x += localCenter.x;
        local.b.y += localCenter.y;
        local.b.z += localCenter.z;
        local.t.x += localCenter.x;
        local.t.y += localCenter.y;
        local.t.z += localCenter.z;

        if (def->m_mapObj->BoxVsBounds(local)) {
            if (flags & 0xf0) {
                CMapObjGroup::s_hitFlags = 0;
                CMapObjGroup::s_hitRecordCount = 0;
                CMapObjGroup::s_hitFacePoolCount = 0;
                CMapObjGroup::s_hitIndexPoolCount = 0;
                CMapObjGroup::s_hitPlacementCount = 0;

                def->m_mapObj->QueryBoxGroups(local, flags, &def->m_placement, def);

                // The building's owner (+0x148, a destructible's game object) is zero for a
                // placed building.
                CWorld::AddHitFacets(list, 0, 0);

                if (hitFlags) {
                    *hitFlags |= CMapObjGroup::s_hitFlags;
                }
            }

            // NOT PORTED: the building's liquid for query flag 0x30000 (FUN_007af000 over
            // FUN_007ca110), which only swimming's and flying's queries set; the swimming port
            // carries it.
        }

        for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
            auto defGroup = static_cast<CMapObjDefGroup*>(link->owner);

            if (!box.Intersects(defGroup->m_bounds)) {
                continue;
            }

            if (!def->m_mapObj->IsGroupLoaded(defGroup->m_groupIndex)) {
                if (!(flags & 0x80000000)) {
                    return false;
                }

                MapAddBoxFacets(defGroup->m_bounds, list);
                continue;
            }

            if ((flags & 0xf0000f) && !MapQueryDoodadFacets(defGroup->m_doodadDefLinkList, box, list, flags)) {
                return false;
            }
        }
    }

    return true;
}

// ref: FUN_007a5f20
// PARTIAL: flying (query flag 0x200) also collides against the low-detail area heights
// (FUN_007a4590); the flight port carries it.
bool MapQueryBoxFacets(const CAaBox& sweep, const CAaBox& box, CFacetList& list, uint32_t flags, uint32_t* hitFlags) {
    CMap::s_queryStamp++;
    s_boxQueryStamp++;

    list.facets.SetCount(0);

    if (!MapQueryMapObjFacets(sweep, box, list, flags, hitFlags)) {
        return false;
    }

    float fromX = -(box.b.x - MAP_HALF_EXTENT);
    float fromY = -(box.b.y - MAP_HALF_EXTENT);

    // 0x009e2ac8: the map's extent.
    const float extent = 34133.33203125f;

    if (!(0.0f <= -(box.t.y - MAP_HALF_EXTENT) && 0.0f <= -(box.t.x - MAP_HALF_EXTENT)
          && fromY < extent && fromX < extent)) {
        return false;
    }

    CiRect cells;
    cells.maxX = static_cast<int32_t>(std::nearbyint(fromY * CELLS_PER_YARD - 0.5f));
    cells.maxY = static_cast<int32_t>(std::nearbyint(fromX * CELLS_PER_YARD - 0.5f));
    cells.minX = static_cast<int32_t>(std::nearbyint(-(box.t.y - MAP_HALF_EXTENT) * CELLS_PER_YARD - 0.5f));
    cells.minY = static_cast<int32_t>(std::nearbyint(-(box.t.x - MAP_HALF_EXTENT) * CELLS_PER_YARD - 0.5f));

    bool complete = true;

    for (int32_t row = cells.minY >> 3; row <= cells.maxY >> 3; row++) {
        for (int32_t col = cells.minX >> 3; col <= cells.maxX >> 3; col++) {
            if (!MapQueryCellFacets(col, row, cells, sweep, box, list, flags)) {
                complete = false;
            }
        }
    }

    return complete;
}
