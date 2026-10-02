#include "world/map/CMapStaticEntity.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMap.hpp"
#include "util/Unimplemented.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"

// How big a thing has to be to reach each detail band, in yards across its widest side.
// DAT_00adf378
static const float DETAIL_THRESHOLDS[5] = { 1.0f, 4.0f, 15.0f, 100.0f, 100000.0f };

// ref: FUN_007bdb10
// Everything about where the entity is that could not be known until its model arrived. Before
// that the traversal has only the placement point and the model's global box to go on; this
// replaces both with the real thing.
void CMapStaticEntity::Place(const C44Matrix& placement) {
    CAaBox modelBox = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    CAaBox collisionBox = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    CAaSphere modelSphere;

    modelSphere.c = { 0.0f, 0.0f, 0.0f };
    modelSphere.r = 0.0f;

    if (this->m_model) {
        this->m_model->GetBoundingBox(modelBox);
        this->m_model->GetBoundingSphere(modelSphere);

        auto shared = this->m_model->m_shared;

        if (shared && !shared->m_m2DataLoaded) {
            this->m_model->WaitForLoad("CMapStaticEntity::Place");
        }

        if (shared && shared->m_data) {
            collisionBox = shared->m_data->collisionBounds.extent;
        }
    }

    this->m_sphere.c = modelSphere.c * placement;
    this->m_sphere.r = this->m_scale * modelSphere.r;

    // A model box that comes back fully inverted has nothing in it, and the entity collapses
    // onto its own point rather than taking a box turned inside out into the traversal.
    if (modelBox.b.x <= modelBox.t.x
        || modelBox.b.y <= modelBox.t.y
        || modelBox.b.z <= modelBox.t.z) {
        this->m_bounds = TransformBox(modelBox, placement);
    } else {
        this->m_bounds.b = this->m_position;
        this->m_bounds.t = this->m_position;
    }

    this->m_collisionBounds = TransformBox(collisionBox, placement);

    C3Vector mid = {
        (collisionBox.b.x + collisionBox.t.x) * 0.5f,
        (collisionBox.b.y + collisionBox.t.y) * 0.5f,
        (collisionBox.b.z + collisionBox.t.z) * 0.5f
    };

    this->m_collisionCenter = mid * placement;

    // The detail band is decided by the widest side of the placed box, so the same model counts
    // as bigger when it is scaled up.
    float extent = this->m_bounds.t.x - this->m_bounds.b.x;

    if (extent <= this->m_bounds.t.y - this->m_bounds.b.y) {
        extent = this->m_bounds.t.y - this->m_bounds.b.y;
    }

    if (extent <= this->m_bounds.t.z - this->m_bounds.b.z) {
        extent = this->m_bounds.t.z - this->m_bounds.b.z;
    }

    uint8_t level = 0;

    while (level < 4 && extent >= DETAIL_THRESHOLDS[level]) {
        level++;
    }

    this->m_detailLevel = level;

    // Anything below the third band gives up its own flag bit 6. The reference guards this on
    // world flag 0x8000, which nothing sets, so the guard is left out and the clear always
    // applies. It reads the model without checking there is one; frozen checks.
    if (level < 3 && this->m_model) {
        // m_flag40, not `m_flags & ~0x40`: the same two-storages confusion that stopped doodads
        // drawing at all. The reference's flags word is frozen's bitfield block.
        this->m_model->m_flag40 = 0;
    }
}

// ref: FUN_007c15f0
// Which side of a building's wall a placed entity ended up on, and its floor light if it ended
// up inside.
//
// The group it landed on decides. A group that is neither Exterior (MOGP 0x8) nor ExteriorLit
// (0x40) is a room, so the entity is marked interior; an open-air group -- or no group at all,
// which is what an unloaded one reads as -- marks it exterior. The two bits are ORed rather than
// assigned, so an entity that has already been classified keeps what it had.
//
// Only an interior entity then takes the floor light, through vtable slot 3. That slot is the
// whole reason this function could not be written before: CMapStaticEntity had three virtuals and
// the reference calls a fourth here, so porting it earlier would have meant either inventing a
// no-op or guessing what the call does.
//
// The face arrives BY VALUE and its address is what goes to FloorLight, so this is always the
// known-face path -- whatever found the group also knows which polygon the entity stands on, and
// FloorLight does not have to probe for it.
//
// It tests the flag rather than the branch it just took, which is not the same thing: an entity
// that was already interior from an earlier group is lit even when this group put 0x4 on it.
// Kept as the reference has it.
void ClassifyEntityInterior(CMapStaticEntity* entity, CMapObjDef* def,
                            CMapObjDefGroup* defGroup, uint16_t face, const C3Vector& point) {
    CMapObjGroup* group = def->m_mapObj->GetGroup(defGroup->m_groupIndex, 0);

    if (group && !(group->m_flags & 0x8) && !(group->m_flags & 0x40)) {
        entity->m_flags |= CMapBaseObj::Flag_Interior;
    } else {
        entity->m_flags |= CMapBaseObj::Flag_Exterior;
    }

    if (entity->m_flags & CMapBaseObj::Flag_Interior) {
        entity->FloorLight(def, defGroup->m_groupIndex, &face, &point);
    }
}

