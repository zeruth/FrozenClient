#include <tempest/Matrix.hpp>
#include "object/client/CMovement_C.hpp"
#include "object/movement/CMovementShared.hpp"
#include <new>
#include <algorithm>
#include <storm/Memory.hpp>
#include "object/movement/CMovementStatus.hpp"
#include "object/movement/CMoveSpline.hpp"
#include <common/Time.hpp>
#include <cmath>
#include <vector>

float NormalizeAngle(float angle);

namespace {

// The constants MovementShared.cpp reads, at their reference addresses.
const float MOVE_SECONDS_PER_MS = 0.0010000000474974513f;   // 0x009e1134
const float MOVE_TURN_EPSILON = 9.5367431640625e-07f;       // 0x009f1224
const float MOVE_SPEED_EPSILON = 2.384185791015625e-07f;    // 0x009ea27c
const float MOVE_DIAGONAL = 0.7071067690849304f;            // 0x009e8d48
const float MOVE_NEG_DIAGONAL = -0.7071067690849304f;       // 0x00aa33d0
const float MOVE_FALL_RATE_SCALE = 0.75f;                   // 0x009e9ee4
const float MOVE_TWO_PI = 6.2831854820251465f;              // 0x00b2d9f0
const float MOVE_TERMINAL_VELOCITY = 60.14800262451172f;    // 0x00b2d9e8
const float MOVE_SAFEFALL_TERMINAL = 7.0f;                  // 0x00b2d9ec
const float MOVE_GRAVITY = 19.291105270385742f;             // 0x00a32f74
const float MOVE_HALF_GRAVITY = 9.645552635192871f;         // 0x00aa33ac
const float MOVE_INV_GRAVITY = 0.051837362349033356f;       // 0x00a37f8c
const float MOVE_NEG_INV_GRAVITY = -0.051837362349033356f;  // 0x00aa33a8
const float MOVE_JUMP_SPEED = -7.955547332763672f;          // 0x00aa33dc
const float MOVE_SWIM_JUMP_SPEED = -9.096748352050781f;     // 0x00aa33e0
const float MOVE_STEEP_PITCH = -0.6457718014717102f;        // 0x00a37f60

// The reference converts an unsigned millisecond count by loading it signed and adding 2^32 when
// the sign bit is set; a uint32_t to float conversion is the same thing.
inline float MsToSeconds(uint32_t ms) {
    return static_cast<float>(ms) * MOVE_SECONDS_PER_MS;
}

// The terminal velocity: safe fall caps it at 7.
inline float TerminalVelocity(uint32_t moveFlags) {
    return (moveFlags & MOVEFLAG_SAFEFALL) ? MOVE_SAFEFALL_TERMINAL : MOVE_TERMINAL_VELOCITY;
}

// ref: FUN_00986f00
// The distance fallen `t` seconds into a fall that started at `startSpeed` (positive downward),
// accelerating at gravity until terminal velocity and constant after it.
float FallDistance(float t, int32_t safeFall, float startSpeed) {
    float terminal = safeFall ? MOVE_SAFEFALL_TERMINAL : MOVE_TERMINAL_VELOCITY;
    float v0 = startSpeed;

    if (terminal < startSpeed) {
        v0 = terminal;
    }

    float v = MOVE_GRAVITY * t + v0;

    if (v > terminal) {
        float tReach = (terminal - v0) * MOVE_INV_GRAVITY;
        return (MOVE_HALF_GRAVITY * tReach + v0) * tReach + (t - tReach) * terminal;
    }

    return (t * MOVE_HALF_GRAVITY + v0) * t;
}

// ref: FUN_00988220
// The time a fall from rest takes to cover `distance`.
float FallTimeForDistance(float distance, int32_t safeFall) {
    float terminal = safeFall ? MOVE_SAFEFALL_TERMINAL : MOVE_TERMINAL_VELOCITY;
    float reachDistance = MOVE_INV_GRAVITY * terminal * terminal * 0.5f;

    if (reachDistance <= distance) {
        return MOVE_INV_GRAVITY * terminal + (distance - reachDistance) / terminal;
    }

    if (distance <= 0.0f) {
        return 0.0f;
    }

    // 0x00aa33d4 is 2 / g.
    return std::sqrt(distance * 0.10367472469806671f);
}

} // namespace

// ref: FUN_0098c580
CMovementShared::CMovementShared(const WOWGUID& transportGUID, const C3Vector& position, float facing, const WOWGUID& guid)
    : CPassenger(transportGUID, position, guid)
{
    this->m_passengerFlags |= 0x1;
    this->m_up = { 0.0f, 0.0f, 1.0f };
    this->m_moveFlags = 0x0;
    this->m_moveFlags2 = 0;
    this->m_transportSeat = 0xFF;

    this->m_anchorPosition = position;
    this->m_anchorFacing = facing;
    this->m_anchorPitch = 0.0f;
    this->m_direction = { 0.0f, 0.0f, 0.0f };
    this->m_direction2d = { 0.0f, 0.0f };
    this->m_fallTime = 0;
    this->m_sinAnchorPitch = 0.0f;
    this->m_cosAnchorPitch = 1.0f;
    this->m_fallStartElevation = position.z;

    this->m_spline = nullptr;
    this->m_currentSpeed = 0.0f;
    this->m_walkSpeed = 0.0f;
    this->m_runSpeed = 0.0f;
    this->m_runBackSpeed = 0.0f;
    this->m_swimSpeed = 0.0f;
    this->m_swimBackSpeed = 0.0f;
    this->m_flightSpeed = 0.0f;
    this->m_flightBackSpeed = 0.0f;
    this->m_turnRate = 0.0f;
    this->m_pitchRate = 0.0f;
    this->m_jumpVelocity = 0.0f;

    this->m_facing = facing;
    this->m_localTimeBase = OsGetAsyncTimeMs();
    this->m_hoverHeight = 1.0f;
}

