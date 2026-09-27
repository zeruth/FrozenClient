#include "world/map/CMapStaticEntity.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObjGroup.hpp"
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