// Where the portal search starts, as a fraction of the segment: DAT_00a40314, and slightly PAST
// the far end at 1.05 rather than 1.0, so a portal sitting exactly on the endpoint is still found.
static const float PORTAL_SEARCH_LIMIT = 1.05f;

// How much nearer a portal crossing has to be than the geometry hit to replace it. DAT_009e8cd0.
static const float PORTAL_NEARER_EPSILON = 0.0001f;

// ref: FUN_007c1dc0
// One placed building group's answer to a segment: what geometry it hits, and whether the segment
// leaves the room through a portal instead.
//
// The group's BSP is asked for both hits at once -- QuerySegmentDual returns a collision hit and a
// render hit, which are different surfaces: a doorway's collision volume is not its drawn
// geometry. A face of -1 from either means that half found nothing, and that half's record is left
// alone rather than cleared, because its caller is accumulating across many groups.
//
// THE PORTAL PASS runs only for an interior group, and only replaces the COLLISION record. It is
// what lets a query started inside a room reach out of it: if the nearest portal crossing is
// closer than the geometry, the hit becomes that portal, the face becomes 0xffff, and the record's
// group becomes the one on the far side. The 0xffff is not a sentinel for failure -- the caller
// reads it as 'left through a doorway'.
//
// The reference compares `t - collision->distance` against 1e-4 with an x87 pair whose `jp` is
// taken unless the difference is strictly less, so an equal or NaN difference does NOT replace the
// record. A plain C `<` reproduces both of those.
void QueryDefGroupSegment(CMapObjDef* def, CMapObjDefGroup* defGroup, const C3Vector& start,
                          const C3Vector& end, SMapObjHit* collision, SMapObjHit* render) {
    CMapObj* mapObj = def->m_mapObj;

    CMapObjGroup* group = mapObj->GetGroup(defGroup->m_groupIndex, 0);

    if (!group) {
        return;
    }

    int32_t collisionFace = -1;
    int32_t renderFace = -1;

    C3Segment segment;
    segment.start = start;
    segment.end = end;

    // MOGP bit 3 is Exterior, so its complement is 'this group is a room'.
    uint16_t interior = (group->m_flags & 0x8) ? 0 : 1;

    if (group->QuerySegmentDual(segment, &collision->distance, &collisionFace,
                               &render->distance, &renderFace)) {
        if (collisionFace != -1) {
            collision->def = def;
            collision->defGroup = defGroup;
            collision->face = static_cast<uint16_t>(collisionFace);
            collision->interior = interior;
        }

        if (renderFace != -1) {
            render->face = static_cast<uint16_t>(renderFace);
            render->def = def;
            render->defGroup = defGroup;
            render->interior = interior;
        }
    }

    if (!interior) {
        return;
    }

    float t = PORTAL_SEARCH_LIMIT;
    uint32_t portalGroups[2] = { 0, 0 };

    if (!mapObj->SegmentVsPortals(defGroup->m_groupIndex, segment, &t, portalGroups)) {
        return;
    }

    if (!(t - collision->distance < PORTAL_NEARER_EPSILON)) {
        return;
    }

    // portalGroups[0] is the group the segment is heading into, as an index into the def's own
    // per-group array -- the reference reaches it through a hand-rolled accessor over four inline
    // slots and a heap spill (FUN_007917b0), which is frozen's m_defGroups.
    CMapObjDefGroup* target = def->m_defGroups[portalGroups[0]];

    if (!target) {
        return;
    }

    collision->def = def;
    collision->distance = t;
    collision->defGroup = target;
    collision->face = 0xffff;

    // FROZEN-ONLY null check: the reference reads the far group's flags without one.
    CMapObjGroup* targetGroup = mapObj->GetGroup(target->m_groupIndex, 0);

    if (targetGroup) {
        collision->interior = (targetGroup->m_flags & 0x8) ? 0 : 1;
    }
}

