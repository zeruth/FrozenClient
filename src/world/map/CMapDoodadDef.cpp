#include "world/map/CMapDoodadDef.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapEntity.hpp"

// ref: FUN_007c21e0
// A placed doodad says what it is. Without this the def reads as a plain base object, and every
// test that asks whether an entity is a doodad -- the placement, the lighting, the group
// linking -- quietly answers no.
CMapDoodadDef::CMapDoodadDef() {
    this->m_type |= CMapBaseObj::Type_DoodadDef;
}

// How far above the doodad the probe starts and how far down it reaches, out in the WORLD:
// DAT_00a41b18 and DAT_00a3e6e8. The drop is 1760 yards rather than a unit's twelve because a
// doodad is placed before anything has settled its height, so the probe has to cross a whole
// building from above rather than just find the floor beneath a pair of feet.
static const float DOODAD_PROBE_RISE = 1.5f;
static const float DOODAD_PROBE_DROP = 1760.0f;

// ref: FUN_007c1c40
// Vtable slot 3 for a placed doodad -- the same function CMapEntity answers for a unit, and the
// reason CMapStaticEntity's slot is pure.
//
// It differs from the unit's in the order of the two steps: this one offsets the doodad's world
// position and then transforms BOTH endpoints into the building, where CMapEntity transforms once
// and offsets in the building's space. That is not interchangeable -- a building standing at any
// pitch or roll turns a world-space vertical into a slanted segment -- so the two are separate
// bodies in the reference and separate bodies here.
void CMapDoodadDef::FloorLight(CMapObjDef* def, uint32_t groupIndex, const uint16_t* face,
                               const C3Vector* point) {
    CImVector color;
    color.value = 0;
    uint8_t exteriorBlend = 0;
    bool sampled;

    if (!face) {
        C3Vector above = { this->m_position.x, this->m_position.y,
                           this->m_position.z + DOODAD_PROBE_RISE };
        C3Vector below = { this->m_position.x, this->m_position.y,
                           this->m_position.z - DOODAD_PROBE_DROP };

        C3Segment segment;
        segment.start = above * def->m_inversePlacement;
        segment.end = below * def->m_inversePlacement;

        sampled = def->m_mapObj->GroupFloorColor(groupIndex, segment, &color, &exteriorBlend);
    } else {
        C3Vector local = *point * def->m_inversePlacement;

        sampled = def->m_mapObj->GroupFaceColor(groupIndex, local, *face, &color, &exteriorBlend);
    }

    if (!sampled) {
        return;
    }

    uint8_t alpha = 0;

    CMapEntity::ApplyFloorLight(color, exteriorBlend, &this->m_interiorDirColor,
                                &this->m_ambient, &this->m_flags7c, &alpha);

    if (exteriorBlend) {
        this->m_interiorDirColor.a = alpha;
    }
}