// ref: FUN_00987570
float CMovementShared::GetCurrentSpeed(int walk) const {
    uint32_t moveFlags = this->m_moveFlags;

    // Not moving forwards, backwards, strafing, ascending or descending
    if (!(moveFlags & 0xc0000f)) {
        return 0.0f;
    }

    CMoveSpline* spline = this->m_spline;

    // A running spline sets its own pace. The test is the spline's flag word at +0x20; this read
    // the unnamed +0x20 slot frozen kept beside it before, which nothing writes.
    if (spline && !(spline->flags & 0x400)) {
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

// ref: FUN_0098b5b0
void CMovementShared::SetMoveFlags2Bit40(int32_t enable) {
    if (enable) {
        this->m_moveFlags2 |= 0x40;
        return;
    }

    this->m_moveFlags2 &= 0xffbf;
}

// ref: FUN_0098b590
void CMovementShared::SetMoveFlags2Bit100(int32_t enable) {
    if (enable) {
        this->m_moveFlags2 |= 0x100;
        return;
    }

    this->m_moveFlags2 &= 0xfeff;
}

// ref: FUN_00723350
bool CMovementShared::IsInForcedMotion() const {
    if ((this->m_moveFlags & 0x1000)
        && ((this->m_moveFlags & 0x2000) || this->m_jumpVelocity != 0.0f)) {
        return true;
    }

    auto spline = this->m_spline;

    if (spline && !(spline->flags & 0x400) && (spline->flags & 0x200)) {
        return true;
    }

    if ((this->m_moveFlags2 & 0x80) && spline && !(spline->flags & 0x400)
        && (spline->flags & 0x800)) {
        return true;
    }

    return false;
}

// ref: FUN_004f5290
// The reference's receiver is the three-pointer block at CGUnit_C +0xd0, whose +8 is the active
// movement; frozen reaches the same spline through the movement itself, as the other three
// spline-flag predicates here do.
int32_t CMovementShared::IsSplineFlag2000() const {
    auto spline = this->m_spline;

    if (spline && !(spline->flags & 0x400) && (spline->flags & 0x2000)) {
        return 1;
    }

    return 0;
}

// ref: FUN_00987490
C3Vector CMovementShared::GetWorldUp() const {
    if (!this->m_transportGUID) {
        return this->m_up;
    }

    C44Matrix transport;
    MovementGetTransportMatrixChecked(this->m_transportGUID, transport, this->m_guid, ".\\MovementShared.cpp", 0x4f);

    return {
        transport.b0 * this->m_up.y + transport.c0 * this->m_up.z + this->m_up.x * transport.a0,
        transport.b1 * this->m_up.y + transport.c1 * this->m_up.z + this->m_up.x * transport.a1,
        transport.b2 * this->m_up.y + transport.c2 * this->m_up.z + this->m_up.x * transport.a2
    };
}

// ref: FUN_009880f0
C3Vector CMovementShared::GetWorldDirection() const {
    if (!this->m_transportGUID) {
        return this->m_direction;
    }

    C44Matrix transport;
    MovementGetTransportMatrixChecked(this->m_transportGUID, transport, this->m_guid, ".\\MovementShared.cpp", 0x3ac);

    return {
        transport.b0 * this->m_direction.y + transport.c0 * this->m_direction.z + this->m_direction.x * transport.a0,
        transport.b1 * this->m_direction.y + transport.c1 * this->m_direction.z + this->m_direction.x * transport.a1,
        transport.b2 * this->m_direction.y + transport.c2 * this->m_direction.z + this->m_direction.x * transport.a2
    };
}

// ---- falling --------------------------------------------------------------------------------

// ref: FUN_00986e10
float CMovementShared::GetFallSpeed() const {
    float terminal = TerminalVelocity(this->m_moveFlags);
    float v0 = this->m_jumpVelocity;

    if (v0 > terminal) {
        v0 = terminal;
    }

    float v = MsToSeconds(this->m_fallTime) * MOVE_GRAVITY + v0;

    if (v > terminal) {
        return terminal;
    }

    return v;
}

// ref: FUN_00986e80
float CMovementShared::GetJumpLiftTime() const {
    if (this->m_moveFlags & 0x1000) {
        return this->m_jumpVelocity * MOVE_NEG_INV_GRAVITY;
    }

    return 0.0f;
}

// ref: FUN_00986ea0
int32_t CMovementShared::IsRising() const {
    if ((this->m_moveFlags & 0x1000) && this->m_jumpVelocity != 0.0f) {
        if (MsToSeconds(this->m_fallTime) < this->m_jumpVelocity * MOVE_NEG_INV_GRAVITY) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_00986f70
float CMovementShared::GetFallHeight(int32_t ms) const {
    float fallen = FallDistance(MsToSeconds(static_cast<uint32_t>(ms)), this->m_moveFlags & 0x20000000, this->m_jumpVelocity);

    // The reference adds the fall to the movement's current z (+0x18), the start of the fall it
    // was asked about: callers pass the time since the fall started.
    return fallen + this->m_position.z;
}

// ref: FUN_00987050
float CMovementShared::GetFallDistance(int32_t ms, float z) const {
    float fallen = FallDistance(MsToSeconds(static_cast<uint32_t>(ms)), this->m_moveFlags & 0x20000000, this->m_jumpVelocity);

    if (!(this->m_moveFlags & 0x1000)) {
        return fallen;
    }

    return (z - this->m_fallStartElevation) + fallen;
}

// ref: FUN_009870d0
float CMovementShared::GetFallDistanceFromHere(int32_t ms) const {
    return this->GetFallDistance(ms, this->m_position.z);
}

// ref: FUN_00987410
void CMovementShared::RestartFall() {
    if (this->m_moveFlags & 0x1000) {
        this->m_jumpVelocity = this->GetFallSpeed();
        this->m_fallTime = 0;
        this->m_fallStartElevation = this->m_position.z;
    }
}

// ---- the anchor and the direction ------------------------------------------------------------

// ref: FUN_00987e30
void CMovementShared::ComputeDirection() {
    float c = std::cos(this->m_anchorFacing);
    float s = std::sin(this->m_anchorFacing);

    this->m_direction2d.x = c;
    this->m_direction2d.y = s;

    if ((this->m_moveFlags & 0x2200000) && MOVE_TURN_EPSILON <= std::fabs(this->m_anchorPitch)) {
        this->m_cosAnchorPitch = std::cos(this->m_anchorPitch);
        this->m_sinAnchorPitch = std::sin(this->m_anchorPitch);

        this->m_direction.x = this->m_cosAnchorPitch * c;
        this->m_direction.y = this->m_cosAnchorPitch * s;
        this->m_direction.z = this->m_sinAnchorPitch;

        return;
    }

    this->m_direction.x = this->m_direction2d.x;
    this->m_direction.y = this->m_direction2d.y;
    this->m_sinAnchorPitch = 0.0f;
    this->m_direction.z = 0.0f;
    this->m_cosAnchorPitch = 1.0f;
}

// ref: FUN_00987ef0
void CMovementShared::UpdateDirection(int32_t force) {
    if ((this->m_moveFlags & 0x1000) && !force) {
        return;
    }

    this->ComputeDirection();

    uint32_t moveFlags = this->m_moveFlags;

    if (!(moveFlags & 0x3)) {
        if (moveFlags & 0xc) {
            // A pure strafe: the heading turned a quarter, left or right.
            float x = this->m_direction2d.x;
            this->m_direction2d.x = this->m_direction2d.y;
            this->m_direction2d.y = x;

            if (this->m_moveFlags & 0x4) {
                this->m_direction2d.x = -this->m_direction2d.x;
            } else {
                this->m_direction2d.y = -this->m_direction2d.y;
            }

            this->m_direction.x = this->m_direction2d.x;
            this->m_direction.y = this->m_direction2d.y;
            this->m_direction.z = 0.0f;

            return;
        }
    } else if (moveFlags & 0xc) {
        // A diagonal: the (possibly reversed) heading plus the strafe direction, scaled by
        // 1/sqrt(2).
        C2Vector heading = this->m_direction2d;
        C3Vector direction = this->m_direction;

        if (moveFlags & 0x2) {
            heading.x = -heading.x;
            heading.y = -heading.y;
            direction.x = -direction.x;
            direction.y = -direction.y;
            direction.z = -direction.z;
        }

        float x = this->m_direction2d.x;
        this->m_direction2d.x = this->m_direction2d.y;
        this->m_direction2d.y = x;

        if (this->m_moveFlags & 0x4) {
            this->m_direction2d.x = -this->m_direction2d.x;
        } else {
            this->m_direction2d.y = -this->m_direction2d.y;
        }

        this->m_direction.x = this->m_direction2d.x + direction.x;
        this->m_direction.y = this->m_direction2d.y + direction.y;
        this->m_direction.z = direction.z;
        this->m_direction2d.x = heading.x + this->m_direction2d.x;
        this->m_direction2d.y = this->m_direction2d.y + heading.y;

        this->m_direction.x *= MOVE_DIAGONAL;
        this->m_direction.y *= MOVE_DIAGONAL;
        this->m_direction.z *= MOVE_DIAGONAL;
        this->m_direction2d.x *= MOVE_DIAGONAL;
        this->m_direction2d.y = MOVE_DIAGONAL * this->m_direction2d.y;

        return;
    }

    if (moveFlags & 0x2) {
        this->m_direction.x = -this->m_direction.x;
        this->m_direction.y = -this->m_direction.y;
        this->m_direction.z = -this->m_direction.z;
        this->m_direction2d.x = -this->m_direction2d.x;
        this->m_direction2d.y = -this->m_direction2d.y;
    }
}

// ref: FUN_009881d0
// The reference's tail call to FUN_005eeb70 is an empty notification (a nullsub in this build).
void CMovementShared::ResetAnchor(int32_t force) {
    this->m_anchorFacing = this->m_facing;
    this->m_anchorPosition = this->m_position;
    this->m_anchorPitch = this->m_pitch;
    this->m_anchorTime = 0;

    this->UpdateDirection(force);
}

void CMovementShared::ReAnchorInline(int32_t refreshSpeed) {
    this->ResetAnchor(0);

    if (refreshSpeed && !(this->m_moveFlags & 0x1000)) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }
}

// ---- displacement ---------------------------------------------------------------------------

namespace {

// ref: FUN_009876b0
// Straight along the 3D direction.
void DisplaceStraight(const CMovementShared* move, float t, C3Vector* out) {
    float speed = move->m_currentSpeed;

    out->x = move->m_direction.x * t * speed;
    out->y = move->m_direction.y * t * speed;
    out->z = move->m_direction.z * t * speed;
}

// ref: FUN_00987700
// Straight along the 2D heading while ascending (or descending), both at 1/sqrt(2).
void DisplaceAscendingStraight(const CMovementShared* move, float t, C3Vector* out) {
    float vertical = (move->m_moveFlags & 0x400000) ? MOVE_DIAGONAL : MOVE_NEG_DIAGONAL;
    float speed = move->m_currentSpeed;
    float y = move->m_direction2d.y * MOVE_DIAGONAL;

    out->x = move->m_direction2d.x * MOVE_DIAGONAL * t * speed;
    out->y = y * t * speed;
    out->z = t * vertical * speed;
}

// The signed turn rate, slowed to three quarters while moving unless move-flags-2 0x8 says the
// unit turns at full rate on the move.
float SignedTurnRate(const CMovementShared* move) {
    uint32_t moveFlags = move->m_moveFlags;
    float rate = 0.0f;

    if (moveFlags & 0x10) {
        rate = move->m_turnRate;
    } else if (moveFlags & 0x20) {
        rate = -move->m_turnRate;
    }

    if ((moveFlags & 0xc0100f) && !(move->m_moveFlags2 & 0x8)) {
        rate *= MOVE_FALL_RATE_SCALE;
    }

    return rate;
}

// The same for pitch, gated on move-flags-2 0x10.
float SignedPitchRate(const CMovementShared* move) {
    uint32_t moveFlags = move->m_moveFlags;
    float rate = 0.0f;

    if (moveFlags & 0x40) {
        rate = move->m_pitchRate;
    } else if (moveFlags & 0x80) {
        rate = -move->m_pitchRate;
    }

    if ((moveFlags & 0xc0100f) && !(move->m_moveFlags2 & 0x10)) {
        rate *= MOVE_FALL_RATE_SCALE;
    }

    return rate;
}

// ref: FUN_00987770
// The facing `t` seconds after the anchor while turning, in [0, 2pi).
float TurnedFacing(const CMovementShared* move, float t) {
    float facing = std::fmod(SignedTurnRate(move) * t + move->m_anchorFacing, MOVE_TWO_PI);

    if (facing < 0.0f) {
        facing += MOVE_TWO_PI;
    }

    return facing;
}

// ref: FUN_009877d0
float PitchedPitch(const CMovementShared* move, float t) {
    return std::fmod(SignedPitchRate(move) * t + move->m_anchorPitch, MOVE_TWO_PI);
}

// ref: FUN_00987820
// Moving while turning and pitching: an arc in both planes.
void DisplaceTurnPitchArc(const CMovementShared* move, float t, C3Vector* out) {
    float turnRate = SignedTurnRate(move);
    float pitchRate = SignedPitchRate(move);

    float speed = move->m_currentSpeed * MOVE_DIAGONAL;
    float turnRadius = speed / turnRate;
    float pitchRadius = speed / pitchRate;

    float cosTurn = std::cos(turnRate * t);
    float sinTurn = std::sin(turnRate * t);
    float cosPitch = std::cos(pitchRate * t);
    float sinPitch = std::sin(pitchRate * t);

    float chord = turnRadius - cosTurn * turnRadius;

    out->x = move->m_direction2d.x * sinTurn * turnRadius - move->m_direction2d.y * chord;
    out->y = chord * move->m_direction2d.x + move->m_direction2d.y * sinTurn * turnRadius;
    out->z = sinPitch * pitchRadius * move->m_sinAnchorPitch
        + (pitchRadius - cosPitch * pitchRadius) * move->m_cosAnchorPitch;
}

// ref: FUN_00987950
// Moving while pitching: an arc in the vertical plane of the heading.
void DisplacePitchArc(const CMovementShared* move, float t, C3Vector* out) {
    float pitchRate = SignedPitchRate(move);

    float radius = move->m_currentSpeed / pitchRate;
    float c = std::cos(pitchRate * t);
    float s = std::sin(pitchRate * t);

    float along = s * radius;
    float chord = radius - c * radius;

    out->x = (along * move->m_cosAnchorPitch - chord * move->m_sinAnchorPitch) * move->m_direction2d.x;
    out->y = (along * move->m_cosAnchorPitch - chord * move->m_sinAnchorPitch) * move->m_direction2d.y;
    out->z = along * move->m_sinAnchorPitch + chord * move->m_cosAnchorPitch;
}

// ref: FUN_00987a00
// Moving while turning: an arc in the horizontal plane, rising with the pitch or the ascent.
void DisplaceTurnArc(const CMovementShared* move, float t, C3Vector* out) {
    float speed = move->m_currentSpeed;
    float turnRate = SignedTurnRate(move);

    float radius = speed / turnRate;
    float c = std::cos(turnRate * t);
    float s = std::sin(turnRate * t);

    uint32_t moveFlags = move->m_moveFlags;
    float along = s * radius;
    float chord = radius - c * radius;
    float x = move->m_direction2d.x * along - move->m_direction2d.y * chord;

    if (moveFlags & 0x400000) {
        out->x = x * MOVE_DIAGONAL;
        out->y = (move->m_direction2d.x * chord + move->m_direction2d.y * along) * MOVE_DIAGONAL;
        out->z = speed * t * MOVE_DIAGONAL;
        return;
    }

    if (moveFlags & 0x800000) {
        out->x = x * MOVE_DIAGONAL;
        out->y = MOVE_DIAGONAL * (move->m_direction2d.x * chord + move->m_direction2d.y * along);
        out->z = speed * t * MOVE_NEG_DIAGONAL;
        return;
    }

    if (moveFlags & 0x3) {
        out->x = x * move->m_cosAnchorPitch;
        out->y = (chord * move->m_direction2d.x + move->m_direction2d.y * along) * move->m_cosAnchorPitch;
        out->z = move->m_sinAnchorPitch * speed * t;
        return;
    }

    out->x = x;
    out->y = chord * move->m_direction2d.x + move->m_direction2d.y * along;
    out->z = 0.0f;
}

} // namespace

// ref: FUN_00987b50
uint32_t CMovementShared::GetDisplacement(uint32_t ms, C3Vector* out, float* facing, float* pitch) {
    if (ms == 0) {
        return this->m_moveFlags & 0xc0100f;
    }

    float t = MsToSeconds(ms);
    uint32_t moveFlags = this->m_moveFlags;
    uint32_t kind = 0;

    if (moveFlags & 0x30) {
        if (facing) {
            *facing = TurnedFacing(this, t);
        }

        moveFlags = this->m_moveFlags;

        // A falling unit's facing still turns, but its path does not curve.
        if (!(moveFlags & 0x1000)) {
            kind = 0x2;
        }
    }

    if (moveFlags & 0xc0) {
        if (pitch) {
            *pitch = PitchedPitch(this, t);
        }

        moveFlags = this->m_moveFlags;

        if (moveFlags & 0xc00000) {
            kind |= 0x10;
        } else {
            kind |= 0x8;
        }
    } else if (moveFlags & 0xc00000) {
        kind |= 0x10;
    }

    if (moveFlags & 0x3) {
        kind |= 0x1;
    }

    if (moveFlags & 0xc) {
        kind |= 0x4;
    }

    switch (kind) {
        case 0x1:
        case 0x4:
        case 0x5:
        case 0xc:
            DisplaceStraight(this, t, out);
            break;

        case 0x3:
        case 0x6:
        case 0x7:
        case 0xe:
        case 0x13:
        case 0x16:
        case 0x17:
            DisplaceTurnArc(this, t, out);
            break;

        case 0x9:
        case 0xd:
            DisplacePitchArc(this, t, out);
            break;

        case 0xb:
        case 0xf:
            DisplaceTurnPitchArc(this, t, out);
            break;

        case 0x10:
        case 0x12: {
            float z = t * this->m_currentSpeed;

            if (!(moveFlags & 0x400000)) {
                z = -z;
            }

            out->x = 0.0f;
            out->y = 0.0f;
            out->z = z;

            break;
        }

        case 0x11:
        case 0x14:
        case 0x15:
            DisplaceAscendingStraight(this, t, out);
            break;

        default:
            break;
    }

    return this->m_moveFlags & 0xc0100f;
}

// ref: FUN_00987d00
uint32_t CMovementShared::AdvanceFromAnchor(uint32_t ms, C3Vector* out) {
    return this->GetDisplacement(ms, out, &this->m_facing, &this->m_pitch);
}

// ---- flag setters ---------------------------------------------------------------------------

// ref: FUN_00988370
int32_t CMovementShared::StartFall(float verticalSpeed) {
    if ((this->m_moveFlags & 0xa00)
        || (this->m_spline && (this->m_spline->flags & 0xa00))) {
        return 0;
    }

    this->ResetAnchor(0);

    uint32_t moveFlags = this->m_moveFlags;
    this->m_moveFlags = (moveFlags & 0xf91fffff) | 0x1000;

    if (!(this->m_moveFlags2 & 0x20)) {
        this->m_moveFlags = (moveFlags & 0xf91fff3f) | 0x1000;
    }

    this->m_fallTime = 0;
    this->m_fallStartElevation = this->m_position.z;
    this->m_jumpVelocity = verticalSpeed;

    return 1;
}

// ref: FUN_009883f0
int32_t CMovementShared::Jump(int32_t checkHover) {
    uint32_t moveFlags = this->m_moveFlags;

    if (checkHover && (moveFlags & 0x40000000) && !(moveFlags & 0x200000)) {
        return 0;
    }

    auto spline = this->m_spline;

    if (spline && !(spline->flags & 0x400) && (spline->flags & 0x2000)) {
        return 0;
    }

    if (moveFlags & 0x2001800) {
        return 0;
    }

    uint16_t moveFlags2 = this->m_moveFlags2;

    if (!(moveFlags2 & 0x4) && this->IsHeldOffGround()) {
        return 0;
    }

    if (moveFlags2 & 0x2) {
        return 0;
    }

    this->StartFall((moveFlags & 0x200000) ? MOVE_SWIM_JUMP_SPEED : MOVE_JUMP_SPEED);

    return 1;
}

// ref: FUN_00988490
void CMovementShared::StopFall() {
    uint32_t falling = this->m_moveFlags & 0x1000;

    if (falling) {
        this->m_moveFlags &= 0xffffcfff;
    }

    if (this->m_moveFlags & 0x100000) {
        this->m_moveFlags = (this->m_moveFlags & 0xff203f00) | 0x800;
    } else if (!falling) {
        return;
    }

    this->ReAnchorInline(1);
}

// ref: FUN_009886e0
void CMovementShared::ForceStopFall() {
    if (this->m_moveFlags & 0x1000) {
        this->m_moveFlags &= 0xffffcfff;
    }

    if (this->m_moveFlags & 0x100000) {
        this->m_moveFlags = (this->m_moveFlags & 0xff203f00) | 0x800;
    }

    this->ReAnchorInline(1);
}

// ref: FUN_00988920
void CMovementShared::SetFromStatus(const CMovementStatus& status) {
    this->m_position = status.position28;
    this->m_anchorPosition = status.position28;
    this->m_facing = status.facing34;
    this->m_anchorFacing = status.facing34;
    this->m_pitch = status.float38;
    this->m_anchorPitch = status.float38;
    this->m_anchorTime = 0;
    this->m_splineElevation = status.float50;

    if (!(this->m_moveFlags & 0x1000)) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }
}

// ref: FUN_00988990
void CMovementShared::SetFallFromStatus(const CMovementStatus& status) {
    this->m_fallTime = status.uint3C;
    this->m_jumpVelocity = status.float40;
    this->m_fallStartElevation = FallDistance(MsToSeconds(status.uint3C), this->m_moveFlags & 0x20000000, status.float40)
        + this->m_position.z;
    this->m_direction2d.x = status.float44;
    this->m_direction2d.y = status.float48;
    this->m_direction.x = status.float44;
    this->m_direction.y = status.float48;
    this->m_direction.z = 0.0f;
    this->m_currentSpeed = status.float4C;
}

// ref: FUN_00988a20
int32_t CMovementShared::SetMoveForward(int32_t forward, int32_t force) {
    this->m_moveFlags &= 0xfffcbfff;
    uint32_t moveFlags = this->m_moveFlags;
    int32_t keepFall = 0;

    if (!force && (moveFlags & 0x1000)) {
        if (moveFlags & 0xf) {
            // Already moving in the air: remember the request for the landing.
            if (!forward) {
                if (!(moveFlags & 0x2)) {
                    this->m_moveFlags = moveFlags | 0x20000;
                }
            } else if (!(moveFlags & 0x1)) {
                this->m_moveFlags = moveFlags | 0x10000;
            }

            return 0;
        }

        keepFall = 1;
    }

    if (!forward) {
        moveFlags = (moveFlags & 0xfffffffe) | 0x2;
    } else {
        moveFlags = (moveFlags & 0xfffffffd) | 0x1;
    }

    this->m_anchorFacing = this->m_facing;
    this->m_moveFlags = moveFlags;
    this->m_anchorPitch = this->m_pitch;
    this->m_anchorPosition = this->m_position;
    this->m_anchorTime = 0;

    this->UpdateDirection(keepFall);

    if (!(this->m_moveFlags & 0x1000) || keepFall) {
        this->m_currentSpeed = this->GetCurrentSpeed(keepFall);
    }

    return 1;
}

// ref: FUN_00988b00
int32_t CMovementShared::SetStrafe(int32_t left) {
    this->m_moveFlags &= 0xfff37fff;
    uint32_t moveFlags = this->m_moveFlags;
    int32_t keepFall = 0;

    if (this->m_moveFlags2 & 0x1) {
        return 0;
    }

    if (moveFlags & 0x1000) {
        if (moveFlags & 0xf) {
            if (!left) {
                if (!(moveFlags & 0x8)) {
                    this->m_moveFlags = moveFlags | 0x80000;
                }
            } else if (!(moveFlags & 0x4)) {
                this->m_moveFlags = moveFlags | 0x40000;
                return 0;
            }

            return 0;
        }

        keepFall = 1;
    }

    if (!left) {
        moveFlags = (moveFlags & 0xfffffffb) | 0x8;
    } else {
        moveFlags = (moveFlags & 0xfffffff7) | 0x4;
    }

    this->m_moveFlags = moveFlags;
    this->ResetAnchor(keepFall);

    if (!(this->m_moveFlags & 0x1000) || keepFall) {
        this->m_currentSpeed = this->GetCurrentSpeed(keepFall);
    }

    return 1;
}

// ref: FUN_00988ba0
void CMovementShared::StopStrafe() {
    this->m_moveFlags &= 0xffff7ff3;
    uint32_t falling = this->m_moveFlags & 0x1000;

    if (!falling) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }

    this->ResetAnchor(0);
}

// ref: FUN_00988dc0
int32_t CMovementShared::StopStrafeIfMoving(uint8_t flags) {
    if ((flags & 0xc) && !(this->m_moveFlags & 0x1000)) {
        this->StopStrafe();
        return 1;
    }

    return 0;
}

// ref: FUN_00988df0
void CMovementShared::SetTurn(int32_t left) {
    if (!left) {
        this->m_moveFlags = (this->m_moveFlags & 0xffffffef) | 0x20;
    } else {
        this->m_moveFlags = (this->m_moveFlags & 0xffffffdf) | 0x10;
    }

    this->m_moveFlags2 &= 0xf7ff;
    this->ResetAnchor(0);
}

// ref: FUN_00989010
int32_t CMovementShared::StopTurn() {
    uint32_t moveFlags = this->m_moveFlags;

    if (!(moveFlags & 0x30)) {
        return 0;
    }

    this->m_anchorPosition = this->m_position;
    this->m_anchorFacing = this->m_facing;
    this->m_anchorPitch = this->m_pitch;
    this->m_moveFlags = moveFlags & 0xffffffcf;
    this->m_anchorTime = 0;

    this->UpdateDirection(0);

    return 1;
}

// ref: FUN_00989220
void CMovementShared::SetPitch(int32_t up) {
    uint32_t moveFlags = this->m_moveFlags;

    if (!(moveFlags & 0x2200000) && !(this->m_moveFlags2 & 0x20)) {
        return;
    }

    if (!up) {
        moveFlags = (moveFlags & 0xffffffbf) | 0x80;
    } else {
        moveFlags = (moveFlags & 0xffffff7f) | 0x40;
    }

    this->m_moveFlags2 &= 0xefff;
    this->m_moveFlags = moveFlags;
    this->ResetAnchor(0);
}

// ref: FUN_00989450
int32_t CMovementShared::StopPitch() {
    uint32_t moveFlags = this->m_moveFlags;

    if (!(moveFlags & 0xc0)) {
        return 0;
    }

    this->m_anchorPosition = this->m_position;
    this->m_anchorFacing = this->m_facing;
    this->m_anchorPitch = this->m_pitch;
    this->m_moveFlags = moveFlags & 0xffffff3f;
    this->m_anchorTime = 0;

    this->UpdateDirection(0);

    return 1;
}

// ref: FUN_00989660
void CMovementShared::StartSwim() {
    this->m_moveFlags = (this->m_moveFlags & 0xf9ffffff) | 0x200000;
    this->ResetAnchor(0);

    this->StopFall();

    if (!(this->m_moveFlags & 0x1000)) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }
}