// ref: FUN_007c25d0
// One placed building's answer to a segment, over all of its groups.
//
// Everything below the transform is in the building's own space: all three points are brought in
// through the def's inverse placement once, and nothing is transformed back, because the hit
// records carry a distance along the segment rather than a position.
//
// WHICH RECORD gets written is chosen by the def's m_flags bit 0x400, and the two record arrays
// hold two entries each for exactly that reason. Note the polarity: bit SET writes entry 0, bit
// CLEAR writes entry 1. Its sibling walk QuerySegmentMapObjs (FUN_007d59b0) uses the OPPOSITE
// convention for the same flag, and both were read off their own disassembly rather than assumed
// from each other -- do not "fix" either to match the other.
//
// The per-group gate is three tests. The group must be a room (none of 0x410080 -- exterior,
// skybox and the rest), the segment must reach its bounds, and then a point test that only
// sometimes applies: an exterior group with the def's 0x400 clear is taken on the bounds alone,
// while everything else has to contain the third point outright.
bool QueryDefSegment(CMapObjDef* def, const C3Vector& start, const C3Vector& end,
                     const C3Vector& point, SMapObjHit* collision, SMapObjHit* render) {
    CMapObj* mapObj = def->m_mapObj;

    if (!mapObj) {
        return false;
    }

    C3Vector localStart = start * def->m_inversePlacement;
    C3Vector localEnd = end * def->m_inversePlacement;
    C3Vector localPoint = point * def->m_inversePlacement;

    uint32_t interiorOnly = def->m_flags & 0x400;
    uint32_t slot = interiorOnly ? 0 : 1;

    for (auto link = def->m_defGroupLinkList.Head(); link;
         link = def->m_defGroupLinkList.Next(link)) {
        auto defGroup = static_cast<CMapObjDefGroup*>(link->owner);

        if (!defGroup) {
            continue;
        }

        uint32_t groupIndex = defGroup->m_groupIndex;
        uint32_t flags = mapObj->GroupFlags(groupIndex);

        // Exterior, skybox and the rest: a group with any of these is not a room.
        if (flags & 0x410080) {
            continue;
        }

        if (!mapObj->SegmentVsGroupBounds(localStart, localEnd, groupIndex)) {
            continue;
        }

        // MOGP bit 3 is Exterior. An exterior group is taken on its bounds alone, but only while
        // the def is not asking for interiors; anything else has to contain the point.
        bool exterior = (flags & 0x8) != 0;

        if (!(!interiorOnly && exterior) && !mapObj->PointInGroupBox(localPoint, groupIndex)) {
            continue;
        }

        QueryDefGroupSegment(def, defGroup, localStart, localEnd, &collision[slot],
                             &render[slot]);
    }

    return true;
}

