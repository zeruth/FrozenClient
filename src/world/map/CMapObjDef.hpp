#ifndef WORLD_MAP_C_MAP_OBJ_DEF_HPP
#define WORLD_MAP_C_MAP_OBJ_DEF_HPP

#include "world/map/CMapBaseObj.hpp"
#include <storm/Array.hpp>
#include <storm/Hash.hpp>
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CMapLight;
class CMapObj;
class CM2Model;
class CMapObjDefGroup;

// MODF: one WMO placement in an ADT, 64 bytes. The same record shapes the global WMO a WDT can
// name instead of a terrain grid.
struct SMODF {
    uint32_t nameId;            // +0x00: into the tile's MWID
    uint32_t uniqueId;          // +0x04: unique across the map; the dedup key
    C3Vector position;          // +0x08
    C3Vector rotation;          // +0x14: degrees
    C3Vector extentsMin;        // +0x20
    C3Vector extentsMax;        // +0x2c
    uint16_t flags;             // +0x38: bit 0 marks a destructible building
    uint16_t doodadSet;         // +0x3a
    uint16_t nameSet;           // +0x3c
    uint16_t scale;             // +0x3e
};

static_assert(sizeof(SMODF) == 0x40, "SMODF is 64 bytes");

// One placed WMO (reference 0x158 bytes). A def is shared between every chunk it overlaps, which
// each hold a link to it; the uniqueId hash keeps two tiles from placing the same building twice.
class CMapObjDef : public CMapBaseObj, public TSHashObject<CMapObjDef, HASHKEY_NONE> {
    public:
        // Static variables
        // Every placed WMO by its MODF uniqueId (DAT_00d25434), so the tile that loads second
        // finds the building the first one already placed
        static TSHashTable<CMapObjDef, HASHKEY_NONE> s_uniqueIds;

        // Member variables. The reference's offsets follow the base object, whose hash fields
        // take +0x24 (the uniqueId) through +0x3b.
        C3Vector m_position;             // +0x3c: the placement's origin, in world space
        CAaBox m_bounds;                 // +0x48: the MODF extents, in world space
        C3Vector m_center;               // +0x60
        float m_radius = 0.0f;           // +0x6c
        C44Matrix m_placement;           // +0x70: model space to world
        C44Matrix m_inversePlacement;    // +0xb0
        uint32_t m_nameId = 0;           // +0xf0
        CMapObj* m_mapObj = nullptr;     // +0xf4: the root this places

        // The ground type of one polygon's material, for a thing standing on this building.
        // 0xFFFFFFFF when the group, the polygon or the material is not there.
        // ref: FUN_007b39b0
        uint32_t GetPolyGroundType(uint32_t groupIndex, uint16_t polyIndex);
        uint32_t m_doodadSet = 0;        // +0x100
        uint32_t m_nameSet = 0;          // +0x104
        // The links the def's own groups hold on it (+0x114); each group is the owner
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_defGroupLinkList;

        // The building's own props, built from the root's MODD placements for this def's doodad set.
        // DIVERGED from the reference's layout on purpose: the reference hangs its WMO doodads off
        // the GROUP (CMapObjGroup::QuerySegment gates a doodad query on MOGP 0x800 and MODR gives
        // each group its slice of MODD), and its creator has not been located. These live on the def
        // instead, which is where the doodad SET is chosen, so one list covers the whole building.
        // Moving them onto the group later changes only where the list is stored.
        CM2Model** m_doodads = nullptr;
        float* m_doodadScale = nullptr;
        // Per-doodad baked lighting colour, from the MODD entry. The lighting callback takes the
        // address of an element, so this array must not move once a model points into it.
        C3Vector* m_doodadAmbient = nullptr;
        uint32_t m_doodadCount = 0;

        // One entry per group of the root, and one per MOLT light. The reference keeps the first
        // four groups inline and spills to the heap past that (+0x120 through +0x130) and grows
        // the lights by hand (+0x134 through +0x140); both are plain growable arrays here.
        TSGrowableArray<CMapObjDefGroup*> m_defGroups;
        TSGrowableArray<CMapLight*> m_lights;
        CImVector m_ambientColor;        // +0x144: the root's, copied so a def can override it
        uint32_t m_unk138 = 0;           // cleared on create, not read by anything ported
};

#endif
