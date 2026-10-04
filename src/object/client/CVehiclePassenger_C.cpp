#include "object/client/CVehiclePassenger_C.hpp"
#include "db/Db.hpp"
#include "db/rec/VehicleRec.hpp"
#include "db/rec/VehicleSeatRec.hpp"
#include "model/CM2Model.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CVehicle_C.hpp"
#include "object/client/CVehicleCamera_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/UnitVehicle_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/InputControl.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "ui/game/CGRaidInfo.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "ui/game/ScriptEvents.hpp"
#include "world/CWorld.hpp"
#include <common/DataStore.hpp>
#include <storm/Array.hpp>
#include <storm/Memory.hpp>
#include <cmath>
#include <vector>

namespace {

// DAT_00ca1368 / DAT_00ca136c: every ride, newest first.
CVehiclePassenger_C* s_head = nullptr;
CVehiclePassenger_C* s_tail = nullptr;

// DAT_00ca1358: the rides waiting to be set off at the end of the update (a seat change that
// arrived while the units were being stepped), and DAT_00ca1364 while that is so.
struct RescueTransition {
    WOWGUID unit;
    WOWGUID vehicle;
    uint8_t seat;
    uint8_t exitAnim;
};

TSGrowableArray<RescueTransition> s_rescues;
int32_t s_deferRescues = 0;

// DAT_00a2d3f0: a seat's attachment slot to the model attachment it names.
const int32_t s_seatAttachments[0x16] = {
    0x14, 0x22, 0x13, 0x15, 0x16, 0x11, 0x17, 0x18, 0x19, 0x0f, 0x10,
    0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x00,
};

int32_t SeatAttachment(const VehicleSeatRec* seat) {
    return static_cast<uint32_t>(seat->m_attachmentID) < 0x16 ? s_seatAttachments[seat->m_attachmentID] : -1;
}

uint32_t Now() {
    return CWorld::GetCurTimeMs();
}

CGUnit_C* UnitPtr(WOWGUID guid, int32_t line) {
    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\VehiclePassenger_C.cpp", line));
}

bool GuidIsUnit(WOWGUID guid) {
    uint32_t low = static_cast<uint32_t>(guid);
    uint32_t high = static_cast<uint32_t>(guid >> 32);

    if ((high & 0xf0f00000) == 0xf0500000) {
        return true;
    }

    return (high & 0xf0000000) == 0 && !(low == 0 && (high & 0xf07fffff) == 0);
}

// The rider is in state 1..3: entering or aboard.
bool RideIsInVehicle(const CVehiclePassenger_C* ride) {
    return ride && (ride->m_state == 1 || ride->m_state == 2 || ride->m_state == 3);
}

// ref: FUN_00748810
// UNIT_ENTERING_VEHICLE / UNIT_ENTERED_VEHICLE: the unit's tokens, whether the seat has a vehicle
// UI, its skin, the rider's class power, whether the rider may leave, and the vehicle's indicator.
void SignalVehicleEvent(WOWGUID unit, const VehicleRec* vehicle, const VehicleSeatRec* seat, int32_t event) {
    int32_t hasUI = 0;
    const char* skin = "";
    const char* power = "";
    uint32_t canExit = 0;

    if (seat) {
        hasUI = (static_cast<uint32_t>(seat->m_flags) >> 0x1d) & 1;
        static const char* const s_skins[2] = { "Natural", "Mechanical" };
        skin = static_cast<uint32_t>(seat->m_uiSkin) < 2 ? s_skins[seat->m_uiSkin] : "";

        if (auto rec = g_soundEntriesDB.GetRecord(seat->m_enterUISoundID)) {
            power = rec->m_name;
        }

        canExit = (static_cast<uint32_t>(seat->m_flags) >> 0xb) & 1;
    }

    int32_t count = 0;
    auto tokens = ScriptEventsGetUnitTokens(unit, &count);

    for (int32_t i = 0; i < count; i++) {
        FrameScript_SignalEvent(event, "%s%b%s%s%b%d", tokens[i], hasUI, skin, power, canExit,
                                vehicle ? vehicle->m_vehicleUIIndicatorID : 0);
    }
}

// ref: FUN_00749650
void SignalSeatEvents(WOWGUID unit, const VehicleRec* vehicle, const VehicleSeatRec* seat, const ChrClassesRec* cls, uint32_t which) {
    (void)cls;

    if (which & 1) {
        SignalVehicleEvent(unit, vehicle, seat, 0x24b);
    }

    if (which & 2) {
        SignalVehicleEvent(unit, vehicle, seat, 0x24c);
    }

    if (which & 4) {
        ScriptEventsSignalUnitEvent(unit, 0x24d);
    }

    if (which & 8) {
        const char* power = "";

        if (seat) {
            if (auto rec = g_soundEntriesDB.GetRecord(seat->m_exitUISoundID)) {
                power = rec->m_name;
            }
        }

        int32_t count = 0;
        auto tokens = ScriptEventsGetUnitTokens(unit, &count);

        for (int32_t i = 0; i < count; i++) {
            FrameScript_SignalEvent(0x24e, "%s%s", tokens[i], power);
        }
    }
}

// ref: FUN_00749fb0
// The UI events a state change raises.
void SignalStateEvents(WOWGUID unit, const VehicleRec* vehicle, const VehicleSeatRec* seat, int32_t prevState, int32_t state) {
    switch (state) {
        case 0:
            SignalSeatEvents(unit, vehicle, seat, nullptr, prevState == 3 ? 0xc : 8);
            break;

        case 1:
            SignalSeatEvents(unit, vehicle, seat, nullptr, 1);
            break;

        case 2:
            if (prevState != 1) {
                SignalSeatEvents(unit, vehicle, seat, nullptr, 1);
            }

            break;

        case 3:
            SignalSeatEvents(unit, vehicle, seat, nullptr, (prevState != 0 && prevState != 3) ? 2 : 3);
            break;

        case 4:
            SignalSeatEvents(unit, vehicle, seat, nullptr, 4);
            break;

        case 5:
            if (prevState != 4) {
                SignalSeatEvents(unit, vehicle, seat, nullptr, 4);
            }

            break;
    }
}

} // namespace

// ---- the list --------------------------------------------------------------------------------------

// ref: FUN_0074a070
void CVehiclePassenger_C::Initialize() {
    s_rescues.SetCount(0);
    s_deferRescues = 0;
    s_head = nullptr;
    s_tail = nullptr;
}

// ref: FUN_0074a160
void CVehiclePassenger_C::Shutdown() {
    s_rescues.SetCount(0);
    s_deferRescues = 0;
    s_head = nullptr;
    s_tail = nullptr;
}

// ref: FUN_00749790
// ref: FUN_007488f0
CVehiclePassenger_C* CVehiclePassenger_C::Create(CGUnit_C* unit) {
    auto ride = STORM_NEW(CVehiclePassenger_C);
    ride->m_unit = unit;

    ride->m_prev = nullptr;
    ride->m_next = s_head;

    if (s_head) {
        s_head->m_prev = ride;
    } else {
        s_tail = ride;
    }

    s_head = ride;

    return ride;
}

// ref: FUN_00748950
void CVehiclePassenger_C::Free() {
    if (this == s_head) {
        s_head = this->m_next;

        if (s_head) {
            s_head->m_prev = nullptr;
        }
    } else if (this == s_tail) {
        s_tail = this->m_prev;

        if (s_tail) {
            s_tail->m_next = nullptr;
        }
    } else {
        this->m_prev->m_next = this->m_next;
        this->m_next->m_prev = this->m_prev;
    }

    this->FreeQueuedMove();
    this->~CVehiclePassenger_C();
    STORM_FREE(this);
}

// ref: FUN_00749a30
void CVehiclePassenger_C::Destroy() {
    if (this->m_flags & 0x800) {
        this->m_flags &= ~0x800u;

        if (this->m_seatIndex < 8) {
            auto vehicle = UnitPtr(this->m_vehicleGUID, 0x722);

            if (vehicle && vehicle->m_vehicle) {
                vehicle->m_vehicle->ClearStateBit(this->m_seatIndex);
            }
        }
    }

    if (auto vehicle = this->GetVehicle()) {
        vehicle->UpdateFreeSeats();
    }

    this->Free();
}

// ref: FUN_0074b130
void CVehiclePassenger_C::UpdateAll(uint32_t time) {
    for (auto ride = s_head; ride;) {
        auto next = ride->m_next;
        ride->Tick(time);
        ride = next;
    }
}

