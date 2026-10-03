#include "object/client/CVehicle_C.hpp"
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "db/rec/VehicleRec.hpp"
#include "model/CM2Model.hpp"
#include "net/Types.hpp"
#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "world/CWorld.hpp"
#include <common/DataStore.hpp>
#include <storm/Array.hpp>
#include <storm/Memory.hpp>
#include <new>

namespace {

// DAT_00ca1608: set while StopSlotSequence replays an animation for a passenger that still wants it,
// so the replay does not stop itself.
int32_t s_replaying = 0;

// A unit's guid as a transport: a vehicle's (high 0xf05.) or a player's.
bool GuidIsUnit(WOWGUID guid) {
    uint32_t low = static_cast<uint32_t>(guid);
    uint32_t high = static_cast<uint32_t>(guid >> 32);

    if ((high & 0xf0f00000) == 0xf0500000) {
        return true;
    }

    return (high & 0xf0000000) == 0 && !(low == 0 && (high & 0xf07fffff) == 0);
}

CGUnit_C* PassengerUnit(CPassenger* passenger, int32_t line) {
    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(passenger->m_guid, TYPE_UNIT, ".\\Vehicle_C.cpp", line));
}

uint32_t ClampKind(uint32_t kind) {
    return 0x22 < kind ? 0x1a : kind;
}

} // namespace

// ref: FUN_00756e30
// The reference makes the vehicles' object heap ("Vehicle", 0x180-byte objects); frozen allocates
// each with Storm.
void CVehicle_C::Initialize() {
}

// ref: FUN_00756e90
void CVehicle_C::Destroy() {
}

// ref: FUN_00757fa0
CVehicle_C* CVehicle_C::Create(const CClientObjCreate* init, CGUnit_C* owner, int32_t recID) {
    auto vehicle = STORM_NEW(CVehicle_C);

    vehicle->m_animating = 0;
    vehicle->m_owner = owner;
    vehicle->m_matrix = C44Matrix();

    if (!init) {
        vehicle->m_rec = g_vehicleDB.GetRecord(recID);
        vehicle->m_matrix.Translate(owner->GetPosition());
        vehicle->m_matrix.RotateAroundZ(owner->GetFacing());
        vehicle->m_pitch = owner->GetRawFacing();
    } else {
        vehicle->m_rec = g_vehicleDB.GetRecord(static_cast<int32_t>(init->uint2C4));
        vehicle->m_matrix.Translate(init->move.status.position28);
        vehicle->m_matrix.RotateAroundZ(init->move.status.facing34);
        vehicle->m_pitch = init->float2C8;
    }

    vehicle->m_passengerRadius = 0.0f;
    vehicle->UpdateFreeSeats();
    vehicle->m_flags[0] = 0;
    vehicle->m_flags[1] = 0;

    for (auto& slot : vehicle->m_slots) {
        slot.guid = 0;
    }

    vehicle->m_pendingGUID = 0;
    vehicle->m_pendingKind = 0xFFFFFFFF;
    vehicle->m_stateBits = 0;

    return vehicle;
}

// ref: FUN_00757bb0
// ref: FUN_00757b40
void CVehicle_C::Release(int32_t ejectPassengers) {
    if (ejectPassengers) {
        this->EjectAllPassengers();
    }

    // FUN_007cecd0
    while (auto passenger = this->m_passengers.Head()) {
        passenger->m_transportLink.Unlink();
    }

    this->~CVehicle_C();
    STORM_FREE(this);
}

// ref: FUN_007580f0
void CVehicle_C::SetRec(int32_t recID) {
    this->m_rec = g_vehicleDB.GetRecord(recID);
    this->UpdateFreeSeats();
}

// ref: FUN_00756ec0
const VehicleSeatRec* CVehicle_C::GetSeatRec(uint8_t seat) const {
    if (seat >= 8 || !this->m_rec) {
        return nullptr;
    }

    return g_vehicleSeatDB.GetRecord(this->m_rec->m_seatID[seat]);
}

