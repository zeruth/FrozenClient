#ifndef WORLD_MAP_C_MAP_DOODAD_DEF_HPP
#define WORLD_MAP_C_MAP_DOODAD_DEF_HPP

#include "world/map/CMapStaticEntity.hpp"
#include "sound/SOUNDKITOBJECT.hpp"
#include <storm/List.hpp>

// A placed doodad (MDDF). Reference size 0x170 (FUN_007c0d10 allocates it with that much room).
class CMapDoodadDef : public CMapStaticEntity {
    public:
        // Member variables
        // TODO +0x28..+0x90
        // Two links CMap::UnlinkDoodadDef (FUN_007bfe80) takes the def out of together, gated on the
        // first being linked. Which lists they belong to is not known yet, so named by offset.
        TSLink<CMapDoodadDef> m_link94;   // +0x94
        TSLink<CMapDoodadDef> m_link9c;   // +0x9c
        // TODO +0xa4..+0x154
        // The doodad's own sound emitter, stopped by CMap::FreeDoodadDef before the def is released.
        SOUNDKITOBJECT m_soundKit;        // +0x158
};

#endif