// ref: FUN_00747ae0
void CVehiclePassenger_C::ClearOverrideFacings() {
    for (auto ride = s_head; ride; ride = ride->m_next) {
        ride->m_flags &= ~0x1000u;
    }
}

// ref: FUN_00749c20
void CVehiclePassenger_C::QueueRescue(WOWGUID unit, WOWGUID vehicle, uint8_t seat, int32_t exitAnim) {
    RescueTransition rescue = { unit, vehicle, seat, static_cast<uint8_t>(exitAnim != 0) };
    s_rescues.Add(1, &rescue);
}

// ref: FUN_00749cb0
void CVehiclePassenger_C::ProcessRescues() {
    for (uint32_t i = 0; i < s_rescues.Count(); i++) {
        auto& rescue = s_rescues[i];
        auto unit = UnitPtr(rescue.unit, 0x3b0);

        if (unit && unit->m_vehiclePassenger) {
            unit->m_vehiclePassenger->BeginTransition(rescue.vehicle, rescue.seat, rescue.exitAnim);
        }
    }

    s_rescues.SetCount(0);
    s_deferRescues = 0;
}

// ref: FUN_00747980
void CVehiclePassenger_C::KeepSlot(CGUnit_C* unit, int32_t* keep, void* passengers) {
    (void)unit;
    (void)passengers;

    *keep = 1;
}

// ref: FUN_00749d50
void CVehiclePassenger_C::ReleaseSlotCallback(CGUnit_C* unit, int32_t* keep, void* passengers) {
    *keep = 0;

    auto ride = unit->m_vehiclePassenger;

    if (ride && !(ride->m_flags & 0x80)) {
        static_cast<TSGrowableArray<CVehiclePassenger_C*>*>(passengers)->Add(1, &ride);
    }
}

// ref: FUN_00747f40
// VEHICLE_UPDATE (0x290), then the vehicle's aim and pitch events.
//
// PARTIAL: the missile-trajectory display an aimed vehicle shows (FUN_006fe9b0 / FUN_006fbf00,
// UnitMissileTrajectory_C) is not ported.
void CVehiclePassenger_C::OnActivePlayerVehicleChanged(CGUnit_C* vehicle) {
    if (vehicle) {
        auto rec = vehicle->m_vehicle ? vehicle->m_vehicle->m_rec : nullptr;
        FrameScript_SignalEvent(0x290, nullptr);

        if (rec) {
            VehicleUpdateAngleEvents(vehicle, rec);
            return;
        }
    }

    VehicleUpdateAngleEvents(vehicle, nullptr);
}

// ref: FUN_00749ed0
void CVehiclePassenger_C::SignalActivePlayerSeat() {
    auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\Player_C.h", 0xa0));

    if (!player || !player->m_vehiclePassenger || player->m_vehiclePassenger->m_state == 0) {
        return;
    }

    auto vehicleUnit = UnitPtr(player->GetTransportGUID(), 0x73a);

    if (!vehicleUnit) {
        return;
    }

    auto rec = vehicleUnit->m_vehicle ? vehicleUnit->m_vehicle->m_rec : nullptr;
    uint8_t seat = player->m_localMove.m_transportSeat;

    if (!rec || seat >= 8) {
        return;
    }

    auto seatRec = g_vehicleSeatDB.GetRecord(rec->m_seatID[seat]);
    int32_t state = player->m_vehiclePassenger->m_state;

    if (!seatRec || state <= 0) {
        return;
    }

    if (state < 3) {
        SignalSeatEvents(player->GetGUID(), rec, seatRec, nullptr, 1);
    } else if (state == 3) {
        SignalSeatEvents(player->GetGUID(), rec, seatRec, nullptr, 3);
    }
}

// ref: FUN_007498e0
void CVehiclePassenger_C::OnTransportReplaced(CGUnit_C* unit) {
    WOWGUID transport = unit->GetTransportGUID();

    if (GuidIsUnit(transport)) {
        auto vehicle = UnitPtr(transport, 0xb4);
        uint8_t seat = unit->m_localMove.m_transportSeat;

        VehicleOnTransportChanged(unit, transport, seat, 1);

        if (unit->m_vehiclePassenger) {
            unit->m_vehiclePassenger->SetState(3, vehicle, seat, unit->m_vehiclePassenger->m_seat, Now(), transport);
        }

        return;
    }

    // PARTIAL: a player off its vehicle signals UNIT_EXITING_VEHICLE / UNIT_EXITED_VEHICLE (0x24d,
    // 0x24e) when a lookup the decompiler names CGUnit::Unit answers with flag 0x200 at its +8;
    // what that lookup returns is not identified, so the events are not raised here.
}

// ---- the seat's animation ------------------------------------------------------------------------

// ref: FUN_007487e0
bool CVehiclePassenger_C::IsRidingLiveVehicle() const {
    return (this->m_flags & 0x800) && 0 < this->m_unit->Unit()->health;
}

// ref: FUN_00747b20
int32_t CVehiclePassenger_C::GetSeatAnimation(const VehicleSeatRec* seat) const {
    if (!seat) {
        return 0x1FA;
    }

    switch (this->m_state) {
        case 1:
        case 2:
            if (seat->m_flags & 0x1) {
                if (!(this->m_flags & 0x2) && seat->m_enterAnimStart != -1) {
                    return seat->m_enterAnimStart;
                }

                if (seat->m_enterAnimLoop != -1) {
                    return seat->m_enterAnimLoop;
                }
            }

            break;

        case 3:
            if (seat->m_flags & 0x2) {
                if (!(this->m_flags & 0x2) && seat->m_rideAnimStart != -1) {
                    return seat->m_rideAnimStart;
                }

                if (seat->m_rideAnimLoop != -1) {
                    return seat->m_rideAnimLoop;
                }
            }

            break;

        case 4:
        case 5:
            if (this->m_unit->SeatAllowsExitAnimation(seat)) {
                if (!(this->m_flags & 0x2) && seat->m_exitAnimStart != -1) {
                    return seat->m_exitAnimStart;
                }

                if (seat->m_exitAnimLoop != -1) {
                    return seat->m_exitAnimLoop;
                }
            }

            break;
    }

    return 0x1FA;
}

// ref: FUN_00747bd0
int32_t CVehiclePassenger_C::GetSeatUpperAnimation(const VehicleSeatRec* seat) const {
    if (!seat || this->m_state != 3 || !(seat->m_flags & 0x4)) {
        return 0x1FA;
    }

    if (!(this->m_flags & 0x4) && seat->m_rideUpperAnimStart != -1) {
        return seat->m_rideUpperAnimStart;
    }

    if (seat->m_rideUpperAnimLoop != -1) {
        return seat->m_rideUpperAnimLoop;
    }

    return 0x1FA;
}

// ref: FUN_007484e0
void CVehiclePassenger_C::OnRiderSequenceDone(uint32_t boneId) {
    auto seat = this->m_seat;

    if (this->GetSeatAnimation(seat) != 0x1FA && seat && this->m_state == 3 && (seat->m_flags & 0x4)) {
        int32_t upper = -1;

        if (!(this->m_flags & 0x4)) {
            upper = seat->m_rideUpperAnimStart;
        }

        if (upper == -1) {
            upper = seat->m_rideUpperAnimLoop;
        }

        if (upper != -1 && upper != 0x1FA) {
            if (boneId != 0xFFFFFFFF && boneId != 0x1A) {
                this->m_flags |= 0x4;
            } else {
                this->m_flags |= 0x2;
            }

            return;
        }
    }

    this->m_flags |= 0x6;
}

// ref: FUN_00748560
bool CVehiclePassenger_C::GetRideAnimation(uint32_t allow, int32_t* out) const {
    (void)allow;

    if (((this->m_state != 0 && this->m_state != 3) || this->m_unit->Unit()->health < 1) && this->m_seat) {
        int32_t anim = this->GetSeatAnimation(this->m_seat);

        if (anim != 0x1FA) {
            *out = anim;
            return true;
        }
    }

    return false;
}

// ref: FUN_007485b0
bool CVehiclePassenger_C::GetRideUpperAnimation(uint32_t allow, int32_t* out) const {
    (void)allow;

    if (this->m_state != 3 && this->m_unit->Unit()->health >= 1) {
        return false;
    }

    auto seat = this->m_seat;

    if (!seat || this->m_state != 3 || !(seat->m_flags & 0x4)) {
        return false;
    }

    int32_t anim;

    if (!(this->m_flags & 0x4) && seat->m_rideUpperAnimStart != -1) {
        anim = seat->m_rideUpperAnimStart;
    } else {
        anim = seat->m_rideUpperAnimLoop;

        if (anim == -1) {
            return false;
        }
    }

    if (anim == 0x1FA) {
        return false;
    }

    *out = anim;

    return true;
}

