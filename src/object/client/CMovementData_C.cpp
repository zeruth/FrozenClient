#include "object/client/CMovementData_C.hpp"
#include "model/CM2Model.hpp"
#include "object/client/CClientMoveUpdate.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CVehicle_C.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/UnitVehicle_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/movement/CMovementStatus.hpp"
#include "object/movement/CMoveSpline.hpp"
#include "net/Types.hpp"
#include "ui/InputControl.hpp"
#include "util/Unimplemented.hpp"
#include <common/Time.hpp>
#include <tempest/Matrix.hpp>
#include <cmath>

float NormalizeAngle(float angle);

namespace {

const float MOVE_TURN_EPSILON = 9.5367431640625e-07f;   // 0x009f1224
const float MOVE_PI = 3.1415927410125732f;              // 0x00aa33a4

// ref: FUN_004c50c0
// An angle difference brought into (-pi, pi].
float WrapAngleDelta(float angle) {
    float wrapped = std::fmod(angle, 6.2831854820251465f);

    if (wrapped < -MOVE_PI) {
        return wrapped + 6.2831854820251465f;
    }

    if (MOVE_PI < wrapped) {
        wrapped -= 6.2831854820251465f;
    }

    return wrapped;
}

// ref: FUN_00406de0
// A point the map can hold: finite, and `margin` inside the map's square on both axes.
bool MovementPointOnMap(float x, float y, float z, float margin) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return false;
    }

    // 0x009e2ac8 is the map's full extent (64 tiles of 533.33).
    const float extent = 34133.33203125f;
    const float half = 17066.666f;

    float fromY = -(y - half);
    float fromX = -(x - half);

    if (fromY <= margin) {
        return false;
    }

    return fromY < extent - margin && margin < fromX && fromX < extent - margin;
}

} // namespace

// ref: FUN_006ebd30
CMovementData_C::CMovementData_C(const C3Vector& position, float facing, const WOWGUID& guid, CGUnit_C* unit)
    : CMovementShared(0, position, facing, guid)
{
    this->m_collisionRadius = 0.3333333432674408f;  // 0x00a12098
    this->m_collisionHeight = 2.027777671813965f;   // 0x00a32848
    this->m_stepHeightScale = 1.0f;
    this->m_interpPosition = { 0.0f, 0.0f, 0.0f };
    this->m_latencyIndex = 0;
    this->m_interpTime = 0;
    this->m_extrapolateTime = 0;
    this->m_uint138 = 0;
    this->m_owner = unit;

    this->UpdateDirection(0);

    for (int32_t i = 0; i < 16; i++) {
        this->m_latency[i] = -50;
        this->m_latency[i + 16] = 50;
    }

    this->m_latencyMax = 50;
}

bool CMovementData_C::IsActivePlayer() const {
    return this->m_guid == ClntObjMgrGetActivePlayer();
}

// ref: FUN_004f5240
bool CMovementData_C::IsSplineActive() const {
    return this->m_spline && !(this->m_spline->flags & 0x400);
}

// ---- event creation -------------------------------------------------------------------------

namespace {

// The body every simple creator shares: a fresh event with no facing, pitch, counter or flags,
// marked to be sent, queued, and the movement put on the movers list if it is not there yet.
CPlayerMoveEvent* QueueSimple(CMovementData_C* move, int32_t time, int32_t type, uint8_t send) {
    auto event = MoveEventAllocate(time, type);

    if (!event) {
        return nullptr;
    }

    event->facing = 0.0f;
    event->pitch = 0.0f;
    event->send = send;
    event->counter = 0;
    event->moveFlags2 = 0;

    MoveEventQueueInsert(&move->m_events, event);

    if (!move->m_moverLink.IsLinked() && MovementGetGlobals()) {
        MovementLinkMover(move);
    }

    return event;
}

} // namespace

// ref: FUN_006ecb50
void CMovementData_C::QueueStartMove(int32_t time, int32_t forward) {
    QueueSimple(this, time, forward == 0 ? 1 : 0, 1);
}

// ref: FUN_006ecbb0
void CMovementData_C::QueueStartStrafe(int32_t time, int32_t left) {
    QueueSimple(this, time, 4 - (left != 0 ? 1 : 0), 1);
}

// ref: FUN_006ecc20
void CMovementData_C::QueueJump(int32_t time) {
    QueueSimple(this, time, 10, 1);
}

// ref: FUN_006eccf0
void CMovementData_C::QueueFallIfUnsupported(int32_t time) {
    QueueSimple(this, time, 9, 1);
}

// ref: FUN_006ecd50
void CMovementData_C::QueueFallIfUnsupportedLocal(int32_t time) {
    QueueSimple(this, time, 9, this->IsActivePlayer() ? 1 : 0);
}

// ref: FUN_006ecde0
void CMovementData_C::QueueStopMove(int32_t time) {
    QueueSimple(this, time, 2, 1);
}

// ref: FUN_006ece40
void CMovementData_C::QueueStopStrafe(int32_t time) {
    QueueSimple(this, time, 5, 1);
}

// ref: FUN_006ecea0
// A stop drops the pending "stop at the target facing" event (0x32) a mouse turn queued.
void CMovementData_C::QueueStopTurn(int32_t time) {
    QueueSimple(this, time, 0xd, 1);
    MoveEventQueueRemoveType(&this->m_events, 0x32);
}

// ref: FUN_006ef370
void CMovementData_C::QueueTimeSync(int32_t time, uint32_t counter) {
    auto event = QueueSimple(this, time, 0x31, 1);

    if (event) {
        event->counter = counter;
    }
}

// ref: FUN_006ecf10
void CMovementData_C::QueueSetRun(int32_t time, int32_t run) {
    QueueSimple(this, time, 0x12 - (run != 0 ? 1 : 0), 1);
}

// ref: FUN_006eeca0
void CMovementData_C::QueueStopPitch(int32_t time) {
    QueueSimple(this, time, 0x10, 1);
    MoveEventQueueRemoveType(&this->m_events, 0x33);
}

// ref: FUN_006ef230
void CMovementData_C::QueueSetFlying(int32_t time, int32_t fly) {
    QueueSimple(this, time, 0x2e - (fly != 0 ? 1 : 0), 1);
}

// ref: FUN_006ef2a0
void CMovementData_C::QueueStartAscend(int32_t time, int32_t up) {
    QueueSimple(this, time, 7 - (up != 0 ? 1 : 0), 1);
}

// ref: FUN_006ef310
void CMovementData_C::QueueStopAscend(int32_t time) {
    QueueSimple(this, time, 8, 1);
}

// ref: FUN_006ec840
void CMovementData_C::QueueEvent(int32_t time, int32_t type, uint8_t send, uint32_t counter, float facing,
                                 float pitch, uint16_t moveFlags2) {
    auto event = MoveEventAllocate(time, type);

    if (!event) {
        return;
    }

    event->facing = facing;
    event->pitch = pitch;
    event->send = send;
    event->counter = counter;
    event->moveFlags2 = moveFlags2;

    MoveEventQueueInsert(&this->m_events, event);

    if (!this->m_moverLink.IsLinked() && MovementGetGlobals()) {
        MovementLinkMover(this);
    }
}

// ref: FUN_006eaa50
// The seat record is the vehicle port's; frozen has no vehicle seats yet, so no seat limits the
// facing.
int32_t CMovementData_C::GetSeatFacingLimits(float* lo, float* hi) const {
    (void)lo;
    (void)hi;

    return 0;
}

// ref: FUN_006ef3d0
void CMovementData_C::QueueTurnTo(int32_t time, float facing) {
    float lo;
    float hi;

    if (this->GetSeatFacingLimits(&lo, &hi)) {
        MovementWrapAngle(&facing, lo, hi);

        if (facing < lo) {
            facing = lo;
        } else if (hi <= facing) {
            facing = hi;
        }
    }

    auto event = QueueSimple(this, time, 0x35, 1);

    if (event) {
        event->facing = facing;
    }
}

// ref: FUN_006ee3a0
// The facing the mouse turned the player to (event 0x13, MSG_MOVE_SET_FACING), inside the seat's
// facing range when it rides one.
void CMovementData_C::QueueSetFacing(int32_t time, float facing) {
    float lo;
    float hi;

    if (this->GetSeatFacingLimits(&lo, &hi)) {
        MovementWrapAngle(&facing, lo, hi);

        if (facing < lo) {
            facing = lo;
        } else if (hi <= facing) {
            facing = hi;
        }
    }

    auto event = QueueSimple(this, time, 0x13, 1);

    if (event) {
        event->facing = facing;
    }
}

// ref: FUN_006ee460
// The pitch the mouse tilted the player to (event 0x14, MSG_MOVE_SET_PITCH). The reference clamps
// it to the seat's pitch range (seat flag 0x40) first; frozen has no seats.
void CMovementData_C::QueueSetPitch(int32_t time, float pitch) {
    auto event = QueueSimple(this, time, 0x14, 1);

    if (event) {
        event->pitch = pitch;
    }
}

// ref: FUN_006ef490
// The vehicle seat's pitch limits clamp `pitch` first in the reference; frozen has no seats.
void CMovementData_C::QueuePitchTo(int32_t time, float pitch) {
    auto event = QueueSimple(this, time, 0x36, 1);

    if (event) {
        event->pitch = pitch;
    }
}

// ref: FUN_006f0f70
void CMovementData_C::QueueStartTurn(int32_t time, int32_t left) {
    float lo;
    float hi;

    if (!this->GetSeatFacingLimits(&lo, &hi)) {
        this->QueueEvent(time, 0xc - (left != 0 ? 1 : 0), 1, 0, 0.0f, 0.0f, 0);
        MoveEventQueueRemoveType(&this->m_events, 0x32);

        if (auto input = InputControlGetActive()) {
            input->OnTurnStarted();
        }

        return;
    }

    this->QueueTurnTo(time, left ? hi : lo);
}

// ref: FUN_006f1310
void CMovementData_C::QueueStartPitch(int32_t time, int32_t up) {
    // The vehicle seat's pitch range (seat flag 0x40) would turn this into a QueuePitchTo; frozen
    // has no seats, so the plain event is the only path.
    QueueSimple(this, time, 0xf - (up != 0 ? 1 : 0), 1);
    MoveEventQueueRemoveType(&this->m_events, 0x33);

    if (auto input = InputControlGetActive()) {
        input->RefreshMousePitch();
    }
}

// ref: FUN_006ec170
int32_t CMovementData_C::RetimeEvent(int32_t type, int32_t time) {
    for (auto event = this->m_events.Head(); event; event = this->m_events.Next(event)) {
        if (event->type == type) {
            this->m_events.UnlinkNode(event);
            event->time = time;
            MoveEventQueueInsert(&this->m_events, event);

            return 1;
        }
    }

    return 0;
}

// ref: FUN_006eb4e0
void CMovementData_C::ClearEventsExceptTimeSync() {
    auto event = this->m_events.Head();

    while (event) {
        auto next = this->m_events.Next(event);

        if (event->type != 0x31) {
            this->m_events.UnlinkNode(event);
            MoveEventFree(event);
        }

        event = next;
    }
}

// ---- small helpers ----------------------------------------------------------------------------

// ref: FUN_006e9b20
// The local player tells the server it skipped time (a long frame), so the server's clock for it
// does not fall behind.
void CMovementData_C::SkipTime(uint32_t ms) {
    if (!this->IsActivePlayer()) {
        return;
    }

    auto globals = MovementGetGlobals();
    this->m_owner->SendTimeSkipped(ms);
    globals->m_nextHeartbeat += ms;
}

// ref: FUN_006e9b70
void CMovementData_C::ScheduleHeartbeat(int32_t time) {
    if (!this->IsActivePlayer()) {
        return;
    }

    MovementGetGlobals()->m_nextHeartbeat = time + 500;
}

// ref: FUN_006e9440
bool CMovementData_C::IsPositionValid() const {
    return MovementPointOnMap(this->m_position.x, this->m_position.y, this->m_position.z, 2.0f);
}

// ref: FUN_006e9470
// FUN_00632050 after the move is an empty notification in this build.
void CMovementData_C::SetPositionAndLand(const C3Vector& position, int32_t force) {
    C3Vector d = { position.x - this->m_position.x, position.y - this->m_position.y, position.z - this->m_position.z };

    if (!force && d.x * d.x + d.y * d.y + d.z * d.z < 9.0f) {
        return;
    }

    this->m_position = position;

    if (!(this->m_spline->flags & 0x200)) {
        this->ForceStopFall();
    } else {
        this->m_fallTime = this->m_spline->uint28;
        this->ResetAnchor(0);
    }
}

// ref: FUN_006e9f50
// A remote unit's prediction runs out: after half a second without word from the server it slows
// to a stop over the next second instead of running on forever.
uint32_t CMovementData_C::GetPredictedAnchorTime(uint32_t ms) {
    if (!this->IsActivePlayer() && (this->m_moveFlags & 0xc0100f)) {
        this->m_extrapolateTime += ms;
        uint32_t over = this->m_extrapolateTime;

        if (over > 500) {
            int32_t stopped = static_cast<int32_t>(over) - 1500;

            if (stopped < 0) {
                stopped = 0;
            }

            int32_t braking = static_cast<int32_t>(over) - 500;

            if (braking > 1000) {
                braking = 1000;
            }

            int32_t time = static_cast<int32_t>(this->m_anchorTime) - stopped;

            if (time < 0) {
                time = 0;
            }

            time -= (braking * braking) / 2000;

            return time < 0 ? 0 : static_cast<uint32_t>(time);
        }
    }

    return this->m_anchorTime;
}

