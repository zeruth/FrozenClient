#ifndef WORLD_MAP_C_MAP_OBJ_DEF_GROUP_HPP
#define WORLD_MAP_C_MAP_OBJ_DEF_GROUP_HPP

#include "world/CWFrustum.hpp"
#include "world/map/CMapBaseObj.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Vector.hpp>

// One placed WMO group (an instance of a CMapObjGroup under a CMapObjDef). The four lists hold
// the CMapBaseObjLinks of the objects placed in the group; CMap::LinkToMapObjDefGroup appends an
// entity's link to m_entityLinkList and a doodad def's to m_doodadDefLinkList, and the destroy
// path (FUN_007c3150) drains all four. What the last two hold is not known yet.
namespace Liquid { class CInstance; }
using Liquid::CInstance;

class CMapObjDefGroup;

class CMapObjDefGroup : public CMapBaseObj {
    public:
        // Member variables
        CAaBox m_bounds;                     // +0x24: the group's box, placed into world space
        C3Vector m_center;                   // +0x3c: its bounding sphere, likewise
        float m_radius = 0.0f;               // +0x48
        float m_sortDistance = 0.0f;         // +0x4c: along the camera forward
        uint32_t m_groupIndex = 0;           // +0x50: into the root's groups
        CImVector m_ambientColor;            // +0x5c: taken from the def
        // +0x68: this placed group's liquid surface, or null when it has none yet. The map-object
        // liquid queue (FUN_00793d20) tests it to decide whether to build one, and stores the
        // Liquid::CInstance it makes here. Nothing fills it yet -- see the note at that queue.
        CInstance* m_liquidSurface = nullptr;
        // TODO +0x54, +0x60..+0x6c
        // What the traversal left of the view each time it reached this group, one record per
        // doorway. The render pass draws the group once per record, clipped to it, and gives
        // them all back afterwards (+0x6c).
        STORM_EXPLICIT_LIST(CWFrustum, link) m_frustums;
        // Its place in the frame's candidate list and then in the distance row the
        // bucketing put it in (+0xa8): one link, because it is only ever in one of them
        TSLink<CMapObjDefGroup> m_rowLink;
        // Its place in the frame's visible list (+0xb0); empty means the traversal has
        // not reached it yet this frame
        TSLink<CMapObjDefGroup> m_renderLink;
        // +0xb8: its place in the list of groups WAITING for a liquid surface. A third link beside
        // the two above, which is what the reference's +0xb8 falls on once m_renderLink at +0xb0 has
        // had its eight bytes -- the queue drains this list and unlinks each group as it goes.
        TSLink<CMapObjDefGroup> m_liquidQueueLink;

        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_doodadDefLinkList;   // +0x78
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_entityLinkList;      // +0x84
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_link90List;          // +0x90
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_link9cList;          // +0x9c
        // TODO
};

#endif