// ref: FUN_00748620
void CVehiclePassenger_C::SyncToVehicle() {
    if (this->m_nextSeatIndex >= 8) {
        return;
    }

    auto vehicle = UnitPtr(this->m_nextVehicleGUID, 0x6ec);

    if (!vehicle || !vehicle->m_vehicle) {
        return;
    }

    vehicle->m_vehicle->SetStateBit(this->m_nextSeatIndex);
    this->m_flags |= 0x800;

    if (this->m_unit->Unit()->health <= 0) {
        return;
    }

    auto vehicleModel = vehicle->GetObjectModel();
    auto riderModel = this->m_unit->GetObjectModel();

    if (!vehicleModel || !riderModel || !vehicleModel->IsLoaded(0, 0) || !riderModel->IsLoaded(0, 0)) {
        return;
    }

    M2BoneSequenceState state;
    vehicleModel->GetBoneSequenceState(0xFFFFFFFF, &state);

    if (state.uint90 < 0x1fa) {
        riderModel->SetBoneSequence(0xFFFFFFFF, state.uint90, state.uint94, state.currentTime, state.speed, 1, 1);
    } else {
        riderModel->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
    }

    for (uint32_t bone = 0; bone < 0x23; bone++) {
        int32_t vehicleHas = vehicleModel->HasBone(bone);
        int32_t riderHas = riderModel->HasBone(bone);

        if (!riderHas) {
            continue;
        }

        if (vehicleHas) {
            vehicleModel->GetBoneSequenceState(bone, &state);

            if (state.uint90 < 0x1fa) {
                riderModel->SetBoneSequence(bone, state.uint90, state.uint94, state.currentTime, state.speed, 1, 1);
                continue;
            }
        }

        if (riderModel->GetBoneUint90(bone) != 0xFFFFFFFF) {
            riderModel->UnsetBoneSequence(bone, 0, 1);
        }
    }

    // The animation hold follows the vehicle's.
    if (vehicleModel->m_animationHeldTime != 0) {
        uint32_t held = riderModel->m_time;
        riderModel->m_animationHeldTime = held ? held : 1;
    } else {
        riderModel->m_animationHeldTime = 0;
    }
}

// ---- small pieces -----------------------------------------------------------------------------------

// ref: FUN_00747900
void CVehiclePassenger_C::ClearInputLock() {
    this->m_flags &= ~0x8u;
}

// ref: FUN_00747910
void CVehiclePassenger_C::FreeQueuedMove() {
    if (this->m_queuedMove) {
        delete this->m_queuedMove;
        this->m_queuedMove = nullptr;
    }
}

// ref: FUN_00748170
void CVehiclePassenger_C::QueueMove(CDataStore* msg) {
    this->m_queuedMove = new CDataStore();

    uint32_t remaining = msg->Size() - msg->Tell();
    std::vector<uint8_t> bytes(remaining);
    msg->GetArray(bytes.data(), remaining);
    this->m_queuedMove->PutData(bytes.data(), remaining);
    this->m_queuedMove->Finalize();
}

// ref: FUN_00747990
void CVehiclePassenger_C::PlayQueuedMove() {
    auto msg = this->m_queuedMove;
    this->m_queuedMove = nullptr;

    // SMSG_MONSTER_MOVE_TRANSPORT when the pending move is onto a transport, SMSG_MONSTER_MOVE else.
    int32_t opcode = this->m_pendingTarget ? 0x2ae : 0xdd;
    this->m_unit->OnMonsterMove(msg, opcode, this->m_pendingTarget, this->m_pendingSeat, 0);

    delete msg;
}

// ref: FUN_00747930
// An animation event names the pending exit: its digit (after '0') is 0 or the pending seat + 1.
bool CVehiclePassenger_C::MatchesExitEvent(uint32_t eventId) const {
    uint32_t flags = this->m_flags;
    uint8_t digit = static_cast<uint8_t>((eventId >> 0x18) - 0x30);

    return (flags & 0x200) && !(flags & 0x80) && (flags & 0x100)
        && (digit == 0 || this->m_pendingSeat == static_cast<uint8_t>(digit - 1));
}

// ref: FUN_007481e0
bool CVehiclePassenger_C::MatchesEnterEvent(uint32_t eventId) const {
    uint32_t flags = this->m_flags;
    uint8_t digit = static_cast<uint8_t>((eventId >> 0x18) - 0x30);

    return (flags & 0x200) && !(flags & 0x80) && !(flags & 0x100)
        && (digit == 0 || this->m_unit->m_localMove.m_transportSeat == static_cast<uint8_t>(digit - 1));
}

// ref: FUN_00748230
void CVehiclePassenger_C::ReleaseSlot() {
    if (!(this->m_flags & 0x200)) {
        return;
    }

    this->m_flags &= ~0x200u;

    auto root = UnitPtr(this->m_pendingRoot, 0x482);

    if (root && root->m_vehicle) {
        root->m_vehicle->RemoveSlot(this->m_unit->GetGUID());
    }

    if (this->m_queuedMove) {
        this->PlayQueuedMove();
    }
}

// ref: FUN_007479e0
void CVehiclePassenger_C::ClearAnimVehicle(WOWGUID vehicle, const VehicleSeatRec* seat) {
    if (vehicle != this->m_animVehicleGUID) {
        return;
    }

    uint32_t a = static_cast<uint32_t>(seat->m_vehicleRideAnimLoopBone);
    uint32_t b = static_cast<uint32_t>(seat->m_vehicleExitAnimBone);

    if ((0x22 < a ? 0x1a : a) == (0x22 < b ? 0x1a : b)) {
        this->m_animVehicleGUID = 0;
    }
}

// ref: FUN_00747a30
// The facing turns toward the target by the transition's progress, by the short way round.
void CVehiclePassenger_C::BlendFacing() {
    this->m_targetFacing = this->m_unit->GetFacing();

    while (this->m_facing > this->m_targetFacing + 3.1415927f) {
        this->m_targetFacing += 6.2831855f;
    }

    while (this->m_facing < this->m_targetFacing - 3.1415927f) {
        this->m_targetFacing -= 6.2831855f;
    }

    this->m_facing = this->m_targetFacing * this->m_progress + (1.0f - this->m_progress) * this->m_startFacing;
}

// ref: FUN_00747b00
void CVehiclePassenger_C::SetOverrideFacing(float facing) {
    this->m_flags |= 0x1000;
    this->m_overrideFacing = facing;
}

// ref: FUN_00747d50
bool CVehiclePassenger_C::IsMoving() const {
    return this->m_state == 1 || this->m_state == 2;
}

// ref: FUN_00747d70
// How far through the transition `time` is, eased as the seat asks: in (0x20 / 0x80), out (0x40 /
// 0x100), both (a cosine).
void CVehiclePassenger_C::UpdateProgress(uint32_t time, const VehicleSeatRec* seat) {
    if (!seat || !(this->m_flags & 0x1)) {
        this->m_progress = 0.0f;
        return;
    }

    float duration = static_cast<float>(static_cast<int32_t>(this->m_transitionEnd - this->m_transitionStart)) * 0.001f;

    if (duration < 0.0001f) {
        this->m_progress = 1.0f;
        return;
    }

    float t = (0.001f * static_cast<float>(static_cast<int32_t>(time - this->m_transitionStart))) / duration;
    this->m_progress = t < 0.0f ? 0.0f : (1.0f <= t ? 1.0f : t);

    uint32_t easeIn;
    uint32_t easeOut;

    if (this->m_state == 2) {
        easeIn = 0x20;
        easeOut = 0x40;
    } else if (this->m_state == 5) {
        easeIn = 0x80;
        easeOut = 0x100;
    } else {
        return;
    }

    uint32_t flags = static_cast<uint32_t>(seat->m_flags);

    if ((flags & easeIn) && (flags & easeOut)) {
        this->m_progress = 0.5f - std::cos(this->m_progress * 3.1415927f) * 0.5f;
    } else if (flags & easeIn) {
        this->m_progress = this->m_progress * this->m_progress;
    } else if (flags & easeOut) {
        float u = 1.0f - this->m_progress;
        this->m_progress = 1.0f - u * u;
    }
}

