#ifndef WORLD_MAP_C_MAP_OBJ_DEF_HPP
#define WORLD_MAP_C_MAP_OBJ_DEF_HPP

#include "world/map/CMapBaseObj.hpp"
#include <storm/List.hpp>

class CMapObjDef : public CMapBaseObj {
    public:
        // Member variables. The two links right after the base object are what
        // CMap::UnlinkMapObjDef (FUN_0079e6a0) takes the def out of before it is freed; which lists
        // they belong to is not known yet, so they are named by offset.
        TSLink<CMapObjDef> m_link28;      // +0x28
        TSLink<CMapObjDef> m_link30;      // +0x30
        // TODO
};

#endif
