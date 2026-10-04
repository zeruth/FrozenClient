#include "object/client/CGObject_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CVehicle_C.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/client/CGGameObject_C.hpp"
#include "object/client/GameObjectTypes.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "event/Event.hpp"
#include <common/Time.hpp>
#include <storm/String.hpp>
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
        event->position = { 0.0f, 0.0f, 0.0f };

        new (&event->link) TSLink<CPlayerMoveEvent>();
    }

    event->time = time;
    event->type = type;
    event->hasStatus = 0;

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

// ref: FUN_0074b510
C4Quaternion MovementGetTransportRotation(WOWGUID transport) {
    auto object = ClntObjMgrObjectPtr(transport, TYPE_OBJECT, ".\\Movement_C.cpp", 0x6b);

    if (!object) {
        // "Failed to dereference transport! Provided GUID 0x%016I64X" in the reference.
        return C4Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return object->GetRotation();
}

// ref: FUN_0074b340
int32_t MovementNotifyTransport(CPassenger* passenger, WOWGUID transport, int32_t mode) {
    auto object = ClntObjMgrObjectPtr(transport, TYPE_OBJECT, ".\\Movement_C.cpp", 0x28);

    if (!object) {
        return 0;
    }

    return object->Virtual0F4(passenger, mode);
}

static STORM_EXPLICIT_LIST(CGGameObject_C, m_transportLink) s_movingTransports;

// ref: FUN_0074b730
void MovementLinkTransport(CGGameObject_C* transport) {
    s_movingTransports.LinkToTail(transport);
}

// ref: FUN_0074b750
void MovementUnlinkTransport(CGGameObject_C* transport) {
    transport->m_transportLink.Unlink();
}

// ref: FUN_0074b6e0
void MovementUpdateTransports(uint32_t time, uint32_t elapsed) {
    for (auto transport = s_movingTransports.Head(); transport; transport = s_movingTransports.Next(transport)) {
        transport->m_type->UpdateTransport(time, static_cast<int32_t>(elapsed));
    }
}

// ref: FUN_0074b3f0
bool MovementTransportIsValid(WOWGUID transport) {
    auto object = ClntObjMgrObjectPtr(transport, TYPE_OBJECT, ".\\Movement_C.cpp", 0x44);

    if (!object) {
        return false;
    }

    return object->Virtual0EC();
}

// ref: FUN_0074b5e0
int32_t MovementTransportContains(WOWGUID transport, const C3Vector& position) {
    auto object = ClntObjMgrObjectPtr(transport, TYPE_OBJECT, ".\\Movement_C.cpp", 0x81);

    if (!object) {
        return 0;
    }

    return object->Virtual0F0(&position);
}

// ref: FUN_0074b670
void MovementVehicleRecountSeats(WOWGUID transport) {
    uint32_t low = static_cast<uint32_t>(transport);
    uint32_t high = static_cast<uint32_t>(transport >> 32);
    bool unit = (high & 0xf0f00000) == 0xf0500000 || ((high & 0xf0000000) == 0 && !(low == 0 && (high & 0xf07fffff) == 0));

    if (!unit) {
        return;
    }

    auto vehicle = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(transport, TYPE_UNIT, ".\\Movement_C.cpp", 0x9f));

    if (vehicle && vehicle->m_vehicle) {
        vehicle->m_vehicle->UpdateFreeSeats();
    }
}

// ref: FUN_0074b380
void MovementCameraFollowTransport(WOWGUID guid, WOWGUID transport) {
    uint32_t low = static_cast<uint32_t>(transport);
    uint32_t high = static_cast<uint32_t>(transport >> 32);

    if ((high & 0xf0f00000) == 0xf0500000) {
        return;
    }

    if ((high & 0xf0000000) == 0 && !(low == 0 && (high & 0xf07fffff) == 0)) {
        return;
    }

    auto camera = CGWorldFrame::GetActiveCamera();

    if (camera && camera->GetTarget() == guid) {
        camera->SetRelativeTo(transport);
    }
}

// ref: FUN_006e8f70
void MovementSetTransportTime(uint32_t time) {
    auto globals = MovementGetGlobals();
    globals->m_transportTime2 = globals->m_transportTime;
    globals->m_transportTime = time;
}

