#include "world/map/CMapDoodadDef.hpp"

// ref: FUN_007c21e0
// A placed doodad says what it is. Without this the def reads as a plain base object, and every
// test that asks whether an entity is a doodad -- the placement, the lighting, the group
// linking -- quietly answers no.
CMapDoodadDef::CMapDoodadDef() {
    this->m_type |= CMapBaseObj::Type_DoodadDef;
}
