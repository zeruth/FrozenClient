#include "object/client/UnitVehicle_C.hpp"
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "db/rec/VehicleRec.hpp"
#include "db/rec/VehicleSeatRec.hpp"
#include "model/CM2Model.hpp"
#include "net/Types.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/CVehicle_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "sound/SI2.hpp"
#include "ui/FrameScript.hpp"
#include "ui/InputControl.hpp"
#include "ui/game/ScriptEvents.hpp"
#include "world/CWorld.hpp"
#include <common/DataStore.hpp>
#include <cmath>

namespace {

bool GuidIsUnit(WOWGUID guid) {
    uint32_t low = static_cast<uint32_t>(guid);
    uint32_t high = static_cast<uint32_t>(guid >> 32);

    if ((high & 0xf0f00000) == 0xf0500000) {
        return true;
    }

    return (high & 0xf0000000) == 0 && !(low == 0 && (high & 0xf07fffff) == 0);
}

CGUnit_C* UnitPtr(WOWGUID guid, int32_t line) {
    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\UnitVehicle_C.cpp", line));
}

bool IsVehicle(CGUnit_C* unit) {
    return unit->m_vehicle && unit->m_vehicle->m_rec;
}

void SendEmpty(uint32_t opcode) {
    CDataStore msg;
    msg.Put(opcode);
    msg.Finalize();
    ClientServices::Send(&msg);
}

} // namespace

// ref: FUN_00737390
// The ceiling the unit's alpha scales by: the rider of a seat whose model hangs on the vehicle's is
// opaque; everyone below takes the same value.
void UnitSetRideAlpha(CGUnit_C* unit, float alpha) {
    uint8_t ceiling = static_cast<uint8_t>(static_cast<int32_t>(std::nearbyint(alpha * 255.0f)));
    auto ride = unit->m_vehiclePassenger;

    if (ride && ride->m_state == 3) {
        auto vehicle = UnitPtr(unit->GetTransportGUID(), 0x59bb);

        if (vehicle) {
            auto model = unit->GetObjectModel();

            if (model && model->m_attachParent == vehicle->GetObjectModel()) {
                ceiling = 0xff;
            }
        }
    }

    unit->m_alphaScale = ceiling;

    if (IsVehicle(unit)) {
        auto vehicle = unit->m_vehicle;

        for (auto passenger = vehicle->m_passengers.Head(); passenger; passenger = vehicle->m_passengers.Next(passenger)) {
            if (auto rider = UnitPtr(passenger->m_guid, 0x59cb)) {
                UnitSetRideAlpha(rider, alpha);
            }
        }
    }
}

// ref: FUN_005fb560
// The active player's seat on the vehicle the active mover is (or rides) carries `flag`.
int32_t InputControlSeatAllows(uint32_t flag) {
    auto mover = UnitPtr(CGUnit_C::s_activeMover, 0xa4a);

    if (!mover) {
        return 0;
    }

    auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\Player_C.h", 0xa0));

    if (!player || !player->m_vehiclePassenger || player->m_vehiclePassenger->m_state != 3) {
        return 0;
    }

    if (player->GetTransportGUID() != mover->GetGUID() && player->GetGUID() != mover->GetGUID()) {
        return 0;
    }

    auto vehicle = UnitPtr(player->GetTransportGUID(), 0xa53);

    if (!vehicle || !vehicle->m_vehicle) {
        return 0;
    }

    auto seat = vehicle->m_vehicle->GetSeatRec(player->m_localMove.m_transportSeat);

    return seat && (static_cast<uint32_t>(seat->m_flags) & flag) ? 1 : 0;
}

// ref: FUN_00717fc0
C3Vector UnitTransportPosition(CGUnit_C* unit) {
    C3Vector position = unit->GetRawPosition();
    auto transport = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(unit->GetTransportGUID(), TYPE_OBJECT, ".\\Unit_C.cpp", 0x215f));

    if (!transport) {
        return position;
    }

    C44Matrix matrix;

    if (!transport->IsA(TYPE_UNIT)) {
        transport->GetWorldMatrix(matrix);
    } else {
        UnitSeatMatrix(static_cast<CGUnit_C*>(transport), matrix);
    }

    C3Vector out;
    TransformPointInPlace(out, position, matrix);

    return position;
}

