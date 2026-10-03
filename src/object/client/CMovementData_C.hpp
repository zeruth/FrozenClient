#ifndef OBJECT_CLIENT_C_MOVEMENT_DATA_C_HPP
#define OBJECT_CLIENT_C_MOVEMENT_DATA_C_HPP

#include "object/movement/CMovementShared.hpp"
#include <storm/List.hpp>

class CGUnit_C;
struct CClientMoveUpdate;
struct CMovementStatus;

// A queued movement event, 0x58 bytes in the reference. Events from input carry only a type and a
// time; events from the server also carry the status they were sent with (+0x50 set).
//
// THE REFERENCE'S RECORD IS 0x58 BYTES AND FROZEN'S IS NOT, and it cannot be: TSLink holds two
// pointers, which are eight bytes each here and four there, so every offset past the link is
// shifted and no amount of padding recovers the original layout. The reference offsets below are
// therefore documentation of where a field came FROM, not a claim about where it sits now.
struct CPlayerMoveEvent {
    TSLink<CPlayerMoveEvent> link;  // ref +0x0
    int32_t time;                   // ref +0x8, the queue's sort key
    int32_t type;                   // ref +0xc
    C3Vector position;              // ref +0x10
    float facing;                   // ref +0x1c
    float pitch;                    // ref +0x20
    uint32_t uint24;                // ref +0x24
    WOWGUID transport;              // ref +0x28
    uint32_t moveFlags;             // ref +0x30
    uint16_t moveFlags2;            // ref +0x34
    uint32_t fallTime;              // ref +0x38
    // ref +0x3c .. +0x48: the event's value -- a speed (+0x3c alone), or a knockback's direction
    // (+0x3c, +0x40), horizontal and vertical speed (+0x44, +0x48).
    float value[4];
    uint32_t counter;               // ref +0x4c, the server's acknowledgement counter
    uint8_t hasStatus;              // ref +0x50
    uint8_t send;                   // ref +0x51, the event is the local player's and is sent
    uint8_t seat;                   // ref +0x52
};

typedef STORM_EXPLICIT_LIST(CPlayerMoveEvent, link) CPlayerMoveEventList;

// The client half of a unit's movement (Movement.cpp in the reference -- the class names itself
// CMovementData_C in its own assertion text). It owns the queue of movement events the input and
// the server feed it, integrates the path between them each frame, sends the local player's
// movement packets and runs the collision.
class CMovementData_C : public CMovementShared {
    public:
        // Public member functions
        // ref: FUN_006ebd30
        CMovementData_C(const C3Vector& position, float facing, const WOWGUID& guid, CGUnit_C* unit);

        bool IsActivePlayer() const;
        bool IsSplineActive() const;

        // ---- event creation (each queues one event and joins the movers list) -------------

        // ref: FUN_006ecb50
        void QueueStartMove(int32_t time, int32_t forward);
        // ref: FUN_006ecbb0
        void QueueStartStrafe(int32_t time, int32_t left);
        // ref: FUN_006ecc20
        void QueueJump(int32_t time);
        // ref: FUN_006eccf0
        void QueueFallIfUnsupported(int32_t time);
        // ref: FUN_006ecd50
        void QueueFallIfUnsupportedLocal(int32_t time);
        // ref: FUN_006ecde0
        void QueueStopMove(int32_t time);
        // ref: FUN_006ece40
        void QueueStopStrafe(int32_t time);
        // ref: FUN_006ecea0
        void QueueStopTurn(int32_t time);
        // ref: FUN_006ecf10
        void QueueSetRun(int32_t time, int32_t run);
        // ref: FUN_006ef370
        void QueueTimeSync(int32_t time, uint32_t counter);
        // ref: FUN_006eeca0
        void QueueStopPitch(int32_t time);
        // ref: FUN_006ef230
        void QueueSetFlying(int32_t time, int32_t fly);
        // ref: FUN_006ef2a0
        void QueueStartAscend(int32_t time, int32_t up);
        // ref: FUN_006ef310
        void QueueStopAscend(int32_t time);
        // ref: FUN_006ec840
        void QueueEvent(int32_t time, int32_t type, uint8_t send, uint32_t counter, float facing,
                        float pitch, uint16_t moveFlags2);
        // ref: FUN_006ef3d0
        void QueueTurnTo(int32_t time, float facing);
        void QueueSetFacing(int32_t time, float facing);
        void QueueSetPitch(int32_t time, float pitch);
        // ref: FUN_006ef490
        void QueuePitchTo(int32_t time, float pitch);
        // ref: FUN_006f0f70
        void QueueStartTurn(int32_t time, int32_t left);
        // ref: FUN_006f1310
        void QueueStartPitch(int32_t time, int32_t up);
        // ref: FUN_006ec170
        // Re-time the first queued event of a type; false when there is none.
        int32_t RetimeEvent(int32_t type, int32_t time);
        // ref: FUN_006eb4e0
        // Drop every queued event but the time-sync ones.
        void ClearEventsExceptTimeSync();

        // ---- the per-frame update -------------------------------------------------------------