// ref: FUN_00989890
int32_t CMovementShared::StartFly() {
    uint32_t moveFlags = this->m_moveFlags;

    if (!(moveFlags & 0x1000000) || (moveFlags & 0x100000)) {
        return 0;
    }

    this->m_moveFlags = (moveFlags & 0xfbdfcfff) | 0x2000000;
    this->ResetAnchor(0);

    if (!(this->m_moveFlags & 0x1000)) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }

    return 1;
}

// ref: FUN_009898e0
int32_t CMovementShared::SetAscend(int32_t up) {
    uint32_t moveFlags = this->m_moveFlags;

    if (!(moveFlags & 0x2200000) || (this->m_moveFlags2 & 0x1)) {
        return 0;
    }

    this->m_moveFlags = moveFlags | (up ? 0x400000 : 0x800000);
    this->ResetAnchor(0);

    if (!(this->m_moveFlags & 0x1000)) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }

    return 1;
}

// ref: FUN_00989940
int32_t CMovementShared::StopAscend() {
    uint32_t moveFlags = this->m_moveFlags;

    if (!(moveFlags & 0x2200000)) {
        return 0;
    }

    this->m_moveFlags = moveFlags & 0xff3fffff;

    if (!(moveFlags & 0x1000)) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }

    this->ResetAnchor(0);

    return 1;
}

