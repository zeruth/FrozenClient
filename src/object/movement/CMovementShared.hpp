#ifndef OBJECT_MOVEMENT_C_MOVEMENT_SHARED_HPP
#define OBJECT_MOVEMENT_C_MOVEMENT_SHARED_HPP

#include "object/movement/CPassenger.hpp"
#include "util/GUID.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

struct CMoveSpline;

class CMovementShared : public CPassenger {
    public:
        // Public member functions
        CMovementShared(const WOWGUID& transportGUID, const C3Vector& position, float facing, const WOWGUID& guid);
        float GetCurrentSpeed(int walk) const;

    protected:
        // Protected member variables
        // TODO
        uint32_t m_moveFlags;       // ref +0x44
        C3Vector m_anchorPosition;
        float m_anchorFacing;
        float m_anchorPitch;
        // TODO
        C3Vector m_direction;
        C2Vector m_direction2d;
        float m_cosAnchorPitch;
        float m_sinAnchorPitch;
        // TODO
        float m_walkSpeed;          // ref +0x90
        float m_runSpeed;           // ref +0x94
        float m_runBackSpeed;       // ref +0x98
        float m_swimSpeed;          // ref +0x9c
        float m_swimBackSpeed;      // ref +0xa0
        float m_flightSpeed;        // ref +0xa4
        float m_flightBackSpeed;    // ref +0xa8
        // TODO
        CMoveSpline* m_spline;
        // TODO
};

#endif