// ref: FUN_00747e90
C3Vector CVehiclePassenger_C::FromTransitionSpace(const C3Vector& local, const C3Vector& fallback) const {
    auto space = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(this->m_spaceGUID, TYPE_OBJECT, ".\\VehiclePassenger_C.cpp", 0x38d));

    if (!space) {
        return fallback;
    }

    if (space->IsA(TYPE_UNIT)) {
        C3Vector origin = UnitTransportPosition(static_cast<CGUnit_C*>(space));
        return { local.x + origin.x, origin.y + local.y, origin.z + local.z };
    }

    C44Matrix world;
    space->GetWorldMatrix(world);

    return local * world;
}

// ref: FUN_00747fa0
// The active player taking its seat cannot move for the moment it does.
void CVehiclePassenger_C::LockInput() {
    if (this->m_state != 3 || this->m_unit->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return;
    }

    uint32_t now = Now();
    auto input = InputControlGetActive();

    if (input) {
        // FUN_005fbf10
        uint32_t bits = input->m_unk04 & 0x1330;

        if (input->UnsetControlBit(bits, now, 0)) {
            input->UpdatePlayerMovement(now, 1);
        }

        input->UpdatePlayerMovement(now, 1);

        if (input->SetControlBit(bits, now)) {
            if (input->m_unk04 & 0x1030) {
                InputControlCancelChannel();
            }

            input->UpdatePlayerMovement(now, 1);
        }
    }

    this->m_flags |= 0x8;
    this->m_inputLockTime = now;
}

// ref: FUN_00747ff0
CGUnit_C* CVehiclePassenger_C::GetVehicleUnit() const {
    if (this->m_nextVehicleGUID) {
        if (auto unit = UnitPtr(this->m_nextVehicleGUID, 0x3f0)) {
            return unit;
        }
    }

    return UnitPtr(this->m_vehicleGUID, 0x3f4);
}

// ref: FUN_00748040
CGUnit_C* CVehiclePassenger_C::GetRootVehicleUnit() const {
    if (this->m_state != 0) {
        return UnitPtr(this->m_rootGUID, 0x3f9);
    }

    return UnitGetVehicleRoot(this->m_unit, nullptr);
}

// ref: FUN_00749060
CVehicle_C* CVehiclePassenger_C::GetVehicle() const {
    auto unit = this->GetVehicleUnit();

    return unit ? unit->m_vehicle : nullptr;
}

// ref: FUN_00748070
// A seat change the vehicle plays an animation for first: the change waits (0x200) for the
// vehicle's event or a timeout -- the seat's own delay with 0x10000000 (0x8000000 for an exit), up
// to 3 s, or 3 s.
void CVehiclePassenger_C::SetPending(WOWGUID target, uint8_t seat, const VehicleSeatRec* seatRec, int32_t exiting, CVehicle_C* root) {
    this->m_flags |= 0x200;
    this->m_pendingRoot = root->m_owner->GetGUID();
    this->m_pendingTarget = target;
    this->m_pendingSeat = seat;

    uint32_t flags = this->m_flags;
    uint32_t mask = exiting ? 0x8000000 : 0x10000000;

    if (static_cast<uint32_t>(seatRec->m_flags) & mask) {
        float delay = exiting ? seatRec->m_vehicleExitAnimDelay : seatRec->m_vehicleEnterAnimDelay;
        uint32_t ms = static_cast<uint32_t>(static_cast<int64_t>(std::nearbyint(delay * 1000.0f)));

        if (3000 < ms) {
            ms = 3000;
        }

        this->m_flags = flags | 0x80;
        this->m_pendingTime = Now() + ms;
        return;
    }

    this->m_flags = flags & ~0x80u;

    if (exiting) {
        this->m_flags |= 0x100;
    } else {
        this->m_flags &= ~0x100u;
    }

    this->m_pendingTime = Now() + 3000;
}

// ref: FUN_007490c0
bool CVehiclePassenger_C::CheckPendingTimeout() {
    if ((this->m_flags & 0x200) && static_cast<int32_t>(Now() - this->m_pendingTime) >= 0) {
        this->ReleaseSlot();
        return true;
    }

    return false;
}

// ref: FUN_007482a0
C3Vector CVehiclePassenger_C::GetTransitionPosition() const {
    switch (this->m_state) {
        case 1:
        case 4:
            if (!(this->m_flags & 0x1)) {
                return this->m_startTransport;
            }

            return this->FromTransitionSpace(this->m_startTransportLocal, this->m_startTransport);

        case 2:
        case 5: {
            if (!(this->m_flags & 0x1)) {
                return this->m_startTransport;
            }

            float t = this->m_progress;
            C3Vector here = UnitTransportPosition(this->m_unit);
            C3Vector from = this->FromTransitionSpace(this->m_startTransportLocal, this->m_startTransport);
            float u = 1.0f - this->m_progress;

            return { from.x * u + t * here.x, from.y * u + here.y * t, from.z * u + t * here.z };
        }

        case 3:
            return UnitTransportPosition(this->m_unit);

        default:
            return this->m_unit->GetPosition();
    }
}

// ref: FUN_00748400
// The rider's attachment point on its own model, the seat's offset from it.
void CVehiclePassenger_C::UpdateAttachOffset(const VehicleSeatRec* seat, CM2Model* model) {
    if (!seat || !model || !model->IsLoaded(0, 0)) {
        return;
    }

    uint32_t id = static_cast<uint32_t>(seat->m_passengerAttachmentID);

    if (model->HasAttachment(id)) {
        C3Vector position;
        model->GetAttachmentPosition(&position, id);

        this->m_flags |= 0x40 | 0x20;
        this->m_attachOffset = { -position.x, -position.y, -position.z };
        return;
    }

    this->m_attachOffset = { 0.0f, 0.0f, 0.0f };
    this->m_flags &= ~0x40u;
    this->m_flags |= 0x20;
}

// ref: FUN_007484c0
float CVehiclePassenger_C::GetFacing() const {
    if (this->m_flags & 0x1000) {
        return this->m_overrideFacing;
    }

    return this->m_unit->GetFacing();
}

// ---- where the seat is ---------------------------------------------------------------------------

// ref: FUN_007490f0
// The seat's matrix on the vehicle: turned by the rider's facing less the seat's yaw, then its pitch
// and roll; scaled to the rider (against the vehicle's attachment scale); placed at the seat's offset
// (or the rider's attachment offset, turned); then, when the vehicle has no such attachment, carried
// by the vehicle's own placement.
void CVehiclePassenger_C::BuildSeatMatrix(C44Matrix& matrix, CM2Model** model, CGUnit_C* vehicle, const VehicleSeatRec* seat,
                                          uint32_t attachment, float facing) {
    auto ride = vehicle->m_vehiclePassenger;
    float vehicleFacing = (ride && (ride->m_flags & 0x1000)) ? ride->m_overrideFacing
                                                             : (ride ? ride->m_unit->GetFacing() : vehicle->GetFacing());

    if (this->m_flags & 0x1000) {
        facing = this->m_overrideFacing - vehicleFacing;
    }

    int32_t hasAttachment = (*model)->HasAttachment(attachment);

    matrix.RotateAroundZ(facing - seat->m_passengerYaw);

    if (seat->m_passengerRoll != 0.0f) {
        matrix.RotateAroundX(seat->m_passengerRoll);
    }

    if (seat->m_passengerPitch != 0.0f) {
        matrix.RotateAroundY(seat->m_passengerPitch);
    }

    float scale;

    if (!hasAttachment) {
        matrix.Scale(this->m_unit->GetBaseScale());
        scale = vehicle->GetBaseScale();
    } else {
        auto transform = (*model)->GetAttachmentWorldTransform(attachment);
        float length = std::sqrt(transform.a0 * transform.a0 + transform.a1 * transform.a1 + transform.a2 * transform.a2);
        float own = this->m_unit->GetBaseScale();

        matrix.Scale(length <= 0.0001f ? own : own / length);
        scale = 1.0f;
    }

    C3Vector offset;

    if (!(this->m_flags & 0x20) || !(this->m_flags & 0x40)) {
        offset = { seat->m_attachmentOffset[0] * scale, seat->m_attachmentOffset[1] * scale, scale * seat->m_attachmentOffset[2] };
    } else {
        offset = this->m_attachOffset;
        C3Vector out;
        TransformPointInPlace(out, offset, matrix);
        offset.x = seat->m_attachmentOffset[0] * scale + offset.x;
        offset.y = seat->m_attachmentOffset[1] * scale + offset.y;
        offset.z = scale * seat->m_attachmentOffset[2] + offset.z;
    }

    matrix.d0 = offset.x;
    matrix.d1 = offset.y;
    matrix.d2 = offset.z;

    if (hasAttachment) {
        return;
    }

    C44Matrix placement;
    C3Vector position;

    if (!ride || ride->m_state == 0 || ride->m_state == 3) {
        position = vehicle->GetPosition();
    } else {
        // Animate, then the model's own placement through the scene's inverse view.
        auto world = (*model)->AnimateAndGetWorldMatrix();
        position = { world.d0, world.d1, world.d2 };
    }

    placement.d0 = position.x;
    placement.d1 = position.y;
    placement.d2 = position.z;

    float turn = vehicleFacing;

    if (ride && ride->m_state != 0 && ride->m_state != 3) {
        turn = ride->m_facing;
    }

    placement.RotateAroundZ(turn);
    matrix = matrix * placement;
    *model = nullptr;
}