// ref: FUN_00756f00
bool CVehicle_C::AimsFreely() const {
    return !(this->m_owner->m_localMove.m_moveFlags & 0x2200000) && this->m_rec
        && (static_cast<uint32_t>(this->m_rec->m_flags) & 0x40040000) == 0x40000;
}

// ref: FUN_00756c90
uint32_t CVehicle_C::HasFlag26() const {
    return this->m_flags[0] & 0x4000000;
}

// ref: FUN_00756cd0
uint32_t CVehicle_C::TestFlag(uint32_t index) const {
    if (index == 0x1a) {
        return 0;
    }

    if (index > 0x22) {
        index = 0x1a;
    }

    return (1u << (index & 0x1f)) & this->m_flags[index >> 5];
}

// ref: FUN_00756d10
int32_t CVehicle_C::AddSlot(WOWGUID guid, uint32_t kind, SlotCallback callback) {
    kind = ClampKind(kind);

    for (auto& slot : this->m_slots) {
        if (slot.guid == 0) {
            slot.guid = guid;
            slot.kind = kind;
            slot.callback = callback;

            return 1;
        }
    }

    return 0;
}

// ref: FUN_00756d70
void CVehicle_C::RemoveSlot(WOWGUID guid) {
    for (auto& slot : this->m_slots) {
        if (slot.guid == guid) {
            slot.guid = 0;
        }
    }
}

// ref: FUN_00756de0
bool CVehicle_C::HasStateBits() const {
    return this->m_stateBits != 0;
}

// ref: FUN_00756df0
void CVehicle_C::SetStateBit(uint8_t bit) {
    this->m_stateBits |= static_cast<uint8_t>(1 << (bit & 0x1f));
}

// ref: FUN_00756e10
void CVehicle_C::ClearStateBit(uint8_t bit) {
    this->m_stateBits &= static_cast<uint8_t>(~(1 << (bit & 0x1f)));
}

// ref: FUN_00756db0
void CVehicle_C::SetPendingSlot(WOWGUID guid, uint32_t kind) {
    this->m_pendingGUID = guid;
    this->m_pendingKind = ClampKind(kind);
}

// ref: FUN_00757200
void VehicleSendEjectPassenger(WOWGUID passenger) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_EJECT_PASSENGER));
    msg.Put(passenger);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_00756f40
bool CVehicle_C::OwnerIsControllingAnimation() const {
    if (!this->m_rec || !(this->m_rec->m_flags & 0x10000)) {
        return false;
    }

    return (this->m_owner->m_animFlags & 0x400) != 0 || this->m_owner->m_intFA4 != -1;
}

// ref: FUN_007571c0
bool CVehicle_C::ControlsPassengerAnimation() const {
    return this->HasFlag26() != 0 || this->OwnerIsControllingAnimation();
}

// ref: FUN_00756ca0
bool CVehicle_C::CanPlay(uint32_t sequence) const {
    return this->m_owner->GetObjectModel() && sequence < 0x1fa;
}

// ref: FUN_00756f80
int32_t CVehicle_C::PlaySlotSequence(uint32_t kind, uint32_t sequence) {
    auto model = this->m_owner->GetObjectModel();

    if (!model || 0x1fa <= sequence) {
        return 0;
    }

    kind = ClampKind(kind);
    this->m_owner->SetBoneSequence(model, kind == 0x1a ? 0xFFFFFFFF : kind, sequence, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);

    this->m_flags[kind >> 5] |= 1u << (kind & 0x1f);
    this->m_animating |= 1;

    return 1;
}

// ref: FUN_00757420
static void VehicleReleaseSlotBone(CVehicle_C* vehicle, CM2Model* model, uint32_t kind) {
    if (kind == 0x1a) {
        kind = 0xFFFFFFFF;
    }

    if (!vehicle->m_owner->UnsetBoneSequence(model, kind, 1, 1, 0)) {
        vehicle->StopSlotSequence(vehicle->m_owner, kind, 0xFFFFFFFF, 1, 0);
        vehicle->m_owner->UpdateAnimation(0, 0xFFFFFFFF);
    }
}