// ref: FUN_00989b70
void CMovementShared::SetFacing(float facing) {
    if (MOVE_TURN_EPSILON <= std::fabs(facing - this->m_facing)) {
        this->m_facing = facing;

        if (!(this->m_moveFlags & 0x1000)) {
            this->ResetAnchor(0);
        }
    }

    this->m_moveFlags &= 0xffffffcf;
}

// ref: FUN_00989bc0
int32_t CMovementShared::SetPitchAngle(float pitch) {
    if (MOVE_TURN_EPSILON <= std::fabs(pitch - this->m_pitch)) {
        uint32_t moveFlags = this->m_moveFlags;
        this->m_pitch = pitch;

        if (moveFlags & 0x2200000) {
            this->ResetAnchor(0);
            this->m_moveFlags &= 0xffffff3f;
            return 0;
        }

        // Water walking on a steep downward look dips the unit in.
        if ((moveFlags & 0x10000000) && !(moveFlags & 0xc0100f) && !(this->m_moveFlags2 & 0x200)
            && pitch <= MOVE_STEEP_PITCH) {
            this->m_moveFlags &= 0xffffff3f;
            return 1;
        }
    }

    this->m_moveFlags &= 0xffffff3f;

    return 0;
}

// The speed setters: one shape, nine fields.
#define MOVEMENT_SPEED_SETTER(name, field, refreshSpeed)            \
    int32_t CMovementShared::name(float value) {                    \
        if (std::fabs(value - this->field) < MOVE_SPEED_EPSILON) {  \
            return 0;                                               \
        }                                                           \
                                                                    \
        this->field = value;                                        \
        this->ReAnchorInline(refreshSpeed);                         \
                                                                    \
        return 1;                                                   \
    }

