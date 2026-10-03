#ifndef OBJECT_MOVEMENT_C_MOVEMENT_SHARED_HPP
#define OBJECT_MOVEMENT_C_MOVEMENT_SHARED_HPP

#include "object/movement/CPassenger.hpp"
#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

struct CMoveSpline;
struct CMovementStatus;

// The movement flag word (+0x44) as the reference tests it. Only the bits the movement port reads
// by meaning are named; the masks it combines stay numeric, as they are in the reference.
enum MOVEMENTFLAGS : uint32_t {
    MOVEFLAG_FORWARD        = 0x1,
    MOVEFLAG_BACKWARD       = 0x2,
    MOVEFLAG_STRAFE_LEFT    = 0x4,
    MOVEFLAG_STRAFE_RIGHT   = 0x8,
    MOVEFLAG_TURN_LEFT      = 0x10,
    MOVEFLAG_TURN_RIGHT     = 0x20,
    MOVEFLAG_PITCH_UP       = 0x40,
    MOVEFLAG_PITCH_DOWN     = 0x80,
    MOVEFLAG_WALK           = 0x100,
    MOVEFLAG_ONTRANSPORT    = 0x200,
    MOVEFLAG_LEVITATING     = 0x400,
    MOVEFLAG_ROOT           = 0x800,
    MOVEFLAG_FALLING        = 0x1000,
    MOVEFLAG_FALLINGFAR     = 0x2000,
    MOVEFLAG_SWIMMING       = 0x200000,
    MOVEFLAG_ASCENDING      = 0x400000,
    MOVEFLAG_DESCENDING     = 0x800000,
    MOVEFLAG_CAN_FLY        = 0x1000000,
    MOVEFLAG_FLYING         = 0x2000000,
    MOVEFLAG_SPLINE_ELEV    = 0x4000000,
    MOVEFLAG_SPLINE_ENABLED = 0x8000000,
    MOVEFLAG_WATERWALKING   = 0x10000000,
    MOVEFLAG_SAFEFALL       = 0x20000000,
    MOVEFLAG_HOVER          = 0x40000000,
};

class CMovementShared : public CPassenger {
    public:
        // Public member functions
        // ref: FUN_0098c580
        CMovementShared(const WOWGUID& transportGUID, const C3Vector& position, float facing, const WOWGUID& guid);
        // The spline belongs to the movement (ClearSpline frees it the same way).
        ~CMovementShared();
        CMovementShared(const CMovementShared&) = delete;
        CMovementShared& operator=(const CMovementShared&) = delete;
        float GetCurrentSpeed(int walk) const;
        uint32_t GetMoveFlags() const { return this->m_moveFlags; }
        uint16_t GetMoveFlags2() const { return this->m_moveFlags2; }
        float GetFloatB8() const { return this->m_jumpVelocity; }
        const CMoveSpline* GetSpline() const { return this->m_spline; }
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
        // ref: FUN_0098b5b0
        void SetMoveFlags2Bit40(int32_t enable);

        // The server spline (0x00986de0 .. 0x0098ca00)
        static CMoveSpline* NewSpline();
        static void DeleteSpline(CMoveSpline* spline);
        void PrepareSpline(const C3Vector& end);
        void InitSpline(int32_t time, const C3Vector* points, uint32_t count, uint32_t duration, uint32_t flags,
                        uint32_t id);
        void SetSplineStopped(uint32_t id, uint32_t flags);
        float GetSplineDuration() const;
        float GetSplineHeight(uint32_t elapsed, float z) const;
        void RestartSplineLoop(int32_t time);
        uint32_t EvaluateSpline(int32_t time, C3Vector* position);
        void SyncSplineProgress(float progress);

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

        // ref: FUN_004f5290
        // Spline flag 0x2000 (with 0x400 clear), which the movement-animation chooser reads as
        // "carried backwards", and answers with 135 rather than a walk or a run.
        int32_t IsSplineFlag2000() const;

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

