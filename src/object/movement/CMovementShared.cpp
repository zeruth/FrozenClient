#include "object/movement/CMovementShared.hpp"
#include "object/movement/CMoveSpline.hpp"

CMovementShared::CMovementShared(const WOWGUID& transportGUID, const C3Vector& position, float facing, const WOWGUID& guid)
    : CPassenger(transportGUID, position, guid)
{
    this->m_facing = facing;

    this->m_moveFlags = 0x0;

    this->m_anchorPosition = position;
    this->m_anchorFacing = facing;
    this->m_anchorPitch = 0.0f;
    this->m_cosAnchorPitch = 1.0f;
    this->m_sinAnchorPitch = 0.0f;

    this->m_walkSpeed = 0.0f;
    this->m_runSpeed = 0.0f;
    this->m_runBackSpeed = 0.0f;
    this->m_swimSpeed = 0.0f;
    this->m_swimBackSpeed = 0.0f;
    this->m_flightSpeed = 0.0f;
    this->m_flightBackSpeed = 0.0f;

    this->m_spline = nullptr;
}

// ref: FUN_00987570
float CMovementShared::GetCurrentSpeed(int walk) const {
    uint32_t moveFlags = this->m_moveFlags;

    // Not moving forwards, backwards, strafing, ascending or descending
    if (!(moveFlags & 0xc0000f)) {
        return 0.0f;
    }

    CMoveSpline* spline = this->m_spline;

    if (spline && !(spline->uint20 & 0x400)) {
        if (spline->uint2C) {
            return (spline->spline.m_length / static_cast<float>(spline->uint2C)) * 1000.0f;
        }

        return 0.0f;
    }

    // Flying
    if (moveFlags & 0x2000000) {
        if ((moveFlags & 0x2) && this->m_flightBackSpeed <= this->m_flightSpeed) {
            return this->m_flightBackSpeed;
        }

        return this->m_flightSpeed;
    }

    // Swimming
    if (moveFlags & 0x200000) {
        if ((moveFlags & 0x2) && this->m_swimBackSpeed <= this->m_swimSpeed) {
            return this->m_swimBackSpeed;
        }

        return this->m_swimSpeed;
    }

    if (!(moveFlags & 0x100) && !walk) {
        if ((moveFlags & 0x2) && this->m_runBackSpeed <= this->m_runSpeed) {
            return this->m_runBackSpeed;
        }
    } else if (this->m_walkSpeed < this->m_runSpeed) {
        return this->m_walkSpeed;
    }

    return this->m_runSpeed;
}
