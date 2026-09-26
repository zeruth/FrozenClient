#include "world/map/CMapEntity.hpp"
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/CWorld.hpp"
#include <tempest/ColorConvert.hpp>
#include <cmath>

CMapEntity::CMapEntity() {
    this->m_type |= CMapBaseObj::Type_Entity;
}

// ref: FUN_007c1ad0
// A floor colour becomes the entity's two lights: the diffuse is the colour re-scaled in HSV so
// its brightest channel reaches diffuseMax (a dark room still lights the model), the ambient is
// the colour scaled down so no channel exceeds ambientMax.
void CMapEntity::SplitFloorLight(const CImVector& color, CImVector* diffuse, uint8_t diffuseMax, CImVector* ambient, uint8_t ambientMax) {
    uint8_t r = color.r;
    uint8_t g = color.g;

    uint8_t rg = r <= g ? g : r;
    uint8_t b = color.b;
    uint8_t maxC = b;

    if (b < rg) {
        maxC = g;

        if (g < r) {
            maxC = r;
        }
    }

    uint8_t peak;

    if (maxC == 0) {
        peak = 1;
    } else {
        peak = r <= g ? g : r;

        if (b < peak) {
            peak = r;

            if (r <= g) {
                peak = g;
            }
        } else {
            peak = b;
        }
    }

    *diffuse = color;

    if (peak < diffuseMax) {
        C3Vector rgb = { diffuse->r * (1.0f / 255.0f), diffuse->g * (1.0f / 255.0f), (1.0f / 255.0f) * (color.value & 0xFF) };
        C3Vector hsv = { 0.0f, 0.0f, 0.0f };

        RgbToHsv(rgb, hsv);
        hsv.z = (static_cast<float>(diffuseMax) / static_cast<float>(peak)) * hsv.z;
        HsvToRgb(hsv, rgb);

        CImVector packed;
        PackColor(packed, rgb);
        *diffuse = packed;
    }

    *ambient = color;

    if (ambientMax < peak) {
        int32_t scale = static_cast<int32_t>(nearbyintf((static_cast<float>(ambientMax) * 255.0f) / static_cast<float>(peak) - 0.5f));

        ambient->value = (static_cast<uint32_t>(static_cast<uint8_t>((ambient->r * scale + 0xFF) >> 8)) << 16)
            | (static_cast<uint32_t>(static_cast<uint8_t>((ambient->g * scale + 0xFF) >> 8)) << 8)
            | ((ambient->b * scale + 0xFF) >> 8 & 0xFF)
            | (ambient->value & 0xFF000000);
    }
}

// ref: FUN_007a0d60
// The entity's light from the WMO floor under it. A probe from one yard above its feet to twelve
// below, in the object's space, finds the floor face; its MOCV colour is split into the entity's
// diffuse and ambient, and the MOCV alpha then blends both toward the outdoor light when the face
// is flagged for it (MOPY bit 0). The reference reads the entity's position and its MapObjDef link
// (with the def's inverse placement); the caller passes the already-transformed position and the
// group here. flags bit 0x1000 records that the blend applies.
bool CMapEntity::FloorLight(const C3Vector& localPos, CMapObj* mapObj, CMapObjGroup* group, CImVector* diffuse, CImVector* ambient, uint32_t* flags, uint8_t* outAlpha) {
    CImVector color;
    color.value = 0;
    uint8_t exteriorBlend = 0;

    C3Segment segment;
    segment.start = { localPos.x, localPos.y, 1.0f + localPos.z };
    segment.end = { localPos.x, localPos.y, localPos.z - 12.0f };

    if (!mapObj->GroupFloorColor(group, segment, &color, &exteriorBlend)) {
        return false;
    }

    CMapEntity::SplitFloorLight(color, diffuse, 0xA8, ambient, 0x60);

    if (exteriorBlend) {
        // DayNight's current outdoor diffuse (+0x1a8) and ambient (+0x1ac), as bytes
        const C3Vector& dif = CWorld::GetOutdoorDiffuse();
        const C3Vector& amb = CWorld::GetOutdoorAmbient();
        CImVector dayDiffuse;
        CImVector dayAmbient;
        dayDiffuse.Set(1.0f, dif.x, dif.y, dif.z);
        dayAmbient.Set(1.0f, amb.x, amb.y, amb.z);

        uint32_t alpha = color.value >> 24;

        if (alpha != 0) {
            LerpColor(*diffuse, alpha, dayDiffuse);

            if (alpha != 0) {
                LerpColor(*ambient, alpha, dayAmbient);
            }
        }

        *flags |= 0x1000;
        *outAlpha = static_cast<uint8_t>(alpha);
        return true;
    }

    *flags &= ~0x1000u;
    return true;
}

