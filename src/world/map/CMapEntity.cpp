#include "world/map/CMapEntity.hpp"
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
