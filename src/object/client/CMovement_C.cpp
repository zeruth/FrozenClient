#include "object/client/CGObject_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/CMovement_C.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include <common/DataStore.hpp>
#include <storm/Memory.hpp>

// Spent move events, reused by the allocator. The reference keeps this as a statically
// initialised empty list.
// ref: DAT_00ada370
static CPlayerMoveEventList s_moveEventFreeList;

// ref: FUN_006ebc70
// Take an event off the free list, or make one, and stamp it with a time and a type.
//
// THIS WAS PREVIOUSLY MARKED NOT PORTABLE because the decompilation drops the event it returns --
// Ghidra types the whole function void and the allocated block simply vanishes. It reads straight
// off the disassembly: eax carries the event down all three paths and is the return value, which
// is the case CLAUDE.md means about reading the instructions rather than the decompilation.
//
// The reference open-codes the unlink, doing the tagged-pointer arithmetic that Storm's own list
// already does. frozen calls the list instead. That is a DIVERGENCE in form only -- the same two
// pointers get the same two values -- and it avoids a second copy of logic that is easy to get
// subtly wrong and impossible to test in isolation.
//
// Two reference behaviours deliberately NOT reproduced, both on paths that should not arise:
//
//   * a free-list node whose `next` is null skips the unlink entirely (0x006ebd0e) and is handed
//     out while the list still points at it. A correctly linked node never has a null next, so
//     this is the reference coping with corruption by spreading it.
//
//   * when the allocation FAILS the reference zeroes eax and then writes the time, the type and
//     the byte through it -- a store to address 8. Returning null is what the callers can
//     actually cope with.
CPlayerMoveEvent* MoveEventAllocate(int32_t time, int32_t type) {
    CPlayerMoveEvent* event = s_moveEventFreeList.Head();

    if (event) {
        s_moveEventFreeList.UnlinkNode(event);
    } else {
        event = static_cast<CPlayerMoveEvent*>(
            SMemAlloc(sizeof(CPlayerMoveEvent), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));

        if (!event) {
            return nullptr;
        }

        // Already zero from the allocation flag; the reference stores them anyway and so does
        // this, because the flag is the thing most likely to be changed by a later reader.
        event->float10 = 0.0f;
        event->float14 = 0.0f;
        event->float18 = 0.0f;

        new (&event->link) TSLink<CPlayerMoveEvent>();
    }

    event->time = time;
    event->type = type;
    event->byte50 = 0;

    return event;
}

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

// ref: FUN_0074b430
int32_t MovementGetTransportMatrix(WOWGUID transport, C44Matrix& matrix) {
    auto object = ClntObjMgrObjectPtr(transport, TYPE_OBJECT, ".\\Movement_C.cpp", 0x53);

    if (object) {
        C44Matrix world;
        object->GetWorldMatrix(world);
        matrix = world;

        return 1;
    }

    matrix = C44Matrix();

    return 0;
}

// ref: FUN_0074b4c0
int32_t MovementGetTransportMatrixChecked(WOWGUID transport, C44Matrix& matrix, WOWGUID owner,
                                          const char* file, int32_t line) {
    if (MovementGetTransportMatrix(transport, matrix)) {
        return 1;
    }

    // "Failed to dereference transport! Provided GUID 0x%016I64X from %s(%d)" (FUN_005eeb70), a
    // debug-build message.
    (void)owner;
    (void)file;
    (void)line;

    return 0;
}

// ref: FUN_0074b590
float MovementGetTransportFacing(WOWGUID transport) {
    auto object = ClntObjMgrObjectPtr(transport, TYPE_OBJECT, ".\\Movement_C.cpp", 0x77);

    if (!object) {
        return 0.0f;
    }

    return object->GetFacing();
}