// ref: FUN_007c2700
// The whole segment query over placed buildings.
//
// Two ways in. With a chunk, only the buildings that chunk holds a link to are asked, which is how
// a query with a known location avoids the world. With none, every placed building in
// CMapObjDef::s_uniqueIds is asked. Both skip a def carrying m_flags 0x20 and reject the rest on
// the MODF box before opening one up.
//
// BOTH ARRAYS HOLD TWO RECORDS, and everything after the walk is about reconciling the four. They
// start empty at a distance of 1.05 -- past the end of the segment, so any real hit beats them --
// and then, in this order:
//
//   1. a collision slot that found nothing takes the render slot's hit outright;
//   2. if slot 0 is still empty, slot 1 slides down into it in BOTH arrays and slot 1 is
//      emptied -- and if slot 1 was empty too, nothing was hit anywhere and it returns false;
//   3. a render slot that found nothing takes the collision hit with its face forced to 0xffff.
//
// Step 3 is the one worth not "tidying": the face is deliberately made to read as 'not a real
// polygon', because the record is standing in for a render hit that never happened rather than
// reporting one.
bool QueryMapObjDefSegment(const C3Vector& start, const C3Vector& end, const C3Vector& point,
                           SMapObjHit* collision, SMapObjHit* render, CMapChunk* chunk) {
    for (uint32_t i = 0; i < 2; i++) {
        collision[i].def = nullptr;
        collision[i].distance = PORTAL_SEARCH_LIMIT;
        render[i].def = nullptr;
        render[i].distance = PORTAL_SEARCH_LIMIT;
    }

    if (chunk) {
        for (auto link = chunk->m_mapObjDefLinkList.Head(); link;
             link = chunk->m_mapObjDefLinkList.Next(link)) {
            auto def = static_cast<CMapObjDef*>(link->owner);

            if (!def || (def->m_flags & 0x20)) {
                continue;
            }

            if (!def->SegmentVsBounds(start, end)) {
                continue;
            }

            QueryDefSegment(def, start, end, point, collision, render);
        }
    } else {
        for (auto def = CMapObjDef::s_uniqueIds.Head(); def;
             def = CMapObjDef::s_uniqueIds.Next(def)) {
            if (def->m_flags & 0x20) {
                continue;
            }

            if (!def->SegmentVsBounds(start, end)) {
                continue;
            }

            QueryDefSegment(def, start, end, point, collision, render);
        }
    }

    for (uint32_t i = 0; i < 2; i++) {
        if (!collision[i].def && render[i].def) {
            collision[i] = render[i];
        }
    }

    if (!collision[0].def) {
        if (!collision[1].def) {
            return false;
        }

        collision[0] = collision[1];
        render[0] = render[1];
        collision[1].def = nullptr;
        render[1].def = nullptr;
    }

    for (uint32_t i = 0; i < 2; i++) {
        if (!render[i].def) {
            render[i] = collision[i];
            render[i].face = 0xffff;
        }
    }

    return true;
}

// The height a point gets when no terrain answered: DAT_00a3e6e4, and far above anything, so the
// distance computed from it lands outside every record and discards nothing.
static const float NO_TERRAIN_HEIGHT = 100000.0f;

// How far the fallback probe reaches UP from the entity's collision centre (DAT_009ea080), and the
// reciprocal that turns a height difference into a fraction of it (DAT_009e1134). They are a pair:
// the probe is a thousand yards long, so a distance along it and a height over a thousand are the
// same number and can be compared directly.
static const float FALLBACK_PROBE_RISE = 1000.0f;
static const float PROBE_FRACTION_SCALE = 0.001f;

// ref: FUN_007c28f0
// What a placed entity is standing in.
//
// Three steps. Ask the buildings along the segment; if the terrain turned out to be in FRONT of
// what they found, throw that away, because a thing standing on a hillside outside a wall is not
// inside the building behind it. And if neither the terrain nor a building answered, probe straight
// up a thousand yards from the entity's collision centre and ask again -- casting UP is how being
// indoors is decided, since what matters is whether there is a room's geometry overhead.
//
// THE TERRAIN CHUNK IS NOT KEPT, and the reference makes that easy to misread: it passes the
// address of one stack slot as GetTerrainHeight's out-chunk AND then stores the function's return
// value into the same slot, so the chunk is overwritten by the bool before anything reads it. What
// the rest of the function tests is only whether terrain answered. Reproduced as a discarded
// out-parameter rather than as a pointer that is secretly a flag.
//
// The two discard tests are `!(fraction > distance)` rather than `fraction < distance`. The
// reference's x87 pair only keeps the record when the terrain is strictly further, so an equal
// distance -- or a NaN from a degenerate segment -- discards it. A plain `<` would keep both.
//
// On a global-WMO map (CMap::s_globalMapObj) there is no terrain, and the query is skipped.
void QueryEntityMapObj(CMapStaticEntity* entity, C3Vector* start, C3Vector* end,
                       const C3Vector& point, uint32_t* outInterior, uint32_t* outHit,
                       SMapObjHit* collision, SMapObjHit* render) {
    collision[0].def = nullptr;
    render[0].def = nullptr;
    collision[1].def = nullptr;
    render[1].def = nullptr;

    float terrainHeight = NO_TERRAIN_HEIGHT;
    float terrainFraction = 2.0f;

    // Written by the call and then thrown away, exactly as the reference throws it away.
    CMapChunk* terrainChunk = nullptr;

    bool terrainFound = false;

    if (!CMap::s_globalMapObj) {
        terrainFound = CMap::GetTerrainHeight(*start, &terrainHeight, &terrainChunk);
        terrainFraction = (start->z - terrainHeight) * PROBE_FRACTION_SCALE;

        if (terrainFraction < 0.0f) {
            terrainFound = false;
        }
    }

    bool hit = false;

    if (!(entity->m_flags & 0x2000)) {
        hit = QueryMapObjDefSegment(*start, *end, point, collision, render, nullptr);
    }

    if (!terrainFound && !hit) {
        *start = entity->m_collisionCenter;
        *end = entity->m_collisionCenter;
        end->z += FALLBACK_PROBE_RISE;

        // The retry's answer is discarded; what matters is what it left in the records.
        QueryMapObjDefSegment(*start, *end, point, collision, render, nullptr);
    }

    if (entity->m_flags7c & 0x2000) {
        collision[1].def = nullptr;
        render[1].def = nullptr;
    }

    if (terrainFound) {
        if (!(terrainFraction > collision[0].distance)) {
            collision[0].def = nullptr;
            render[0].def = nullptr;
        }

        if (!(terrainFraction > collision[1].distance)) {
            collision[1].def = nullptr;
            render[1].def = nullptr;
        }
    }

    *outInterior = 0;
    *outHit = 0;

    if (collision[0].def) {
        *outHit = 1;
    }

    if (collision[1].def) {
        *outHit = 1;
    }

    if (collision[0].def) {
        *outInterior = collision[0].interior;

        return;
    }

    if (collision[1].def) {
        *outInterior = collision[1].interior;
    }
}