// ref: FUN_00989c50
MOVEMENT_SPEED_SETTER(SetTurnRate, m_turnRate, 0)
// ref: FUN_00989e80
MOVEMENT_SPEED_SETTER(SetPitchRate, m_pitchRate, 0)
// ref: FUN_0098a0b0
MOVEMENT_SPEED_SETTER(SetRunSpeed, m_runSpeed, 1)
// ref: FUN_0098a300
MOVEMENT_SPEED_SETTER(SetRunBackSpeed, m_runBackSpeed, 1)
// ref: FUN_0098a550
MOVEMENT_SPEED_SETTER(SetWalkSpeed, m_walkSpeed, 1)
// ref: FUN_0098a7a0
MOVEMENT_SPEED_SETTER(SetSwimSpeed, m_swimSpeed, 1)
// ref: FUN_0098a9f0
MOVEMENT_SPEED_SETTER(SetSwimBackSpeed, m_swimBackSpeed, 1)
// ref: FUN_0098ac40
MOVEMENT_SPEED_SETTER(SetFlightSpeed, m_flightSpeed, 1)
// ref: FUN_0098ae90
MOVEMENT_SPEED_SETTER(SetFlightBackSpeed, m_flightBackSpeed, 1)

#undef MOVEMENT_SPEED_SETTER

// ref: FUN_0098b0e0
void CMovementShared::SetRun(int32_t run) {
    if (!run) {
        this->m_moveFlags |= 0x100;
    } else {
        this->m_moveFlags &= 0xfffffeff;
    }

    this->ReAnchorInline(1);
}

// ref: FUN_005fede0
uint32_t CMovementShared::IsJumping() const {
    return (this->m_moveFlags & 0x1000) && this->m_jumpVelocity != 0.0f ? 1 : 0;
}

// ref: FUN_006e9ad0
int32_t CMovementShared::IsHeldOffGround() const {
    auto spline = this->m_spline;

    if (spline) {
        if (!(spline->flags & 0x400) && (spline->flags & 0x200)) {
            return 0;
        }

        if (!(spline->flags & 0x400) && (spline->flags & 0x2000)) {
            return 1;
        }
    }

    if (!(this->m_moveFlags2 & 0x4) && !(this->m_moveFlags & 0x400)) {
        return 0;
    }

    return 1;
}

// ref: FUN_00986fb0
float CMovementShared::GetLandingFallHeight() const {
    auto spline = this->m_spline;

    if (spline && (spline->flags & 0xa00)) {
        float t = static_cast<float>(spline->uint2C - spline->uint210) * 0.0010000000474974513f;

        // 0x009e2ec4 is 0.5: the spline's vertical acceleration over half the remaining time.
        return FallDistance(t, 0, -(spline->float20C * t * 0.5f));
    }

    float height = this->m_fallStartElevation - this->m_position.z;

    if (this->m_jumpVelocity < -2.384185791015625e-07f) {
        // The jump's rise, v^2 / 2g (0x00aa33b0 is 1 / 2g).
        height += this->m_jumpVelocity * this->m_jumpVelocity * 0.025918681174516678f;
    }

    return 0.0f < height ? height : 0.0f;
}

// ref: FUN_0098b310
int32_t CMovementShared::SetGravity(int32_t enable) {
    uint32_t before = this->m_moveFlags;
    uint32_t after = enable ? (before & 0xfffffbff) : (before | 0x400);

    this->m_moveFlags = after;

    if (before == after) {
        return 0;
    }

    this->ReAnchorInline(0);

    return 1;
}

// ref: FUN_0098b540
void CMovementShared::StopFallAndMoving() {
    this->m_moveFlags |= 0x800;
    this->StopFall();
    this->m_moveFlags &= 0xff203f00;

    if (!(this->m_moveFlags & 0x1000)) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }
}