// ref: FUN_0074b590
float MovementGetTransportFacing(WOWGUID transport) {
    auto object = ClntObjMgrObjectPtr(transport, TYPE_OBJECT, ".\\Movement_C.cpp", 0x77);

    if (!object) {
        return 0.0f;
    }

    return object->GetFacing();
}

// ref: FUN_0074b330
CMovementGlobals* MovementGetGlobals() {
    auto mgr = ClntObjMgrGetCurrent();

    return mgr ? mgr->m_movementGlobals : nullptr;
}

// ref: FUN_006e8f90
int32_t MovementGetLastTime(uint32_t* time) {
    auto globals = MovementGetGlobals();

    if (globals && (globals->m_flags & 0x1)) {
        *time = globals->m_lastTime;
        return 1;
    }

    return 0;
}

// ref: FUN_006ec2c0
// The reference also registers the "SplineOpt" CVar here ("toggles use of spline coll
// optimization"), which only the spline collision reads.
void MovementInitialize(const char* logName) {
    auto m = SMemAlloc(sizeof(CMovementGlobals), ".\\Movement.cpp", 0x399, 0x0);
    auto globals = m ? new (m) CMovementGlobals() : nullptr;

    if (auto mgr = ClntObjMgrGetCurrent()) {
        mgr->m_movementGlobals = globals;
    }

    globals = MovementGetGlobals();

    if (!globals) {
        return;
    }

    SStrCopy(globals->m_logName, logName ? logName : "", sizeof(globals->m_logName));

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    globals->m_flags |= 0x1;
    globals->m_stepTime = now;
    globals->m_lastTime = now;
}

// ref: FUN_00401520
// DIVERGENCE: the reference names the movement log from a registry value (FUN_00770720, with a
// numbered "%04d.txt" fallback). The log is a debug aid nothing reads back, so frozen starts the
// globals with an empty name.
void MovementStartWorld() {
    MovementInitialize("");

    // 0x004015be: the poll's priority is the 2.0 at 0x00a4040c.
    EventRegisterEx(EVENT_ID_IDLE, &MovementPoll, nullptr, 2.0f);
}

// ref: FUN_006f13e0
// A unit whose server spline is enabled (0x8000000) is carried by it; the local player only
// tells the server how much time went by.
void MovementUpdateMovers(uint32_t now, uint32_t last) {
    auto globals = MovementGetGlobals();

    if (!globals) {
        return;
    }

    auto move = globals->m_movers.Head();

    while (move) {
        auto next = globals->m_movers.Next(move);
        auto data = static_cast<CMovementData_C*>(move);

        if (!(move->m_moveFlags & 0x8000000)) {
            data->Update(now, last);
        } else if (data->IsActivePlayer()) {
            data->m_owner->SendTimeSkipped(now - last);
            globals->m_nextHeartbeat += now - last;
        }

        move = next;
    }
}

// ref: FUN_006f1490
int32_t MovementPoll(const void* data, void* param) {
    (void)data;
    (void)param;

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    auto globals = MovementGetGlobals();

    if (!globals) {
        return 1;
    }

    int32_t elapsed = static_cast<int32_t>(now - globals->m_lastTime);

    if (elapsed > 0) {
        MovementUpdateTransports(now, static_cast<uint32_t>(elapsed));

        if (globals->m_movers.Head()) {
            MovementUpdateMovers(now, globals->m_lastTime);
        }

        // The active mover's deferred turn and pitch reports.
        auto mover = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(CGUnit_C::s_activeMover, TYPE_UNIT, ".\\Movement.cpp", 0x37f));

        if (mover) {
            mover->SendDeferredMovement(now);
        }

        globals->m_stepTime = now;
        globals->m_lastTime = now;
    }

    return 1;
}

// ref: FUN_007b5020
void MovementLinkMover(CMovementShared* move) {
    auto globals = MovementGetGlobals();

    if (!globals) {
        return;
    }

    globals->m_movers.LinkToTail(move);
}

void MoveEventFree(CPlayerMoveEvent* event) {
    if (event->link.IsLinked()) {
        event->link.Unlink();
    }

    s_moveEventFreeList.LinkToTail(event);
}