// ref: FUN_006e9570
// The collision box from a model's width and height at `scale` (the model's own scale times the
// object's): half the scaled width is the radius, and the step height follows the object's share
// of the scale. A unit the player controls keeps its height unless `force`.
//
// PARTIAL: the world update at the unit's position afterwards (FUN_00632050) is not identified.
void CMovementData_C::SetCollisionBox(float width, float height, float scale, float modelScale, int32_t force) {
    this->m_stepHeightScale = std::fabs(modelScale) < 2.38419e-07f ? 1.0f : scale / modelScale;
    this->m_collisionRadius = width * scale * 0.5f;

    if (force || !this->m_owner->IsPlayerControlled()) {
        this->m_collisionHeight = scale * height;
    }
}

// ref: FUN_006e9600
void CMovementData_C::SetCollisionHeight(float height) {
    this->m_collisionHeight = height;
}

// ref: FUN_006e9920
void CMovementData_C::SetHoverState(int32_t hover, int32_t land) {
    if (land && !hover) {
        int32_t fell = this->FallIfUnsupported();
        uint32_t moveFlags = this->m_moveFlags;

        if ((moveFlags & 0x1000) && !(moveFlags & 0x2000)) {
            this->m_moveFlags = moveFlags | 0x2000;
            this->m_owner->UpdateFallAnimation();
        } else if (fell) {
            this->m_owner->UpdateFallAnimation();
        }
    }

    this->SetHover(hover);
}

// ref: FUN_006e9980
void CMovementData_C::StopAllForTeleport() {
    this->StopFall();
    this->m_moveFlags &= 0xfb303fff;

    auto globals = MovementGetGlobals();

    if (this->StopTurn()) {
        this->m_owner->SendMovement(globals->m_lastTime, MSG_MOVE_STOP_TURN, 0, 0, 0, 0, 0xff);
    }

    if (this->StopPitch()) {
        this->m_owner->SendMovement(globals->m_lastTime, MSG_MOVE_STOP_PITCH, 0, 0, 0, 0, 0xff);
    }

    if (this->m_moveFlags & 0xc) {
        this->StopStrafe();
        this->m_owner->SendMovement(globals->m_lastTime, MSG_MOVE_STOP_STRAFE, 0, 0, 0, 0, 0xff);
    }

    this->StopMove(0);
    this->m_moveFlags &= 0xffffcfff;
}

// ref: FUN_006e9f10
void CMovementData_C::FinishSplineAt(int32_t force) {
    this->SetPositionAndLand(this->m_spline->vector1F8, force);
    this->m_spline->uint28 = this->m_spline->uint2C;
    this->m_spline->flags |= 0x100;
}

// ref: FUN_006ef6a0
int32_t CMovementData_C::QueueTurnStop(int32_t time, float facing) {
    float lo;
    float hi;
    float delta;

    if (!this->GetSeatFacingLimits(&lo, &hi)) {
        delta = WrapAngleDelta(facing - this->m_facing);
    } else {
        float current = this->m_facing;
        MovementWrapAngle(&current, lo, hi);
        delta = facing - current;
    }

    int32_t stopTime = static_cast<int32_t>(std::nearbyint((std::fabs(delta) / this->m_turnRate) * 1000.0f)) + time;

    if (!this->RetimeEvent(0x32, stopTime)) {
        QueueSimple(this, stopTime, 0x32, 1);
    }

    return 0.0f < delta ? 1 : 0;
}

// ref: FUN_006ef7a0
int32_t CMovementData_C::QueuePitchStop(int32_t time, float pitch) {
    float delta = WrapAngleDelta(pitch - this->m_pitch);
    int32_t stopTime = static_cast<int32_t>(std::nearbyint((std::fabs(delta) / this->m_pitchRate) * 1000.0f)) + time;

    if (!this->RetimeEvent(0x33, stopTime)) {
        QueueSimple(this, stopTime, 0x33, 1);
    }

    return 0.0f < delta ? 1 : 0;
}

// ref: FUN_006e9380
int32_t CMovementData_C::SetPitchEvent(int32_t time, const CPlayerMoveEvent* event) {
    if (!this->SetPitchAngle(event->pitch)) {
        return this->m_owner->SendMovement(time, MSG_MOVE_SET_PITCH, event->send, 0, 0, 0, 0xff);
    }

    if (event->send) {
        // A water-walker looking steeply down drops into the water when there is a floor under it
        // (CWorld::GetObjectFloor).
        float floor;

        if (this->m_owner->GetFloorHeight(&floor) && this->FallIfUnsupported()) {
            this->m_owner->SendMovementStatus(time, MSG_MOVE_HEARTBEAT, 0, 0, 0, 0xff);
            return 1;
        }
    }

    return 0;
}

// ---- the movers list's idle exit -----------------------------------------------------------

// ref: FUN_006eaf50
void CMovementData_C::LeaveMoversIfIdle(int32_t endSpline) {
    if (!this->m_moverLink.IsLinked()) {
        return;
    }

    if (this->m_moveFlags & 0xc010ff) {
        return;
    }

    if (this->m_events.Head()) {
        return;
    }

    if (this->m_moveFlags & 0x40000000) {
        // A hovering unit stays while the ground under it is not where the hover holds it.
        float height;

        if (!this->QueryHoverHeight(&height, nullptr, nullptr)) {
            return;
        }

        if (MOVE_TURN_EPSILON <= std::fabs(height - this->m_hoverHeight)) {
            return;
        }
    }

    auto spline = this->m_spline;

    if (spline && !(spline->flags & 0x400)) {
        if (endSpline) {
            this->EndSpline(MovementGetGlobals()->m_stepTime);

            if (this->m_moveFlags & 0xc010ff) {
                return;
            }

            this->m_moverLink.Unlink();
            return;
        }

        spline->flags |= 0x400;
    }

    this->m_moverLink.Unlink();
}

// ---- interpolation toward the server's position ---------------------------------------------

// ref: FUN_006ea6a0
void CMovementData_C::UpdateInterpolation(int32_t time) {
    this->m_moveFlags2 &= 0xe3ff;
    this->m_interpTime = 0;

    auto next = this->m_events.Head();

    if (!next || !next->hasStatus || next->type == 0x2c || this->m_transportGUID != next->transport) {
        return;
    }

    int32_t ahead = next->time - time;

    if (ahead == 0) {
        return;
    }

    if (this->m_moveFlags & 0xc0100f) {
        this->m_moveFlags2 |= 0x400;
    }

    if (MOVE_TURN_EPSILON <= std::fabs(this->m_facing - next->facing)) {
        this->m_moveFlags2 |= 0x800;
    }

    if ((this->m_moveFlags & 0x2200000) && MOVE_TURN_EPSILON <= std::fabs(this->m_pitch - next->pitch)) {
        this->m_moveFlags2 |= 0x1000;
    }

    this->m_interpPosition = {
        next->position.x - this->m_position.x,
        next->position.y - this->m_position.y,
        next->position.z - this->m_position.z
    };

    this->m_interpFacing = WrapAngleDelta(next->facing - this->m_facing);
    this->m_interpPitch = WrapAngleDelta(next->pitch - this->m_pitch);
    this->m_interpTime = ahead;
}

// ref: FUN_006ea7e0
int32_t CMovementData_C::Interpolate(int32_t time, int32_t ms, C3Vector* position, float* facing, float* pitch) {
    auto next = this->m_events.Head();
    int32_t ahead = next ? next->time - time : -1;

    if (!next || ahead < 0) {
        this->m_moveFlags2 &= 0xe3ff;
        return 0;
    }

    float t = static_cast<float>(ahead) / static_cast<float>(static_cast<uint32_t>(this->m_interpTime));
    float u = 1.0f - t;

    if (this->m_moveFlags2 & 0x400) {
        float x = position->x * t + (next->position.x - this->m_interpPosition.x * t) * u;
        float y = position->y * t + (next->position.y - this->m_interpPosition.y * t) * u;

        float dx = x - this->m_position.x;
        float dy = y - this->m_position.y;

        // 0x009ec218 caps the correction at a speed.
        float reach = static_cast<float>(static_cast<uint32_t>(ms)) * 0.0010000000474974513f * 50.0f;

        if (dy * dy + dx * dx + MOVE_TURN_EPSILON <= reach * reach) {
            position->x = x;
            position->y = y;
            position->z = position->z * t + (next->position.z - this->m_interpPosition.z * t) * u;
        } else {
            this->m_moveFlags2 &= 0xfbff;
        }
    }

    if (this->m_moveFlags2 & 0x800) {
        float target = NormalizeAngle(next->facing - t * this->m_interpFacing);
        *facing = NormalizeAngle(WrapAngleDelta(target - *facing) * u + *facing);
    }

    if (this->m_moveFlags2 & 0x1000) {
        float target = NormalizeAngle(next->pitch - t * this->m_interpPitch);
        *pitch = NormalizeAngle(WrapAngleDelta(target - *pitch) * u + *pitch);
    }

    return this->m_moveFlags2 & 0x400;
}

// ---- integration ----------------------------------------------------------------------------

// ref: FUN_006e9e20
uint32_t CMovementData_C::ApplyDisplacement(int32_t time, uint32_t ms, const C3Vector& delta) {
    C3Vector target = {
        delta.x + this->m_anchorPosition.x,
        this->m_anchorPosition.y + delta.y,
        this->m_anchorPosition.z + delta.z
    };

    // A remote unit on a spline is placed where the spline says; nothing collides it.
    if (!this->IsActivePlayer() && this->m_spline && !(this->m_owner->m_unreached)) {
        if (!(this->m_spline->flags & 0x200)) {
            this->StopFall();
        }

        this->m_anchorTime += ms;
        this->m_position = target;

        return ms;
    }

    C3Vector move = {
        target.x - this->m_position.x,
        target.y - this->m_position.y,
        target.z - this->m_position.z
    };

    return this->Collide(time, ms, move);
}

// ref: FUN_006eac40
void CMovementData_C::Integrate(int32_t time, uint32_t ms) {
    if (!MovementPointOnMap(this->m_position.x, this->m_position.y, this->m_position.z, 2.0f) || ms == 0) {
        return;
    }

    C3Vector splinePosition = { 0.0f, 0.0f, 0.0f };
    auto spline = this->m_spline;

    if (spline && !(spline->flags & 0x400)) {
        splinePosition = spline->vector1F8;
    }

    uint32_t done = 0;
    int32_t interpolated = 0;

    while (done < ms) {
        uint32_t moveFlags = this->m_moveFlags;

        if (!(moveFlags & 0xc010ff) && (!(moveFlags & 0x40000000) || (moveFlags & 0x800))) {
            break;
        }

        uint32_t step = ms - done;
        C3Vector delta = { 0.0f, 0.0f, 0.0f };

        this->m_anchorTime += step;

        if (!this->IsSplineActive()) {
            uint32_t anchorTime = this->GetPredictedAnchorTime(step);

            if (!this->AdvanceFromAnchor(anchorTime, &delta) && !(this->m_moveFlags & 0x40000000)) {
                return;
            }

            if (this->m_moveFlags2 & 0x1c00) {
                C3Vector position = {
                    this->m_anchorPosition.x + delta.x,
                    this->m_anchorPosition.y + delta.y,
                    this->m_anchorPosition.z + delta.z
                };

                if (this->Interpolate(time - static_cast<int32_t>(step), static_cast<int32_t>(step), &position, &this->m_facing, &this->m_pitch)) {
                    interpolated = 1;

                    delta = {
                        position.x - this->m_anchorPosition.x,
                        position.y - this->m_anchorPosition.y,
                        position.z - this->m_anchorPosition.z
                    };
                }
            }
        } else {
            if (!this->StepSpline(time, step, &splinePosition)) {
                return;
            }

            delta = {
                splinePosition.x - this->m_anchorPosition.x,
                splinePosition.y - this->m_anchorPosition.y,
                splinePosition.z - this->m_anchorPosition.z
            };
        }

        done += this->ApplyDisplacement(time - static_cast<int32_t>(step), step, delta);

        if (interpolated) {
            this->ResetAnchor(0);
        }
    }

    if (this->IsPositionValid()) {
        if (this->m_spline && !(this->m_spline->flags & 0x400)) {
            this->SetPositionAndLand(splinePosition, 0);
        }

        // FUN_004f5260: a running spline flagged 0x2000 re-anchors after the step.
        if (this->m_spline && !(this->m_spline->flags & 0x400) && (this->m_spline->flags & 0x2000)) {
            this->ResetAnchor(0);
        }
    }
}

