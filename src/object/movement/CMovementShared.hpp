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
        uint32_t GetMoveFlags() const { return this->m_moveFlags; }
        uint16_t GetMoveFlags2() const { return this->m_moveFlags2; }
        float GetWalkSpeed() const { return this->m_walkSpeed; }
        void ClearSplineEnabled();
        void SetWaterWalking(int32_t enable);
        void SetSafeFall(int32_t enable);
        void SetHover(int32_t enable);

        // ref: FUN_009880c0
        // Refresh the cached speed, unless the 0x1000 move flag holds it and walk is 0.
        void UpdateCurrentSpeed(int32_t walk);

        // ref: FUN_0098b570
        void SetMoveFlags2Bit80(int32_t enable);

        // ref: FUN_0098b590
        void SetMoveFlags2Bit100(int32_t enable);

        // ref: FUN_006e9640
        void SetSplineFacingSpot(const C3Vector& spot);

        // ref: FUN_006e9670
        void SetSplineFacingTarget(const WOWGUID& target);

        // ref: FUN_006e96a0
        void SetSplineFacingAngle(float facing);

        // ref: FUN_006e9a70
        // A spline is running (0x400 clear) and carries flag 0x200.
        int32_t IsSplineFlag200() const;

        // ref: FUN_006e9aa0
        // A spline is running (0x400 clear) and carries flag 0x800.
        int32_t IsSplineFlag800() const;

        // ref: FUN_006eaba0
        // Off the ground by any of the movement flags the reference tests: flying, swimming or
        // falling, a running spline with flag 0x2000, or the 0x4 bit of the second flag word.
        int32_t IsOffGround() const;

        // Three variants of the same test with a different last mask: unsupported (a running
        // spline with flag 0x2000, second-word bit 0x4, or move flag 0x400), or any of the mask's
        // move flags. A running spline with flag 0x200 goes straight to the mask.

        // ref: FUN_0071c660
        // Mask 0x20200000: swimming, falling slowly.
        int32_t IsUnsupportedSwimmingOrSlowFalling() const;

        // ref: FUN_0071c6c0
        // Mask 0x40000000: hovering.
        int32_t IsUnsupportedOrHovering() const;

        // ref: FUN_0071c720
        // Mask 0x42200000: hovering, swimming, falling slowly.
        int32_t IsUnsupportedHoveringSwimmingOrSlowFalling() const;

    protected:
        // Protected member variables
        // TODO
        uint32_t m_moveFlags;       // ref +0x44
        // The second movement flag word. Written by movement code not ported yet, so it reads 0.
        uint16_t m_moveFlags2 = 0;  // ref +0x48
        C3Vector m_anchorPosition;
        float m_anchorFacing;
        float m_anchorPitch;
        // TODO
        C3Vector m_direction;
        C2Vector m_direction2d;
        float m_cosAnchorPitch;
        float m_sinAnchorPitch;
        // TODO
        float m_currentSpeed = 0.0f;    // ref +0x8c
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