// ref: FUN_00757280
void CVehicle_C::StopSlotSequence(CGUnit_C* owner, uint32_t kind, uint32_t sequence, int32_t force, uint32_t time) {
    if (force && s_replaying) {
        return;
    }

    kind = ClampKind(kind);

    TSGrowableArray<CVehiclePassenger_C*> released;
    int32_t wanted = 0;

    for (auto& slot : this->m_slots) {
        if (slot.guid == 0 || slot.kind != kind) {
            continue;
        }

        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(slot.guid, TYPE_UNIT, ".\\Vehicle_C.cpp", 0x17a));
        int32_t keep = 0;

        if (unit && slot.callback) {
            slot.callback(unit, &keep, &released);
        }

        if (!keep) {
            slot.guid = 0;
        } else {
            wanted = 1;
        }
    }

    if (force || !wanted) {
        this->m_flags[kind >> 5] &= ~(1u << (kind & 0x1f));
    }

    if (kind == this->m_pendingKind) {
        this->m_pendingGUID = 0;
        this->m_pendingKind = 0xFFFFFFFF;
    }

    auto model = this->m_owner->GetObjectModel();

    if (!force) {
        if (!wanted) {
            if (kind == 0x1a) {
                this->m_owner->UpdateAnimation(0, 0xFFFFFFFF);
            } else {
                VehicleReleaseSlotBone(this, model, kind);
            }
        } else {
            s_replaying = 1;
            this->m_owner->SetBoneSequence(model, kind == 0x1a ? 0xFFFFFFFF : kind, sequence, 0xFFFFFFFF, time, 1.0f, 1, 1, 0);
            s_replaying = 0;
        }
    }

    (void)owner;

    for (uint32_t i = 0; i < released.Count(); i++) {
        released[i]->ReleaseSlot();
    }
}

// ref: FUN_007577e0
void CVehicle_C::RemoveSlotSequence(uint32_t kind, WOWGUID guid) {
    auto model = this->m_owner->GetObjectModel();

    if (!model) {
        return;
    }

    kind = ClampKind(kind);
    this->RemoveSlot(guid);

    for (auto& slot : this->m_slots) {
        if (slot.guid != 0 && slot.kind == kind) {
            return;
        }
    }

    if (this->m_flags[kind >> 5] & (1u << (kind & 0x1f))) {
        VehicleReleaseSlotBone(this, model, kind);
    }
}

// ref: FUN_00757060
// An enter animation event ('$...' with a seat digit) on the vehicle ends the matching passengers'
// enter wait.
void CVehicle_C::EndEnterSequences(uint32_t kind, uint32_t eventId) {
    kind = ClampKind(kind);

    for (auto& slot : this->m_slots) {
        if (slot.guid == 0 || slot.kind != kind) {
            continue;
        }

        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(slot.guid, TYPE_UNIT, ".\\Vehicle_C.cpp", 0x1bb));

        if (unit && unit->m_vehiclePassenger && unit->m_vehiclePassenger->MatchesExitEvent(eventId)) {
            unit->m_vehiclePassenger->ReleaseSlot();
            slot.guid = 0;
        }
    }
}

// ref: FUN_007570f0
void CVehicle_C::EndExitSequences(uint32_t kind, uint32_t eventId) {
    kind = ClampKind(kind);

    for (auto& slot : this->m_slots) {
        if (slot.guid == 0 || slot.kind != kind) {
            continue;
        }

        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(slot.guid, TYPE_UNIT, ".\\Vehicle_C.cpp", 0x1cf));

        if (unit && unit->m_vehiclePassenger && unit->m_vehiclePassenger->MatchesEnterEvent(eventId)) {
            unit->m_vehiclePassenger->ReleaseSlot();
            slot.guid = 0;
        }
    }
}

// ref: FUN_00757000
CVehicle_C* CVehicle_C::GetRoot() {
    auto unit = this->m_owner;

    if (unit->m_vehiclePassenger) {
        while (unit->m_vehiclePassenger->IsRidingLiveVehicle()) {
            auto next = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(unit->m_vehiclePassenger->m_nextVehicleGUID, TYPE_UNIT, ".\\Vehicle_C.cpp", 0x111));

            if (!next) {
                break;
            }

            unit = next;

            if (!unit->m_vehiclePassenger) {
                break;
            }
        }
    }

    return unit->m_vehicle ? unit->m_vehicle : this;
}

