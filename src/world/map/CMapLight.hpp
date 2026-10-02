#ifndef WORLD_MAP_C_MAP_LIGHT_HPP
#define WORLD_MAP_C_MAP_LIGHT_HPP

#include "model/CM2Light.hpp"
#include "world/map/CMapBaseObj.hpp"

// A light the map owns. The one the world needs is the sun (CMap::s_outdoorLight, made with the
// map), whose CM2Light the day/night update aims and colours every frame.
class CMapLight : public CMapBaseObj {
    public:
        // Member variables
        uint32_t m_fields24[13] = {};   // +0x24..+0x54, zeroed by the constructor
        CM2Light m_light;               // +0x58
        uint32_t m_fieldc4 = 0;         // +0xc4
        uint32_t m_fieldc8 = 0;         // +0xc8
        uint32_t m_fieldcc = 0;         // +0xcc
        uint8_t m_fieldd0 = 0;          // +0xd0
        uint8_t m_fieldd1 = 0;          // +0xd1

        // Member functions
        // ref: FUN_007d9b10
        CMapLight();
        // ref: FUN_007d9bd0
        static CMapLight* Create(uint8_t d0, uint8_t d1);
        // ref: FUN_007d9d50
        void Enable();
        // ref: FUN_007da100
        void Link();
};

#endif