// ref: FUN_0098b710
int32_t CMovementShared::FallIfUnsupported() {
    if (this->IsOffGround()) {
        return 0;
    }

    return this->StartFall(0.0f);
}

// ref: FUN_0098b730
// FUN_0074b7e0 frees the spline into its pool; frozen allocates splines with the movement update
// that carries them, and none reaches a movement yet, so there is nothing to free beyond the
// pointer.
void CMovementShared::ClearSpline() {
    this->m_moveFlags2 &= 0xff7f;

    if (this->m_spline) {
        CMovementShared::DeleteSpline(this->m_spline);
        this->m_spline = nullptr;

        if (!(this->m_moveFlags & 0x1000)) {
            this->m_currentSpeed = this->GetCurrentSpeed(0);
        }
    }
}

// ref: FUN_0098ba90
void CMovementShared::Teleport(const C3Vector& position, float facing, int32_t clearSpline) {
    this->m_position = position;
    this->m_facing = facing;
    this->m_pitch = 0.0f;

    this->StopFall();

    this->m_moveFlags &= 0xf9003f00;
    this->ReAnchorInline(1);

    auto spline = this->m_spline;

    if (spline && !(spline->flags & 0x400)) {
        if (clearSpline) {
            this->ClearSpline();
            return;
        }

        spline->flags |= 0x400;
    }
}

// ref: FUN_0098bd10
void CMovementShared::StopMove(int32_t endSpline) {
    this->m_moveFlags &= 0xffffbffc;
    uint32_t falling = this->m_moveFlags & 0x1000;

    if (!falling) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }

    this->ResetAnchor(0);

    auto spline = this->m_spline;

    if (spline && !(spline->flags & 0x400)) {
        spline->flags |= 0x400;

        if (endSpline && this->m_spline && (this->m_spline->flags & 0x2000)) {
            if (!this->IsOffGround()) {
                this->StartFall(0.0f);
            }
        }
    }
}

// ref: FUN_0098bf80
int32_t CMovementShared::StopStrafeEvent() {
    uint32_t moveFlags = this->m_moveFlags;

    if (!(moveFlags & 0xc)) {
        if (moveFlags & 0xc0000) {
            this->m_moveFlags = moveFlags & 0xfff3ffff;
        }

        return 0;
    }

    if (moveFlags & 0x4000000) {
        this->m_moveFlags = moveFlags & 0xfbffffff;

        if (!this->IsOffGround()) {
            this->StartFall(0.0f);
        }
    }

    if (this->m_moveFlags & 0x1000) {
        // Strafing stops on landing instead.
        this->m_moveFlags = (this->m_moveFlags & 0xfff3ffff) | 0x8000;
        return 0;
    }

    this->StopStrafe();

    return 1;
}

// ref: FUN_0098bff0
void CMovementShared::StopFly() {
    if (!(this->m_moveFlags2 & 0x20)) {
        this->m_moveFlags &= 0xff1fff3f;
        this->m_anchorPitch = 0.0f;
        this->m_pitch = 0.0f;
    } else {
        this->m_moveFlags &= 0xff1fffff;
    }

    if (!this->IsOffGround()) {
        this->StartFall(0.0f);
    }

    uint32_t falling = this->m_moveFlags & 0x1000;

    if (!falling) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }

    this->ResetAnchor(0);
}

// ref: FUN_0098c240
void CMovementShared::StopFlyAndSwim() {
    this->m_moveFlags &= 0xfd3fff3f;
    this->m_anchorPitch = 0.0f;
    this->m_pitch = 0.0f;

    if (!this->IsOffGround()) {
        this->StartFall(0.0f);
    }

    uint32_t falling = this->m_moveFlags & 0x1000;

    if (!falling) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }

    this->ResetAnchor(0);
}

// ref: FUN_0098c480
void CMovementShared::SetWorldFacing(float facing) {
    float transportFacing = this->m_transportGUID ? MovementGetTransportFacing(this->m_transportGUID) : 0.0f;
    float local = NormalizeAngle(facing - transportFacing);

    if (MOVE_TURN_EPSILON <= std::fabs(local - this->m_facing)) {
        this->m_facing = local;

        if (!(this->m_moveFlags & 0x1000)) {
            this->ResetAnchor(0);
        }
    }

    this->m_moveFlags &= 0xffffffcf;
}

// ref: FUN_0098c4f0
void CMovementShared::Root() {
    uint32_t moveFlags = this->m_moveFlags;

    if (moveFlags & 0x800) {
        return;
    }

    if ((moveFlags & 0x1000) && !this->m_spline) {
        this->m_moveFlags = moveFlags | 0x100000;
        return;
    }

    this->m_moveFlags = moveFlags | 0x800;
    this->StopFall();
    this->m_moveFlags &= 0xff203f00;

    if (!(this->m_moveFlags & 0x1000)) {
        this->m_currentSpeed = this->GetCurrentSpeed(0);
    }
}

// ref: FUN_0098c550
void CMovementShared::Unroot(int32_t fall) {
    this->m_moveFlags &= 0xffeff7ff;

    if (fall && !this->IsOffGround()) {
        this->StartFall(0.0f);
    }
}

// ref: FUN_0098c8a0
int32_t CMovementShared::StopMoveIfMoving(uint8_t flags) {
    if ((flags & 0x3) && !(this->m_moveFlags & 0x1000)) {
        this->StopMove(1);
        return 1;
    }

    return 0;
}

// ref: FUN_0098c8d0
int32_t CMovementShared::StopMoveEvent() {
    uint32_t moveFlags = this->m_moveFlags;

    if (!(moveFlags & 0x3)) {
        if (moveFlags & 0x30000) {
            this->m_moveFlags = moveFlags & 0xfffcffff;
        }

        return 0;
    }

    if (moveFlags & 0x4000000) {
        this->m_moveFlags = moveFlags & 0xfbffffff;

        if (!this->IsOffGround()) {
            this->StartFall(0.0f);
        }
    }

    if (this->m_moveFlags & 0x1000) {
        // Moving stops on landing instead.
        this->m_moveFlags = (this->m_moveFlags & 0xfffcffff) | 0x4000;
        return 0;
    }

    this->StopMove(1);

    return 1;
}

// ref: FUN_00987140
// The transport-time fields come from the movement globals (+0x130, and +0x138 under the +0x134
// latch) in the reference.
void CMovementShared::BuildStatus(int32_t opcode, uint32_t time, CMovementStatus& status) const {
    status.uint0 = time;
    status.uint14 = this->m_moveFlags2;

    if (!this->m_transportGUID && opcode != 0x38d) {
        status.moveFlags &= 0xfffffdff;
        status.transport = 0;
        status.uint54 = 0;
        status.uint58 = 0;
    } else {
        status.transport = this->m_transportGUID;

        auto globals = MovementGetGlobals();
        status.uint54 = globals ? globals->m_transportTime : 0;

        if (!globals || !globals->m_transportTimeLatched) {
            status.uint58 = 0;
        } else {
            status.uint14 |= 0x400;
            status.uint58 = globals->m_transportTime2;
            globals->m_transportTimeLatched = 0;
        }

        status.moveFlags |= 0x200;
        status.byte16 = this->m_transportSeat;
    }

    if (!this->m_spline || (this->m_spline->flags & 0x400)) {
        status.moveFlags &= 0xf7ffffff;
    } else {
        status.moveFlags |= 0x8000000;
    }

    status.position18 = this->m_position;
    status.facing24 = this->m_facing;
    status.position28 = this->GetPosition(this->m_position);
    status.facing34 = this->GetFacing(this->m_facing);
    status.float38 = this->m_pitch;
    status.uint3C = this->m_fallTime;

    if (status.moveFlags & 0x1000) {
        status.float40 = this->m_jumpVelocity;
        status.float44 = this->m_direction2d.x;
        status.float48 = this->m_direction2d.y;
        status.float4C = this->m_currentSpeed;
    }

    if (this->m_moveFlags & 0x4000000) {
        status.float50 = this->m_splineElevation;
    }
}

