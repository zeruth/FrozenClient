#ifndef WORLD_MAP_C_MAP_DOODAD_DEF_HPP
#define WORLD_MAP_C_MAP_DOODAD_DEF_HPP

#include "world/map/CMapStaticEntity.hpp"
#include "sound/SOUNDKITOBJECT.hpp"
#include <storm/Hash.hpp>
#include <storm/List.hpp>
#include <tempest/Matrix.hpp>

// MDDF: one doodad placement in a tile, 36 bytes.
struct SMDDF {
    uint32_t nameId;            // +0x00: into MMID
    uint32_t uniqueId;          // +0x04
    C3Vector position;          // +0x08: in the tile's own axes
    C3Vector rotation;          // +0x14: degrees
    uint16_t scale;             // +0x20: 1024 is life size
    uint16_t flags;             // +0x22: bit 0 marks it as belonging to a building
};

static_assert(sizeof(SMDDF) == 0x24, "SMDDF is 36 bytes");

// A placed doodad (MDDF). Reference size 0x170 (FUN_007c0d10 allocates it with that much room).
//
// Two tiles that share an edge both list the doodads that straddle it, so a placement is looked
// up by its uniqueId before a second copy is made; the table's own fields are the two links the
// class already carried at +0x94 and +0x9c, which is what they were for.
class CMapDoodadDef : public CMapStaticEntity, public TSHashObject<CMapDoodadDef, HASHKEY_NONE> {
    public:
        // Member variables
        // TODO +0x28..+0x90, +0xa8..+0xd8
        // Where the doodad stands, built from its placement's position, rotation and scale, and
        // the inverse for queries that need to get into its own space (+0xd8 and +0x118).
        C44Matrix m_placement;
        C44Matrix m_inversePlacement;

        // Member functions
        // Vtable slot 3. Tagged on the definition in the .cpp, not here.
        void FloorLight(CMapObjDef* def, uint32_t groupIndex, const uint16_t* face,
                        const C3Vector* point) override;
        // Vtable slot 2 (0x00a40320). Tagged on the definition.
        void SelectUnderwater(CM2Lighting* lighting) override;
        // TODO +0x158 onwards beyond the sound kit
        // The doodad's own sound emitter, stopped by CMap::FreeDoodadDef before the def is released.
        SOUNDKITOBJECT m_soundKit;        // +0x158

        // Member functions
        // ref: FUN_007c21e0
        CMapDoodadDef();
};

#endif
