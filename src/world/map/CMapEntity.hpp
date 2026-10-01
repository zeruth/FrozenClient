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

        // FROZEN-ONLY, both of these. The reference does this work inside the two FloorLight
        // overrides, which reach the building through the entity's parent links; frozen has no
        // such link on a unit yet, so FloorLightAt searches for the building and needs the
        // probe and the tail as separate pieces it can drive itself.
        static bool ApplyFloorLight(const CImVector& color, uint8_t exteriorBlend, CImVector* diffuse, CImVector* ambient, uint32_t* flags, uint8_t* outAlpha);
        static bool FloorLightLocal(const C3Vector& localPos, CMapObj* mapObj, uint32_t groupIndex, CImVector* diffuse, CImVector* ambient, uint32_t* flags, uint8_t* outAlpha);

        // The floor light for a world position: finds the building and room the point stands in and
        // asks FloorLight above. Lived in the stand-in renderer until 2026-09-26, purely because it
        // was the thing that owned a list of buildings to search.
        static bool FloorLightAt(const C3Vector& pos, CImVector* diffuse, CImVector* ambient);

        // Is a world position inside an interior WMO room? Answered the reference's way, by
        // dropping a segment and asking whether the surface below belongs to a non-exterior group.
        static bool PointIsIndoors(const C3Vector& pos);

        // Member variables
        void* m_handler = nullptr;
        void* m_handlerParam = nullptr;
        uint64_t m_param64 = 0;
        uint32_t m_param32 = 0;
        // TODO
        CImVector m_ambientTarget = { 0xFF, 0x00, 0x00, 0x00 };
        float m_dirLightScaleTarget = 0.0f;
        // TODO
        // Reference +0xbc, reported by CWorld::GetObjectFloor beside the liquid height while
        // m_flags7c has 0x20 set. (+0x80, the height, moved to CMapStaticEntity::m_liquidHeight
        // on 2026-10-01: the doodad def uses the same offset, so it belongs to the base.)
        uint16_t m_fieldBC = 0;
        // TODO

        // Its place in the distance row the traversal put it in (+0xc8), and in the frame's
        // hidden list when nothing could see it.
        TSLink<CMapEntity> m_entityRowLink;
        TSLink<CMapEntity> m_hiddenLink;

        // Member functions
        CMapEntity();

        // Vtable slot 3. Tagged on the definition in the .cpp, not here.
        void FloorLight(CMapObjDef* def, uint32_t groupIndex, const uint16_t* face,
                        const C3Vector* point) override;
};

#endif