// ref: FUN_007493b0
// The seat's place in the world: the seat matrix's origin, out through the vehicle's attachment
// (or the vehicle's own world matrix).
void CVehiclePassenger_C::GetSeatPosition(CGUnit_C* vehicle, CM2Model* model, const VehicleSeatRec* seat, C3Vector& out) {
    auto riderModel = this->m_unit->GetObjectModel();

    if (!(this->m_flags & 0x20)) {
        this->UpdateAttachOffset(seat, riderModel);
    }

    uint32_t attachment = static_cast<uint32_t>(SeatAttachment(seat));

    if (!(this->m_flags & 0x20) || !(this->m_flags & 0x40)) {
        out = { seat->m_attachmentOffset[0], seat->m_attachmentOffset[1], seat->m_attachmentOffset[2] };
    } else {
        C44Matrix matrix;
        float facing = (this->m_state == 4 || this->m_state == 5) ? this->m_startSmoothFacing : this->m_unit->m_smoothFacing;
        this->BuildSeatMatrix(matrix, &model, vehicle, seat, attachment, facing);
        out = { matrix.d0, matrix.d1, matrix.d2 };
    }

    if (!model) {
        return;
    }

    C3Vector tmp;

    if (!model->HasAttachment(attachment)) {
        auto ride = vehicle->m_vehiclePassenger;

        if (ride && ride->m_state != 0 && ride->m_state != 3) {
            auto world = model->AnimateAndGetWorldMatrix();
            TransformPointInPlace(tmp, out, world);
            return;
        }

        C44Matrix world;
        vehicle->GetWorldMatrix(world);
        TransformPointInPlace(tmp, out, world);
        return;
    }

    auto transform = model->GetAttachmentWorldTransform(attachment);
    TransformPointInPlace(tmp, out, transform);

    auto ride = vehicle->m_vehiclePassenger;

    if (ride && ride->m_state != 0 && ride->m_state != 3) {
        return;
    }

    // An attachment on a vehicle the frame has not placed yet: the point is taken back out of the
    // model's own placement into the vehicle's.
    auto world = model->AnimateAndGetWorldMatrix();
    float scale = vehicle->GetBaseScale();

    if (world.c2 <= 0.999f) {
        out.x -= world.d0;
        out.y -= world.d1;
        out.z -= world.d2;
    } else {
        auto inverse = world.AffineInverse(scale);
        TransformPointInPlace(tmp, out, inverse);
    }

    C44Matrix seatMatrix;
    UnitSeatMatrix(vehicle, seatMatrix);
    seatMatrix.Scale(scale);

    if (0.999f < world.c2) {
        TransformPointInPlace(tmp, out, seatMatrix);
        return;
    }

    out.x += seatMatrix.d0;
    out.y += seatMatrix.d1;
    out.z += seatMatrix.d2;
}

// ref: FUN_00749d80
C3Vector CVehiclePassenger_C::GetPosition() {
    auto seat = this->m_seat;

    if (this->m_state != 0 && this->m_nextVehicleGUID) {
        auto vehicle = UnitPtr(this->m_nextVehicleGUID, 0x573);

        if (vehicle) {
            auto model = vehicle->GetObjectModel();

            if (model && model->IsLoaded(0, 0) && seat) {
                C3Vector out = { 0.0f, 0.0f, 0.0f };
                this->GetSeatPosition(vehicle, model, seat, out);
                return out;
            }
        }
    }

    return this->m_unit->GetPosition();
}

// ref: FUN_00749e40
C3Vector CVehiclePassenger_C::GetTransitionTarget(CGUnit_C* vehicle, const VehicleSeatRec* seat) {
    auto model = vehicle ? vehicle->GetObjectModel() : nullptr;

    if (this->m_state == 2 && model && model->IsLoaded(0, 0) && seat) {
        C3Vector out = { 0.0f, 0.0f, 0.0f };
        this->GetSeatPosition(vehicle, model, seat, out);
        return out;
    }

    return this->m_unit->GetPosition();
}

// ---- the transition ---------------------------------------------------------------------------------

// ref: FUN_0074a200
// The move into or out of the seat is planned: where it starts in the transition space's terms, the
// facing to blend toward, how long it takes (the seat's pre-delay, or its speed over the distance
// clamped to its duration range), and the arc it follows (its gravity, scaled to stay inside the
// seat's arc heights).

void CVehiclePassenger_C::PlanTransition(CGUnit_C* vehicle, const VehicleSeatRec* seat, uint32_t time, C3Vector* target) {
    this->m_flags |= 0x1;

    if (!seat) {
        return;
    }

    auto space = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(this->m_spaceGUID, TYPE_OBJECT, ".\\VehiclePassenger_C.cpp", 0x2fe));

    if (!space) {
        this->m_startLocal = { 0.0f, 0.0f, 0.0f };
        this->m_startTransportLocal = { 0.0f, 0.0f, 0.0f };
    } else if (!space->IsA(TYPE_UNIT)) {
        C44Matrix world;
        space->GetWorldMatrix(world);
        auto inverse = world.AffineInverse();
        this->m_startLocal = this->m_startPosition * inverse;
        this->m_startTransportLocal = this->m_startTransport * inverse;
    } else {
        C3Vector origin = UnitTransportPosition(static_cast<CGUnit_C*>(space));
        this->m_startLocal = { this->m_startPosition.x - origin.x, this->m_startPosition.y - origin.y, this->m_startPosition.z - origin.z };
        this->m_startTransportLocal = { this->m_startTransport.x - origin.x, this->m_startTransport.y - origin.y, this->m_startTransport.z - origin.z };
    }

    if (this->m_state == 3) {
        return;
    }

    this->m_targetFacing = this->m_unit->GetFacing();

    while (this->m_targetFacing + 3.1415927f < this->m_facing) {
        this->m_targetFacing += 6.2831855f;
    }

    while (this->m_facing < this->m_targetFacing - 3.1415927f) {
        this->m_targetFacing -= 6.2831855f;
    }

    *target = this->GetTransitionTarget(vehicle, seat);

    int32_t state = this->m_state;

    if (state == 1 || state == 4) {
        float delay = state == 1 ? seat->m_enterPreDelay : seat->m_exitPreDelay;

        if (10.0f < delay) {
            delay = 10.0f;
        }

        this->m_transitionEnd = this->m_transitionStart - static_cast<int32_t>(std::nearbyint(delay * -1000.0f));
        return;
    }

    float speed;
    float minDuration;
    float maxDuration;
    float gravity;
    float minArc;
    float maxArc;

    if (state == 2) {
        gravity = seat->m_enterGravity;
        minDuration = seat->m_enterMinDuration;
        maxDuration = seat->m_enterMaxDuration;
        speed = seat->m_enterSpeed;
        minArc = seat->m_enterMinArcHeight;
        maxArc = seat->m_enterMaxArcHeight;
    } else {
        gravity = seat->m_exitGravity;
        minDuration = seat->m_exitMinDuration;
        maxDuration = seat->m_exitMaxDuration;
        speed = seat->m_exitSpeed;
        minArc = seat->m_exitMinArcHeight;
        maxArc = seat->m_exitMaxArcHeight;
    }

    this->m_gravity = gravity;
    (void)speed;

    float duration;

    if (0.0001f <= maxDuration) {
        duration = 10.0f < maxDuration ? 10.0f : maxDuration;
    } else {
        duration = 1.5f;
    }

    float cap = duration;

    if (state == 2 && vehicle) {
        // Into a seat on a moving vehicle: the distance closes along the vehicle's direction.
        auto& move = vehicle->m_localMove;
        C3Vector velocity = { move.m_direction.x * move.m_currentSpeed, move.m_direction.y * move.m_currentSpeed, move.m_currentSpeed * move.m_direction.z };
        C3Vector toward = { target->x - this->m_startPosition.x, target->y - this->m_startPosition.y, target->z - this->m_startPosition.z };
        float distance = std::sqrt(toward.x * toward.x + toward.z * toward.z + toward.y * toward.y);

        if (distance <= 0.0001f) {
            duration = 0.0f;
            this->m_transitionEnd = this->m_transitionStart;
        } else {
            toward = toward * (1.0f / distance);
            float closing = seat->m_enterSpeed - (velocity.x * toward.x + velocity.z * toward.z + velocity.y * toward.y);

            if (0.0001f < closing) {
                float t = distance / closing;
                // FUN_00497a90: clamped to the seat's duration range.
                duration = t < minDuration ? minDuration : (cap <= t ? cap : t);
            }

            this->m_transitionEnd = this->m_transitionStart - static_cast<int32_t>(std::nearbyint(duration * -1000.0f));
        }
    } else {
        C3Vector here = this->m_unit->GetPosition();
        float dx = here.x - this->m_startPosition.x;
        float dy = here.y - this->m_startPosition.y;
        float dz = here.z - this->m_startPosition.z;
        float t = std::sqrt(dz * dz + dy * dy + dx * dx) / seat->m_exitSpeed;

        duration = t < minDuration ? minDuration : (cap <= t ? cap : t);
        this->m_transitionEnd = this->m_transitionStart - static_cast<int32_t>(std::nearbyint(duration * -1000.0f));
    }

    if (std::fabs(this->m_gravity) < 0.0001f) {
        if (0.0f < minArc || maxArc < 0.0f) {
            this->m_gravity = 0.1f;
        }
    }

    float scale = 1.0f;

    if (0.0001f < duration) {
        float peak = this->m_gravity * duration * duration * 0.125f;

        if (peak < minArc) {
            scale = minArc / peak;
        } else if (maxArc < peak) {
            scale = maxArc / peak;
        }
    }

    this->m_arcScale = scale;

    // The rider's camera moves over the same time.
    if (this->m_unit->m_vehicleCamera) {
        this->m_unit->m_vehicleCamera->SetTransitionTime(time, static_cast<int32_t>(std::nearbyint(duration * 1000.0f)));
    }

    this->UpdateProgress(time, seat);

    // A rider coming in fades in while it moves.
    if (vehicle && vehicle->m_fadeDuration != 0 && this->IsMoving()) {
        int32_t span = static_cast<int32_t>(this->m_transitionEnd - this->m_transitionStart);
        vehicle->SetAlpha(vehicle->GetFadeInAlpha(), static_cast<uint32_t>(span));
    }
}