// ref: FUN_009872c0
// FUN_0074b3f0 asks whether the transport is there; frozen asks the object manager the same
// question through the matrix lookup.
WOWGUID CMovementShared::TakeTransportFromStatus(const CMovementStatus& status) {
    WOWGUID transport = status.transport;

    if (transport) {
        C44Matrix matrix;

        if (!MovementGetTransportMatrix(transport, matrix)) {
            return 0;
        }

        this->m_position = status.position18;
        this->m_anchorPosition = status.position18;
        this->m_facing = status.facing24;
        this->m_anchorFacing = status.facing24;

        this->m_fallStartElevation = FallDistance(MsToSeconds(this->m_fallTime), this->m_moveFlags & 0x20000000, this->m_jumpVelocity)
            + this->m_position.z;
        this->m_splineElevation -= status.position28.z - status.position18.z;
        this->m_transportSeat = status.byte16;
    }

    return transport;
}

// ------------------------------------------------------------------------------------------------
// The server spline a unit is walked along (MovementShared.cpp 0x00986de0 .. 0x0098ca00)
// ------------------------------------------------------------------------------------------------

CMovementShared::~CMovementShared() {
    CMovementShared::DeleteSpline(this->m_spline);
    this->m_spline = nullptr;
}

// ref: FUN_0074b7b0
CMoveSpline* CMovementShared::NewSpline() {
    auto memory = SMemAlloc(sizeof(CMoveSpline), ".\\Movement_C.cpp", 0xa7, 0x0);

    return memory ? new (memory) CMoveSpline() : nullptr;
}

// ref: FUN_0074b7e0
void CMovementShared::DeleteSpline(CMoveSpline* spline) {
    if (!spline) {
        return;
    }

    spline->~CMoveSpline();
    SMemFree(spline, "delete", -1, 0x0);
}

// ref: FUN_009870f0
// A spline to fill, ending at `end`.
void CMovementShared::PrepareSpline(const C3Vector& end) {
    if (!this->m_spline) {
        this->m_spline = CMovementShared::NewSpline();
    }

    this->m_spline->flags = 0;
    this->m_spline->vector1F8 = end;
}

// ref: FUN_0098c770
// Walk the unit along `points` from `time`, taking `duration` ms: curved when the flags ask for a
// Catmull-Rom (0x2000) or a cyclic flight (0x40000), straight otherwise. The spline moves the unit
// forward (backward under 0x8000000), lifts a root for a spline that falls or jumps (0xa00), and
// starts the fall of a falling one (0x200).
void CMovementShared::InitSpline(int32_t time, const C3Vector* points, uint32_t count, uint32_t duration,
                                 uint32_t flags, uint32_t id) {
    auto spline = this->m_spline;

    spline->flags = flags;
    spline->start = static_cast<uint32_t>(time);
    spline->uint28 = 0;
    spline->uint2C = duration;
    spline->spline.m_splineMode = (flags & 0x42000) ? 1 : 0;
    spline->spline.SetPoints(points, count);
    spline->uint30 = id;
    spline->float208 = 1.0f;
    spline->float204 = 1.0f;

    this->SetMoveForward(~(flags >> 27) & 1, 1);

    if ((this->m_moveFlags & 0x800) && (flags & 0xa00)) {
        this->m_moveFlags = (this->m_moveFlags & 0xfffff7ff) | 0x100000;
    }

    if (flags & 0x200) {
        if (this->m_moveFlags & 0x2000000) {
            this->StopFlyAndSwim();
        }

        this->StartFall(0.0f);
    }

    spline->uint210 = 0;
    spline->float20C = 0.0f;
}

// ref: FUN_009873a0
// The spline the unit was told to stop at: its id, no points. A rooted unit the stop lifts
// (0x1000000) stops being rooted by the spline.
void CMovementShared::SetSplineStopped(uint32_t id, uint32_t flags) {
    if ((flags & 0x1000000) && (this->m_moveFlags & 0x800)) {
        this->m_moveFlags = (this->m_moveFlags & 0xfffff7ff) | 0x100000;
    }

    if (this->m_spline) {
        this->m_spline->uint30 = id;
        this->m_spline->spline.SetPoints(nullptr, 0);
    }
}

// ref: FUN_00986de0
// The time the spline takes, its duration stretched by the speed change in force.
float CMovementShared::GetSplineDuration() const {
    return static_cast<float>(this->m_spline->uint2C) * this->m_spline->float204;
}

// ref: FUN_00987d20
// The height the spline's vertical motion puts the unit at `elapsed` ms in: a falling spline
// (0x200) by the fall from where it started, down to its end; a parabolic one (0x800) by the arc
// it was given, from its start time on.
float CMovementShared::GetSplineHeight(uint32_t elapsed, float z) const {
    auto spline = this->m_spline;

    if (!elapsed || !(elapsed < spline->uint2C)) {
        return z;
    }

    if (!(spline->flags & 0x800)) {
        if (spline->flags & 0x200) {
            float height = this->m_fallStartElevation - FallDistance(MsToSeconds(elapsed), 0, 0.0f);

            return spline->vector1F8.z < height ? height : spline->vector1F8.z;
        }

        return z;
    }

    if (!(spline->uint210 < elapsed)) {
        return z;
    }

    float start = MsToSeconds(spline->uint210);
    float t = MsToSeconds(elapsed) - start;
    float total = MsToSeconds(spline->uint2C) - start;

    return (total * spline->float20C * 0.5f * t - spline->float20C * t * t * 0.5f) + z;
}

// ref: FUN_0098c940
// A cyclic spline that came in from outside its loop (0x100000) starts over on the loop alone:
// its first point dropped, the loop's own closing point in its place.
void CMovementShared::RestartSplineLoop(int32_t time) {
    auto old = this->m_spline;

    if (!old) {
        return;
    }

    uint32_t count = old->spline.PointCount();

    if (count <= 3) {
        return;
    }

    TSGrowableArray<C3Vector> points;
    points.SetCount(count);
    old->spline.GetPoints(points.m_data, count);
    points[0] = points[count - 3];

    auto fresh = CMovementShared::NewSpline();

    if (!fresh) {
        return;
    }

    this->m_spline = fresh;
    this->InitSpline(time, points.m_data, count - 1, old->uint2C, old->flags, old->uint30);

    CMovementShared::DeleteSpline(old);
}

