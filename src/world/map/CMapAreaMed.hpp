#ifndef WORLD_MAP_C_MAP_AREA_MED_HPP
#define WORLD_MAP_C_MAP_AREA_MED_HPP

#include "world/map/CMapBaseObj.hpp"

// The medium-detail area record ("WAREAMED" heap, 33404 bytes a piece in the reference, 16 to a
// block; see CMap::MapMemInitialize). A CMapBaseObj: CMap::FreeAreaMed unlinks m_lameAssLink and
// runs the virtual destructor. The unload path (FUN_007c3730) frees one per cell of a 64x64 grid.
class CMapAreaMed : public CMapBaseObj {
    public:
        // TODO
};

#endif