// ref: FUN_006f09f0
void CMovementData_C::Update(uint32_t now, uint32_t last) {
    uint32_t span = now - last;
    uint32_t done = 0;
    auto globals = MovementGetGlobals();

    if (span > 250) {
        // A long frame: integrate only the last quarter second and tell the server the rest was
        // skipped.
        last = (span - 250) + last;
        this->SkipTime(span - 250);
        span = 250;
    } else if (span == 0) {
        goto finished;
    }

    do {
        uint32_t step = span - done;
        auto next = this->m_events.Head();

        if (next) {
            int32_t untilNext = next->time - static_cast<int32_t>(last);

            if (untilNext < 0) {
                untilNext = 0;
            }

            if (static_cast<uint32_t>(untilNext) < step) {
                step = static_cast<uint32_t>(untilNext);
            }
        }

        bool local = this->IsActivePlayer();

        if (local && !this->IsSplineActive() && (this->m_moveFlags & 0xc0100f)) {
            uint32_t untilHeartbeat = globals->m_nextHeartbeat - last;

            if (untilHeartbeat < step) {
                step = untilHeartbeat;
            }
        }

        last += step;
        globals->m_stepTime = last;

        if (step) {
            if (this->m_moveFlags & 0x40c010ff) {
                if (!(this->m_moveFlags & 0x200)) {
                    this->Integrate(static_cast<int32_t>(last), step);

                    if (this->IsActivePlayer() && (this->m_moveFlags & 0xc0100f) && !this->IsSplineActive()
                        && static_cast<int32_t>(last - globals->m_nextHeartbeat) >= 0) {
                        this->m_owner->SendMovementStatus(last, MSG_MOVE_HEARTBEAT, 0, 0, 0, 0xff);
                    }
                } else {
                    globals->m_nextHeartbeat += step;
                }
            }

            done += step;
        }

        int32_t result = this->ProcessEvents(static_cast<int32_t>(last));

        if (result != 1) {
            if (result == 0) {
                this->LeaveMoversIfIdle(0);
            } else if (this->IsActivePlayer()) {
                globals->m_nextHeartbeat += span - done;
            }

            break;
        }
    } while (done < span);

finished:
    this->m_owner->OnMovementStep(now, 0, 0);

    // The reference prints a console warning when the position has gone NaN ("FUN_0040c947").
    this->m_owner->m_unreached = 0;
}

