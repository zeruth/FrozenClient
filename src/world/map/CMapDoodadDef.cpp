#include "world/map/CMapDoodadDef.hpp"
#include "model/CM2Lighting.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObjGroup.hpp"
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

// ref: FUN_007c10c0
// Hand the cached water side to a model's lighting. With no liquid over the entity it is
// above water only: bit 0x20 on, 0x40 off. With liquid and the top of its box above the surface
// it straddles it: both bits, and the lighting's liquid plane is the horizontal plane at the
// surface, (0, 0, 1, -height), which is what CM2SceneRender::SetupLighting clips a straddling
// model against. With liquid and the whole box under it: below only, 0x40 on, 0x20 off.
void CMapStaticEntity::ApplyWaterSide(CM2Lighting* lighting) const {
    if (!(this->m_flags7c & 0x20)) {
        lighting->m_flags = (lighting->m_flags & ~0x40u) | 0x20;

        return;
    }

    if (this->m_flags7c & 0x40) {
        lighting->m_flags |= 0x60;
        lighting->m_liquidPlane.n = { 0.0f, 0.0f, 1.0f };
        lighting->m_liquidPlane.d = -this->m_liquidHeight;

        return;
    }

    lighting->m_flags = (lighting->m_flags & ~0x20u) | 0x40;
}

// ref: FUN_007c23f0
// Which side of the water this doodad is on, worked out once and cached in m_flags7c (0x80 marks
// it done), then applied to the model's lighting. The probe point is the doodad's position at the
// bottom of its box.
//
// A doodad inside a building asks the building first: for each parent that is a placed group, the
// group's own liquid at the point, taken into the building's space and back. A group whose file has
// not arrived clears the done bit so the question is asked again next frame. The first parent that
// is not a group ends the walk and the terrain is asked instead.
//
// "Straddling" (0x40 kept) means the surface is at or below the top of the box. On the terrain
// branch the reference also writes 1.0 to +0x8c (m_opacity here) and clears bit 0x8; both are
// reproduced.
void CMapDoodadDef::SelectUnderwater(CM2Lighting* lighting) {
    if (!(this->m_flags7c & 0x80)) {
        this->m_flags7c = (this->m_flags7c & ~0x60u) | 0x80;

        C3Vector probe = { this->m_position.x, this->m_position.y, this->m_bounds.b.z };

        bool askTerrain = false;

        for (auto link = this->m_parentLinkList.Head(); link; link = this->m_parentLinkList.Next(link)) {
            auto parent = link->ref;

            if (!parent || !(parent->GetType() & CMapBaseObj::Type_MapObjDefGroup)) {
                askTerrain = true;
                break;
            }

            auto defGroup = static_cast<CMapObjDefGroup*>(parent);
            auto defLink = defGroup->m_parentLinkList.Head();
            auto def = defLink ? static_cast<CMapObjDef*>(defLink->ref) : nullptr;
            auto group = (def && def->m_mapObj) ? def->m_mapObj->GetGroup(defGroup->m_groupIndex, 0) : nullptr;

            if (!group) {
                this->m_flags7c &= ~0x80u;
                continue;
            }

            C3Vector local = probe * def->m_inversePlacement;
            uint32_t liquidType = 0;
            float height = 0.0f;

            if (group->GetLiquidAt(local, &liquidType, &height)) {
                this->m_flags7c |= 0x20;

                C3Vector surface = { local.x, local.y, height };
                this->m_liquidHeight = (surface * def->m_placement).z;

                if (this->m_liquidHeight <= this->m_bounds.t.z) {
                    this->m_flags7c |= 0x40 | 0x80;
                } else {
                    this->m_flags7c = (this->m_flags7c & ~0x40u) | 0x80;
                }

                this->ApplyWaterSide(lighting);

                return;
            }
        }

        if (askTerrain) {
            this->m_flags7c |= 0x80;

            uint32_t liquidType = 0;

            if (CMap::GetTerrainLiquid(probe, &liquidType, &this->m_liquidHeight, 0)) {
                uint32_t flags = this->m_flags7c;
                this->m_opacity = 1.0f;

                if (this->m_liquidHeight <= this->m_bounds.t.z) {
                    this->m_flags7c = (flags & ~0x8u) | 0x60;
                } else {
                    this->m_flags7c = (flags & ~0x48u) | 0x20;
                }
            }
        }
    }

    this->ApplyWaterSide(lighting);
}