// ref: FUN_00757470
uint32_t CVehicle_C::CountSeats() {
    if (!this->m_rec) {
        return 0;
    }

    uint32_t count = 0;

    for (int32_t seat : this->m_rec->m_seatID) {
        if (0 < seat) {
            count++;
        }
    }

    for (auto passenger = this->m_passengers.Head(); passenger; passenger = this->m_passengers.Next(passenger)) {
        auto unit = PassengerUnit(passenger, 0x24d);

        // A passenger that is itself a vehicle (and not a player) adds its seats in place of its own.
        if (unit->m_vehicle && unit->m_vehicle->m_rec && !unit->IsA(TYPE_PLAYER)) {
            count = count - 1 + unit->m_vehicle->CountSeats();
        }
    }

    return count;
}

// ref: FUN_00757550
// The `*index`-th seat of the chain, counting each vehicle's seats in order with a vehicle sitting
// in a seat standing for its own seats.
int32_t CVehicle_C::FindSeat(int32_t* index, CVehicle_C** vehicle, uint8_t* seat) {
    if (!this->m_rec) {
        return 0;
    }

    CGUnit_C* bySeat[8] = {};

    for (auto passenger = this->m_passengers.Head(); passenger; passenger = this->m_passengers.Next(passenger)) {
        auto unit = PassengerUnit(passenger, 0x293);

        if (unit->m_vehicle && unit->m_vehicle->m_rec && !unit->IsA(TYPE_PLAYER)) {
            bySeat[unit->m_localMove.m_transportSeat] = unit;
        }
    }

    for (int32_t i = 0; i < 8; i++) {
        if (!bySeat[i]) {
            if (0 < this->m_rec->m_seatID[i]) {
                if (*index == 0) {
                    *vehicle = this;
                    *seat = static_cast<uint8_t>(i);

                    return 1;
                }

                *index = *index - 1;
            }
        } else if (bySeat[i]->m_vehicle->FindSeat(index, vehicle, seat)) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_00757680
CGUnit_C* CVehicle_C::GetPassengerInSeat(uint8_t seat) {
    for (auto passenger = this->m_passengers.Head(); passenger; passenger = this->m_passengers.Next(passenger)) {
        auto unit = PassengerUnit(passenger, 0x2c2);

        if (unit->m_localMove.m_transportSeat == seat) {
            return unit;
        }
    }

    return nullptr;
}

// ref: FUN_007576e0
// Every passenger put off where it is: the camera no longer relative to it, the active player falls
// if nothing holds it up, any other unit is left to fall.
void CVehicle_C::EjectAllPassengers() {
    auto camera = CGWorldFrame::GetActiveCamera();
    int32_t now = static_cast<int32_t>(CWorld::GetCurTimeMs());

    for (auto passenger = this->m_passengers.Head(); passenger;) {
        auto next = this->m_passengers.Next(passenger);

        if (camera && camera->GetTarget() == passenger->m_guid) {
            camera->SetRelativeTo(0);
        }

        auto unit = PassengerUnit(passenger, 0x2dd);
        auto& move = unit->m_localMove;

        if (unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
            move.QueueFallIfUnsupported(now);
        } else if (move.FallIfUnsupported() && !move.m_moverLink.IsLinked() && MovementGetGlobals()) {
            // FUN_006eb650
            MovementLinkMover(&move);
        }

        unit->m_stateFlags |= 0x20000000;
        move.SetSplineTransport(0, 0xff, 1);
        unit->m_stateFlags &= 0xdfffffff;

        passenger = next;
    }
}

// ref: FUN_00757980
int32_t CVehicle_C::AddPassenger(CPassenger* passenger) {
    if (!(passenger->m_passengerFlags & 0x1)) {
        return 0;
    }

    passenger->m_transportLink.Unlink();
    this->m_passengers.LinkToTail(passenger);

    return 1;
}

// ref: FUN_00757be0
void CVehicle_C::UpdateMatrix(const C44Matrix* parent) {
    this->m_matrix = C44Matrix();

    C3Vector position = this->m_owner->GetRawPosition();
    this->m_matrix.d0 = position.x;
    this->m_matrix.d1 = position.y;
    this->m_matrix.d2 = position.z;
    this->m_matrix.RotateAroundZ(this->m_owner->GetRawFacing());

    if (parent) {
        this->m_matrix = this->m_matrix * *parent;
    }

    for (auto passenger = this->m_passengers.Head(); passenger; passenger = this->m_passengers.Next(passenger)) {
        if (!GuidIsUnit(passenger->m_guid)) {
            continue;
        }

        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(passenger->m_guid, TYPE_UNIT, ".\\Vehicle_C.cpp", 0x8d));

        if (unit->m_vehicle && unit->m_vehicle->m_rec) {
            unit->m_vehicle->UpdateMatrix(&this->m_matrix);
        }
    }
}

// ref: FUN_00758130
void CVehicle_C::UpdateMatrixFromTransport() {
    WOWGUID transport = this->m_owner->GetTransportGUID();

    if (!transport) {
        this->UpdateMatrix(nullptr);
        return;
    }

    auto object = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(transport, TYPE_OBJECT, ".\\Vehicle_C.cpp", 0x75));

    if (object) {
        C44Matrix world;
        object->GetWorldMatrix(world);
        this->UpdateMatrix(&world);
    }
}