// ref: FUN_006ef860
int32_t CMovementData_C::ProcessEvents(int32_t time) {
    auto unit = this->m_owner;

    for (auto event = this->m_events.Head(); event; event = this->m_events.Head()) {
        if (time - event->time < 0) {
            break;
        }

        // A unit a vehicle drives waits for it, and a teleport onto a transport that has not
        // arrived yet waits for the transport (FUN_0071c600).
        if (event->type == 0x2c && event->transport
            && !ClntObjMgrObjectPtr(event->transport, TYPE_OBJECT, ".\\Unit_C.cpp", 0x5c7b)) {
            return 2;
        }

        this->m_events.UnlinkNode(event);

        uint32_t oldFlags = this->m_moveFlags;
        this->m_extrapolateTime = 0;

        bool skip = false;

        // On a transport, falling or rooted (0x200, 0x800, 0x100000), most of the local
        // movement events are dropped: the transport, the fall or the root decides.
        auto allowedWhileHeld = [](int32_t type, bool knockback) {
            switch (type) {
                case 0: case 1: case 2: case 3: case 4: case 5:
                case 9: case 10: case 0x2d: case 0x2e:
                    return true;

                default:
                    return (0x15 <= type && type <= 0x16) || (knockback && type == 0x22);
            }
        };

        if (oldFlags & 0x200) {
            skip = allowedWhileHeld(event->type, true);
        } else if (oldFlags & 0x800) {
            skip = allowedWhileHeld(event->type, false);
        } else if (oldFlags & 0x100000) {
            switch (event->type) {
                case 0: case 1: case 2: case 3: case 4: case 5:
                case 9: case 10: case 0x2d: case 0x2e:
                    skip = true;
                    break;

                default:
                    break;
            }
        }

        if (skip) {
            MoveEventFree(event);
            continue;
        }

        uint32_t oldInput = this->IsJumping();
        bool landed = false;

        if (event->hasStatus) {
            if (!(event->moveFlags & 0x8000000)) {
                if (this->m_spline && (this->m_spline->flags & 0x800)) {
                    unit->OnLanded(0, 1);
                }

                this->ClearSpline();
            }

            if ((this->m_moveFlags & 0x1000) && !(event->moveFlags & 0x1000)) {
                this->StopFall();
                landed = true;
            }
        }

        int32_t sent = 0;
        uint8_t send = event->send;

        switch (event->type) {
            case 0:
                this->SetMoveForward(1, 0);
                sent = unit->SendMovement(time, MSG_MOVE_START_FORWARD, send, 0, 0, 0, 0xff);
                break;

            case 1:
                this->SetMoveForward(0, 0);
                sent = unit->SendMovement(time, MSG_MOVE_START_BACKWARD, send, 0, 0, 0, 0xff);
                break;

            case 2:
                this->StopMoveEvent();
                sent = unit->SendMovement(time, MSG_MOVE_STOP, send, 0, 0, 0, 0xff);
                break;

            case 3:
                this->SetStrafe(1);
                sent = unit->SendMovement(time, MSG_MOVE_START_STRAFE_LEFT, send, 0, 0, 0, 0xff);
                break;

            case 4:
                this->SetStrafe(0);
                sent = unit->SendMovement(time, MSG_MOVE_START_STRAFE_RIGHT, send, 0, 0, 0, 0xff);
                break;

            case 5:
                this->StopStrafeEvent();
                sent = unit->SendMovement(time, MSG_MOVE_STOP_STRAFE, send, 0, 0, 0, 0xff);
                break;

            case 6:
                if (this->SetAscend(1)) {
                    sent = unit->SendMovement(time, MSG_MOVE_START_ASCEND, send, 0, 0, 0, 0xff);
                }
                break;

            case 7:
                if (this->SetAscend(0)) {
                    sent = unit->SendMovement(time, MSG_MOVE_START_DESCEND, send, 0, 0, 0, 0xff);
                }
                break;

            case 8:
                if (this->StopAscend()) {
                    sent = unit->SendMovement(time, MSG_MOVE_STOP_ASCEND, send, 0, 0, 0, 0xff);
                }
                break;

            case 9:
                if (this->FallIfUnsupported()) {
                    unit->UpdateFallAnimation();

                    if (send) {
                        unit->SendMovementStatus(time, MSG_MOVE_HEARTBEAT, 0, 0, 0, 0xff);
                        sent = 1;
                    } else {
                        goto noInterpolation;
                    }
                }
                break;

            case 10:
                if (this->Jump(1)) {
                    sent = unit->SendMovement(time, MSG_MOVE_JUMP, send, 0, 0, 0, 0xff);
                }
                break;

            case 0xb:
                this->SetTurn(1);
                sent = unit->SendMovement(time, MSG_MOVE_START_TURN_LEFT, send, 0, 0, 0, 0xff);
                break;

            case 0xc:
                this->SetTurn(0);
                sent = unit->SendMovement(time, MSG_MOVE_START_TURN_RIGHT, send, 0, 0, 0, 0xff);
                break;

            case 0xd:
            case 0x32:
                this->StopTurn();
                sent = unit->SendMovement(time, MSG_MOVE_STOP_TURN, send, 0, 0, 0, 0xff);

                if (auto input = InputControlGetActive()) {
                    input->OnTurnStarted();
                }
                break;

            case 0xe:
                this->SetPitch(1);
                sent = unit->SendMovement(time, MSG_MOVE_START_PITCH_UP, send, 0, 0, 0, 0xff);
                break;

            case 0xf:
                this->SetPitch(0);
                sent = unit->SendMovement(time, MSG_MOVE_START_PITCH_DOWN, send, 0, 0, 0, 0xff);
                break;

            case 0x10:
            case 0x33:
                this->StopPitch();
                sent = unit->SendMovement(time, MSG_MOVE_STOP_PITCH, send, 0, 0, 0, 0xff);

                if (auto input = InputControlGetActive()) {
                    input->RefreshMousePitch();
                }
                break;

            case 0x11:
                this->SetRun(1);
                unit->UpdateMovementEffects();
                sent = unit->SendMovement(time, MSG_MOVE_SET_RUN_MODE, send, 0, 0, 0, 0xff);
                break;

            case 0x12:
                this->SetRun(0);
                unit->UpdateMovementEffects();
                sent = unit->SendMovement(time, MSG_MOVE_SET_WALK_MODE, send, 0, 0, 0, 0xff);
                break;

            case 0x13:
                this->SetFacing(event->facing);
                sent = unit->SendMovement(time, MSG_MOVE_SET_FACING, send, 0, 0, 0, 0xff);
                break;

            case 0x14:
                sent = this->SetPitchEvent(time, event);
                break;

            case 0x15:
                this->StartSwim();
                this->ApplyDeferredMoves();
                sent = unit->SendMovement(time, MSG_MOVE_START_SWIM, send, 0, 0, 0, 0xff);
                break;

            case 0x16:
                this->StopFly();
                sent = unit->SendMovement(time, MSG_MOVE_STOP_SWIM, send, 0, 0, 0, 0xff);
                break;

            // The speed changes the server orders, each acknowledged with its counter.
            case 0x17:
                this->SetRunSpeed(event->value[0]);
                sent = unit->SendMovement(time, CMSG_FORCE_RUN_SPEED_CHANGE_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            case 0x18:
                this->SetRunBackSpeed(event->value[0]);
                sent = unit->SendMovement(time, CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            case 0x19:
                this->SetWalkSpeed(event->value[0]);
                sent = unit->SendMovement(time, CMSG_FORCE_WALK_SPEED_CHANGE_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            case 0x1a:
                this->SetSwimSpeed(event->value[0]);
                sent = unit->SendMovement(time, CMSG_FORCE_SWIM_SPEED_CHANGE_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            case 0x1b:
                this->SetSwimBackSpeed(event->value[0]);
                sent = unit->SendMovement(time, CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            case 0x1c:
                this->SetFlightSpeed(event->value[0]);
                sent = unit->SendMovement(time, CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            case 0x1d:
                this->SetFlightBackSpeed(event->value[0]);
                sent = unit->SendMovement(time, CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            case 0x1e:
                this->SetTurnRate(event->value[0]);
                sent = unit->SendMovement(time, CMSG_FORCE_TURN_RATE_CHANGE_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            case 0x1f:
                this->SetPitchRate(event->value[0]);
                sent = unit->SendMovement(time, CMSG_FORCE_PITCH_RATE_CHANGE_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            case 0x20:
                if (this->SetGravity(1)) {
                    this->OnGravityChanged();
                }

                sent = unit->SendMovement(time, CMSG_MOVE_GRAVITY_ENABLE_ACK, send, 0.0f, event->counter, 0, 0xff);
                break;

            case 0x21:
                if (this->SetGravity(0)) {
                    this->OnGravityChanged();
                }

                sent = unit->SendMovement(time, CMSG_MOVE_GRAVITY_DISABLE_ACK, send, 0.0f, event->counter, 0, 0xff);
                break;

            case 0x22: {
                C2Vector direction = { event->value[0], event->value[1] };
                this->Knockback(direction, event->value[2], event->value[3]);
                sent = unit->SendMovement(time, CMSG_MOVE_KNOCK_BACK_ACK, send, 0.0f, event->counter, 0, 0xff);
                break;
            }

            case 0x23:
            case 0x24:
                this->SetSafeFall(event->type == 0x23);
                sent = unit->SendMovement(time, CMSG_MOVE_FEATHER_FALL_ACK, send, event->type == 0x23 ? 1.0f : 0.0f, event->counter, 0, 0xff);
                this->RestartFall();
                unit->UpdateFallAnimation();
                break;

            case 0x25:
                this->SetHover(1);
                sent = unit->SendMovement(time, CMSG_MOVE_HOVER_ACK, send, 1.0f, event->counter, 0, 0xff);
                break;

            case 0x26:
                this->SetHoverState(0, !send || unit->IsActiveMover() ? 1 : 0);
                sent = unit->SendMovement(time, CMSG_MOVE_HOVER_ACK, send, 0.0f, event->counter, 0, 0xff);
                break;

            case 0x27:
                this->SetWaterWalking(1);
                sent = unit->SendMovement(time, CMSG_MOVE_WATER_WALK_ACK, send, 1.0f, event->counter, 0, 0xff);
                break;

            case 0x28:
                this->SetWaterWalking(0);

                if (!send || unit->IsActiveMover()) {
                    this->FallIfUnsupported();
                }

                sent = unit->SendMovement(time, CMSG_MOVE_WATER_WALK_ACK, send, 0.0f, event->counter, 0, 0xff);
                break;

            case 0x29: {
                if (send && this->IsSplineActive()) {
                    this->FinishSplineAt(0);
                }

                uint32_t jumping = this->IsJumping();
                uint32_t flags = this->m_moveFlags;
                uint16_t flags2 = this->m_moveFlags2;

                this->Root();
                sent = unit->SendMovement(time, CMSG_FORCE_MOVE_ROOT_ACK, send, 0.0f, event->counter, 0, 0xff);
                this->OnCollided(time, 0, flags, flags2, jumping, 0);
                break;
            }

            case 0x2a: {
                uint32_t jumping = this->IsJumping();
                uint16_t flags2 = this->m_moveFlags2;
                uint32_t flags = this->m_moveFlags;

                this->Unroot(!send || unit->IsActiveMover() ? 1 : 0);
                unit->ClearEffectFlag4000();
                sent = unit->SendMovement(time, CMSG_FORCE_MOVE_UNROOT_ACK, send, 0.0f, event->counter, 0, 0xff);
                this->OnCollided(time, 0, flags, flags2, jumping, 0);
                break;
            }

            case 0x2d: {
                uint16_t oldFlags2 = this->m_moveFlags2;
                uint32_t flags = this->m_moveFlags;

                if (this->TryStartFly()) {
                    sent = this->StartFlyEvent(time, CMSG_MOVE_SET_FLY, flags, oldFlags2, send, 0);
                    unit->UpdateMovementEffects();
                }

                break;
            }

            case 0x2e:
                this->StopFlyAndSwim();
                sent = unit->SendMovement(time, CMSG_MOVE_SET_FLY, send, 0, 0, 0, 0xff);
                break;

            case 0x2f:
                this->m_moveFlags |= 0x1000000;
                unit->SendMovement(time, CMSG_MOVE_SET_CAN_FLY_ACK, send, 1.0f, event->counter, 0, 0xff);
                break;

            case 0x30:
                if (this->m_moveFlags & 0x2000000) {
                    this->StopFlyAndSwim();
                }

                this->m_moveFlags &= 0xfeffffff;
                unit->SendMovement(time, CMSG_MOVE_SET_CAN_FLY_ACK, send, 0.0f, event->counter, 0, 0xff);
                break;

            case 0x31:
                // A time sync. A unit still flagged on a transport the server has moved it off
                // leaves it first.
                if (this->m_moveFlags & 0x200) {
                    this->m_moveFlags &= 0xfffffdff;

                    if (this->FallIfUnsupported()) {
                        unit->UpdateFallAnimation();
                    }

                    sent = unit->SendMovement(time, CMSG_FORCE_MOVE_UNROOT_ACK, 0, 0, 0, 0, 0xff);
                    this->ScheduleHeartbeat(time);
                }

                MovementSendTimeSyncResponse(event->time, event->counter);
                break;

            case 0x35: {
                int32_t left = this->QueueTurnStop(time, event->facing);
                this->SetTurn(left);
                sent = unit->SendMovement(time, left ? MSG_MOVE_START_TURN_LEFT : MSG_MOVE_START_TURN_RIGHT, send, 0, 0, 0, 0xff);
                break;
            }

            case 0x37:
                this->StopAllForTeleport();
                sent = unit->SendMovement(time, 0x49b, send, 0, 0, event->transport, event->seat);
                break;

            case 0x36: {
                int32_t up = this->QueuePitchStop(time, event->pitch);
                this->SetPitch(up);
                sent = unit->SendMovement(time, up ? MSG_MOVE_START_PITCH_UP : MSG_MOVE_START_PITCH_DOWN, send, 0, 0, 0, 0xff);
                break;
            }

            case 0x38:
                this->m_moveFlags2 |= 0x4000;
                unit->SendMovement(time, CMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY_ACK, send, 1.0f, event->counter, 0, 0xff);
                break;

            case 0x39:
                this->m_moveFlags2 &= 0xbfff;
                unit->SendMovement(time, CMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY_ACK, send, 0.0f, event->counter, 0, 0xff);
                break;

            case 0x3a:
                this->SetCollisionHeight(event->value[0]);
                unit->SendMovement(time, CMSG_MOVE_SET_COLLISION_HGT_ACK, send, event->value[0], event->counter, 0, 0xff);
                break;

            default:
                // 0x2c (teleport), 0x34 and 0x37 (the stop-for-teleport pair) arrive with the
                // SMSG_MOVE_* handlers the remote-movement port brings in.
                break;
        }

        if (send && !sent && (this->m_moveFlags & 0xc0100f) && !(oldFlags & 0xc0100f)) {
            this->ScheduleHeartbeat(time);
        }

        if (event->type != 0x2c) {
            unit->m_stateFlags |= 0x20000000;

            if (event->hasStatus && this->ApplyEventStatus(event) && landed) {
                unit->OnLanded(oldFlags, oldInput);
            }

            unit->m_stateFlags &= 0xdfffffff;
        }

    noInterpolation:
        this->UpdateInterpolation(time);

        MoveEventFree(event);
    }

    if (!this->m_events.Head() && !(this->m_moveFlags & 0xc010ff)) {
        return 0;
    }

    return 1;
}

// ref: FUN_006ea9b0
int32_t CMovementData_C::ApplyEventStatus(const CPlayerMoveEvent* event) {
    if (!this->SetTransport(event->transport, event->seat)) {
        return 0;
    }

    this->m_moveFlags ^= (event->moveFlags ^ this->m_moveFlags) & 0x77fffdff;
    this->m_position = event->position;
    this->m_facing = event->facing;
    this->m_pitch = event->pitch;

    if (this->m_moveFlags & 0x1000) {
        this->m_fallTime = event->fallTime;
        this->m_fallStartElevation = this->GetFallHeight(static_cast<int32_t>(event->fallTime));
    }

    this->ResetAnchor(0);
    this->UpdateCurrentSpeed(0);

    return 1;
}

// ref: FUN_006eb3b0
uint32_t CMovementData_C::ApplyDeferredMoves() {
    uint32_t moveFlags = this->m_moveFlags;

    if (moveFlags & 0x4000) {
        this->StopMove(1);

        // The local player whose stop came in mid-air lets go of the movement keys on landing
        // (FUN_006eac00 looks for a stop already queued).
        if (this->IsActivePlayer() && !this->FindQueuedStop()) {
            if (auto input = InputControlGetActive()) {
                input->ClearFlagBits12And16();
            }
        }
    }

    if (this->m_moveFlags & 0x8000) {
        this->StopStrafe();
    }

    if (this->m_moveFlags & 0x30000) {
        this->SetMoveForward(this->m_moveFlags & 0x10000, 0);
    }

    if (this->m_moveFlags & 0xc0000) {
        this->SetStrafe(this->m_moveFlags & 0x40000);
    }

    this->m_moveFlags &= 0xffe03fff;

    return this->m_moveFlags;
}

// ref: FUN_006eac00
int32_t CMovementData_C::FindQueuedStop() const {
    auto& events = const_cast<CPlayerMoveEventList&>(this->m_events);

    for (auto event = events.Head(); event; event = events.Next(event)) {
        if (0 <= event->type && event->type <= 1) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_006eba60
void CMovementData_C::OnGravityChanged() {
    auto spline = this->m_spline;
    bool held = false;

    if (spline) {
        if (!(spline->flags & 0x400) && (spline->flags & 0x200)) {
            goto fall;
        }

        if (!(spline->flags & 0x400) && (spline->flags & 0x2000)) {
            held = true;
        }
    }

    if (held || (this->m_moveFlags2 & 0x4) || (this->m_moveFlags & 0x400)) {
        uint32_t oldFlags = this->m_moveFlags;
        int32_t wasJumping = (oldFlags & 0x1000) && this->m_jumpVelocity != 0.0f;

        this->StopFall();
        this->ApplyDeferredMoves();

        if (!(this->m_moveFlags & 0x1000) && (oldFlags & 0x1000)) {
            this->m_owner->OnLanded(oldFlags & 0xfffffbff, wasJumping);
        }

        this->LeaveMoversIfIdle(1);

        return;
    }

fall:
    if (this->FallIfUnsupported() && !this->m_moverLink.IsLinked() && MovementGetGlobals()) {
        MovementLinkMover(this);
    }
}

// ref: FUN_006ebc50
int32_t CMovementData_C::TryStartFly() {
    if (!this->StartFly()) {
        return 0;
    }

    this->ApplyDeferredMoves();

    return 1;
}

// ref: FUN_006ebb40
int32_t CMovementData_C::StartFlyEvent(int32_t time, int32_t opcode, uint32_t oldFlags, uint16_t oldFlags2,
                                        uint8_t send, uint32_t counter) {
    if (!this->IsOffGround()) {
        if (!this->FallIfUnsupported()) {
            return 0;
        }
    } else if (this->m_moveFlags & 0x1000) {
        float jumpVelocity = this->m_jumpVelocity;

        this->StopFall();
        int32_t sent = this->m_owner->SendMovement(time, opcode, send, static_cast<float>(counter), 0, 0, 0xff);
        this->ApplyDeferredMoves();
        this->OnCollided(time, 0, oldFlags, oldFlags2, jumpVelocity != 0.0f ? 1 : 0, 0);

        return sent;
    }

    return this->m_owner->SendMovement(time, opcode, send, static_cast<float>(counter), 0, 0, 0xff);
}

// ref: FUN_006eb0b0
void CMovementData_C::OnCollided(int32_t time, int32_t remaining, uint32_t oldFlags, uint16_t oldFlags2,
                                  uint32_t oldInput, int32_t transportChanged) {
    auto spline = this->m_spline;

    if (!remaining && spline && !(spline->flags & 0x400) && (this->m_moveFlags & 0x3) && (spline->flags & 0x100)) {
        // The spline ran out at its end: stop there, facing the way it says.
        this->StopMove(1);
        this->SetPositionAndLand(this->m_spline->vector1F8, 0);

        uint32_t splineFlags = this->m_spline->flags;

        if (splineFlags & 0x20000) {
            this->m_facing = this->m_spline->face.facing;
        } else if (splineFlags & 0x10000) {
            if (auto target = ClntObjMgrObjectPtr(this->m_spline->face.guid, TYPE_OBJECT, ".\\Movement.cpp", 0xfd9)) {
                C3Vector to = target->GetPosition();
                C3Vector from = this->m_owner->GetPosition();
                this->SetWorldFacing(std::atan2(to.y - from.y, to.x - from.x));
            }
        } else if (splineFlags & 0x8000) {
            C3Vector spot = this->m_spline->face.spot;
            this->m_facing = std::atan2(spot.y - this->m_position.y, spot.x - this->m_position.x);
        }

        uint32_t endedFlags = this->m_spline->flags;
        this->EndSpline(time);

        if (!(endedFlags & 0xa00)) {
            this->m_owner->SendMovement(time, MSG_MOVE_STOP, 0, 0, 0, 0, 0xff);
        } else {
            this->m_owner->OnLanded(oldFlags, endedFlags & 0x800);
        }
    } else if (spline && (spline->flags & 0x400)) {
        this->SetPositionAndLand(spline->vector1F8, 0);
        this->EndSpline(time);
    }

    if ((this->m_moveFlags & 0x1000) || !(oldFlags & 0x1000)
        || !this->m_owner->AcknowledgeLanding(time, oldFlags, oldFlags2, oldInput)) {
        if (transportChanged && this->IsActivePlayer() && !this->IsSplineActive()) {
            this->m_owner->SendMovementStatus(time, CMSG_MOVE_CHANGE_TRANSPORT, 0, 0, 0, 0xff);
        } else if (((this->m_moveFlags ^ oldFlags) & 0xf) && this->IsActivePlayer() && !this->IsSplineActive()) {
            this->m_owner->SendMovementStatus(time, this->SelectLandingOpcode(oldFlags, oldInput), 0, 0, 0, 0xff);
        }
    }

    if (((this->m_moveFlags & 0x1000) && !(oldFlags & 0x1000))
        || ((this->m_moveFlags & 0x2000) && !(oldFlags & 0x2000))) {
        this->m_owner->UpdateFallAnimation();
    }

    if (!(this->m_moveFlags & 0x200000) && (oldFlags & 0x200000) && this->IsActivePlayer() && !this->IsSplineActive()) {
        this->m_owner->SendMovement(time, MSG_MOVE_JUMP, 1, 0, 0, 0, 0xff);
    }
}

// ref: FUN_006e9870
// The opcode that reports how the movement changed when a collision step altered its flags.
int32_t CMovementData_C::SelectLandingOpcode(uint32_t oldFlags, uint32_t jumping) const {
    uint32_t moveFlags = this->m_moveFlags;
    uint32_t changed = moveFlags ^ oldFlags;

    if (changed & 0x3) {
        if (moveFlags & 0x1) {
            return MSG_MOVE_START_FORWARD;
        }

        return ((~moveFlags & 0x2) | 0x16c) >> 1;
    }

    if (changed & 0xc) {
        if (moveFlags & 0x4) {
            return MSG_MOVE_START_STRAFE_LEFT;
        }

        return MSG_MOVE_STOP_STRAFE - ((moveFlags & 0x8) != 0 ? 1 : 0);
    }

    if ((moveFlags & 0x1000) && this->m_jumpVelocity != 0.0f && !jumping) {
        return MSG_MOVE_JUMP;
    }

    if (changed & 0x200000) {
        return (~(moveFlags >> 21) & 1) | MSG_MOVE_START_SWIM;
    }

    return MSG_MOVE_STOP;
}

// ref: FUN_006e9ff0
void CMovementData_C::Knockback(const C2Vector& direction, float horizontalSpeed, float verticalSpeed) {
    if (this->m_moveFlags & 0x200000) {
        this->StopFly();
    } else if (this->m_moveFlags & 0x2000000) {
        this->StopFlyAndSwim();
    }

    if (this->m_moveFlags & 0x800) {
        this->m_moveFlags = (this->m_moveFlags & 0xfffff7ff) | 0x100000;
    }

    this->m_moveFlags &= 0xfeffffff;
    this->StartFall(verticalSpeed);

    this->m_direction2d = direction;
    this->m_direction = { direction.x, direction.y, 0.0f };

    if (this->m_transportGUID && this->IsActivePlayer()) {
        // The knockback's direction is in the world; a passenger moves in its transport's space.
        C44Matrix transport;
        MovementGetTransportMatrixChecked(this->m_transportGUID, transport, this->m_guid, ".\\Movement.cpp", 0x711);
        C44Matrix inverse = transport.AffineInverse();

        C3Vector local = {
            this->m_direction2d.x * inverse.a0 + inverse.b0 * this->m_direction2d.y,
            inverse.a1 * this->m_direction2d.x + inverse.b1 * this->m_direction2d.y,
            inverse.a2 * this->m_direction2d.x + inverse.b2 * this->m_direction2d.y
        };

        this->m_direction = local;
        this->m_direction2d = { local.x, local.y };

        float length = this->m_direction2d.x * this->m_direction2d.x + this->m_direction2d.y * this->m_direction2d.y;

        if (2.384185791015625e-07f < length) {
            float inv = 1.0f / std::sqrt(length);
            this->m_direction2d.x *= inv;
            this->m_direction2d.y = inv * this->m_direction2d.y;
        }
    }

    this->m_currentSpeed = horizontalSpeed;
    this->m_moveFlags = (this->m_moveFlags & 0xfffffffd) | 0x4001;
}

// ---- transports and the server's view -------------------------------------------------------

// A unit's guid as a transport: a vehicle's (high 0xf05.) or a player's, whose seats the vehicle
// code keeps rather than the transport's own space.
static bool GuidIsUnitTransport(WOWGUID guid) {
    uint32_t low = static_cast<uint32_t>(guid);
    uint32_t high = static_cast<uint32_t>(guid >> 32);

    if ((high & 0xf0f00000) == 0xf0500000) {
        return true;
    }

    return (high & 0xf0000000) == 0 && !(low == 0 && (high & 0xf07fffff) == 0);
}

// ref: FUN_006ea1d0
int32_t CMovementData_C::SetTransport(WOWGUID transport, uint8_t seat) {
    if (this->m_transportGUID == transport && this->m_transportSeat == seat) {
        return 1;
    }

    if (GuidIsUnitTransport(this->m_transportGUID) || GuidIsUnitTransport(transport)) {
        VehicleOnTransportChanged(this->m_owner, transport, seat, 0);
    }

    this->m_transportSeat = seat;

    if (this->m_transportGUID == transport) {
        return 1;
    }

    if (this->m_transportGUID) {
        if (this->m_moveFlags & 0x1000) {
            // The fall's direction leaves the transport's space.
            C44Matrix matrix;
            MovementGetTransportMatrixChecked(this->m_transportGUID, matrix, this->m_guid, ".\\Movement.cpp", 0x9bc);

            this->m_direction = {
                this->m_direction.x * matrix.a0 + this->m_direction.y * matrix.b0 + this->m_direction.z * matrix.c0,
                this->m_direction.x * matrix.a1 + this->m_direction.y * matrix.b1 + this->m_direction.z * matrix.c1,
                this->m_direction.x * matrix.a2 + this->m_direction.y * matrix.b2 + this->m_direction.z * matrix.c2
            };
        }

        float oldFacing = MovementGetTransportFacing(this->m_transportGUID);
        // FUN_0079f820
        this->m_transportLink.Unlink();

        if (GuidIsUnitTransport(this->m_transportGUID)) {
            auto vehicle = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_transportGUID, TYPE_UNIT, ".\\Movement.cpp", 0x9c3));

            if (vehicle && vehicle->m_vehicle) {
                vehicle->m_vehicle->UpdateFreeSeats();
            }
        }
        this->m_transportGUID = 0;
        this->ClearSplineEnabled();

        auto model = this->m_owner->GetObjectModel();

        if (model && model->m_loaded) {
            model->SetParticleRelative(nullptr);
        }

        this->m_owner->AddFacingOffset(oldFacing);
    }

    if (transport) {
        // One that has not entered the world, or whose spline (if any) has stopped, is held to
        // the server's position until the transport is in.
        int32_t mode = !this->m_owner->m_postInited && (!this->m_spline || (this->m_spline->flags & 0x400)) ? 1 : 0;

        if (!MovementNotifyTransport(this, transport, mode)) {
            return 0;
        }

        this->m_transportGUID = transport;
        float newFacing = MovementGetTransportFacing(transport);

        if (this->m_moveFlags & 0x1000) {
            C44Matrix matrix;
            MovementGetTransportMatrixChecked(transport, matrix, this->m_guid, ".\\Movement.cpp", 0x9d4);
            C44Matrix inverse = matrix.AffineInverse();

            this->m_direction = {
                this->m_direction.x * inverse.a0 + this->m_direction.y * inverse.b0 + this->m_direction.z * inverse.c0,
                this->m_direction.x * inverse.a1 + this->m_direction.y * inverse.b1 + this->m_direction.z * inverse.c1,
                this->m_direction.x * inverse.a2 + this->m_direction.y * inverse.b2 + this->m_direction.z * inverse.c2
            };
        }

        auto model = this->m_owner->GetObjectModel();

        if (model && model->m_loaded) {
            C44Matrix matrix;
            MovementGetTransportMatrixChecked(transport, matrix, this->m_guid, ".\\Movement.cpp", 0x9d4);
            model->m_particleRelativeMatrix = matrix;
            model->SetParticleRelative(&model->m_particleRelativeMatrix);
        }

        this->m_owner->AddFacingOffset(-newFacing);
    }

    if (this->m_moveFlags & 0x1000) {
        this->m_direction2d = { this->m_direction.x, this->m_direction.y };

        float length = std::sqrt(this->m_direction2d.x * this->m_direction2d.x + this->m_direction2d.y * this->m_direction2d.y);

        if (length > 0.0f) {
            this->m_direction2d.x /= length;
            this->m_direction2d.y /= length;
        }
    }

    return 1;
}

// ref: FUN_006ec3b0
void CMovementData_C::RotateQueuedEvents(float facing) {
    for (auto event = this->m_events.Head(); event; event = this->m_events.Next(event)) {
        if (event->type == 0x13 || event->hasStatus) {
            event->facing = NormalizeAngle(facing + event->facing);
        }
    }
}

// ref: FUN_006ec400
int32_t CMovementData_C::ForceSetTransport(WOWGUID transport, uint8_t seat, int32_t force) {
    if (transport == this->m_transportGUID && seat == this->m_transportSeat) {
        return 0;
    }

    if (!force && this->m_spline) {
        return 0;
    }

    if (transport && !MovementTransportIsValid(transport)) {
        return 0;
    }

    if (this->m_transportGUID && !ClntObjMgrObjectPtr(this->m_transportGUID, TYPE_OBJECT, ".\\Movement.cpp", 0x593)) {
        // "CMovementData_C::ForceSetTransportInt() was called with an invalid m_transportGUID"
        // (FUN_005eeb70, a debug-build message).
        this->m_transportGUID = 0;

        return 0;
    }

    if (GuidIsUnitTransport(this->m_transportGUID) || GuidIsUnitTransport(transport)) {
        VehicleOnTransportChanged(this->m_owner, transport, seat, 0);
    }

    this->m_transportSeat = seat;

    if (transport == this->m_transportGUID) {
        return 1;
    }

    auto owner = this->m_owner;
    C44Matrix matrix;

    if (this->m_transportGUID) {
        float facing = this->LeaveTransportSpace(matrix);
        auto model = owner->GetObjectModel();

        if (model && model->m_loaded) {
            model->SetParticleRelative(nullptr);
        }

        owner->AddFacingOffset(facing);
        owner->CarryClickToMove(matrix, facing);

        if (GuidIsUnitTransport(this->m_transportGUID)) {
            auto vehicle = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_transportGUID, TYPE_UNIT, ".\\Movement.cpp", 0x5af));

            if (vehicle && vehicle->m_vehicle) {
                vehicle->m_vehicle->UpdateFreeSeats();
            }
        } else {
            this->RotateQueuedEvents(facing);
        }
    }

    if (transport) {
        C44Matrix inverse;
        float facing = this->EnterTransportSpace(transport, inverse, &matrix);
        auto model = owner->GetObjectModel();

        if (model && model->m_loaded) {
            model->m_particleRelativeMatrix = matrix;
            model->SetParticleRelative(&model->m_particleRelativeMatrix);
        }

        owner->AddFacingOffset(facing);
        owner->CarryClickToMove(inverse, facing);

        if (!GuidIsUnitTransport(transport)) {
            this->RotateQueuedEvents(facing);
        }

        if (this->m_moveFlags & 0x200000) {
            this->StopFly();
        }

        // Changing from one transport to another while the active mover: the next status says
        // which path time it was taken at.
        if (this->m_transportGUID && owner->IsActiveMover()) {
            auto globals = MovementGetGlobals();
            globals->m_transportTimeLatched = globals->m_transportTime != globals->m_transportTime2;
        }
    }

    if (this->m_moveFlags & 0xc00000) {
        this->ResetAnchor(0);
    }

    this->m_transportGUID = transport;
    MovementCameraFollowTransport(this->m_guid, owner->GetCameraTransportGUID());

    return 1;
}

// ref: FUN_006ea520
void CMovementData_C::TakeTransport(const CMovementStatus& status) {
    WOWGUID transport = this->TakeTransportFromStatus(status);
    this->SetTransport(transport, status.byte16);
}

// ref: FUN_006ea550
void CMovementData_C::CatchUp(int32_t time, uint32_t ms) {
    if (!ms || !(this->m_moveFlags & 0xc0100f) || this->IsSplineActive()) {
        return;
    }

    C3Vector delta = { 0.0f, 0.0f, 0.0f };

    while (ms > 250) {
        if (this->m_moveFlags & 0xc0000f) {
            this->m_anchorTime += 250;
        }

        this->AdvanceFromAnchor(250, &delta);

        if (MovementPointOnMap(this->m_position.x + delta.x, this->m_position.y + delta.y, this->m_position.z + delta.z, 0.0f)) {
            this->ApplyDisplacement(time, 250, delta);
        }

        ms -= 250;
        time += 250;
    }

    if (this->m_moveFlags & 0xc0000f) {
        this->m_anchorTime += ms;
    }

    this->AdvanceFromAnchor(this->m_anchorTime, &delta);

    if (MovementPointOnMap(this->m_position.x + delta.x, this->m_position.y + delta.y, this->m_position.z + delta.z, 0.0f)) {
        this->ApplyDisplacement(time, ms, delta);
    }
}

// ref: FUN_006e97d0
// Record the latest clock skew and answer the largest of the last 32.
int32_t CMovementData_C::AddLatencySample(int32_t remoteDelta, int32_t localDelta) {
    int32_t sample = (this->m_latencyMax - remoteDelta) + localDelta;

    if (0xffffu < static_cast<uint32_t>(sample + 0x8000)) {
        sample = sample < -0x8000 ? -0x8000 : 0x7fff;
    }

    this->m_latency[this->m_latencyIndex] = static_cast<int16_t>(sample);
    this->m_latencyIndex = (this->m_latencyIndex + 1) & 0x1f;

    for (int32_t i = 0; i < 32; i++) {
        if (sample < this->m_latency[i]) {
            sample = this->m_latency[i];
        }
    }

    return sample;
}

// ref: FUN_006eb730
int32_t CMovementData_C::ApplyStatus(int32_t time, const CMovementStatus& status, int32_t* skew,
                                     int32_t fromCreate, int32_t force) {
    if (this->m_spline) {
        force = 1;
    }

    if (!(this->m_moveFlags & 0x80000000)) {
        this->m_remoteTimeBase = status.uint0;
        this->m_localTimeBase = time;
        this->m_moveFlags |= 0x80000000;
    }

    auto globals = MovementGetGlobals();

    if (time - static_cast<int32_t>(globals->m_lastTime) < 0) {
        time = globals->m_lastTime;
    }

    int32_t remoteDelta = static_cast<int32_t>(status.uint0 - this->m_remoteTimeBase);
    int32_t localDelta = time - static_cast<int32_t>(this->m_localTimeBase);

    if (remoteDelta < 1) {
        remoteDelta = 0;
    } else {
        this->m_remoteTimeBase = status.uint0;
    }

    *skew = remoteDelta - localDelta;
    int32_t latency = this->AddLatencySample(remoteDelta, localDelta);

    if (!force) {
        if (!(this->m_moveFlags & 0xc010ff) && !this->m_events.Head()) {
            *skew += latency - this->m_latencyMax;

            int32_t clamped = *skew;

            if (0x5dcu < static_cast<uint32_t>(clamped + 500)) {
                clamped = clamped + 500 < 0 ? -500 : 1000;
            }

            *skew = clamped;

            if ((clamped - static_cast<int32_t>(this->m_localTimeBase)) + time < 0) {
                *skew = static_cast<int32_t>(this->m_localTimeBase) - time;
            }

            this->m_latencyMax = latency;
        }

        int32_t clamped = *skew;

        if (0x5dcu < static_cast<uint32_t>(clamped + 500)) {
            clamped = clamped + 500 < 0 ? -500 : 1000;
        }

        *skew = clamped;
        this->m_localTimeBase = clamped + time;

        if (static_cast<int32_t>(globals->m_lastTime) - (clamped + time) < 0) {
            return 0;
        }
    } else {
        this->m_localTimeBase = *skew + time;
    }

    uint32_t oldFlags = this->m_moveFlags;
    int32_t wasJumping = (oldFlags & 0x1000) && this->m_jumpVelocity != 0.0f;

    if (!fromCreate || force) {
        this->m_moveFlags ^= (oldFlags ^ status.moveFlags) & 0x77fffdff;
    } else {
        this->m_moveFlags ^= (oldFlags ^ status.moveFlags) & 0x77e00dff;
    }

    this->m_moveFlags2 = status.uint14;
    this->SetFromStatus(status);

    if (this->m_owner->m_postInited) {
        this->TakeTransport(status);
    }

    if (!(this->m_moveFlags & 0x1000)) {
        this->UpdateDirection(0);

        if (oldFlags & 0x1000) {
            this->m_owner->OnLanded(oldFlags, wasJumping);
        }
    } else {
        this->SetFallFromStatus(status);
    }

    if (!force) {
        this->CatchUp(static_cast<int32_t>(this->m_localTimeBase), globals->m_lastTime - this->m_localTimeBase);
    }

    if (!(this->m_moveFlags & 0xc010ff)) {
        this->m_owner->OnMovementStep(time, 1, 0);
    }

    this->ClearEventsExceptTimeSync();
    this->m_extrapolateTime = 0;

    return 1;
}

// ref: FUN_006f1520
// PARTIAL: a create block that carries a spline (move flag 0x8000000) starts it here in the
// reference (FUN_009870f0, FUN_006ed7e0, FUN_006f1240); the spline port brings that branch.
void CMovementData_C::InitFromCreate(int32_t time, const CClientMoveUpdate& move, int32_t activeMover) {
    this->m_walkSpeed = move.float60;
    this->m_runSpeed = move.float64;
    this->m_runBackSpeed = move.float68;
    this->m_swimSpeed = move.float6C;
    this->m_swimBackSpeed = move.float70;
    this->m_flightSpeed = move.float74;
    this->m_flightBackSpeed = move.float78;
    this->m_turnRate = move.float7C;
    this->m_anchorTime = 0;
    this->m_pitchRate = move.float80;
    this->m_moveFlags = 0;

    if (!(move.status.moveFlags & 0x8000000)) {
        if (this->m_spline && (this->m_spline->flags & 0x800)) {
            this->m_owner->OnLanded(0, 1);
        }

        this->ClearSpline();
    } else {
        // A unit created part way along a spline carries it on (FUN_006f1240 copies it).
        this->PrepareSpline(this->m_position);
        this->FlushEvents(0, 1);
        this->LeaveMoversIfIdle(0);
        *this->m_spline = move.spline;
        this->m_spline->spline.m_splineMode = (this->m_spline->flags & 0x42000) ? 1 : 0;
    }

    int32_t skew = 0;
    this->ApplyStatus(time, move.status, &skew, activeMover, 1);

    if (!(this->m_moveFlags & 0xc010ff) && !this->m_events.Head()) {
        this->LeaveMoversIfIdle(1);
    } else if (!this->m_moverLink.IsLinked() && MovementGetGlobals()) {
        MovementLinkMover(this);
    }

    if ((this->m_moveFlags & 0xc0100f) && this->IsActivePlayer()) {
        MovementGetGlobals()->m_nextHeartbeat = time + 500;
    }

    if (!(this->m_moveFlags & 0xc010ff)) {
        if (!activeMover) {
            if (this->FallIfUnsupported() && !this->m_moverLink.IsLinked() && MovementGetGlobals()) {
                MovementLinkMover(this);
            }
        } else {
            this->QueueFallIfUnsupported(time);
        }
    }
}

// ref: FUN_006e9c30
// One step along the spline: the position it puts the unit at, the jump and the animation the
// spline begins, and a jump too far to walk taken at once (0).
int32_t CMovementData_C::StepSpline(int32_t time, uint32_t ms, C3Vector* position) {
    auto spline = this->m_spline;
    bool wasArcing = (this->m_moveFlags2 & 0x80) && spline && !(spline->flags & 0x400) && (spline->flags & 0x800);
    bool wasAnimating = (this->m_moveFlags2 & 0x100) && spline && !(spline->flags & 0x400) && (spline->flags & 0x200000);

    if (!this->EvaluateSpline(time, position)) {
        return 0;
    }

    spline = this->m_spline;

    // The arc or the spline's animation began this step.
    if (!wasArcing && (this->m_moveFlags2 & 0x80) && spline && !(spline->flags & 0x400) && (spline->flags & 0x800)) {
        this->m_owner->SendMovement(static_cast<uint32_t>(time), MSG_MOVE_JUMP, 1, 0.0f, 0, 0, 0xff);
    }

    if (!wasAnimating && (this->m_moveFlags2 & 0x100) && spline && !(spline->flags & 0x400) && (spline->flags & 0x200000)) {
        this->m_owner->SetSplineAnimationTier(static_cast<uint8_t>(spline->flags & 0xff));
    }

    if (this->m_moveFlags & 0xc0100f) {
        float dx = position->x - this->m_position.x;
        float dy = position->y - this->m_position.y;
        float dz = position->z - this->m_position.z;
        float flat = dx * dx + dy * dy;
        float seconds = static_cast<float>(ms) * 0.0010000000474974513f;
        float speed = flat / (seconds * seconds);

        // Faster than sixty yards a second, or three yards away: there, not walked there.
        if (3600.0f < speed || 9.0f < flat + dz * dz) {
            this->m_position = *position;

            if (this->m_moveFlags & 0x1000) {
                if (!this->IsSplineFlag200()) {
                    this->m_fallStartElevation = this->GetFallHeight(static_cast<int32_t>(this->m_fallTime));
                } else {
                    this->m_fallTime = this->m_spline->uint28;
                }
            }

            // FUN_00632050: the world update at the new position, not identified.
            return 0;
        }
    }

    return 1;
}

// ref: FUN_006eae70
void CMovementData_C::EndSpline(int32_t time) {
    this->m_spline->flags |= 0x400;

    if (this->m_spline->flags & 0x800) {
        this->m_owner->OnLanded(0, 1);
    }

    if (this->m_spline->flags & 0xa00) {
        this->StopFall();
    }

    this->SetMoveFlags2Bit80(0);

    if (this->IsActivePlayer()) {
        uint32_t id = this->m_spline->uint30;

        if (this->m_spline && (this->m_spline->flags & 0x800)) {
            this->m_owner->OnLanded(0, 1);
        }

        this->ClearSpline();

        if (this->FallIfUnsupported() && !this->m_moverLink.IsLinked() && MovementGetGlobals()) {
            MovementLinkMover(this);
        }

        this->m_owner->SendSplineDone(time, id);
    }
}

// ------------------------------------------------------------------------------------------------
// Remote movement: other units' statuses as they arrive, and the server's forced changes to the
// local player. The reference keeps these beside the local queue creators in Movement.cpp
// (0x006ec8b0 .. 0x006f1180): each applies the status at once when it is due, or queues it as an
// event to apply when its time comes.
// ------------------------------------------------------------------------------------------------

namespace {

// ref: FUN_006e9050
// The event carries the status: transport, flags, then the position and facing in the
// transport's space when there is one, the world's otherwise.
void MoveEventSetStatus(CPlayerMoveEvent* event, const CMovementStatus& status) {
    event->transport = status.transport;
    event->moveFlags = status.moveFlags;
    event->moveFlags2 = status.uint14;

    if (status.transport) {
        event->position = status.position18;
        event->facing = status.facing24;
        event->seat = status.byte16;
    } else {
        event->position = status.position28;
        event->facing = status.facing34;
    }

    event->pitch = status.float38;
    event->hasStatus = 1;
    event->fallTime = status.uint3C;
}

// The reference's DAT_00c9ecc8: a flush already running.
int32_t s_flushingEvents;

} // namespace

// ref: FUN_006ec8b0
// A status that is not due yet waits as an event. The first such event of an idle queue starts
// the interpolation toward it.
void CMovementData_C::QueueStatusEvent(int32_t time, int32_t type, uint8_t send, uint32_t counter, float value,
                                       const CMovementStatus* status) {
    auto event = MoveEventAllocate(time, type);

    if (!event) {
        return;
    }

    event->value[0] = value;
    event->send = send;
    event->counter = counter;

    bool hadEvents = this->m_events.Head() != nullptr;

    if (status) {
        MoveEventSetStatus(event, *status);
    }

    MoveEventQueueInsert(&this->m_events, event);

    if (!this->m_moverLink.IsLinked() && MovementGetGlobals()) {
        MovementLinkMover(this);
    }

    if (status && !hadEvents && MovementGetGlobals()) {
        this->UpdateInterpolation(static_cast<int32_t>(MovementGetGlobals()->m_lastTime));
    }
}

// ref: FUN_006ec950
// The same for a knockback, whose event carries the direction and the two speeds.
void CMovementData_C::QueueKnockbackStatusEvent(int32_t time, int32_t type, uint8_t send, uint32_t counter,
                                                const C2Vector& direction, float horizontalSpeed,
                                                float verticalSpeed, const CMovementStatus* status) {
    auto event = MoveEventAllocate(time, type);

    if (!event) {
        return;
    }

    event->counter = counter;
    event->send = send;
    event->value[0] = direction.x;
    event->value[1] = direction.y;
    event->value[2] = horizontalSpeed;
    event->value[3] = verticalSpeed;

    bool hadEvents = this->m_events.Head() != nullptr;

    if (status) {
        MoveEventSetStatus(event, *status);
    }

    MoveEventQueueInsert(&this->m_events, event);

    if (!this->m_moverLink.IsLinked() && MovementGetGlobals()) {
        MovementLinkMover(this);
    }

    if (status && !hadEvents && MovementGetGlobals()) {
        this->UpdateInterpolation(static_cast<int32_t>(MovementGetGlobals()->m_lastTime));
    }
}

// The tail every remote apply shares: a status without the spline flag ends the spline the unit
// was on (landing it first when the spline was a fall), and the unit joins or leaves the movers.
int32_t CMovementData_C::FinishRemoteStatus(const CMovementStatus& status) {
    if (!(status.moveFlags & 0x8000000)) {
        if (this->m_spline && (this->m_spline->flags & 0x800)) {
            this->m_owner->OnLanded(0, 1);
        }

        this->ClearSpline();
    }

    if (!(this->m_moveFlags & 0xc010ff) && !this->m_events.Head()) {
        this->LeaveMoversIfIdle(1);
        return 1;
    }

    if (!this->m_moverLink.IsLinked() && MovementGetGlobals()) {
        MovementLinkMover(this);
    }

    return 1;
}

// ref: FUN_006ed990
// Apply another unit's status now, or queue it as event `type` (with `value`) when it is ahead of
// the clock. 1 when it was applied.
int32_t CMovementData_C::ApplyRemoteStatus(int32_t time, const CMovementStatus& status, int32_t type, float value) {
    int32_t skew = 0;

    if (!this->ApplyStatus(time, status, &skew, 0, 0)) {
        this->QueueStatusEvent(skew + time, type, 0, 0, value, &status);
        return 0;
    }

    return this->FinishRemoteStatus(status);
}

// ref: FUN_006eda60
// The same for a facing change (event 0x13).
int32_t CMovementData_C::ApplyRemoteFacingStatus(int32_t time, const CMovementStatus& status) {
    int32_t skew = 0;

    if (!this->ApplyStatus(time, status, &skew, 0, 0)) {
        this->QueueStatusEvent(skew + time, 0x13, 0, 0, 0.0f, &status);
        return 0;
    }

    return this->FinishRemoteStatus(status);
}

// ref: FUN_006ed8b0
// The same for a knockback (event 0x22 with its direction and speeds).
int32_t CMovementData_C::ApplyRemoteKnockbackStatus(int32_t time, const CMovementStatus& status, int32_t type,
                                                    const C2Vector& direction, float horizontalSpeed,
                                                    float verticalSpeed) {
    int32_t skew = 0;

    if (!this->ApplyStatus(time, status, &skew, 0, 0)) {
        this->QueueKnockbackStatusEvent(skew + time, type, 0, 0, direction, horizontalSpeed, verticalSpeed, &status);
        return 0;
    }

    return this->FinishRemoteStatus(status);
}

namespace {

// The local-queue tail of the remote applies: a mover that is not on the list goes on it.
void LinkIfIdle(CMovementData_C* move) {
    if (!move->m_moverLink.IsLinked() && MovementGetGlobals()) {
        MovementLinkMover(move);
    }
}

} // namespace

// ref: FUN_006f0cf0
int32_t CMovementData_C::RemoteStartMove(int32_t time, const CMovementStatus& status, int32_t forward) {
    if (this->ApplyRemoteStatus(time, status, forward == 0 ? 1 : 0, 0.0f) && this->SetMoveForward(forward, 0)) {
        LinkIfIdle(this);
        return 1;
    }

    return 0;
}

// ref: FUN_006f0d60
int32_t CMovementData_C::RemoteStartStrafe(int32_t time, const CMovementStatus& status, int32_t left) {
    if (this->ApplyRemoteStatus(time, status, 4 - (left != 0 ? 1 : 0), 0.0f) && this->SetStrafe(left)) {
        LinkIfIdle(this);
        return 1;
    }

    return 0;
}

// ref: FUN_006f0eb0
int32_t CMovementData_C::RemoteStopMove(int32_t time, const CMovementStatus& status) {
    uint32_t oldFlags = this->m_moveFlags;

    if (this->ApplyRemoteStatus(time, status, 2, 0.0f) && this->StopMoveIfMoving(static_cast<uint8_t>(oldFlags))
        && !(this->m_moveFlags & 0xc)) {
        this->LeaveMoversIfIdle(1);
        return 1;
    }

    return 0;
}

// ref: FUN_006f0f10
int32_t CMovementData_C::RemoteStopStrafe(int32_t time, const CMovementStatus& status) {
    uint32_t oldFlags = this->m_moveFlags;

    if (this->ApplyRemoteStatus(time, status, 5, 0.0f) && this->StopStrafeIfMoving(static_cast<uint8_t>(oldFlags))
        && !(this->m_moveFlags & 0xc0000f)) {
        this->LeaveMoversIfIdle(1);
        return 1;
    }

    return 0;
}

// ref: FUN_006f0dd0
int32_t CMovementData_C::RemoteJump(int32_t time, const CMovementStatus& status) {
    if (this->ApplyRemoteStatus(time, status, 10, 0.0f) && this->Jump(1)) {
        LinkIfIdle(this);
        return 1;
    }

    return 0;
}

// ref: FUN_006f1010
int32_t CMovementData_C::RemoteStartTurn(int32_t time, const CMovementStatus& status, int32_t left) {
    if (this->ApplyRemoteStatus(time, status, 0xc - (left != 0 ? 1 : 0), 0.0f)) {
        this->SetTurn(left);
        LinkIfIdle(this);
        return 1;
    }

    return 0;
}

// ref: FUN_006f1080
int32_t CMovementData_C::RemoteStopTurn(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0xd, 0.0f)) {
        return 0;
    }

    int32_t stopped = this->StopTurn();
    this->LeaveMoversIfIdle(1);

    return stopped;
}

// ref: FUN_006eec30
int32_t CMovementData_C::RemoteStartPitch(int32_t time, const CMovementStatus& status, int32_t up) {
    if (this->ApplyRemoteStatus(time, status, 0xf - (up != 0 ? 1 : 0), 0.0f)) {
        this->SetPitch(up);
        LinkIfIdle(this);
        return 1;
    }

    return 0;
}

// ref: FUN_006eed10
int32_t CMovementData_C::RemoteStopPitch(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x10, 0.0f)) {
        return 0;
    }

    int32_t stopped = this->StopPitch();

    if (!(this->m_moveFlags & 0xc010ff) && !this->m_events.Head()) {
        this->LeaveMoversIfIdle(1);
    }

    return stopped;
}

// ref: FUN_006f10d0
int32_t CMovementData_C::RemoteSetRun(int32_t time, const CMovementStatus& status, int32_t run) {
    if (this->ApplyRemoteStatus(time, status, 0x12 - (run != 0 ? 1 : 0), 0.0f)) {
        this->SetRun(run);
        return 1;
    }

    return 0;
}

// ref: FUN_006ef680
// A heartbeat or a landing: only the status.
int32_t CMovementData_C::RemoteHeartbeat(int32_t time, const CMovementStatus& status) {
    return this->ApplyRemoteStatus(time, status, 0x2b, 0.0f);
}

// ref: FUN_006eeb80
int32_t CMovementData_C::RemoteStartSwim(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x15, 0.0f)) {
        return 0;
    }

    this->StartSwim();
    this->ApplyDeferredMoves();
    this->LeaveMoversIfIdle(1);

    return 1;
}

// ref: FUN_006eebd0
int32_t CMovementData_C::RemoteStopSwim(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x16, 0.0f)) {
        return 0;
    }

    this->StopFly();
    LinkIfIdle(this);

    return 1;
}

