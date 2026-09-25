#ifndef WORLD_MAP_C_MAP_BASE_OBJ_HPP
#define WORLD_MAP_C_MAP_BASE_OBJ_HPP

#include <storm/List.hpp>
#include <cstdint>

class CM2Lighting;
class CMapBaseObj;

// One parent/child relationship between two map objects, pooled from CMap::s_baseObjLinkHeap
// (CMap::AllocBaseObjLink / FreeBaseObjLink). The owner keeps it in m_parentLinkList through
// ownerLink; the object it references keeps it in one of its own child lists through refLink.
// Reference layout: +0 memHandle, +4 owner, +8 ref, +0xc refLink, +0x14 ownerLink.
class CMapBaseObjLink {
    public:
        // Member variables
        uint32_t memHandle;
        CMapBaseObj* owner;
        CMapBaseObj* ref;
        TSLink<CMapBaseObjLink> refLink;
        TSLink<CMapBaseObjLink> ownerLink;
};

class CMapBaseObj {
    friend class CMap;
    friend class CWorld;

    public:
        // Enums
        enum {
            Type_BaseObj        = 0x1,
            Type_Area           = 0x2,
            Type_Chunk          = 0x4,
            Type_MapObjDef      = 0x8,
            Type_MapObjDefGroup = 0x10,
            Type_Entity         = 0x20,
            Type_DoodadDef      = 0x40,
            Type_Light          = 0x80,
            Type_100            = 0x100,
            Type_200            = 0x200,
        };

        // Public member variables, in the reference's order: +0 vtable, +4 m_memHandle,
        // +8 m_type, +0xa m_linkCount, +0xc m_flags, +0x10 m_lameAssLink, +0x18 m_parentLinkList
        uint32_t m_memHandle = 0;
        uint16_t m_type = Type_BaseObj;
        // Links whose owner is this object: CMap::AllocBaseObjLink raises it, FreeBaseObjLink
        // lowers it, and the destroy paths only release an object once it reaches zero.
        int16_t m_linkCount = 0;
        uint32_t m_flags = 0x0;
        TSLink<CMapBaseObj> m_lameAssLink;
        STORM_EXPLICIT_LIST(CMapBaseObjLink, ownerLink) m_parentLinkList;

        // Public virtual member functions
        virtual ~CMapBaseObj() = default;
        virtual void SelectLights(CM2Lighting* lighting) {};
        virtual void SelectUnderwater(CM2Lighting* lighting) {};

        // Public member functions
        uint32_t GetType();
};

#endif
