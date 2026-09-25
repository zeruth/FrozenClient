#ifndef WORLD_MAP_C_MAP_OBJ_DEF_GROUP_HPP
#define WORLD_MAP_C_MAP_OBJ_DEF_GROUP_HPP

#include "world/map/CMapBaseObj.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Vector.hpp>

// One placed WMO group (an instance of a CMapObjGroup under a CMapObjDef). The four lists hold
// the CMapBaseObjLinks of the objects placed in the group; CMap::LinkToMapObjDefGroup appends an
// entity's link to m_entityLinkList and a doodad def's to m_doodadDefLinkList, and the destroy
// path (FUN_007c3150) drains all four. What the last two hold is not known yet.
class CMapObjDefGroup : public CMapBaseObj {
    public:
        // Member variables
        CAaBox m_bounds;                     // +0x24: the group's box, placed into world space
        C3Vector m_center;                   // +0x3c: its bounding sphere, likewise
        float m_radius = 0.0f;               // +0x48
        uint32_t m_groupIndex = 0;           // +0x50: into the root's groups
        CImVector m_ambientColor;            // +0x5c: taken from the def
        // TODO +0x54, +0x60..+0x74
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_doodadDefLinkList;   // +0x78
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_entityLinkList;      // +0x84
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_link90List;          // +0x90
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_link9cList;          // +0x9c
        // TODO
};

#endif