// ref: FUN_0098ca00
// Where the spline puts the unit at `time`, and the facing, direction, pitch and tilt it gives
// it there. 0 when the unit is not moving along it at all.
uint32_t CMovementShared::EvaluateSpline(int32_t time, C3Vector* position) {
    *position = this->m_position;

    if (!(this->m_moveFlags & 0x3)) {
        return this->m_moveFlags & 0xc0100f;
    }

    auto spline = this->m_spline;

    if (spline->flags & 0x400000) {
        return 1;
    }

    spline->uint28 = static_cast<uint32_t>(time) - spline->start;

    uint32_t duration = static_cast<uint32_t>(static_cast<float>(spline->uint2C) * spline->float204 + 0.5f);
    float t = 0.0f;

    if (duration == 0) {
        spline->flags |= 0x100;
        t = 1.0f;
    } else if (0 <= static_cast<int32_t>(spline->uint28)) {
        if (spline->uint28 < duration) {
            t = static_cast<float>(spline->uint28) / static_cast<float>(duration);
        } else if (!(spline->flags & 0x80000)) {
            spline->flags |= 0x100;
            t = 1.0f;
        } else {
            // Around the loop again.
            spline->uint28 = spline->uint28 - duration;
            spline->start = static_cast<uint32_t>(time) - spline->uint28;

            if (spline->flags & 0x100000) {
                this->RestartSplineLoop(static_cast<int32_t>(this->m_spline->start));
                spline = this->m_spline;
                spline->flags &= 0xffefffff;
            }

            spline->float204 = spline->float208;
            spline->float208 = 1.0f;

            duration = static_cast<uint32_t>(std::nearbyint(this->GetSplineDuration() + 0.5f));

            if (duration == 0) {
                t = 1.0f;
            } else {
                t = static_cast<float>(spline->uint28) / static_cast<float>(duration);
            }
        }
    }

    C3SplineFrame frame;
    frame.forward = this->m_direction;
    spline->spline.Frame(t, frame, 1);

    if (!(spline->flags & 0x4200)) {
        if (0.0018490000022575259f < frame.forward.y * frame.forward.y + frame.forward.x * frame.forward.x) {
            this->m_facing = std::atan2(frame.forward.y, frame.forward.x);
        }
    }

    if (spline->flags & 0x8000000) {
        this->m_facing -= 3.1415927410125732f;
    }

    if (this->m_facing < 0.0f) {
        this->m_facing = 6.2831854820251465f + this->m_facing;
    }

    this->m_direction = frame.forward;

    if (!(this->m_moveFlags & 0x2200000)) {
        if (spline->flags & 0x2000) {
            // A flying spline banks into its turns: toward where it will be a second on, by twice
            // the angle between the heading and that, at most a right angle.
            float ahead = 1.0f;

            if (duration != 0) {
                ahead = static_cast<float>(spline->uint28 + 1000) / static_cast<float>(duration);
            }

            if ((spline->flags & 0x80000) && 1.0f < ahead) {
                ahead -= 1.0f;
            }

            ahead = std::min(std::max(ahead, 0.0f), 1.0f);

            C3Vector target = { 0.0f, 0.0f, 0.0f };
            spline->spline.Evaluate(ahead, target, 1);

            C3Vector here = this->m_position;
            C2Vector heading = { this->m_direction.x, this->m_direction.y };
            C2Vector toward = { target.x - here.x, target.y - here.y };
            heading.Normalize();
            toward.Normalize();

            float cosine = std::min(std::max(heading.x * toward.x + toward.y * heading.y, -1.0f), 1.0f);
            float angle = std::acos(cosine);

            if (!(heading.y * toward.x - toward.y * heading.x < 0.0f)) {
                angle = -angle;
            }

            float bank = std::min(std::max(angle + angle, -1.5707963705062866f), 1.5707963705062866f);
            C33Matrix rotation = C33Matrix::RotationAroundAxis(bank, this->m_direction, false);
            C3Vector up = { 0.0f, 0.0f, 1.0f };
            this->m_up = up * rotation;
        }
    } else {
        this->m_pitch = std::asin(frame.forward.z);
    }

    *position = frame.position;

    if (!(spline->flags & 0x200000)) {
        if (!(spline->flags & 0x800)) {
            if (spline->flags & 0x200) {
                float z = this->GetSplineHeight(spline->uint28, position->z);
                position->z = z;

                if (std::fabs(z - this->m_spline->vector1F8.z) < MOVE_SPEED_EPSILON) {
                    float fall = FallTimeForDistance(this->m_fallStartElevation - z, 0);
                    int32_t fallMs = static_cast<int32_t>(std::nearbyint(fall * 1000.0f));

                    if (fallMs < static_cast<int32_t>(this->m_spline->uint28)) {
                        this->m_spline->uint28 = static_cast<uint32_t>(fallMs);
                    }

                    this->m_spline->flags |= 0x100;
                }
            }
        } else {
            if (spline->uint210 < spline->uint28 && !(this->m_moveFlags2 & 0x80)) {
                this->m_fallStartElevation = this->m_position.z;
                this->m_moveFlags2 |= 0x80;
            }

            float z = this->GetSplineHeight(this->m_spline->uint28, position->z);
            position->z = z;

            if (this->m_fallStartElevation < z) {
                this->m_fallStartElevation = this->m_position.z;
                return 1;
            }
        }
    } else if (!(this->m_moveFlags2 & 0x100) && spline->uint210 < spline->uint28) {
        this->m_moveFlags2 |= 0x100;
        return 1;
    }

    return 1;
}

// ref: FUN_0098b5d0
// A flight told how far along it should be: the next lap is stretched or squeezed (between half
// and twice its time) to catch up, the difference wrapped to within half a lap.
void CMovementShared::SyncSplineProgress(float progress) {
    auto spline = this->m_spline;

    if (!spline) {
        return;
    }

    if (spline->uint2C != 0) {
        float duration = static_cast<float>(spline->uint2C);

        if (0.01f <= duration * spline->float204) {
            float behind = progress - static_cast<float>(spline->uint28) / (duration * spline->float204);

            if (0.5f < behind) {
                behind -= 1.0f;
            }

            if (behind < -0.5f) {
                behind += 1.0f;
            }

            int32_t ahead = static_cast<int32_t>(std::nearbyint(duration * behind));
            float stretch = static_cast<float>(static_cast<int32_t>(spline->uint2C) - ahead) / duration;

            spline->float208 = std::min(std::max(stretch, 0.5f), 2.0f);

            return;
        }
    }

    spline->float208 = 1.0f;
}

// ------------------------------------------------------------------------------------------------
// Moving between a transport's space and the world's (MovementShared.cpp 0x0098b770 .. 0x0098c760)
// ------------------------------------------------------------------------------------------------

// ref: FUN_0098b770
// The spline's facing spot is carried, then its facing turned -- both, whichever the spline faces,
// over the same union as the reference does -- and its points carried.
void CMovementShared::TransformSpline(const C44Matrix& matrix, float facing) {
    auto spline = this->m_spline;

    C3Vector out;
    TransformPointInPlace(out, spline->face.spot, matrix);
    spline->face.facing = NormalizeAngle(spline->face.facing + facing);

    uint32_t count = spline->spline.PointCount();

    if (!count) {
        return;
    }

    std::vector<C3Vector> storage(count);
    auto points = storage.data();
    spline->spline.GetPoints(points, count);

    for (uint32_t i = 0; i < count; i++) {
        points[i] = points[i] * matrix;
    }

    spline->spline.SetPoints(points, count);
}

// ref: FUN_0098b850
void CMovementShared::TransformState(const C44Matrix& matrix, float facing, const C3Vector& position) {
    this->m_fallStartElevation = matrix.a2 * position.x + matrix.c2 * this->m_fallStartElevation + matrix.b2 * position.y + matrix.d2;
    this->m_splineElevation = this->m_splineElevation * matrix.c2 + matrix.a2 * position.x + matrix.b2 * position.y + matrix.d2;

    C3Vector out;
    TransformPointInPlace(out, this->m_anchorPosition, matrix);
    this->m_anchorFacing = NormalizeAngle(this->m_anchorFacing + facing);

    C3Vector d = this->m_direction;
    this->m_direction = {
        matrix.a0 * d.x + matrix.b0 * d.y + matrix.c0 * d.z,
        matrix.b1 * d.y + matrix.c1 * d.z + matrix.a1 * d.x,
        d.z * matrix.c2 + matrix.a2 * d.x + matrix.b2 * d.y,
    };

    this->m_direction2d = { this->m_direction.x, this->m_direction.y };

    float length = this->m_direction2d.x * this->m_direction2d.x + this->m_direction2d.y * this->m_direction2d.y;

    if (2.384185791015625e-07f < length) {
        float inv = 1.0f / std::sqrt(length);
        this->m_direction2d.x *= inv;
        this->m_direction2d.y = inv * this->m_direction2d.y;
    }

    if (this->m_spline && !(this->m_spline->flags & 0x400)) {
        this->TransformSpline(matrix, facing);
    }
}

// ref: FUN_0098b9a0
// PHASE4(Vehicle_C): a unit transport's seat lets the passenger go (FUN_0074b670 -> FUN_00757ef0).
float CMovementShared::LeaveTransportState(const C44Matrix& matrix, float facing, const C3Vector& position) {
    this->TransformState(matrix, facing, position);
    this->m_transportLink.Unlink();
    this->m_moveFlags &= ~0x8000000u;

    return facing;
}

// ref: FUN_0098c730
float CMovementShared::LeaveTransportSpace(C44Matrix& matrix) {
    C3Vector position = this->m_position;
    float facing = this->TransformToWorld(matrix);

    return this->LeaveTransportState(matrix, facing, position);
}

// ref: FUN_0098ba20
float CMovementShared::EnterTransportSpace(WOWGUID transport, C44Matrix& inverse, C44Matrix* matrix) {
    C3Vector position = this->m_position;
    float facing = this->TransformToTransport(transport, inverse, matrix);
    this->TransformState(inverse, facing, position);
    MovementNotifyTransport(this, transport, 0);

    return facing;
}