// ref: FUN_00717ec0
void UnitSeatMatrix(CGUnit_C* unit, C44Matrix& out) {
    out = C44Matrix::RotationAroundZ(unit->m_smoothFacing);

    C3Vector position = unit->GetRawPosition();
    out.d0 = position.x;
    out.d1 = position.y;
    out.d2 = position.z;

    auto transport = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(unit->GetTransportGUID(), TYPE_OBJECT, ".\\Unit_C.cpp", 0x2150));

    if (!transport) {
        return;
    }

    C44Matrix parent;

    if (transport->IsA(TYPE_UNIT)) {
        UnitSeatMatrix(static_cast<CGUnit_C*>(transport), parent);
    } else {
        transport->GetWorldMatrix(parent);
    }

    out = out * parent;
}

// ref: FUN_0074c650
CGUnit_C* UnitGetVehicleRoot(CGUnit_C* unit, CGUnit_C* stop) {
    if (unit == stop && IsVehicle(unit)) {
        return unit;
    }

    WOWGUID transport = unit->GetTransportGUID();

    if (!GuidIsUnit(transport)) {
        return IsVehicle(unit) ? unit : nullptr;
    }

    for (auto ride = UnitPtr(transport, 0x10b); ride;) {
        if (ride == stop) {
            return ride;
        }

        WOWGUID next = ride->GetTransportGUID();

        if (!GuidIsUnit(next)) {
            return ride;
        }

        ride = UnitPtr(next, 0x114);
    }

    return nullptr;
}

// ref: FUN_004f6230
CGUnit_C* UnitGetRootVehicle(CGUnit_C* unit) {
    if (unit->m_vehiclePassenger) {
        return unit->m_vehiclePassenger->GetRootVehicleUnit();
    }

    return UnitGetVehicleRoot(unit, nullptr);
}

// ref: FUN_0074cc40
CGUnit_C* UnitGetRideChainTop(CGUnit_C* unit, CGUnit_C* stop) {
    WOWGUID next = unit->m_vehiclePassenger ? unit->m_vehiclePassenger->m_nextVehicleGUID : 0;

    if (!next) {
        return IsVehicle(unit) ? unit : nullptr;
    }

    for (auto ride = UnitPtr(next, 0x2fe); ride;) {
        if (ride == stop) {
            return ride;
        }

        WOWGUID up = ride->m_vehiclePassenger ? ride->m_vehiclePassenger->m_nextVehicleGUID : 0;

        if (!up) {
            return ride;
        }

        ride = UnitPtr(up, 0x307);
    }

    return nullptr;
}

