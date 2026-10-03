#ifndef OBJECT_CLIENT_C_MOVEMENT_C_HPP
#define OBJECT_CLIENT_C_MOVEMENT_C_HPP

#include "object/client/CMovementData_C.hpp"
#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <tempest/Quaternion.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

// ref: FUN_0074b430
// The matrix of the transport `transport` (its world matrix, slot 0xc4), or identity and 0 when
// it is not there.
int32_t MovementGetTransportMatrix(WOWGUID transport, C44Matrix& matrix);

// ref: FUN_0074b4c0
// MovementGetTransportMatrix, complaining when the transport is missing.
int32_t MovementGetTransportMatrixChecked(WOWGUID transport, C44Matrix& matrix, WOWGUID owner,
                                          const char* file, int32_t line);

// ref: FUN_0074b590
// The transport's facing (slot 0x34), 0 when it is not there.
float MovementGetTransportFacing(WOWGUID transport);

// The transport's rotation (slot 0x44), the identity when it is not there.
C4Quaternion MovementGetTransportRotation(WOWGUID transport);

class CPassenger;

// Tell the transport a passenger joins (mode 1) or leaves (mode 2) it, through its slot 0xf4;
// false when the transport is not there.
int32_t MovementNotifyTransport(CPassenger* passenger, WOWGUID transport, int32_t mode);

// The movement globals, one per object manager (the reference reaches them through the TLS object
// manager, +0xd4; 0x140 bytes, allocated in Movement.cpp line 0x399).
struct CMovementGlobals {
    char m_logName[0x104];          // ref +0x0, the movement log's file name
    void* m_logFile = nullptr;      // ref +0x104
    uint32_t m_uint108 = 0;
    uint32_t m_uint110 = 0;
    uint32_t m_uint114 = 0;
    // ref +0x118: the units with movement to integrate (link at CMovementShared +0x30).
    STORM_EXPLICIT_LIST(CMovementShared, m_moverLink) m_movers;
    uint32_t m_flags = 0;           // ref +0x124, bit 0: the clock below is valid
    uint32_t m_lastTime = 0;        // ref +0x128, the time the movers were last brought up to
    uint32_t m_stepTime = 0;        // ref +0x12c, the time of the step being integrated
    uint32_t m_transportTime = 0;   // ref +0x130
    int32_t m_transportTimeLatched = 0; // ref +0x134
    uint32_t m_transportTime2 = 0;  // ref +0x138
    uint32_t m_nextHeartbeat = 0;   // ref +0x13c, when the local player sends its next heartbeat
};

// ref: FUN_0074b330
// The current object manager's movement globals.
CMovementGlobals* MovementGetGlobals();

// ref: FUN_006e8f90
// The movers' clock, when it is valid.
int32_t MovementGetLastTime(uint32_t* time);

// ref: FUN_006ec2c0
// Make the movement globals for the current object manager and start their clock.
void MovementInitialize(const char* logName);

// ref: FUN_00401520
// The world's movement start: the log name from the registry, the globals, and the per-frame
// update on the poll event.
void MovementStartWorld();

// ref: FUN_006f1490
// The per-frame update, on the poll event: bring every mover up to now.
int32_t MovementPoll(const void* data, void* param);

// ref: FUN_006f13e0
void MovementUpdateMovers(uint32_t now, uint32_t last);

// ref: FUN_007b5020
// Put a movement on the movers list, at the tail.
void MovementLinkMover(CMovementShared* move);

// Return an event to the free list (the tail of FUN_006ef860's loop, and FUN_006eb4e0).
void MoveEventFree(CPlayerMoveEvent* event);

class CMovement_C : public CMovementData_C {
    public:
        // Public member functions
        CMovement_C(const C3Vector& position, float facing, const WOWGUID& guid, CGUnit_C* unit)
            : CMovementData_C(position, facing, guid, unit) {};
};


// ref: FUN_006ebc70
// Take an event off the free list, or make one, and stamp it with a time and a type.
CPlayerMoveEvent* MoveEventAllocate(int32_t time, int32_t type);

// ref: FUN_006e9290
// Brings an angle into [lo, hi] by whole turns. When it cannot land inside, it keeps whichever of
// the two neighbouring values sits nearer the range.
void MovementWrapAngle(float* angle, float lo, float hi);

// ref: FUN_006e9bb0
// CMSG_TIME_SYNC_RESPONSE: the request's counter, then the client's time.
void MovementSendTimeSyncResponse(uint32_t time, uint32_t counter);

// ref: FUN_006eb590
// Moves the first queued event of this type onto the free list.
void MoveEventQueueRemoveType(CPlayerMoveEventList* queue, int32_t type);

// ref: FUN_006ec090
// Inserts an event before the first one due strictly later, or at the tail.
void MoveEventQueueInsert(CPlayerMoveEventList* queue, CPlayerMoveEvent* event);

#endif