// ref: FUN_006ee550
int32_t CMovementData_C::RemoteSetFacing(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteFacingStatus(time, status)) {
        return 0;
    }

    this->LeaveMoversIfIdle(1);

    return 1;
}

// ref: FUN_006ee590
int32_t CMovementData_C::RemoteSetPitch(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x14, status.float38)) {
        return 0;
    }

    this->LeaveMoversIfIdle(1);

    return 1;
}

// ref: FUN_006ee5d0
int32_t CMovementData_C::RemoteRoot(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x29, 0.0f)) {
        return 0;
    }

    this->Root();
    this->LeaveMoversIfIdle(1);

    return 1;
}

// ref: FUN_006ee620
int32_t CMovementData_C::RemoteUnroot(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x2a, 0.0f)) {
        return 0;
    }

    this->Unroot(1);

    if (this->m_moveFlags & 0xc010ff) {
        LinkIfIdle(this);
    }

    this->m_owner->ClearEffectFlag4000();

    return 1;
}

// ref: FUN_006ee700
// Gravity on (event 0x20) or off (0x21) as the status says.
int32_t CMovementData_C::RemoteSetGravity(int32_t time, const CMovementStatus& status) {
    int32_t type = static_cast<int32_t>(((status.moveFlags & 0x400) | 0x8000) >> 10);

    if (!this->ApplyRemoteStatus(time, status, type, 0.0f)) {
        return 0;
    }

    this->ResetAnchor(0);
    this->OnGravityChanged();

    return 1;
}