        // ref: FUN_00723350
        // The unit is being carried rather than walking: a long fall that is either stopping or has
        // vertical speed left, or a running spline that is not of the kind flagged 0x400 and is
        // marked either 0x200, or 0x800 while move-flags-2 0x80 is set. What the action-animation
        // gate consults to decide that the unit's own pose is not its to choose.
        bool IsInForcedMotion() const;

        // ref: FUN_00987490
        // The movement's up vector in world space: turned by the transport's matrix when it
        // rides one.
        C3Vector GetWorldUp() const;

        // ref: FUN_0071c720
        // Mask 0x42200000: hovering, swimming, falling slowly.
        int32_t IsUnsupportedHoveringSwimmingOrSlowFalling() const;

        // ---- the anchor and the direction of travel ----------------------------------------
        //
        // Movement is integrated from an ANCHOR: the position, facing and pitch the unit had when
        // its movement last changed, plus the time spent since (+0x60). Every flag change
        // re-anchors, so the integrator never accumulates error over a long run -- it evaluates
        // a closed-form path from the anchor instead.

        // ref: FUN_00987e30
        // The unit vectors of travel from the anchor's facing and pitch: the 2D heading at +0x70,
        // and the 3D one at +0x64, pitched only when swimming or flying.
        void ComputeDirection();

        // ref: FUN_00987ef0
        // ComputeDirection, then turned for backward and strafing movement (a diagonal is the
        // normalised sum). Falling keeps the direction it had unless `force` is set.
        void UpdateDirection(int32_t force);

        // ref: FUN_009881d0
        // Re-anchor at the current position, facing and pitch, and recompute the direction.
        void ResetAnchor(int32_t force);

        // ref: FUN_00987b50
        // The displacement `ms` milliseconds after the anchor, by the combination of moving,
        // turning and pitching flags; the facing and pitch reached go out through the pointers.
        // Returns the moving bits (mask 0xc0100f).
        uint32_t GetDisplacement(uint32_t ms, C3Vector* out, float* facing, float* pitch);

        // ref: FUN_00987d00
        // GetDisplacement into the movement's own facing and pitch.
        uint32_t AdvanceFromAnchor(uint32_t ms, C3Vector* out);

        // ref: FUN_009880f0
        // The 3D direction of travel in world space.
        C3Vector GetWorldDirection() const;

        // ---- falling --------------------------------------------------------------------------

        // ref: FUN_00986e10
        // The fall speed now: the starting vertical speed (capped at terminal) plus gravity for
        // the time fallen, capped at terminal velocity.
        float GetFallSpeed() const;

        // ref: FUN_00986e80
        // The jump's lift as a time (vertical speed over gravity), 0 when not falling.
        float GetJumpLiftTime() const;

        // ref: FUN_00986ea0
        // Still rising: falling, with a vertical speed, and less time fallen than the lift.
        int32_t IsRising() const;

        // ref: FUN_00986f70
        // The height `ms` into the fall: the start elevation (+0x84 is the anchor's z) minus the
        // distance fallen.
        float GetFallHeight(int32_t ms) const;

        // ref: FUN_00987050
        // The distance fallen after `ms`, measured from `z`, when falling.
        float GetFallDistance(int32_t ms, float z) const;

        // ref: FUN_009870d0
        float GetFallDistanceFromHere(int32_t ms) const;

        // ref: FUN_00987410
        // Restart the fall clock at the current height with the current fall speed.
        void RestartFall();

        // ---- flag setters, each re-anchoring ------------------------------------------------

        // ref: FUN_00988370
        // Start falling with an initial vertical speed, unless on a transport or a spline that
        // forbids it.
        int32_t StartFall(float verticalSpeed);

        // ref: FUN_009883f0
        // Jump: start a fall with the jump's lift (swimming jumps lower), when nothing forbids it.
        int32_t Jump(int32_t checkHover);

        // ref: FUN_00988490
        // Land: stop falling (and leave swimming's ascent), re-anchor, refresh the speed.
        void StopFall();

