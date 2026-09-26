#ifndef WORLD_MAP_C_MAP_ENTITY_HPP
#define WORLD_MAP_C_MAP_ENTITY_HPP

#include "world/map/CMapStaticEntity.hpp"
#include <storm/List.hpp>
#include <tempest/Vector.hpp>

class CMapObj;
class CMapObjGroup;

class CMapEntity : public CMapStaticEntity {
    public:
        // Static functions
        static void SplitFloorLight(const CImVector& color, CImVector* diffuse, uint8_t diffuseMax, CImVector* ambient, uint8_t ambientMax);
        static bool FloorLight(const C3Vector& localPos, CMapObj* mapObj, CMapObjGroup* group, CImVector* diffuse, CImVector* ambient, uint32_t* flags, uint8_t* outAlpha);

        // The floor light for a world position: finds the building and room the point stands in and
        // asks FloorLight above. Lived in the stand-in renderer until 2026-09-26, purely because it
        // was the thing that owned a list of buildings to search.
        static bool FloorLightAt(const C3Vector& pos, CImVector* diffuse, CImVector* ambient);

        // Member variables
        void* m_handler = nullptr;
        void* m_handlerParam = nullptr;
        uint64_t m_param64 = 0;
        uint32_t m_param32 = 0;
        // TODO
        CImVector m_ambientTarget = { 0xFF, 0x00, 0x00, 0x00 };
        float m_dirLightScaleTarget = 0.0f;
        // TODO
        // Reference +0x80 and +0xbc, reported by CWorld::GetObjectFloor while m_flags7c has 0x20
        // set; +0x80 is a height its callers compare against a z. Named by offset until the
        // placement code that writes them is ported.
        float m_field80 = 0.0f;
        uint16_t m_fieldBC = 0;
        // TODO

        // Its place in the distance row the traversal put it in (+0xc8), and in the frame's
        // hidden list when nothing could see it.
        TSLink<CMapEntity> m_entityRowLink;
        TSLink<CMapEntity> m_hiddenLink;

        // Member functions
        CMapEntity();
};

#endif