// ref: FUN_006eeec0
int32_t CMovementData_C::RemoteFeatherFall(int32_t time, const CMovementStatus& status) {
    return this->ApplyRemoteStatus(time, status, 0x24 - ((status.moveFlags & 0x20000000) != 0 ? 1 : 0), 0.0f);
}

// ref: FUN_006eede0
int32_t CMovementData_C::RemoteWaterWalk(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x28 - ((status.moveFlags & 0x10000000) != 0 ? 1 : 0), 0.0f)) {
        return 0;
    }

    if (!(this->m_moveFlags & 0x10000000) && this->FallIfUnsupported()) {
        LinkIfIdle(this);
    }

    return 1;
}

// ref: FUN_006eef60
int32_t CMovementData_C::RemoteHover(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x26 - ((status.moveFlags & 0x40000000) != 0 ? 1 : 0), 0.0f)) {
        return 0;
    }

    uint32_t hover = this->m_moveFlags & 0x40000000;

    if (!hover) {
        this->FallIfUnsupported();
    } else {
        this->Jump(0);
    }

    LinkIfIdle(this);
    this->SetHover(hover != 0 ? 1 : 0);

    return 1;
}

// ref: FUN_006ef0d0
int32_t CMovementData_C::RemoteCanFly(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x30 - ((status.moveFlags & 0x1000000) != 0 ? 1 : 0), 0.0f)) {
        return 0;
    }

    if (status.moveFlags & 0x1000000) {
        this->m_moveFlags |= 0x1000000;
        return 1;
    }

    if (this->m_moveFlags & 0x2000000) {
        this->StopFlyAndSwim();
    }

    this->m_moveFlags &= 0xfeffffff;

    return 1;
}