// ref: FUN_00738440
// A ride begins: the unit's own animations are put back to its stand, its held upper body released
// (and the vehicle's riders' with it), and its mount's too.
void UnitResetSequencesForRide(CGUnit_C* unit) {
    auto model = unit->m_model;

    if (model) {
        if ((unit->m_animFlags & 0x80) && (!unit->m_vehiclePassenger || !unit->m_vehiclePassenger->IsRidingLiveVehicle())) {
            uint32_t bone = unit->m_upperBodyBoneId;

            if (model->IsLoaded(0, 0) && model->BoneHasParent(bone)) {
                model->UnsetBoneSequence(bone, 1, 1);
            }

            if (IsVehicle(unit) && unit->m_vehicle->HasStateBits() && unit->GetObjectModel() == model) {
                auto vehicle = unit->m_vehicle;

                for (auto passenger = vehicle->m_passengers.Head(); passenger; passenger = vehicle->m_passengers.Next(passenger)) {
                    auto rider = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(passenger->m_guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x19fd));

                    if (rider && rider->m_vehiclePassenger && rider->m_vehiclePassenger->IsRidingLiveVehicle()) {
                        rider->UnsetBoneSequence(rider->GetObjectModel(), bone, 1, 1, 1);
                    }
                }
            }
        }

        unit->SetBoneSequence(model, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
    }

    if (unit->m_mountModel) {
        unit->SetBoneSequence(unit->m_mountModel, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
    }
}

// ref: FUN_0074b9c0
// Off the seat: the fall, the flight, or the seat's exit animation.
void UnitSetVehicleExitAnimation(CGUnit_C* unit, const VehicleSeatRec* seat, int32_t anim) {
    if (!seat || !(static_cast<uint32_t>(seat->m_flags) & 0x200000) || anim == -1) {
        uint32_t flags = unit->m_localMove.m_moveFlags;

        if (flags & 0x1000) {
            unit->SetAnimation(0x28, 0);
            unit->m_animFlags |= 0x1000000;
            return;
        }

        if (flags & 0x200000) {
            unit->SetAnimation(0x29, 0);
            return;
        }

        if ((flags & 0x2000000) || anim == -1) {
            return;
        }
    }

    unit->SetAnimation(anim, 1);
}

// ref: FUN_0074b840
int32_t UnitAllowsSeatAnimation(CGUnit_C* unit, const VehicleSeatRec* seat, int32_t leaving) {
    if (unit->m_stateFlags & 0x20000000) {
        return 1;
    }

    return leaving && seat && (seat->m_flagsB & 0x1000000) ? 1 : 0;
}

// ref: FUN_0074ba40
int32_t UnitVehicleHoldsControl(CGUnit_C* unit) {
    if (!unit->IsA(TYPE_PLAYER)) {
        return 0;
    }

    auto ride = unit->m_vehiclePassenger;

    if (ride && ride->m_state != 0 && ride->m_state != 3) {
        return 1;
    }

    if (ride && (ride->m_flags & 0x200)) {
        return 1;
    }

    auto charm = UnitPtr(unit->Unit()->charm, 0xb8);

    if (!charm) {
        // A unit that is not under the player's control.
        if (!(unit->Unit()->flags & 0x1000000) && unit->GetGUID() != ClntObjMgrGetActivePlayer()) {
            return 1;
        }

        return 0;
    }

    auto charmRide = charm->m_vehiclePassenger;

    if (charmRide) {
        if (charmRide->m_state != 0 && charmRide->m_state != 3) {
            return 1;
        }

        if (charmRide->m_flags & 0x200) {
            return 1;
        }
    }

    // FUN_0056c220
    bool riding = charm->m_vehiclePassenger && charm->m_vehiclePassenger->m_state != 0;

    return riding && (charmRide->m_flags & 0x8) ? 1 : 0;
}

// ref: FUN_0074bb90
uint32_t UnitVehicleAimFlags(CGUnit_C* unit) {
    auto rec = unit->m_vehicle ? unit->m_vehicle->m_rec : nullptr;

    if (!rec) {
        return 0;
    }

    uint32_t mask = (unit->m_localMove.m_moveFlags & 0x2200000) ? 0x100 : 0x80;

    return mask & static_cast<uint32_t>(rec->m_flags);
}

// ref: FUN_0074bbd0
// UNIT_ENTERING_VEHICLE's sibling for the vehicle itself (0x250): the indicator it shows.
void UnitSignalVehicleBegin(CGUnit_C* unit) {
    auto rec = unit->m_vehicle ? unit->m_vehicle->m_rec : nullptr;
    int32_t count = 0;
    auto tokens = ScriptEventsGetUnitTokens(unit->GetGUID(), &count);

    for (int32_t i = 0; i < count; i++) {
        FrameScript_SignalEvent(0x250, "%s%d", tokens[i], rec ? rec->m_vehicleUIIndicatorID : 0);
    }
}

// ref: FUN_0074bc50
void UnitSignalVehicleEnd(CGUnit_C* unit) {
    int32_t count = 0;
    auto tokens = ScriptEventsGetUnitTokens(unit->GetGUID(), &count);

    for (int32_t i = 0; i < count; i++) {
        FrameScript_SignalEvent(0x251, "%s", tokens[i]);
    }
}

// ref: FUN_0074c5a0
// VEHICLE_ANGLE_SHOW (0x248) and VEHICLE_POWER_SHOW (0x24a) follow the vehicle the player controls,
// and its pitch goes to the input.
void VehicleUpdateAngleEvents(CGUnit_C* unit, const VehicleRec* rec) {
    bool controls = unit && !UnitVehicleHoldsControl(unit);

    if (controls && rec && (static_cast<uint32_t>(rec->m_flags) & 0x400)) {
        FrameScript_SignalEvent(0x248, "%d", 1);
    } else {
        FrameScript_SignalEvent(0x248, nullptr);
    }

    if (controls && rec && (static_cast<uint32_t>(rec->m_flags) & 0x800)) {
        FrameScript_SignalEvent(0x24a, "%d", 1);
    } else {
        FrameScript_SignalEvent(0x24a, nullptr);
    }

    if (unit) {
        InputControlGetActive();
        InputControlSignalPitch(unit->GetMovementPitch());
    }
}

// ref: FUN_0074bcb0
int32_t UnitSeatMoveAnimates(CGUnit_C* unit, WOWGUID transport, uint8_t seat) {
    if (unit->GetTransportGUID() == transport && seat == unit->m_localMove.m_transportSeat) {
        return 0;
    }

    bool entering = GuidIsUnit(transport);

    if (!entering) {
        if (!unit->IsTransportUnit()) {
            return 0;
        }

        transport = unit->GetTransportGUID();
    }

    auto vehicle = UnitPtr(transport, 0x162);

    if (!vehicle || !vehicle->m_vehicle) {
        return 0;
    }

    if (!entering) {
        seat = unit->m_localMove.m_transportSeat;
    }

    auto seatRec = vehicle->m_vehicle->GetSeatRec(seat);

    if (!seatRec) {
        return 0;
    }

    vehicle->m_vehicle->GetRoot();

    if (entering) {
        if ((seatRec->m_flags & 0x400000) && static_cast<uint32_t>(seatRec->m_vehicleEnterAnim) < 0x1fa) {
            return vehicle->m_vehicle->CanPlay(static_cast<uint32_t>(seatRec->m_vehicleEnterAnim)) ? 1 : 0;
        }

        return 0;
    }

    uint32_t mask = (unit->m_localMove.GetMoveFlags2() & 0x40) ? 0x40000 : 0x80000;

    if ((static_cast<uint32_t>(seatRec->m_flags) & mask) && static_cast<uint32_t>(seatRec->m_vehicleExitAnim) < 0x1fa
        && vehicle->m_vehicle->CanPlay(static_cast<uint32_t>(seatRec->m_vehicleExitAnim))) {
        return 1;
    }

    return 0;
}

// ref: FUN_0074be10
int32_t UnitStartSeatMoveAnimation(CGUnit_C* unit, WOWGUID transport, uint8_t seat) {
    bool entering = GuidIsUnit(transport);

    if (!entering) {
        if (!unit->IsTransportUnit()) {
            return 0;
        }

        transport = unit->GetTransportGUID();
    }

    auto vehicle = UnitPtr(transport, 0x18c);

    if (!vehicle || !vehicle->m_vehicle) {
        return 0;
    }

    uint8_t index = entering ? seat : unit->m_localMove.m_transportSeat;
    auto seatRec = vehicle->m_vehicle->GetSeatRec(index);

    if (!seatRec) {
        return 0;
    }

    if (unit->m_vehiclePassenger) {
        unit->m_vehiclePassenger->ReleaseSlot();
    }

    auto root = vehicle->m_vehicle->GetRoot();

    if (!entering) {
        uint32_t mask = (unit->m_localMove.GetMoveFlags2() & 0x40) ? 0x40000 : 0x80000;

        if (!(static_cast<uint32_t>(seatRec->m_flags) & mask) || 0x1fa <= static_cast<uint32_t>(seatRec->m_vehicleExitAnim)
            || !root->PlaySlotSequence(static_cast<uint32_t>(seatRec->m_vehicleExitAnimBone), static_cast<uint32_t>(seatRec->m_vehicleExitAnim))) {
            return 0;
        }

        if (unit->m_vehiclePassenger) {
            unit->m_vehiclePassenger->ClearAnimVehicle(root->m_owner->GetGUID(), seatRec);
        }

        root->AddSlot(unit->GetGUID(), static_cast<uint32_t>(seatRec->m_vehicleExitAnimBone), &CVehiclePassenger_C::ReleaseSlotCallback);
    } else {
        if (!(seatRec->m_flags & 0x400000) || 0x1fa <= static_cast<uint32_t>(seatRec->m_vehicleEnterAnim)
            || !root->PlaySlotSequence(static_cast<uint32_t>(seatRec->m_vehicleEnterAnimBone), static_cast<uint32_t>(seatRec->m_vehicleEnterAnim))) {
            return 0;
        }

        root->AddSlot(unit->GetGUID(), static_cast<uint32_t>(seatRec->m_vehicleEnterAnimBone), &CVehiclePassenger_C::ReleaseSlotCallback);

        if (seatRec->m_flagsB & 0x800000) {
            root->SetPendingSlot(unit->GetGUID(), static_cast<uint32_t>(seatRec->m_vehicleEnterAnimBone));
        }
    }

    if (!unit->m_vehiclePassenger) {
        unit->m_vehiclePassenger = CVehiclePassenger_C::Create(unit);
    }

    unit->m_vehiclePassenger->SetPending(transport, seat, seatRec, entering ? 0 : 1, root);

    return 1;
}

// ref: FUN_0074c040
int32_t UnitQueueVehicleMove(CGUnit_C* unit, CDataStore* msg, WOWGUID transport, uint8_t seat) {
    if (unit->m_vehiclePassenger) {
        unit->m_vehiclePassenger->FreeQueuedMove();
        unit->m_vehiclePassenger->ReleaseSlot();
    }

    if (!UnitSeatMoveAnimates(unit, transport, seat)) {
        return 0;
    }

    unit->m_localMove.FlushEvents(0, 0);

    if (!unit->m_vehiclePassenger) {
        unit->m_vehiclePassenger = CVehiclePassenger_C::Create(unit);
    }

    unit->m_vehiclePassenger->QueueMove(msg);

    if (!UnitStartSeatMoveAnimation(unit, transport, seat)) {
        unit->m_vehiclePassenger->PlayQueuedMove();
    }

    return 1;
}

// ref: FUN_0074c750
void UnitCreateVehicle(CGUnit_C* unit, const CClientObjCreate* init, int32_t recID) {
    if (unit->m_vehicle) {
        unit->m_vehicle->SetRec(recID);
        return;
    }

    unit->m_vehicle = CVehicle_C::Create(init, unit, recID);

    if (unit->m_postInited && unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
        UnitSignalVehicleBegin(unit);
    }
}

// ref: FUN_0074c7b0
void UnitDestroyVehicle(CGUnit_C* unit, int32_t eject) {
    if (!unit->m_vehicle) {
        return;
    }

    unit->m_vehicle->Release(eject);
    unit->m_vehicle = nullptr;

    if (unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
        UnitSignalVehicleEnd(unit);
    }
}

// ref: FUN_0074c7f0
// PARTIAL: the active player leaving a pet-controlled vehicle goes through the pet bar
// (FUN_005d46f0, PetInfo.cpp), not ported; it asks the server instead.
void UnitRequestVehicleExit(CGUnit_C* unit) {
    (void)unit;

    SendEmpty(0x476);
}

// ref: FUN_0074c8b0
void UnitRequestVehiclePrevSeat(CGUnit_C* unit) {
    auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\Player_C.h", 0xa0));

    if (unit->GetGUID() == ClntObjMgrGetActivePlayer() && player && player->GetTransportGUID() == unit->GetGUID()) {
        if (player->m_vehiclePassenger) {
            player->m_vehiclePassenger->LockInput();
        }

        // FUN_006ef5a0 -> FUN_006ecae0: a local seat change event.
        player->m_localMove.QueueSeatChange(static_cast<int32_t>(CWorld::GetCurTimeMs()), 0, 0xff);
        return;
    }

    SendEmpty(0x477);
}

// ref: FUN_0074c9a0
void UnitRequestVehicleNextSeat(CGUnit_C* unit) {
    auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\Player_C.h", 0xa0));

    if (unit->GetGUID() == ClntObjMgrGetActivePlayer() && player && player->GetTransportGUID() == unit->GetGUID()) {
        if (player->m_vehiclePassenger) {
            player->m_vehiclePassenger->LockInput();
        }

        player->m_localMove.QueueSeatChange(static_cast<int32_t>(CWorld::GetCurTimeMs()), 0, 1);
        return;
    }

    SendEmpty(0x478);
}

// ref: FUN_0074ca90
int32_t UnitRequestVehicleSwitchSeat(CGUnit_C* unit, CGUnit_C* vehicle, int32_t index) {
    if (!IsVehicle(vehicle)) {
        return 0;
    }

    CVehicle_C* owner = nullptr;
    uint8_t seat = 0;

    if (!vehicle->m_vehicle->FindSeat(&index, &owner, &seat)) {
        return 0;
    }

    auto seatRec = owner->GetSeatRec(seat);

    if (!seatRec || owner->GetPassengerInSeat(seat) || !(seatRec->m_flags & 0x4000000) || !InputControlGetActive()) {
        return 0;
    }

    if (!InputControlSeatAllows(0x4000000)) {
        return 1;
    }

    WOWGUID mover = CGUnit_C::s_activeMover;

    if (unit->GetTransportGUID() == mover) {
        auto moverUnit = UnitPtr(mover, 0x226);

        if (moverUnit) {
            if (unit->m_vehiclePassenger) {
                unit->m_vehiclePassenger->LockInput();
            }

            moverUnit->m_localMove.QueueSeatChange(static_cast<int32_t>(CWorld::GetCurTimeMs()), owner->m_owner->GetGUID(), seat);
        }

        return 1;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(0x479));
    SmartGUID packed;
    packed = owner->m_owner->GetGUID();
    msg << packed;
    msg.Put(seat);
    msg.Finalize();
    ClientServices::Send(&msg);

    return 1;
}

// ref: FUN_0074cce0
void UnitVehicleExitIfSeatEjects(CGUnit_C* unit) {
    auto seat = unit->m_vehiclePassenger ? unit->m_vehiclePassenger->m_seat : nullptr;

    if (!seat || !(seat->m_flagsB & 0x100000)) {
        return;
    }

    auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\Player_C.h", 0xa0));

    if (unit->GetGUID() == ClntObjMgrGetActivePlayer() && player && player->GetTransportGUID() == unit->GetGUID()) {
        return;
    }

    UnitRequestVehicleExit(unit);
}

// ref: FUN_0074cf30
// PARTIAL: the vehicle camera that follows the ride (FUN_0074ce40, FUN_0075aac0, FUN_0074cd60) is
// the vehicle camera port's.
void VehicleOnTransportChanged(CGUnit_C* unit, WOWGUID transport, uint8_t seat, int32_t force) {
    if (!(unit->m_stateFlags & 0x80000) && !force) {
        return;
    }

    auto root = unit->m_vehiclePassenger ? unit->m_vehiclePassenger->GetRootVehicleUnit() : UnitGetVehicleRoot(unit, nullptr);

    if (root) {
        UnitSetRideAlpha(unit, 1.0f);
    }

    CGUnit_C* vehicle = nullptr;

    if (GuidIsUnit(transport)) {
        vehicle = UnitPtr(transport, 0x8d);

        if (!vehicle) {
            auto ride = unit->m_vehiclePassenger;
            transport = 0;
            seat = 0xff;

            if (!ride || !ride->m_nextVehicleGUID) {
                return;
            }
        }
    }

    if (!unit->m_vehiclePassenger) {
        if (!transport) {
            return;
        }

        unit->m_vehiclePassenger = CVehiclePassenger_C::Create(unit);
    }

    unit->m_vehiclePassenger->OnTransportChanged(transport, seat, vehicle, force);
}

// ref: FUN_007561e0
// A ride that ends in death: the death's combat log line and pose, the pet's death sound, the mount
// sound stopped, and the player's death events.
//
// PARTIAL: the combat log entry (FUN_00752ed0) and the pet death sound (FUN_007474b0) are the
// combat log and pet sound ports'; the corpse release prompt after PLAYER_DEAD (FUN_00519280) is
// the UI's.
void UnitOnLeftVehicle(CGUnit_C* unit) {
    unit->PlayDeathPose(0);

    if (unit->m_mountSound) {
        SI2::StopOrFadeOut(unit->m_mountSound, 0, 0.5f, 1);
    }

    if (unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
        // FUN_00520f70
        FrameScript_SignalEvent(unit->Unit()->health < 1 ? 0x102 : 0x101, nullptr);
    }
}