        // ref: FUN_009886e0
        // StopFall's body, unconditionally.
        void ForceStopFall();

        // ref: FUN_00988920
        // Take position, facing, pitch and spline elevation from a movement status, re-anchored.
        void SetFromStatus(const CMovementStatus& status);

        // ref: FUN_00988990
        // Take the fall clock, speed and direction from a movement status.
        void SetFallFromStatus(const CMovementStatus& status);

        // ref: FUN_00988a20
        // Move forward (1) or backward (0). Mid-fall the request is deferred into the 0x10000 /
        // 0x20000 bits rather than changing the path in the air.
        int32_t SetMoveForward(int32_t forward, int32_t force);

        // ref: FUN_00988b00
        // Strafe left (1) or right (0); deferred the same way mid-fall.
        int32_t SetStrafe(int32_t left);

        // ref: FUN_00988ba0
        // Stop strafing.
        void StopStrafe();

        // ref: FUN_00988dc0
        int32_t StopStrafeIfMoving(uint8_t flags);

        // ref: FUN_00988df0
        // Turn left (1) or right (0).
        void SetTurn(int32_t left);

        // ref: FUN_00989010
        // Stop turning; 0 when it was not turning.
        int32_t StopTurn();

        // ref: FUN_00989220
        // Pitch up (1) or down (0), only when swimming or flying.
        void SetPitch(int32_t up);

        // ref: FUN_00989450
        // Stop pitching; 0 when it was not.
        int32_t StopPitch();

        // ref: FUN_00989660
        // Start swimming.
        void StartSwim();

        // ref: FUN_00989890
        // Start flying, when allowed (0x1000000) and not swimming.
        int32_t StartFly();

        // ref: FUN_009898e0
        // Ascend (1) or descend (0), swimming or flying.
        int32_t SetAscend(int32_t up);

        // ref: FUN_00989940
        // Stop ascending or descending.
        int32_t StopAscend();

        // ref: FUN_00989b70
        void SetFacing(float facing);

        // ref: FUN_00989bc0
        int32_t SetPitchAngle(float pitch);

        // ref: FUN_00989c50, FUN_00989e80, FUN_0098a0b0 .. FUN_0098ae90
        // A speed setter re-anchors when the value really changes (by more than 2^-22) and says
        // whether it did.
        int32_t SetTurnRate(float rate);
        int32_t SetPitchRate(float rate);
        int32_t SetRunSpeed(float speed);
        int32_t SetRunBackSpeed(float speed);
        int32_t SetWalkSpeed(float speed);
        int32_t SetSwimSpeed(float speed);
        int32_t SetSwimBackSpeed(float speed);
        int32_t SetFlightSpeed(float speed);
        int32_t SetFlightBackSpeed(float speed);

        // ref: FUN_0098b0e0
        // Run (1) or walk (0).
        void SetRun(int32_t run);

        // ref: FUN_0098b310
        // Enable (1) or disable (0) gravity -- move flag 0x400; 0 when nothing changed.
        int32_t SetGravity(int32_t enable);

        // ref: FUN_005fede0
        // Falling with the vertical speed a jump gives.
        uint32_t IsJumping() const;

        // ref: FUN_006e9ad0
        // Held off the ground: by a running spline flagged 0x2000 (but not one flagged 0x200), by
        // move-flags-2 0x4, or by disabled gravity.
        int32_t IsHeldOffGround() const;

        // ref: FUN_00986fb0
        // How far the unit has fallen: from the fall's start, plus a jump's rise to its apex, never
        // negative. On a spline that falls, the spline's own drop.
        float GetLandingFallHeight() const;

        // ref: FUN_0098b540
        void StopFallAndMoving();

        // ref: FUN_0098b710
        // Start falling from standing when nothing holds the unit up.
        int32_t FallIfUnsupported();

        // ref: FUN_0098b730
        // Drop the spline.
        void ClearSpline();