// ref: FUN_00757d10
void CVehicle_C::UpdatePassengerRadius() {
    CVehicle_C* vehicle = this;

    while (true) {
        float before = vehicle->m_passengerRadius;
        vehicle->m_passengerRadius = 0.0f;

        for (auto passenger = vehicle->m_passengers.Head(); passenger; passenger = vehicle->m_passengers.Next(passenger)) {
            auto unit = PassengerUnit(passenger, 0x9d);
            auto model = unit->GetObjectModel();

            if (!model || !model->IsLoaded(0, 0)) {
                continue;
            }

            CAaSphere sphere;
            model->GetBoundingSphere(sphere);
            float reach = sphere.r;

            if (unit->m_vehicle && unit->m_vehicle->m_rec) {
                reach = unit->m_vehicle->m_passengerRadius + unit->m_vehicle->m_passengerRadius + reach;
            }

            if (vehicle->m_passengerRadius < reach) {
                vehicle->m_passengerRadius = reach;
            }
        }

        if (vehicle->m_passengerRadius == before) {
            return;
        }

        auto ride = vehicle->m_owner->m_vehiclePassenger;

        if (!ride || ride->m_state != 3) {
            return;
        }

        auto next = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(vehicle->m_owner->GetTransportGUID(), TYPE_UNIT, ".\\Vehicle_C.cpp", 0xae));

        if (!next || !next->m_vehicle || !next->m_vehicle->m_rec) {
            return;
        }

        vehicle = next->m_vehicle;
    }
}

// ref: FUN_00757e70
void CVehicle_C::ResyncPassengerAnimations() {
    for (auto passenger = this->m_passengers.Head(); passenger; passenger = this->m_passengers.Next(passenger)) {
        auto unit = PassengerUnit(passenger, 0x202);

        if (unit && unit->m_vehiclePassenger && unit->m_vehiclePassenger->IsRidingLiveVehicle()) {
            unit->m_vehiclePassenger->SyncToVehicle();
        }
    }
}

// ref: FUN_00757ef0
void CVehicle_C::UpdateFreeSeats() {
    this->m_freeSeats = 0;

    if (!this->m_rec) {
        return;
    }

    uint8_t taken = 0;

    for (auto passenger = this->m_passengers.Head(); passenger; passenger = this->m_passengers.Next(passenger)) {
        auto unit = PassengerUnit(passenger, 0x230);

        if (unit) {
            taken |= static_cast<uint8_t>(1 << (unit->m_localMove.m_transportSeat & 0x1f));
        }
    }

    uint8_t bit = 1;

    for (int32_t seat : this->m_rec->m_seatID) {
        if (seat != 0 && !(bit & taken)) {
            this->m_freeSeats |= bit;
        }

        bit = static_cast<uint8_t>(bit * 2);
    }
}
