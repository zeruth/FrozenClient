#ifndef OBJECT_CLIENT_C_MOVEMENT_C_HPP
#define OBJECT_CLIENT_C_MOVEMENT_C_HPP

#include "object/client/CMovementData_C.hpp"
#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CMovement_C : public CMovementData_C {
    public:
        // Public member functions
        CMovement_C(const C3Vector& position, float facing, const WOWGUID& guid, CGUnit_C* unit)
            : CMovementData_C(position, facing, guid, unit) {};
};

// A queued movement event, 0x58 bytes in the reference. Only the fields the ported code reads are
// named; the rest of the block is not identified yet.
struct CPlayerMoveEvent {
    TSLink<CPlayerMoveEvent> link;  // ref +0x0
    int32_t time;                   // ref +0x8, the queue's sort key
    int32_t type;                   // ref +0xc
    // TODO
};

typedef STORM_EXPLICIT_LIST(CPlayerMoveEvent, link) CPlayerMoveEventList;

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
