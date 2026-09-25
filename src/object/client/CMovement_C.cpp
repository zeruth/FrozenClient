#include "object/client/CMovement_C.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include <common/DataStore.hpp>

// Spent move events, reused by the allocator (FUN_006ebc70, not ported: Ghidra drops the event it
// returns). The reference keeps this as a statically initialised empty list.
// ref: DAT_00ada370
static CPlayerMoveEventList s_moveEventFreeList;

// ref: FUN_006e9290
void MovementWrapAngle(float* angle, float lo, float hi) {
    // The 2pi at 0x009f193c
    const float turn = 6.28318548f;

    while (*angle < lo) {
        *angle = *angle + turn;
    }

    float value = *angle;

    if (value < hi) {
        return;
    }

    float previous;

    do {
        previous = value;
        value = previous - turn;
    } while (hi < value);

    *angle = value;

    if (value <= lo && previous - hi < lo - value) {
        *angle = previous;
    }
}

// ref: FUN_006e9bb0
void MovementSendTimeSyncResponse(uint32_t time, uint32_t counter) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_TIME_SYNC_RESPONSE));
    msg.Put(counter);
    msg.Put(time);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006eb590
void MoveEventQueueRemoveType(CPlayerMoveEventList* queue, int32_t type) {
    for (auto event = queue->Head(); event; event = queue->Next(event)) {
        if (event->type == type) {
            queue->UnlinkNode(event);
            s_moveEventFreeList.LinkToTail(event);

            return;
        }
    }
}

// ref: FUN_006ec090
// The comparison is a signed difference, so it stays right across the millisecond clock wrapping.
void MoveEventQueueInsert(CPlayerMoveEventList* queue, CPlayerMoveEvent* event) {
    for (auto next = queue->Head(); next; next = queue->Next(next)) {
        if (event->time - next->time < 0) {
            queue->LinkNode(event, STORM_LIST_LINK_BEFORE, next);

            return;
        }
    }

    queue->LinkToTail(event);
}