// ref: FUN_0074a7f0
// The rider's model placed for the ride: unseated it is the unit's own placement; aboard it hangs
// on the vehicle's seat attachment (or the seat's matrix); in a move it follows the arc from where
// it started toward the seat.
void CVehiclePassenger_C::PlaceModel() {
    if ((this->m_state == 2 || this->m_state == 5) && !(this->m_flags & 0x1)) {
        return;
    }

    WOWGUID vehicleGUID = this->m_nextVehicleGUID ? this->m_nextVehicleGUID : this->m_vehicleGUID;
    auto seat = this->m_seat;
    auto vehicle = UnitPtr(vehicleGUID, this->m_nextVehicleGUID ? 0x4bf : 0x4c1);

    this->m_flags &= ~0x10u;

    auto model = this->m_unit->GetObjectModel();

    if (!model) {
        return;
    }

    if (this->m_state == 0) {
        if (model->m_attachParent) {
            model->DetachFromParent();
        }

        C3Vector up = this->m_unit->m_localMove.GetWorldUp();
        model->SetWorldTransform(this->m_unit->GetPosition(), this->m_unit->GetFacing(), this->m_unit->GetBaseScale(), &up);
        return;
    }

    C3Vector start = (this->m_state == 3 || this->m_spaceGUID == 0) ? this->m_startPosition
                                                                    : this->FromTransitionSpace(this->m_startLocal, this->m_startPosition);

    C44Matrix matrix;
    C3Vector position = { 0.0f, 0.0f, 0.0f };
    C3Vector up = { 0.0f, 0.0f, 0.0f };
    float facing = 0.0f;
    float scale = 1.0f;
    int32_t useMatrix = 0;
    CM2Model* parent = nullptr;
    uint32_t parentAttachment = 0;

    if (!seat || this->m_state == 0) {
        position = this->m_unit->GetPosition();
        facing = this->m_unit->GetFacing();
        scale = this->m_unit->GetBaseScale();
        up = this->m_unit->m_localMove.GetWorldUp();
    } else if (!this->IsMoving() && this->m_state != 5) {
        // Aboard (3) or waiting (1, 4).
        if (this->m_state == 3) {
            if (!vehicle) {
                return;
            }

            if (!(this->m_flags & 0x20)) {
                this->UpdateAttachOffset(seat, model);
            }

            auto vehicleModel = vehicle->GetObjectModel();

            if (vehicleModel && vehicleModel->IsLoaded(0, 0)) {
                parentAttachment = static_cast<uint32_t>(SeatAttachment(seat));
                parent = vehicleModel;
                this->BuildSeatMatrix(matrix, &parent, vehicle, seat, parentAttachment, this->m_unit->m_smoothFacing);
                useMatrix = 1;
            } else {
                position = this->m_unit->GetPosition();
                facing = this->m_unit->GetFacing();
                scale = this->m_unit->GetBaseScale();
                up = this->m_unit->m_localMove.GetWorldUp();
            }
        } else {
            if (!(this->m_flags & 0x20)) {
                this->UpdateAttachOffset(seat, model);
            }

            auto previous = this->m_vehicleGUID ? UnitPtr(this->m_vehicleGUID, 0x53c) : nullptr;
            auto previousSeat = previous && previous->m_vehicle ? previous->m_vehicle->GetSeatRec(this->m_seatIndex) : nullptr;
            auto previousModel = previous ? previous->GetObjectModel() : nullptr;

            if (previousSeat && previousSeat->m_attachmentID >= 0 && previousModel && previousModel->IsLoaded(0, 0)) {
                parentAttachment = static_cast<uint32_t>(static_cast<uint32_t>(seat->m_attachmentID) < 0x16 ? s_seatAttachments[previousSeat->m_attachmentID] : -1);
                parent = previousModel;
                this->BuildSeatMatrix(matrix, &parent, previous, previousSeat, parentAttachment, this->m_startSmoothFacing);
                useMatrix = 1;
            } else {
                facing = this->m_startFacing;
                position = start;
                scale = this->m_unit->GetBaseScale();
                up = this->m_unit->m_localMove.GetWorldUp();
            }
        }
    } else {
        // Moving: along the arc, lerped toward the seat.
        float lift = 0.0f;

        if (0.0001f < std::fabs(this->m_gravity)) {
            float total = static_cast<float>(static_cast<int32_t>(this->m_transitionEnd - this->m_transitionStart)) * 0.001f;
            float along = this->m_progress * total;
            float half = this->m_gravity * along * 0.5f;
            lift = (half * total - half * along) * this->m_arcScale;
        }

        C3Vector to = this->GetTransitionTarget(vehicle, seat);
        float t = this->m_progress;
        float u = 1.0f - t;

        position = { start.x * u + to.x * t, start.y * u + to.y * t, start.z * u + to.z * t + lift };
        facing = this->m_facing;
        scale = this->m_unit->GetBaseScale();
        up = { 0.0f, 0.0f, 1.0f };
    }

    if (model->m_attachParent != parent || model->m_attachId != parentAttachment) {
        if (model->m_attachParent) {
            model->DetachFromParent();
        }

        if (parent) {
            model->AttachToParent(parent, parentAttachment, nullptr, 0);
        }
    }

    if (!useMatrix || !parent) {
        if (useMatrix) {
            model->m_flag8000 = 1;
            model->matrixB4 = matrix;
        } else {
            model->SetWorldTransform(position, facing, scale, &up);
        }
    } else {
        model->m_flag8000 = 1;
        model->matrixB4 = matrix;
    }

    if (this->m_unit->m_postInited) {
        this->m_flags |= 0x10;
    } else {
        this->m_flags &= ~0x10u;
    }
}

