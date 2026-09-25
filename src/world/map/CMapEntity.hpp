#ifndef WORLD_MAP_C_MAP_ENTITY_HPP
#define WORLD_MAP_C_MAP_ENTITY_HPP

#include "world/map/CMapStaticEntity.hpp"
#include <tempest/Vector.hpp>

class CMapObj;
class CMapObjGroup;

class CMapEntity : public CMapStaticEntity {
    public:
        // Static functions
        static void SplitFloorLight(const CImVector& color, CImVector* diffuse, uint8_t diffuseMax, CImVector* ambient, uint8_t ambientMax);
        static bool FloorLight(const C3Vector& localPos, CMapObj* mapObj, CMapObjGroup* group, CImVector* diffuse, CImVector* ambient, uint32_t* flags, uint8_t* outAlpha);

        // Member variables
        void* m_handler = nullptr;
        void* m_handlerParam = nullptr;
        uint64_t m_param64 = 0;
        uint32_t m_param32 = 0;
        // TODO
        CImVector m_ambientTarget = { 0xFF, 0x00, 0x00, 0x00 };
        float m_dirLightScaleTarget = 0.0f;
        // TODO
        // Entity state bits (reference +0x7c). Bit 1 (0x2) makes CMap::LinkToMapObjDefGroup put
        // the entity at the head of the group's entity list instead of the tail; the placement and
        // lighting code (FUN_007c23f0, FUN_007c1730 ...) keeps 0x20/0x40/0x80/0x1000/0x2000/0x8000
        // here. Named by offset until those are ported.
        uint32_t m_flags7c = 0;
        // Reference +0x80 and +0xbc, reported by CWorld::GetObjectFloor while m_flags7c has 0x20
        // set; +0x80 is a height its callers compare against a z. Named by offset until the
        // placement code that writes them is ported.
        float m_field80 = 0.0f;
        uint16_t m_fieldBC = 0;
        // TODO

        // Member functions
        CMapEntity();
};

#endif
