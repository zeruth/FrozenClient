#ifndef WORLD_MAP_C_MAP_OBJ_HPP
#define WORLD_MAP_C_MAP_OBJ_HPP

#include <storm/List.hpp>
#include <tempest/Segment.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CMapObjGroup;

// MOMT: one material of a WMO (64 bytes). The group queries only read texture1, to tell a
// textured face from an untextured one.
struct SMOMaterial {
    uint32_t flags;
    uint32_t shader;
    uint32_t blendMode;
    uint32_t texture1;
    uint32_t sidnColor;
    uint32_t frameSidnColor;
    uint32_t texture2;
    uint32_t diffColor;
    uint32_t groundType;
    uint32_t texture3;
    uint32_t color3;
    uint32_t flags3;
    uint32_t runtime[4];
};

// A loaded WMO root: what its groups reach through their m_mapObj pointer. Only the fields the
// ported group queries read are here; reference offsets are noted per field.
class CMapObj {
    public:
        // Member variables
        uint32_t m_memHandle = 0;             // +0: CMap::s_mapObjHeap slot (CMap::AllocMapObj)
        uint32_t m_mohdFlags = 0;             // +0x120 -> +0x3c: the MOHD header's flags
        SMOMaterial* m_materials = nullptr;   // +0x160: MOMT
        uint32_t m_materialCount = 0;
        CImVector m_ambientColor;             // +0x1a0: MOHD ambColor
        TSLink<CMapObj> m_link;               // +0x1c4: unlinked by CMap::FreeMapObj

        // Member functions
        bool GroupFloorColor(CMapObjGroup* group, const C3Segment& segment, CImVector* outColor, uint8_t* outFlag);
};

#endif