        // ref: FUN_006f09f0
        void Update(uint32_t now, uint32_t last);
        // ref: FUN_006ef860
        // Apply every event due by `time`: 0 when the queue drained and nothing moves, 1 when it
        // drained with movement left, 2 when the head event must wait.
        int32_t ProcessEvents(int32_t time);
        // ref: FUN_006eac40
        void Integrate(int32_t time, uint32_t ms);
        // ref: FUN_006e9e20
        // Move by `delta` over `ms`: a remote unit is placed, the local player's move collides.
        uint32_t ApplyDisplacement(int32_t time, uint32_t ms, const C3Vector& delta);
        // ref: FUN_006e9f50
        uint32_t GetPredictedAnchorTime(uint32_t ms);
        // ref: FUN_006eaf50
        // Leave the movers list when nothing moves and nothing is queued.
        void LeaveMoversIfIdle(int32_t endSpline);
        // ref: FUN_006e9b20
        void SkipTime(uint32_t ms);
        // ref: FUN_006e9b70
        // Push the next heartbeat half a second out.
        void ScheduleHeartbeat(int32_t time);
        // ref: FUN_006e9440
        bool IsPositionValid() const;
        // ref: FUN_006e9470
        void SetPositionAndLand(const C3Vector& position, int32_t force);
        // ref: FUN_006ea6a0
        void UpdateInterpolation(int32_t time);
        // ref: FUN_006ea9b0
        int32_t ApplyEventStatus(const CPlayerMoveEvent* event);
        // ref: FUN_006eb0b0
        void OnCollided(int32_t time, int32_t remaining, uint32_t oldFlags, uint16_t oldFlags2,
                        uint32_t oldInput, int32_t transportChanged);
        // ref: FUN_006eb3b0
        // Apply the moves deferred while falling, on landing.
        uint32_t ApplyDeferredMoves();
        // ref: FUN_006eba60
        void OnGravityChanged();
        // ref: FUN_006eac00
        // A start or stop of forward movement already queued.
        int32_t FindQueuedStop() const;
        // ref: FUN_006e9870
        int32_t SelectLandingOpcode(uint32_t oldFlags, uint32_t jumping) const;
        // ref: FUN_006e9ff0
        void Knockback(const C2Vector& direction, float horizontalSpeed, float verticalSpeed);
        // ref: FUN_006ea7e0
        int32_t Interpolate(int32_t time, int32_t ms, C3Vector* position, float* facing, float* pitch);
        // ref: FUN_006e9c30
        int32_t StepSpline(int32_t time, uint32_t ms, C3Vector* position);
        // ref: FUN_006eae70
        void EndSpline(int32_t time);
        // ref: FUN_006ebb40
        int32_t StartFlyEvent(int32_t time, int32_t opcode, uint32_t oldFlags, uint16_t oldFlags2,
                              uint8_t send, uint32_t counter);
        // ref: FUN_006ebc50
        int32_t TryStartFly();
        // ref: FUN_006e9380
        int32_t SetPitchEvent(int32_t time, const CPlayerMoveEvent* event);
        // ref: FUN_006e9600
        void SetCollisionHeight(float height);
        // ref: FUN_006e9920
        void SetHoverState(int32_t hover, int32_t land);
        // ref: FUN_006e9980
        void StopAllForTeleport();
        // ref: FUN_006e9f10
        void FinishSplineAt(int32_t force);
        // ref: FUN_006ef6a0
        int32_t QueueTurnStop(int32_t time, float facing);
        // ref: FUN_006ef7a0
        int32_t QueuePitchStop(int32_t time, float pitch);
        // ref: FUN_006eaa50
        // The vehicle seat's facing limits, when the unit sits in a seat that has them.
        int32_t GetSeatFacingLimits(float* lo, float* hi) const;

        // ---- transports and the server's view ------------------------------------------------

        // ref: FUN_006ea1d0
        int32_t SetTransport(WOWGUID transport, uint8_t seat);
        // ref: FUN_006ea520
        void TakeTransport(const CMovementStatus& status);
        // ref: FUN_006ea550
        void CatchUp(int32_t time, uint32_t ms);
        // ref: FUN_006e97d0
        int32_t AddLatencySample(int32_t remoteDelta, int32_t localDelta);
        // ref: FUN_006eb730
        int32_t ApplyStatus(int32_t time, const CMovementStatus& status, int32_t* skew,
                            int32_t fromCreate, int32_t force);
        // ref: FUN_006f1520
        void InitFromCreate(int32_t time, const CClientMoveUpdate& move, int32_t activeMover);

        // ---- collision (Collide.cpp) ----------------------------------------------------------

        // ref: FUN_00762e00
        uint32_t Collide(int32_t time, uint32_t ms, const C3Vector& delta);
        // ref: FUN_0075f520
        // The hover height over what is under the unit; false when nothing is.
        int32_t SetFloorObject(WOWGUID floor);
        int32_t QueryHoverHeight(float* height, int32_t* noFloor, WOWGUID* floorObject);

        // Public member variables
        float m_collisionRadius;        // ref +0xc8, 1/3
        float m_collisionHeight;        // ref +0xcc, 2.0278
        float m_stepHeightScale;        // ref +0xd0, 1.0
        C3Vector m_interpPosition;      // ref +0xd4, the server's lead the interpolation closes
        float m_interpFacing;           // ref +0xe0
        float m_interpPitch;            // ref +0xe4
        int16_t m_latency[32];          // ref +0xe8, -50 then +50
        uint32_t m_latencyIndex = 0;    // ref +0x128
        int32_t m_latencyMax = 50;      // ref +0x12c
        int32_t m_interpTime = 0;       // ref +0x130
        uint32_t m_extrapolateTime = 0; // ref +0x134
        uint32_t m_uint138 = 0;         // ref +0x138
        CPlayerMoveEventList m_events;  // ref +0x13c
        CGUnit_C* m_owner;              // ref +0x144
};

#endif
