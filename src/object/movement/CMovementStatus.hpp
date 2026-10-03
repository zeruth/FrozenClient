#ifndef OBJECT_MOVEMENT_C_MOVEMENT_STATUS_HPP
#define OBJECT_MOVEMENT_C_MOVEMENT_STATUS_HPP

#include "util/GUID.hpp"
#include <common/DataStore.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

struct CMovementStatus {
    uint32_t uint0 = 0;
    // TODO
    WOWGUID transport = 0;
    uint32_t moveFlags = 0x0;
    uint16_t uint14 = 0;
    uint8_t byte16 = 0xFF;
    // TODO
    C3Vector position18;
    float facing24 = 0.0f;
    C3Vector position28;
    float facing34 = 0.0f;
    float float38 = 0.0f;
    uint32_t uint3C = 0;
    float float40 = 0.0f;
    float float44 = 0.0f;
    float float48 = 0.0f;
    float float4C = 0.0f;
    float float50 = 0.0f;
    uint32_t uint54 = 0;
    // ref +0x58: the second transport time, present when move-flags-2 0x400 is set.
    uint32_t uint58 = 0;

    CMovementStatus();

    static uint32_t Skip(CDataStore* msg);
};

CDataStore& operator>>(CDataStore& msg, CMovementStatus& move);

// ref: FUN_004f4ed0
CDataStore& operator<<(CDataStore& msg, const CMovementStatus& move);

#endif