// The map's half extent (DAT_009e2acc), how many CHUNKS a yard is (DAT_00a40310) and the rounding
// bias (DAT_00aeedec). The scale is the coarse one: a chunk is 33.33 yards, where the cell
// addressing in CMap.cpp works eight times finer. Same shape as that addressing otherwise.
static const float CHUNK_MAP_HALF_EXTENT = 17066.666015625f;
static const float CHUNKS_PER_YARD = 0.03f;
static const float CHUNK_ROUND_BIAS = 0.5f;

// A world coordinate to a chunk index on that axis, the reference's own conversion.
static int32_t ChunkIndexFrom(float v) {
    return static_cast<int32_t>(
        roundf(-(v - CHUNK_MAP_HALF_EXTENT) * CHUNKS_PER_YARD - CHUNK_ROUND_BIAS));
}

// ref: FUN_007c2040
// Links the entity to every terrain chunk its box covers, so a chunk can reach the things resting
// on it without searching. Ported 2026-09-27, replacing the stub left when the two list inserts
// under it were still unidentified.
//
// The walk is over CHUNKS, not cells: the box's four horizontal corners become chunk indices at
// 0.03 per yard, and every chunk in that rectangle whose FLOOR is at or below the box's top is
// linked. That last test is what keeps a thing on a hilltop from being filed under the valley
// chunks its box happens to span.
//
// WHICH END of the chunk's list depends on what is being filed, and this is the same shape as
// CMap::LinkToMapObjDefGroup next door -- which is how that function's two inverted ends were found
// and fixed in this same change. An entity goes to the tail when m_flags7c carries 0x2 and the head
// otherwise; a doodad def always goes to the head; anything else is not filed at all, and the link
// stays on the owner's side only.
//
// A global-WMO map has no chunks to link to.
bool LinkEntityToChunks(CMapStaticEntity* entity) {
    if (CMap::s_globalMapObj) {
        return false;
    }

    const CAaBox& box = entity->m_bounds;

    // Larger coordinates give smaller indices, so the box's MAX corner starts each range.
    int32_t rowStart = ChunkIndexFrom(box.t.x);
    int32_t rowEnd = ChunkIndexFrom(box.b.x);
    int32_t colStart = ChunkIndexFrom(box.t.y);
    int32_t colEnd = ChunkIndexFrom(box.b.y);

    bool linked = false;

    for (int32_t row = rowStart; row <= rowEnd; row++) {
        for (int32_t col = colStart; col <= colEnd; col++) {
            // The reference indexes s_areaGrid inline and tests the async object itself, which is
            // exactly what GetLoadedArea does; one call here against none there.
            CMapArea* area = CMap::GetLoadedArea((col >> 4) & 0x3f, (row >> 4) & 0x3f);

            if (!area) {
                continue;
            }

            CMapChunk* chunk = area->m_chunks[(row & 0xf) * 16 + (col & 0xf)];

            if (!chunk) {
                continue;
            }

            // The chunk's floor against the entity's ceiling.
            if (!(chunk->m_bounds.b.z <= box.t.z)) {
                continue;
            }

            auto link = CMap::AllocBaseObjLink(entity);
            link->ref = chunk;

            if (entity->m_type & CMapBaseObj::Type_Entity) {
                if (entity->m_flags7c & 0x2) {
                    chunk->m_groundedLinkList.LinkToTail(link);
                } else {
                    chunk->m_groundedLinkList.LinkToHead(link);
                }
            } else if (entity->m_type & CMapBaseObj::Type_DoodadDef) {
                chunk->m_groundedLinkList.LinkToHead(link);
            }

            entity->m_flags |= CMapBaseObj::Flag_Exterior;
            linked = true;
        }
    }

    return linked;
}