        // ref: FUN_0098ba90
        // Teleport to a position and facing, stopping all movement.
        void Teleport(const C3Vector& position, float facing, int32_t clearSpline);

        // ref: FUN_0098bd10
        // Stop moving forward and backward (and the 0x4000 bit).
        void StopMove(int32_t endSpline);

        // ref: FUN_0098bf80
        int32_t StopStrafeEvent();

        // ref: FUN_0098bff0
        // Stop flying; pitch is kept when move-flags-2 0x20 allows it.
        void StopFly();

        // ref: FUN_0098c240
        // Stop flying and swimming and ascent, and level out.
        void StopFlyAndSwim();

        // ref: FUN_0098c480
        // Face a world facing (made relative to the transport).
        void SetWorldFacing(float facing);

        // ref: FUN_0098c4f0
        void Root();

        // ref: FUN_0098c550
        void Unroot(int32_t fall);

        // ref: FUN_0098c8a0
        int32_t StopMoveIfMoving(uint8_t flags);

        // ref: FUN_0098c8d0
        int32_t StopMoveEvent();

        // ref: FUN_00987140
        // Fill a movement status from this movement, for a packet at `time` of `opcode`.
        void BuildStatus(int32_t opcode, uint32_t time, CMovementStatus& status) const;

        // ref: FUN_009872c0
        // Take the transport position and facing from a status; the transport's guid, or 0 when
        // the transport is not there.
        WOWGUID TakeTransportFromStatus(const CMovementStatus& status);

        // Public member variables. The movement and collision ports read these across the class
        // hierarchy the way the reference does.
        // ref +0x30: the link in the movement globals' list of units that are moving.
        TSLink<CMovementShared> m_moverLink;
        // ref +0x38: the movement's up vector, in the transport's space.
        C3Vector m_up = { 0.0f, 0.0f, 1.0f };
        uint32_t m_moveFlags;           // ref +0x44
        uint16_t m_moveFlags2 = 0;      // ref +0x48
        uint8_t m_transportSeat = 0xFF; // ref +0x4a
        C3Vector m_anchorPosition;      // ref +0x4c
        float m_anchorFacing;           // ref +0x58
        float m_anchorPitch;            // ref +0x5c
        uint32_t m_anchorTime = 0;      // ref +0x60, ms travelled since the anchor
        C3Vector m_direction = {};      // ref +0x64
        C2Vector m_direction2d = {};    // ref +0x70
        float m_cosAnchorPitch;         // ref +0x78
        float m_sinAnchorPitch;         // ref +0x7c
        uint32_t m_fallTime = 0;        // ref +0x80
        float m_fallStartElevation = 0.0f;  // ref +0x84
        float m_splineElevation = 0.0f;     // ref +0x88
        float m_currentSpeed = 0.0f;    // ref +0x8c
        float m_walkSpeed;              // ref +0x90
        float m_runSpeed;               // ref +0x94
        float m_runBackSpeed;           // ref +0x98
        float m_swimSpeed;              // ref +0x9c
        float m_swimBackSpeed;          // ref +0xa0
        float m_flightSpeed;            // ref +0xa4
        float m_flightBackSpeed;        // ref +0xa8
        float m_turnRate = 0.0f;        // ref +0xac
        float m_pitchRate = 0.0f;       // ref +0xb0
        // ref +0xb4, 1.0 at construction: the hover height the collision keeps a hovering unit at.
        float m_hoverHeight = 1.0f;
        // ref +0xb8: the vertical speed the fall started with (a jump's lift, negative upward).
        float m_jumpVelocity = 0.0f;
        CMoveSpline* m_spline;          // ref +0xbc
        uint32_t m_localTimeBase = 0;   // ref +0xc0
        uint32_t m_remoteTimeBase = 0;  // ref +0xc4

    protected:
        // The inlined anchor reset the setters share: the anchor fields, then UpdateDirection
        // when not falling, then the speed. The reference repeats this block in every setter.
        void ReAnchorInline(int32_t refreshSpeed);
};

#endif
