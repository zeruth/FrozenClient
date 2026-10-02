#include "world/map/CMapLight.hpp"
#include "world/map/CMap.hpp"

// ref: FUN_007d9b10
CMapLight::CMapLight() {
    this->m_type |= CMapBaseObj::Type_Light;
}

// ref: FUN_007d9bd0
CMapLight* CMapLight::Create(uint8_t d0, uint8_t d1) {
    auto light = CMap::AllocLight();

    light->m_flags = 0;

    for (auto& field : light->m_fields24) {
        field = 0;
    }

    light->m_fieldcc = 0;
    light->m_fieldc8 = 0;
    light->m_fieldc4 = 0;
    light->m_fieldd0 = d0;
    light->m_fieldd1 = d1;

    return light;
}

// ref: FUN_007d9d50
void CMapLight::Enable() {
    this->m_flags &= ~0x20u;
    this->m_light.SetVisible(1);
}

// ref: FUN_007da100
// A directional light belongs to no area: it gets one link of its own. A point light is linked
// into the areas its range reaches (FUN_007d9c80, FUN_007d9f90, FUN_007d9de0), which nothing in
// the client makes yet -- the map's only light is the sun.
void CMapLight::Link() {
    if (this->m_parentLinkList.Head() == nullptr && this->m_light.m_type == 0) {
        auto link = CMap::AllocBaseObjLink(this);
        link->ref = nullptr;
        this->m_parentLinkList.LinkToTail(link);
    }
}