// ref: FUN_006ef1c0
// Whether the unit may pass between swimming and flying (move-flags-2 0x4000).
int32_t CMovementData_C::RemoteSwimFlyTransition(int32_t time, const CMovementStatus& status) {
    int32_t type = static_cast<int32_t>((~(status.uint14 >> 14) & 1) | 0x38);

    if (!this->ApplyRemoteStatus(time, status, type, 0.0f)) {
        return 0;
    }

    if (status.uint14 & 0x4000) {
        this->m_moveFlags2 |= 0x4000;
    } else {
        this->m_moveFlags2 &= 0xbfff;
    }

    return 1;
}

// ref: FUN_006ef5c0
int32_t CMovementData_C::RemoteStartAscend(int32_t time, const CMovementStatus& status, int32_t up) {
    if (this->ApplyRemoteStatus(time, status, 7 - (up != 0 ? 1 : 0), 0.0f) && this->SetAscend(up)) {
        LinkIfIdle(this);
        return 1;
    }

    return 0;
}

// ref: FUN_006ef630
int32_t CMovementData_C::RemoteStopAscend(int32_t time, const CMovementStatus& status) {
    if (this->ApplyRemoteStatus(time, status, 8, 0.0f) && !(this->m_moveFlags & 0xf)) {
        this->LeaveMoversIfIdle(1);
        return 1;
    }

    return 0;
}

// ref: FUN_006f0e30
int32_t CMovementData_C::RemoteKnockback(int32_t time, const CMovementStatus& status, const C2Vector& direction,
                                         float horizontalSpeed, float verticalSpeed) {
    if (!this->ApplyRemoteKnockbackStatus(time, status, 0x22, direction, horizontalSpeed, verticalSpeed)) {
        return 0;
    }

    this->Knockback(direction, horizontalSpeed, verticalSpeed);
    LinkIfIdle(this);

    return 1;
}

// ref: FUN_006f1120
// Another unit teleported: to the status's position, in its transport's space when it has one.
int32_t CMovementData_C::RemoteTeleport(int32_t time, const CMovementStatus& status) {
    if (!this->ApplyRemoteStatus(time, status, 0x2c, 0.0f)) {
        return 0;
    }

    if (status.transport) {
        this->TeleportTo(status.transport, status.position18, status.facing24, 1, 0, status.byte16);
    } else {
        this->TeleportTo(0, status.position28, status.facing34, 1, 0, 0xff);
    }

    if ((this->m_moveFlags & 0xc0100f) && MovementGetGlobals()) {
        this->ScheduleHeartbeat(static_cast<int32_t>(MovementGetGlobals()->m_stepTime));
    }

    return 1;
}

// ref: FUN_006ee810
// Another unit's collision height (MSG_MOVE_SET_COLLISION_HGT). PARTIAL: the world update the
// reference makes at the unit's position afterwards (FUN_00632050) is not identified.
int32_t CMovementData_C::RemoteSetCollisionHeight(int32_t time, const CMovementStatus& status, float height) {
    if (!this->ApplyRemoteStatus(time, status, 0x3a, height)) {
        return 0;
    }

    this->m_collisionHeight = height;

    return 1;
}

// ref: FUN_006edbe0, FUN_006edcd0, FUN_006eddc0, FUN_006edeb0, FUN_006edfa0, FUN_006ee090,
// FUN_006ee180, FUN_006ee270, FUN_006ee360
// Another unit's speed (event 0x17 .. 0x1f as the speed is, in that order: run, run back, walk,
// swim, swim back, flight, flight back, turn rate, pitch rate).
int32_t CMovementData_C::RemoteSetSpeed(int32_t time, const CMovementStatus& status, int32_t type, float speed) {
    if (!this->ApplyRemoteStatus(time, status, type, speed)) {
        return 0;
    }

    this->SetSpeedForEvent(type, speed);

    return 1;
}

// The speed setter an event type 0x17 .. 0x1f stands for.
int32_t CMovementData_C::SetSpeedForEvent(int32_t type, float speed) {
    switch (type) {
        case 0x17: return this->SetRunSpeed(speed);
        case 0x18: return this->SetRunBackSpeed(speed);
        case 0x19: return this->SetWalkSpeed(speed);
        case 0x1a: return this->SetSwimSpeed(speed);
        case 0x1b: return this->SetSwimBackSpeed(speed);
        case 0x1c: return this->SetFlightSpeed(speed);
        case 0x1d: return this->SetFlightBackSpeed(speed);
        case 0x1e: return this->SetTurnRate(speed);
        case 0x1f: return this->SetPitchRate(speed);
        default: return 0;
    }
}

// ref: FUN_006edb30, FUN_006edc20, FUN_006edd10, FUN_006ede00, FUN_006edef0, FUN_006edfe0,
// FUN_006ee0d0, FUN_006ee1c0, FUN_006ee2b0, FUN_006ee760
// The server changes a value of the local player's (a speed, the collision height): an event
// carrying the value and the counter the acknowledgement echoes.
void CMovementData_C::QueueForcedValue(int32_t time, int32_t type, uint32_t counter, float value) {
    auto event = MoveEventAllocate(time, type);

    if (!event) {
        return;
    }

    event->value[0] = value;
    event->counter = counter;
    event->send = 1;

    MoveEventQueueInsert(&this->m_events, event);
    LinkIfIdle(this);
}

// ref: FUN_006edb80, FUN_006edc70, FUN_006edd60, FUN_006ede50, FUN_006edf40, FUN_006ee030,
// FUN_006ee120, FUN_006ee210, FUN_006ee300, FUN_006ee7b0
// The server's broadcast of a value the local player already took: applied without an answer.
void CMovementData_C::QueueEchoedValue(int32_t time, int32_t type, float value) {
    auto event = MoveEventAllocate(time, type);

    if (!event) {
        return;
    }

    event->value[0] = value;
    event->send = 0;
    event->counter = 0;

    MoveEventQueueInsert(&this->m_events, event);
    LinkIfIdle(this);
}

// ref: FUN_006ee690, FUN_006eed70, FUN_006eee50, FUN_006eeef0, FUN_006eeff0, FUN_006ef060,
// FUN_006ef150
// The server switches a state of the local player's (gravity, water walk, feather fall, hover,
// root, can fly, swim-fly transition): an event to acknowledge with the counter.
void CMovementData_C::QueueForcedState(int32_t time, int32_t type, uint32_t counter) {
    this->QueueEvent(time, type, 1, counter, 0.0f, 0.0f, 0);
}

// ref: FUN_006ecc80
// The server knocks the local player back.
void CMovementData_C::QueueForcedKnockback(int32_t time, uint32_t counter, const C2Vector& direction,
                                           float horizontalSpeed, float verticalSpeed) {
    auto event = MoveEventAllocate(time, 0x22);

    if (!event) {
        return;
    }

    event->counter = counter;
    event->send = 1;
    event->value[0] = direction.x;
    event->value[1] = direction.y;
    event->value[2] = horizontalSpeed;
    event->value[3] = verticalSpeed;

    MoveEventQueueInsert(&this->m_events, event);
    LinkIfIdle(this);
}

// ref: FUN_006ef5a0
// ref: FUN_006ecae0
void CMovementData_C::QueueSeatChange(int32_t time, WOWGUID vehicle, uint8_t seat) {
    auto event = MoveEventAllocate(time, 0x37);

    if (!event) {
        return;
    }

    event->facing = 0.0f;
    event->pitch = 0.0f;
    event->transport = vehicle;
    event->send = 1;
    event->seat = seat;
    event->counter = 0;
    event->moveFlags2 = 0;

    MoveEventQueueInsert(&this->m_events, event);
    LinkIfIdle(this);
}

// ref: FUN_006eca00
// The server teleports the local player (event 0x2c with the destination status). PARTIAL: a
// destination seat (move-flags-2 0x2000) boards it through FUN_0074be10, the vehicle port's.
void CMovementData_C::QueueTeleport(int32_t time, uint8_t send, uint32_t counter, const CMovementStatus& status) {
    auto event = MoveEventAllocate(time, 0x2c);

    if (!event) {
        return;
    }

    event->send = send;
    event->counter = counter;
    event->pitch = status.float38;
    event->transport = status.transport;
    event->moveFlags = status.moveFlags;
    event->moveFlags2 = status.uint14;

    if (!status.transport) {
        event->position = status.position28;
        event->facing = status.facing34;
    } else {
        event->position = status.position18;
        event->facing = status.facing24;
        event->seat = status.byte16;
    }

    MoveEventQueueInsert(&this->m_events, event);
    LinkIfIdle(this);
}