// ref: FUN_007489c0
// The ride moves to `state`: the animation link to the vehicle is released, the start of a move is
// recorded, the seat is taken (3), the model detached when the ride ends (0), the rider's animation
// and the camera's target follow, the vehicle's ride animation and the rider's input are updated, and
// the vehicle's reach changes.
void CVehiclePassenger_C::SetState(int32_t state, CGUnit_C* vehicle, uint8_t seat, const VehicleSeatRec* seatRec, uint32_t time,
                                   WOWGUID space) {
    if (this->m_flags & 0x800) {
        this->m_flags &= ~0x800u;

        if (this->m_seatIndex < 8) {
            auto previous = UnitPtr(this->m_vehicleGUID, 0x722);

            if (previous && previous->m_vehicle) {
                previous->m_vehicle->ClearStateBit(this->m_seatIndex);
            }
        }

        this->m_unit->UpdateAnimation(0, 0xFFFFFFFF);
    }

    if (this->m_state == 3 && this->m_animVehicleGUID) {
        auto animVehicle = UnitPtr(this->m_animVehicleGUID, 0x16b);

        if (animVehicle && animVehicle->m_vehicle) {
            if (vehicle && vehicle->m_vehicle) {
                auto heldSeat = vehicle->m_vehicle->GetSeatRec(this->m_seatIndex);

                if (heldSeat && (heldSeat->m_flags & 0x20000)) {
                    vehicle->m_vehicle->RemoveSlotSequence(static_cast<uint32_t>(heldSeat->m_vehicleRideAnimLoopBone), this->m_unit->GetGUID());
                }
            }

            this->m_animVehicleGUID = 0;
        }
    }

    this->m_spaceGUID = (state == 1 || state == 4) ? this->m_unit->GetTransportGUID() : space;

    switch (state) {
        case 1:
        case 2:
        case 4:
        case 5:
            this->m_startPosition = this->m_unit->GetModelWorldPosition();
            this->m_startTransport = UnitTransportPosition(this->m_unit);

            if (this->m_state == 0 || this->m_state == 3) {
                float facing = this->m_unit->GetWorldSmoothFacing();
                this->m_facing = facing;
                this->m_startFacing = facing;
                this->m_startSmoothFacing = this->m_unit->m_smoothFacing;
            } else {
                this->m_startSmoothFacing = this->m_facing;
                this->m_startFacing = this->m_facing;
            }

            break;

        case 3: {
            WOWGUID guid = vehicle ? vehicle->GetGUID() : 0;
            this->m_nextVehicleGUID = guid;
            this->m_vehicleGUID = guid;
            this->m_nextSeatIndex = seat;
            this->m_seatIndex = seat;
            break;
        }
    }

    if (state == 3 && seatRec && (seatRec->m_flagsB & 0x10000)) {
        this->SyncToVehicle();
    }

    this->m_flags &= ~0x8u;

    auto model = this->m_unit->GetObjectModel();
    bool placed = this->m_unit->m_postInited && model && model->m_flag8000;

    if (placed) {
        this->m_flags |= 0x10;
    } else {
        this->m_flags &= ~0x10u;
    }

    this->m_flags &= ~0x20u;

    if (state == 0) {
        if (model && model->m_attachParent) {
            model->DetachFromParent();
        }

        C3Vector up = this->m_unit->m_localMove.GetWorldUp();
        model->SetWorldTransform(this->m_unit->GetPosition(), this->m_unit->GetFacing(), this->m_unit->GetBaseScale(), &up);
    }

    if (model) {
        // The rider's shadow is drawn only while it is not seated in a way that hides it.
        bool shadow = !(this->m_unit->Unit()->flags & 0x2000000) && (!seatRec || !(seatRec->m_flags & 0x100000)) && state != 0;
        model->m_flag80000 = shadow ? 1 : 0;
    }

    this->m_transitionStart = time;

    bool exitAnimation;

    if (this->m_unit->Unit()->health < 1) {
        // A dead rider coming off plays its death.
        if (this->m_state != 5 && (state == 5 || state == 0)) {
            UnitOnLeftVehicle(this->m_unit);
        }

        exitAnimation = this->m_state == 5;
    } else {
        UnitResetSequencesForRide(this->m_unit);
        exitAnimation = this->m_state == 5;
    }

    if (exitAnimation) {
        if (state == 0 && 0 < this->m_unit->Unit()->health) {
            int32_t anim = -1;

            if (seatRec) {
                anim = seatRec->m_exitAnimEnd;

                if (anim == 0x27 && (this->m_unit->m_localMove.m_moveFlags & 0xf)) {
                    anim = 0xbb;
                }
            }

            UnitSetVehicleExitAnimation(this->m_unit, seatRec, anim);
        }
    }

    {
        int32_t before = this->m_state;
        bool keepStart = (before == 1 && state == 2) || before == 4;
        int32_t enteringFromNothing = (before == 0 && state == 3) ? 1 : 0;

        this->m_state = state;

        CGUnit_C* root = nullptr;

        if (vehicle) {
            auto ride = vehicle->m_vehiclePassenger;
            root = (ride && ride->m_state != 0) ? UnitPtr(ride->m_rootGUID, 0x3f9) : UnitGetVehicleRoot(vehicle, nullptr);
        }

        if (seatRec && (seatRec->m_flags & 0x200)) {
            int32_t now = this->m_state;

            if ((now == 1 || now == 2) && (this->m_flags & 0x400)) {
                auto previous = UnitPtr(this->m_vehicleGUID, 0x1f5);
                auto previousRoot = previous ? UnitGetRootVehicle(previous) : nullptr;

                if (previousRoot == root) {
                    this->m_flags |= 0x400;
                } else {
                    this->m_flags &= ~0x400u;
                }
            } else if (now == 3) {
                this->m_flags |= 0x400;
            } else {
                this->m_flags &= ~0x400u;
            }
        } else {
            this->m_flags &= ~0x400u;
        }

        if (this->m_state != 0 && 0 < this->m_unit->Unit()->health) {
            if (!keepStart) {
                this->m_flags &= ~0x6u;
            }

            this->m_unit->UpdateAnimation(enteringFromNothing, 0xFFFFFFFF);

            if (!keepStart) {
                this->m_flags &= ~0x6u;
            }
        }

        WOWGUID riderGUID = this->m_unit->GetGUID();
        WOWGUID player = ClntObjMgrGetActivePlayer();

        if (riderGUID == player || (vehicle && vehicle->GetGUID() == player)) {
            CGUnit_C* who = riderGUID == player ? this->m_unit : vehicle;
            VehicleUpdateAngleEvents(who, who->m_vehicle ? who->m_vehicle->m_rec : nullptr);
        }

        auto camera = CGWorldFrame::GetActiveCamera();

        if (camera && riderGUID == player) {
            if (camera->GetTarget() == riderGUID || camera->GetTarget() == this->m_vehicleGUID) {
                CGObject_C* target = this->m_unit;
                int32_t now = this->m_state;

                if ((now == 1 || now == 2 || now == 3) && vehicle && seatRec && (seatRec->m_flags & 0x800)) {
                    target = vehicle;
                }

                camera->SetTargetObject(target, 0);
            }
        }

        this->m_flags &= ~0x1u;

        auto vehicleObject = vehicle ? vehicle->m_vehicle : nullptr;

        if (this->m_state == 3 && vehicleObject && seatRec && (seatRec->m_flags & 0x20000) && seatRec->m_vehicleRideAnimLoop < 0x1fa) {
            auto rootVehicle = vehicleObject->GetRoot();

            if (rootVehicle->PlaySlotSequence(static_cast<uint32_t>(seatRec->m_vehicleRideAnimLoopBone), static_cast<uint32_t>(seatRec->m_vehicleRideAnimLoop))) {
                rootVehicle->AddSlot(riderGUID, static_cast<uint32_t>(seatRec->m_vehicleRideAnimLoopBone), &CVehiclePassenger_C::KeepSlot);
                this->m_animVehicleGUID = rootVehicle->m_owner->GetGUID();
            }
        }

        if (this->m_unit->IsA(TYPE_PLAYER) && (this->m_state == 0 || this->m_state == 3)) {
            if (auto input = InputControlGetActive()) {
                input->UpdatePlayerMovement(time, 1);
            }
        }

        if (vehicleObject && (before == 3 || this->m_state == 3)) {
            vehicleObject->UpdatePassengerRadius();
        }
    }

    this->UpdateProgress(time, seatRec);
}