// How far above the collision centre the ground probe starts (DAT_009e3004) and how far below it
// reaches (DAT_009ea080). The same two constants the portal epsilon and the fallback probe use.
static const float GROUND_PROBE_RISE = 0.1f;
static const float GROUND_PROBE_DROP = 1000.0f;

// ref: FUN_007c2a70
// What a placed entity is standing on, and everything that follows from knowing it.
//
// It drops a probe from just above the entity's collision centre to a thousand yards below and asks
// QueryEntityMapObj what is there. Two outcomes:
//
// NOTHING -- open terrain. The entity is linked to the chunks it covers and, if it is a Type_Entity,
// its ground type comes from CMap::GetTerrainType at its m_position. m_flags loses bit 0x200.
//
// A BUILDING. Each collision record's group is linked through CMap::LinkToMapObjDefGroup, the ground
// type comes from the WMO material under the face instead, and m_flags gains 0x200. If the query
// also said the surface was interior, m_flags7c gains bit 0 and the floor light is taken through
// ClassifyEntityInterior -- and that path RETURNS EARLY, so it is the one case where m_flags does
// not gain bit 2.
//
// The record slot is chosen from the RENDER array (slot 0 when it holds a def, else slot 1) while
// the links are made from the COLLISION array. That is not a slip in the reading: the two arrays
// are filled by different halves of the BSP query and the reference uses each for what it knows.
//
// ONE ODDITY KEPT AS-IS. The point handed to ClassifyEntityInterior is
// `center.z - (end.z - start.z) * t`, and since the probe points DOWN that term is negative, so the
// point ends up ABOVE the centre rather than at the hit. The disassembly at 0x007c2bcb is an
// `fsubr` and leaves no room for doubt about the sign. It reads like a sign slip in the original,
// but it is what the client shipped and the floor-light path is measured against it, so it is
// reproduced rather than corrected. Revisit only with a run that shows the lighting is wrong.
void ResolveEntityGround(CMapStaticEntity* entity) {
    C3Vector start = { entity->m_collisionCenter.x, entity->m_collisionCenter.y,
                       entity->m_collisionCenter.z + GROUND_PROBE_RISE };
    C3Vector end = { entity->m_collisionCenter.x, entity->m_collisionCenter.y,
                     entity->m_collisionCenter.z - GROUND_PROBE_DROP };

    uint32_t interior = 0;
    uint32_t hit = 0;

    SMapObjHit collision[2];
    SMapObjHit render[2];

    QueryEntityMapObj(entity, &start, &end, start, &interior, &hit, collision, render);

    // The type is read BEFORE anything below can change it, as the reference reads it.
    bool isEntity = (entity->m_type & CMapBaseObj::Type_Entity) != 0;

    int32_t groundType = -1;
    uint32_t flags;

    if (!hit) {
        LinkEntityToChunks(entity);

        if (isEntity) {
            if (!CMap::GetTerrainType(entity->m_position, &groundType)) {
                groundType = -1;
            }

            entity->m_groundType = groundType;
        }

        flags = entity->m_flags & ~0x200u;
    } else {
        uint32_t slot = render[0].def ? 0 : 1;

        if (collision[0].def) {
            CMap::LinkToMapObjDefGroup(entity, collision[0].defGroup);
        }

        if (collision[1].def) {
            CMap::LinkToMapObjDefGroup(entity, collision[1].defGroup);
        }

        if (isEntity && render[slot].def && render[slot].defGroup) {
            entity->m_groundType = static_cast<int32_t>(render[slot].def->GetPolyGroundType(
                render[slot].defGroup->m_groupIndex, render[slot].face));
        }

        entity->m_flags |= 0x200;
        flags = entity->m_flags;

        if (interior) {
            entity->m_flags7c |= 0x1;

            C3Vector point = { entity->m_collisionCenter.x, entity->m_collisionCenter.y,
                               entity->m_collisionCenter.z
                                   - (end.z - start.z) * render[slot].distance };

            ClassifyEntityInterior(entity, render[slot].def, render[slot].defGroup,
                                   render[slot].face, point);

            return;
        }
    }

    entity->m_flags = flags | 0x4;
}