// ref: FUN_006ecf80
// Put the unit at `position` (in `transport`'s space when it has one). PARTIAL: the vehicle seat
// moves (FUN_0074b380, FUN_0074b620) and the world update at the new position (FUN_00632050) are
// the vehicle and world ports'.
int32_t CMovementData_C::TeleportTo(WOWGUID transport, const C3Vector& position, float facing, int32_t clearSpline,
                                    int32_t fromServer, uint8_t seat) {
    if (this->m_moveFlags2 & 0x2000) {
        this->m_owner->m_stateFlags |= 0x20000000;
    } else {
        this->m_owner->m_stateFlags &= 0xdfffffff;
    }

    int32_t result = this->ForceSetTransport(transport, seat, 1);
    this->m_owner->m_stateFlags &= 0xdfffffff;

    if (this->m_transportGUID == transport) {
        this->Teleport(position, facing, clearSpline);
        this->LeaveMoversIfIdle(1);

        if ((!fromServer || this->m_owner->IsActiveMover()) && this->FallIfUnsupported()) {
            LinkIfIdle(this);
        }

        this->m_moveFlags2 &= 0xe3ff;
    }

    return result;
}

// ref: FUN_006ed0f0
// One queued server event, now: the value or state it carries, the acknowledgement, its status.
// The flush (FlushEvents) runs the queue through this; ProcessEvents has its own copy of these
// cases in the reference too.
void CMovementData_C::RunServerEvent(CPlayerMoveEvent* event, int32_t clearSpline) {
    uint32_t time = MovementGetGlobals() ? MovementGetGlobals()->m_stepTime : 0;
    auto unit = this->m_owner;

    this->m_events.UnlinkNode(event);

    int32_t opcode = 0;
    float value = 0.0f;
    uint32_t counter = event->counter;
    uint8_t send = event->send;
    bool answer = true;

    switch (event->type) {
        case 0x11:
            this->SetRun(1);
            answer = false;
            break;

        case 0x12:
            this->SetRun(0);
            answer = false;
            break;

        case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
            static const int32_t s_acks[9] = { 0xe3, 0xe5, 0x2db, 0xe7, 0x2dd, 0x382, 0x384, 0x2df, 0x45d };
            this->SetSpeedForEvent(event->type, event->value[0]);
            value = event->value[0];
            opcode = s_acks[event->type - 0x17];
            break;
        }

        case 0x20:
            this->SetGravity(1);
            opcode = 0x4d1;
            break;

        case 0x21:
            this->SetGravity(0);
            opcode = 0x4cf;
            break;

        case 0x22:
            this->Knockback({ event->value[0], event->value[1] }, event->value[2], event->value[3]);
            opcode = 0xf0;
            break;

        case 0x23:
        case 0x24:
            this->SetSafeFall(event->type == 0x23 ? 1 : 0);
            unit->SendMovement(time, 0x2cf, send, event->type == 0x23 ? 1.0f : 0.0f, counter, 0, 0xff);
            this->RestartFall();
            unit->UpdateFallAnimation();
            answer = false;
            break;

        case 0x25:
            this->SetHover(1);
            value = 1.0f;
            opcode = 0xf6;
            break;

        case 0x26:
            this->SetHoverState(0, (!send || unit->IsActiveMover()) ? 1 : 0);
            opcode = 0xf6;
            break;

        case 0x27:
            this->SetWaterWalking(1);
            value = 1.0f;
            opcode = 0x2d0;
            break;

        case 0x28:
            this->SetWaterWalking(0);

            if (!send || unit->IsActiveMover()) {
                this->FallIfUnsupported();
            }

            opcode = 0x2d0;
            break;

        case 0x29:
            if (send && this->IsSplineActive()) {
                this->FinishSplineAt(0);
            }

            this->Root();
            opcode = 0xe9;
            break;

        case 0x2a:
            this->Unroot((!send || unit->IsActiveMover()) ? 1 : 0);
            opcode = 0xeb;
            break;

        case 0x2c:
            this->m_moveFlags2 ^= (event->moveFlags2 ^ this->m_moveFlags2) & 0x2040;
            this->TeleportTo(event->transport, event->position, event->facing, clearSpline, send, event->seat);
            opcode = 199;
            break;

        case 0x2f:
            this->m_moveFlags |= 0x1000000;
            value = 1.0f;
            opcode = 0x345;
            break;

        case 0x30:
            if (this->m_moveFlags & 0x2000000) {
                this->StopFlyAndSwim();
            }

            this->m_moveFlags &= 0xfeffffff;
            opcode = 0x345;
            break;

        case 0x31:
            if (this->m_moveFlags & 0x200) {
                this->m_moveFlags &= 0xfffffdff;
            }

            MovementSendTimeSyncResponse(static_cast<uint32_t>(event->time), counter);
            answer = false;
            break;

        case 0x38:
            this->m_moveFlags2 |= 0x4000;
            value = 1.0f;
            opcode = 0x340;
            break;

        case 0x39:
            this->m_moveFlags2 &= 0xbfff;
            opcode = 0x340;
            break;

        case 0x3a:
            this->SetCollisionHeight(event->value[0]);
            value = event->value[0];
            opcode = 0x517;
            break;

        default:
            answer = false;
            break;
    }

    if (answer) {
        unit->SendMovement(time, opcode, send, value, counter, 0, 0xff);
    }

    if (event->hasStatus) {
        this->ApplyEventStatus(event);
    }

    MoveEventFree(event);
}

// ref: FUN_006ed7e0
// Before a spline takes the unit over: every queued event now, then everything the unit was doing
// stops; a riding unit's queued move and pending seat change go first.
void CMovementData_C::FlushEvents(int32_t clearSpline, int32_t stopAll) {
    if (s_flushingEvents) {
        return;
    }

    if (auto ride = this->m_owner->m_vehiclePassenger) {
        ride->FreeQueuedMove();
        ride->ReleaseSlot();
    }

    this->m_moveFlags2 &= 0xe3ff;
    s_flushingEvents = 1;

    while (auto event = this->m_events.Head()) {
        this->RunServerEvent(event, clearSpline);
    }

    if (stopAll || !this->m_spline || (this->m_spline->flags & 0x400)) {
        this->StopAllForTeleport();
    }

    s_flushingEvents = 0;
}

// ref: FUN_006eba20
void CMovementData_C::SplineUnroot() {
    this->Unroot(1);

    if (this->m_moveFlags & 0xc010ff) {
        LinkIfIdle(this);
    }

    this->m_owner->ClearEffectFlag4000();
}

// ref: FUN_006eb060
void CMovementData_C::SplineSetHover(int32_t hover) {
    if (!hover) {
        this->FallIfUnsupported();
    } else {
        this->Jump(0);
    }

    LinkIfIdle(this);
    this->SetHover(hover);
}

// ref: FUN_006eb9d0
// Rooted where the spline ends.
void CMovementData_C::SplineRoot() {
    this->Root();

    if (this->m_spline) {
        this->SetPositionAndLand(this->m_spline->vector1F8, 0);
        this->m_spline->uint28 = this->m_spline->uint2C;
        this->m_spline->flags |= 0x100;
    }

    this->LeaveMoversIfIdle(1);
}

// ref: FUN_006ee940
// The active player starts swimming: queued (event 0x15) unless a spline is driving it, when it
// happens at once and is sent.
void CMovementData_C::QueueStartSwim(int32_t time) {
    if (!this->m_spline || (this->m_spline->flags & 0x400)) {
        QueueSimple(this, time, 0x15, 1);
        return;
    }

    this->StartSwim();
    this->ApplyDeferredMoves();
    this->LeaveMoversIfIdle(1);
    this->m_owner->SendMovement(static_cast<uint32_t>(time), MSG_MOVE_START_SWIM, 1, 0.0f, 0, 0, 0xff);
}

// ref: FUN_006ee9f0
// Another unit starts swimming (event 0x15, not sent).
void CMovementData_C::QueueRemoteStartSwim(int32_t time) {
    QueueSimple(this, time, 0x15, 0);
}

// ref: FUN_006eea50
void CMovementData_C::QueueStopSwim(int32_t time) {
    if (!this->m_spline || (this->m_spline->flags & 0x400)) {
        QueueSimple(this, time, 0x16, 1);
        return;
    }

    this->StopFly();

    if ((this->m_moveFlags & 0xc010ff) || this->m_events.Head()) {
        LinkIfIdle(this);
    }

    this->m_owner->SendMovement(static_cast<uint32_t>(time), MSG_MOVE_STOP_SWIM, 1, 0.0f, 0, 0, 0xff);
}

// ref: FUN_006eeb20
void CMovementData_C::QueueRemoteStopSwim(int32_t time) {
    QueueSimple(this, time, 0x16, 0);
}

// ref: FUN_006ebf70
void CMovementData_C::SplineStartSwim() {
    this->StartSwim();
    this->ApplyDeferredMoves();
    this->LeaveMoversIfIdle(1);
}

// ref: FUN_006eb020
void CMovementData_C::SplineStopSwim() {
    this->StopFly();

    if ((this->m_moveFlags & 0xc010ff) || this->m_events.Head()) {
        LinkIfIdle(this);
    }
}

// ref: FUN_006ebe50
void CMovementData_C::SplineSetFlying(int32_t fly) {
    if (!fly) {
        this->StopFlyAndSwim();
    } else if (this->StartFly()) {
        this->ApplyDeferredMoves();
    }
}

// ref: FUN_006ebc20
int32_t CMovementData_C::SplineSetGravity(int32_t enable) {
    if (!this->SetGravity(enable)) {
        return 0;
    }

    this->OnGravityChanged();

    return 1;
}

// ------------------------------------------------------------------------------------------------
// Server splines: the monster move's start, stop and options (Movement.cpp)
// ------------------------------------------------------------------------------------------------

// ref: FUN_006eb680
// Put the unit on a spline through `points`. Not one that roots it in place (0x1800000), and not a
// rooted or transported unit unless the spline is a fall or a jump (0xa00).
int32_t CMovementData_C::StartSpline(const C3Vector* points, uint32_t count, uint32_t duration, uint32_t flags,
                                     uint32_t id) {
    if (flags & 0x1800000) {
        return 0;
    }

    this->LeaveMoversIfIdle(0);

    if ((this->m_moveFlags & 0x100800) && !(flags & 0xa00)) {
        return 0;
    }

    this->PrepareSpline(points[count - 1]);

    if (auto globals = MovementGetGlobals()) {
        this->InitSpline(static_cast<int32_t>(globals->m_lastTime), points, count, duration, flags, id);
    }

    if (!this->m_moverLink.IsLinked() && MovementGetGlobals()) {
        MovementLinkMover(this);
    }

    this->m_spline->spline.Evaluate(1.0f, this->m_spline->vector1F8, 1);

    return 1;
}

// ref: FUN_006f11b0
// The unit is put where a spline would have taken it, at once: the queue flushed first when the
// message says so.
void CMovementData_C::StopSplineAt(uint32_t id, const C3Vector& destination, uint32_t flags, int32_t flush) {
    this->PrepareSpline(destination);

    if (flush) {
        this->FlushEvents(0, 1);
        this->LeaveMoversIfIdle(0);
    }

    this->SetSplineStopped(id, flags);
    this->SetPositionAndLand(this->m_spline->vector1F8, flags & 0x1800000);
    this->m_spline->uint28 = this->m_spline->uint2C;
    this->m_spline->flags |= 0x100;

    if (auto globals = MovementGetGlobals()) {
        this->EndSpline(static_cast<int32_t>(globals->m_stepTime));
    }
}

// ref: FUN_006e9780
// The spline carries an animation tier (the low byte of its flags) from `time` ms in; one that
// starts at once applies it now.
void CMovementData_C::SetSplineAnimation(uint8_t tier, uint32_t time) {
    this->m_spline->flags = (this->m_spline->flags & 0xffffff00) | tier;
    this->m_spline->uint210 = time;

    if (time == 0) {
        this->SetMoveFlags2Bit100(1);
        this->m_owner->SetSplineAnimationTier(tier);
    }
}

// ref: FUN_006e96c0
// A parabolic spline: its vertical acceleration and when the arc starts. One that starts at once
// jumps now (a knockback, 0x4000, plays the knockback instead).
void CMovementData_C::SetSplineParabolic(float acceleration, uint32_t time) {
    this->m_spline->float20C = acceleration;
    this->m_spline->uint210 = time;

    if (this->m_spline->uint210 != 0) {
        this->SetMoveFlags2Bit80(0);
        return;
    }

    this->SetMoveFlags2Bit80(1);

    if (auto globals = MovementGetGlobals()) {
        int32_t opcode = (this->m_spline->flags & 0x4000) ? 0xf0 : MSG_MOVE_JUMP;
        this->m_owner->SendMovement(globals->m_lastTime, opcode, 0, 0.0f, 0, 0, 0xff);
    }
}

// ref: FUN_006ee510
// Face a world facing; a unit standing still has its queue flushed and leaves the movers.
void CMovementData_C::FaceForSpline(float facing, int32_t flush) {
    this->SetWorldFacing(facing);

    if (!(this->m_moveFlags & 0xc010ff) && flush) {
        this->FlushEvents(1, 1);
        this->LeaveMoversIfIdle(1);
    }
}

// ref: FUN_006f0c70
// The spline's transport, boarded keeping the unit's place; the active player tells the server
// it has (CMSG 0x38d).
void CMovementData_C::SetSplineTransport(WOWGUID transport, uint8_t seat, int32_t force) {
    if (!this->ForceSetTransport(transport, seat, force)) {
        return;
    }

    if (this->m_owner->GetGUID() == ClntObjMgrGetActivePlayer()) {
        auto globals = MovementGetGlobals();
        this->m_owner->SendMovementStatus(globals->m_lastTime, 0x38d, 0.0f, 0, 0, 0xff);
    }
}
