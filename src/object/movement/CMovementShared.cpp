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

// ref: FUN_009872b0
void CMovementShared::ClearSplineEnabled() {
    this->m_moveFlags &= ~0x8000000;
}

// ref: FUN_009873f0
void CMovementShared::SetWaterWalking(int32_t enable) {
    if (enable) {
        this->m_moveFlags |= 0x10000000;
        return;
    }

    this->m_moveFlags &= ~0x10000000;
}

// ref: FUN_00987440
void CMovementShared::SetSafeFall(int32_t enable) {
    if (enable) {
        this->m_moveFlags |= 0x20000000;
        return;
    }

    this->m_moveFlags &= ~0x20000000;
}

// ref: FUN_00987460
// Hovering also drops the spline elevation flag.
void CMovementShared::SetHover(int32_t enable) {
    if (enable) {
        this->m_moveFlags = (this->m_moveFlags & ~0x4000000) | 0x40000000;
        return;
    }

    this->m_moveFlags &= ~0x40000000;
}

// The three facing setters write the spline's face union and its flag without a null check: the
// reference's callers only reach them with a spline in place.
// ref: FUN_006e9640
void CMovementShared::SetSplineFacingSpot(const C3Vector& spot) {
    this->m_spline->flags |= 0x8000;
    this->m_spline->face.spot = spot;
}

// ref: FUN_006e9670
void CMovementShared::SetSplineFacingTarget(const WOWGUID& target) {
    this->m_spline->flags |= 0x10000;
    this->m_spline->face.guid = target;
}

// ref: FUN_006e96a0
void CMovementShared::SetSplineFacingAngle(float facing) {
    this->m_spline->flags |= 0x20000;
    this->m_spline->face.facing = facing;
}

// ref: FUN_006e9a70
int32_t CMovementShared::IsSplineFlag200() const {
    auto spline = this->m_spline;

    if (spline && !(spline->flags & 0x400) && (spline->flags & 0x200)) {
        return 1;
    }

    return 0;
}

// ref: FUN_006e9aa0
int32_t CMovementShared::IsSplineFlag800() const {
    auto spline = this->m_spline;

    if (spline && !(spline->flags & 0x400) && (spline->flags & 0x800)) {
        return 1;
    }

    return 0;
}

// ref: FUN_006eaba0
// A running spline with flag 0x200 skips the 0x4 and 0x400 tests and goes straight to the last one.
int32_t CMovementShared::IsOffGround() const {
    auto spline = this->m_spline;

    if (spline) {
        if (!(spline->flags & 0x400) && (spline->flags & 0x200)) {
            return (this->m_moveFlags & 0x2201000) ? 1 : 0;
        }

        if (!(spline->flags & 0x400) && (spline->flags & 0x2000)) {
            return 1;
        }
    }

    if (this->m_moveFlags2 & 0x4) {
        return 1;
    }

    if (this->m_moveFlags & 0x400) {
        return 1;
    }

    if (this->m_moveFlags & 0x2201000) {
        return 1;
    }

    return 0;
}

// ref: FUN_0071c660
int32_t CMovementShared::IsUnsupportedSwimmingOrSlowFalling() const {
    auto spline = this->m_spline;

    if (spline) {
        if (!(spline->flags & 0x400) && (spline->flags & 0x200)) {
            return (this->m_moveFlags & 0x20200000) ? 1 : 0;
        }

        if (!(spline->flags & 0x400) && (spline->flags & 0x2000)) {
            return 1;
        }
    }

    if (this->m_moveFlags2 & 0x4) {
        return 1;
    }

    if (this->m_moveFlags & 0x400) {
        return 1;
    }

    if (this->m_moveFlags & 0x20200000) {
        return 1;
    }

    return 0;
}

// ref: FUN_0071c6c0
int32_t CMovementShared::IsUnsupportedOrHovering() const {
    auto spline = this->m_spline;

    if (spline) {
        if (!(spline->flags & 0x400) && (spline->flags & 0x200)) {
            return (this->m_moveFlags & 0x40000000) ? 1 : 0;
        }

        if (!(spline->flags & 0x400) && (spline->flags & 0x2000)) {
            return 1;
        }
    }

    if (this->m_moveFlags2 & 0x4) {
        return 1;
    }

    if (this->m_moveFlags & 0x400) {
        return 1;
    }

    if (this->m_moveFlags & 0x40000000) {
        return 1;
    }

    return 0;
}

// ref: FUN_0071c720
int32_t CMovementShared::IsUnsupportedHoveringSwimmingOrSlowFalling() const {
    auto spline = this->m_spline;

    if (spline) {
        if (!(spline->flags & 0x400) && (spline->flags & 0x200)) {
            return (this->m_moveFlags & 0x42200000) ? 1 : 0;
        }

        if (!(spline->flags & 0x400) && (spline->flags & 0x2000)) {
            return 1;
        }
    }

    if (this->m_moveFlags2 & 0x4) {
        return 1;
    }

    if (this->m_moveFlags & 0x400) {
        return 1;
    }

    if (this->m_moveFlags & 0x42200000) {
        return 1;
    }

    return 0;
}

// ref: FUN_009880c0
void CMovementShared::UpdateCurrentSpeed(int32_t walk) {
    if (!(this->m_moveFlags & 0x1000) || walk) {
        this->m_currentSpeed = this->GetCurrentSpeed(walk);
    }
}

// ref: FUN_0098b570
void CMovementShared::SetMoveFlags2Bit80(int32_t enable) {
    if (enable) {
        this->m_moveFlags2 |= 0x80;
        return;
    }

    this->m_moveFlags2 &= 0xff7f;
}

// ref: FUN_0098b590
void CMovementShared::SetMoveFlags2Bit100(int32_t enable) {
    if (enable) {
        this->m_moveFlags2 |= 0x100;
        return;
    }

    this->m_moveFlags2 &= 0xfeff;
}