// ref: FUN_007c2bf0
void LinkEntityToMapObjDefs(CMapStaticEntity* entity) {
    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        if ((def->m_flags & 0x20) || !def->m_mapObj || !def->m_mapObj->m_rootLoaded) {
            continue;
        }

        CAaBox box = TransformBox(entity->m_bounds, def->m_inversePlacement);

        if (!def->m_mapObj->BoxVsBounds(box)) {
            continue;
        }

        for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
            auto defGroup = static_cast<CMapObjDefGroup*>(link->owner);
            uint32_t flags = def->m_mapObj->GroupFlags(defGroup->m_groupIndex);

            if (!(flags & 0x410080) && (flags & 0x8) && def->m_mapObj->GroupBoxIntersects(box, defGroup->m_groupIndex, 1)) {
                CMap::LinkToMapObjDefGroup(entity, defGroup);
            }
        }
    }
}

// ref: FUN_007c2d30
void LinkEntityToMapObjDef(CMapStaticEntity* entity, CMapObjDef* def, CMapObjDefGroup* defGroup) {
    if (!def->m_mapObj || !def->m_mapObj->m_rootLoaded) {
        return;
    }

    CAaBox box = TransformBox(entity->m_bounds, def->m_inversePlacement);

    if (!def->m_mapObj->BoxVsBounds(box)) {
        return;
    }

    CMap::LinkToMapObjDefGroup(entity, defGroup);

    for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
        auto other = static_cast<CMapObjDefGroup*>(link->owner);

        if (other == defGroup) {
            continue;
        }

        uint32_t flags = def->m_mapObj->GroupFlags(other->m_groupIndex);

        if (!(flags & 0x410088) && def->m_mapObj->GroupBoxIntersects(box, other->m_groupIndex, 1)) {
            CMap::LinkToMapObjDefGroup(entity, other);
        }
    }
}

// ref: FUN_007c2e70
void ResolveObjectGround(CMapStaticEntity* entity) {
    const C3Vector& center = entity->m_collisionCenter;

    C3Vector start = { center.x, center.y, center.z + 4.0f };
    C3Vector end = { center.x, center.y, center.z - 1000.0f };
    C3Vector point = { center.x, center.y, center.z + 0.15f };

    float top = entity->m_bounds.t.z + 0.1f;

    if (top < start.z) {
        start.z = top;
    }

    uint32_t interior = 0;
    uint32_t hit = 0;
    SMapObjHit collision[2];
    SMapObjHit render[2];

    QueryEntityMapObj(entity, &start, &end, point, &interior, &hit, collision, render);

    if (interior) {
        LinkEntityToMapObjDef(entity, collision[0].def, collision[0].defGroup);

        entity->m_flags7c |= 0x1;

        C3Vector floor = { center.x, center.y, center.z - (end.z - start.z) * render[0].distance };
        ClassifyEntityInterior(entity, render[0].def, render[0].defGroup, render[0].face, floor);

        return;
    }

    LinkEntityToMapObjDefs(entity);
    LinkEntityToChunks(entity);
    entity->m_flags |= 0x4;
}

// ref: FUN_007c2f80
void RelinkEntity(CMapStaticEntity* entity) {
    for (auto link = entity->m_parentLinkList.Head(); link; ) {
        auto next = entity->m_parentLinkList.Next(link);
        CMap::FreeBaseObjLink(link);
        link = next;
    }

    if (entity->m_type & CMapBaseObj::Type_Entity) {
        entity->m_groundType = -1;
    }

    if (!(entity->m_flags7c & 0x2000)) {
        ResolveEntityGround(entity);
        return;
    }

    ResolveObjectGround(entity);
}