// The light for a unit standing on a WMO floor, by the reference's mechanism: a probe from one
// yard above its feet to twelve below, through the interior groups' BSP, sampling the MOCV at the
// floor face it lands on (CMapEntity::FloorLight, FUN_007a0d60). The reference reaches the
// entity's MapObjDef and group through its parent links; without that graph every loaded instance
// whose box holds the probe is tried, and within it every interior group whose box holds it, first
// hit wins. Exterior groups never answer, so a unit out on a deck is lit by the sky again.
bool CMapEntity::FloorLightAt(const C3Vector& pos, CImVector* diffuse, CImVector* ambient) {
    // Runs on the REFERENCE map objects, not the stand-in's copies. CMapEntity::FloorLight was
    // always a ported reference function taking a CMapObj and a CMapObjGroup -- the stand-in was
    // only supplying its own instances of those two, built out of its own arrays. The real ones
    // carry the same MOBN/MOBR BSP and the same MOCV colours, and the portal walk that reaches
    // them has worked since the m_portalRects fix, so this is a redirect rather than a rewrite.
    for (auto def = CMapObjDef::s_uniqueIds.Head(); def;
         def = CMapObjDef::s_uniqueIds.Next(def)) {
        if (!def->m_mapObj) {
            continue;
        }

        // The whole building first, in world space, with the same asymmetric vertical window the
        // stand-in used: a probe reaches a little above the unit and well below it, because the
        // floor being stood on is what is wanted.
        const CAaBox& box = def->m_bounds;

        if (pos.x < box.b.x || pos.x > box.t.x || pos.y < box.b.y || pos.y > box.t.y
            || pos.z + 1.0f < box.b.z || pos.z - 12.0f > box.t.z) {
            continue;
        }

        // Into the building's own space, where the BSP planes and the group bounds live. The
        // stand-in undid the placement by hand from a yaw sine and cosine; the def carries the
        // whole inverse, which also covers the pitch and roll a hand-rolled yaw could not.
        C3Vector local = pos * def->m_inversePlacement;

        for (auto link = def->m_defGroupLinkList.Head(); link;
             link = def->m_defGroupLinkList.Next(link)) {
            auto defGroup = static_cast<CMapObjDefGroup*>(link->owner);

            if (!defGroup) {
                continue;
            }

            uint32_t groupIndex = defGroup->m_groupIndex;
            CMapObjGroup* group = def->m_mapObj->GetGroup(groupIndex, 0);

            // A group with no BSP or no vertex colours cannot answer the probe.
            if (!group || !group->m_bspNodes || !group->m_colors) {
                continue;
            }

            const CAaBox& gb = group->m_bounds;

            if (local.x < gb.b.x || local.x > gb.t.x || local.y < gb.b.y || local.y > gb.t.y
                || local.z + 1.0f < gb.b.z || local.z - 12.0f > gb.t.z) {
                continue;
            }

            uint32_t flags = 0;
            uint8_t alpha = 0;

            if (CMapEntity::FloorLight(local, def->m_mapObj, group, diffuse, ambient, &flags,
                                       &alpha)) {
                return true;
            }
        }
    }

    return false;
}