// ref: FUN_00749aa0
// A seat change arrives: the ride leaves what it was on and heads for `vehicle` (a unit) at `seat`,
// or off; the state it starts in is the move it takes (an enter with a pre-delay waits, one without
// moves at once, an exit likewise; nothing animated starts aboard or off).
void CVehiclePassenger_C::BeginTransition(WOWGUID vehicleGUID, uint8_t seat, int32_t exitAnim) {
    this->ReleaseSlot();

    switch (this->m_state) {
        case 0:
        case 5:
            this->m_vehicleGUID = 0;
            this->m_seatIndex = 0xFF;
            break;

        case 2:
        case 3:
            this->m_vehicleGUID = this->m_nextVehicleGUID;
            this->m_seatIndex = this->m_nextSeatIndex;
            break;

        default:
            break;
    }

    bool toUnit = GuidIsUnit(vehicleGUID);

    this->m_flags &= ~0x1u;

    auto seatRec = this->m_seat;
    this->m_nextVehicleGUID = toUnit ? vehicleGUID : 0;
    this->m_nextSeatIndex = seat;

    int32_t state = toUnit ? 3 : 0;
    WOWGUID target = vehicleGUID;

    if (exitAnim && seatRec) {
        if (toUnit) {
            if ((seatRec->m_flags & 0x1) && 0.0001f < seatRec->m_enterSpeed) {
                state = seatRec->m_enterPreDelay <= 0.0001f ? 2 : 1;
            }
        } else if (this->m_unit->SeatAllowsExitAnimation(seatRec) && 0.0001f < seatRec->m_exitSpeed) {
            state = seatRec->m_exitPreDelay <= 0.0001f ? 5 : 4;
            target = this->m_vehicleGUID;
        }
    } else if (!toUnit) {
        target = this->m_vehicleGUID;
    }

    auto vehicle = UnitPtr(target, 0x159);
    this->SetState(state, vehicle, seat, seatRec, Now(), vehicleGUID);
}

// ref: FUN_0074ad70
// One step of a move: once planned, past the distance a move may be from where it started it is cut
// short to its end; the UI hears of a state change; the root's free seats are recounted.
int32_t CVehiclePassenger_C::UpdateTransition(CGUnit_C* vehicle, const VehicleSeatRec* seat, uint32_t time) {
    C3Vector target = { 0.0f, 0.0f, 0.0f };

    if (this->m_state != 0) {
        this->PlanTransition(vehicle, seat, time, &target);
    }

    int32_t state = this->m_state;

    if (state != 0 && state != 3) {
        float dx = target.x - this->m_startPosition.x;
        float dy = target.y - this->m_startPosition.y;
        float dz = target.z - this->m_startPosition.z;

        if (22500.0f < dz * dz + dy * dy + dx * dx) {
            this->SetState((state == 1 || state == 2) ? 3 : 0, vehicle, this->m_nextSeatIndex, seat, time, this->m_unit->GetTransportGUID());
            this->m_flags |= 0x1;
        }
    }

    WOWGUID unit = this->m_unit->GetGUID();

    if (unit == ClntObjMgrGetActivePlayer() || CGPartyInfo::IsMemberOrPet(unit) || CGRaidInfo::IsMemberOrPet(unit)) {
        SignalStateEvents(unit, this->m_vehicleRec, seat, this->m_prevState, this->m_state);
    }

    if (vehicle) {
        auto ride = vehicle->m_vehiclePassenger;
        auto root = (ride && ride->m_state != 0) ? UnitPtr(ride->m_rootGUID, 0x3f9) : UnitGetVehicleRoot(vehicle, nullptr);

        if (root && (this->m_state == 0 || this->m_prevState == 0 || this->m_state == 3 || this->m_prevState == 3)) {
            auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\Player_C.h", 0xa0));

            if (player && UnitGetRootVehicle(player) == root) {
                FrameScript_SignalEvent(0x24f, nullptr);
            }
        }
    }

    this->m_prevState = this->m_state;

    if (vehicle && vehicle->m_vehicle) {
        vehicle->m_vehicle->UpdateFreeSeats();
    }

    return this->m_state != 0 || (this->m_flags & 0x200);
}

// ref: FUN_0074af70
// The ride's states run on: a wait gives way to its move when its time is up, a move ends aboard
// (or off) when its time is up and otherwise blends its facing; an input lock lapses after 2 s.
int32_t CVehiclePassenger_C::Update(CGUnit_C* vehicle, const VehicleSeatRec* seat, uint32_t time) {
    if (!(this->m_flags & 0x1) && !this->UpdateTransition(vehicle, seat, time)) {
        return 0;
    }

    if (this->m_state == 1 && static_cast<int32_t>(time - this->m_transitionEnd) >= 0) {
        this->SetState(2, vehicle, this->m_nextSeatIndex, seat, time, this->m_unit->GetTransportGUID());

        if (!this->UpdateTransition(vehicle, seat, time)) {
            return 0;
        }
    }

    if (this->m_state == 2) {
        if (static_cast<int32_t>(time - this->m_transitionEnd) < 0) {
            this->BlendFacing();
        } else {
            this->SetState(3, vehicle, this->m_nextSeatIndex, seat, time, this->m_unit->GetTransportGUID());
        }
    }

    if (this->m_state == 4 && static_cast<int32_t>(time - this->m_transitionEnd) >= 0) {
        this->SetState(5, vehicle, 0xff, seat, time, this->m_unit->GetTransportGUID());

        if (!this->UpdateTransition(vehicle, seat, time)) {
            return 0;
        }
    }

    if (this->m_state == 5) {
        if (static_cast<int32_t>(time - this->m_transitionEnd) < 0) {
            this->BlendFacing();
        } else {
            this->SetState(0, vehicle, 0xff, seat, time, this->m_unit->GetTransportGUID());
        }
    }

    if ((this->m_flags & 0x8) && static_cast<int32_t>(time - this->m_inputLockTime - 2000) >= 0) {
        this->m_flags &= ~0x8u;
    }

    return 1;
}

// ref: FUN_0074b0b0
void CVehiclePassenger_C::Tick(uint32_t time) {
    WOWGUID guid = this->m_nextVehicleGUID ? this->m_nextVehicleGUID : this->m_vehicleGUID;
    auto vehicle = UnitPtr(guid, this->m_nextVehicleGUID ? 0xde : 0xe0);
    auto seat = this->m_seat;

    this->UpdateProgress(time, seat);

    if (!this->Update(vehicle, seat, time)) {
        // FUN_0074b8a0
        this->m_unit->m_vehiclePassenger = nullptr;
        this->Destroy();
    }
}

// ref: FUN_0074b160
void CVehiclePassenger_C::SetRootVehicle(WOWGUID root) {
    this->m_rootGUID = root;

    if (!root) {
        root = this->m_unit->GetGUID();
    }

    auto vehicle = this->m_unit->m_vehicle;

    if (!vehicle) {
        return;
    }

    for (auto passenger = vehicle->m_passengers.Head(); passenger; passenger = vehicle->m_passengers.Next(passenger)) {
        auto unit = UnitPtr(passenger->m_guid, 0x408);

        if (unit && unit->m_vehiclePassenger) {
            unit->m_vehiclePassenger->SetRootVehicle(root);
        }
    }
}

// ref: FUN_0074b200
// The rider's transport changed to `transport` at `seat`: the seat and vehicle rows are taken, the
// chain's root is noted for every passenger below, and the move begins -- or, while the units are
// being stepped, waits as a rescue.
void CVehiclePassenger_C::OnTransportChanged(WOWGUID transport, uint8_t seat, CGUnit_C* vehicle, int32_t immediate) {
    WOWGUID root = 0;

    if (transport) {
        if (vehicle) {
            this->m_vehicleRec = vehicle->m_vehicle ? vehicle->m_vehicle->m_rec : nullptr;
            this->m_seat = vehicle->m_vehicle ? vehicle->m_vehicle->GetSeatRec(seat) : nullptr;
        } else {
            this->m_vehicleRec = nullptr;
            this->m_seat = nullptr;
        }
    }

    if (vehicle) {
        if (auto rootUnit = UnitGetVehicleRoot(vehicle, nullptr)) {
            root = rootUnit->GetGUID();
        }
    }

    this->SetRootVehicle(root);

    int32_t exitAnim = UnitAllowsSeatAnimation(this->m_unit, this->m_seat, GuidIsUnit(transport) ? 0 : 1);

    if (this->m_vehicleGUID && !vehicle && s_deferRescues) {
        CVehiclePassenger_C::QueueRescue(this->m_unit->GetGUID(), transport, seat, exitAnim);
        return;
    }

    if (!immediate) {
        this->BeginTransition(transport, seat, exitAnim);
    }
}
