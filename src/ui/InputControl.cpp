#include "ui/InputControl.hpp"
#include "db/Db.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/Spell_C.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGGameUI.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "util/Lua.hpp"
#include "console/Console.hpp"
#include "console/CVar.hpp"
#include <common/Time.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <cmath>
#include <new>

CInputControl* s_inputControl;              // reference DAT_00c24954
// Handed to the missile trajectory code as its fallback value. Written only by code that is not
// ported yet (FUN_005f9550, FUN_005f9f10).
static float s_storedFloat;                 // ref: DAT_00c24958
static CVar* s_cinematicJoystickCvar;       // DAT_00c2495c
static CVar* s_enableWowMouseCvar;          // DAT_00c24960
static int32_t s_joystick = -1;             // DAT_00ad192c: the open joystick, -1 for none

// ref: FUN_005fd1d0
CInputControl::CInputControl() {
    // TODO FUN_005fcd70: constructs the hash table at +0x1c
    this->m_lastTimeMs = OsGetAsyncTimeMs();
}

// ref: FUN_005f95d0
CInputControl* InputControlGetActive() {
    return s_inputControl;
}

// ref: FUN_005f95e0
void CInputControl::ClearFlagBits16To19() {
    this->m_unk04 &= 0xFFF0FFFF;
}

// ref: FUN_005f95f0
void CInputControl::ClearFlagBits12And16() {
    this->m_unk04 &= 0xFFFEEFFF;
}

// ref: FUN_005f96e0
float InputControlGetStoredFloat() {
    return s_storedFloat;
}

// ref: FUN_005f9850
// True when the control word at +0x04 holds none of the masked bits (0x300 alone included).
int32_t CInputControl::IsIdle() {
    uint32_t flags = this->m_unk04;

    if ((flags & 0x1030) == 0
        && (flags & 0xC0) == 0
        && ((flags & 0x2000001) == 0 || (flags & 0x300) == 0)
        && ((flags & 0x300) == 0 || (flags & 0x2000001) != 0)
        && (flags & 0x1E00000) == 0
    ) {
        return 1;
    }

    return 0;
}

// ref: FUN_005f96f0
// Creates or destroys the SteelSeries mouse driver object as the CVar flips.
void CInputControl::SetWowMouseEnabled(bool enabled) {
    if (!this->m_wowMouse) {
        if (enabled) {
            // TODO FUN_008c2f50: allocate (8 bytes, InputControl.cpp:0xa70) and construct the driver
        }
    } else if (!enabled) {
        // TODO the driver's deleting destructor (vtable slot 0, flag 1)
        this->m_wowMouse = nullptr;
    }
}

// ref: FUN_005f9c80
bool JoystickCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (*value != '1') {
        if (s_joystick != -1) {
            // TODO FUN_00870840(s_joystick): close the joystick
            s_joystick = -1;
        }

        return true;
    }

    if (s_joystick == -1) {
        // TODO FUN_00870730(0): open the first joystick; -1 when there is none
        return true;
    }

    // TODO FUN_00870890: the joystick's name, reported as "Joystick: %s" (FUN_007653b0); then the
    // axis binding string is parsed (FUN_005f9890), applied (FUN_005f99f0) and released (FUN_00814d60)
    return true;
}

// ref: FUN_005fa9b0
bool EnableWowMouseCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t enabled = 0;

    if (value) {
        enabled = SStrToInt(value);
    }

    s_inputControl->SetWowMouseEnabled(enabled != 0);

    return true;
}

// ref: FUN_005fd2c0
void InputControlInitialize() {
    CVar::Register("Joystick", "enable joystick control", 0x0, "0", &JoystickCallback, DEFAULT, false, nullptr, false);
    s_cinematicJoystickCvar = CVar::Register("CinematicJoystick", "enable cinematic joystick control", 0x0, "0", nullptr, DEFAULT, false, nullptr, false);

    auto m = SMemAlloc(sizeof(CInputControl), __FILE__, __LINE__, 0x0);
    s_inputControl = m ? new (m) CInputControl() : nullptr;

    s_enableWowMouseCvar = CVar::Register("enableWowMouse", "Enable Steelseries World of Warcraft Mouse", 0x1, "0", &EnableWowMouseCallback, DEFAULT, false, nullptr, false);
}

// ---- movement input ---------------------------------------------------------------------------

namespace {

// The camera ports' view of the active camera, which the input drives as the bits change.
CGCamera* InputCamera() {
    return CGWorldFrame::GetActiveCamera();
}

CGUnit_C* InputActiveUnit(const char* file, int32_t line) {
    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(CGUnit_C::s_activeMover, TYPE_UNIT, file, line));
}

// The protected-action gate (FUN_005191c0, GameUI.cpp): movement from tainted Lua is refused
// with ADDON_ACTION_BLOCKED. Frozen does not track taint yet, so nothing is tainted and every
// request passes, which is what the reference does for untainted code.
int32_t InputActionAllowed(int32_t action) {
    (void)action;

    return 1;
}

// The bit tests SetControlBit and UnsetControlBit both take before changing anything.
bool InputIsIdle(uint32_t flags) {
    return (flags & 0x1030) == 0
        && (flags & 0xc0) == 0
        && ((flags & 0x2000001) == 0 || (flags & 0x300) == 0)
        && ((flags & 0x300) == 0 || (flags & 0x2000001) != 0)
        && (flags & 0x1e00000) == 0;
}

bool InputBothButtons(uint32_t flags) {
    return (flags & 0x1) && (flags & 0x2);
}

bool InputButtonTurn(uint32_t flags) {
    return (flags & 0x2000001) && (flags & 0x300);
}

} // namespace

uint32_t InputControlEventTime() {
    // DIVERGENCE: the reference's event dispatch stamps each input event's time into 0x00b499a4;
    // frozen's does not carry it, so the clock at the call stands in (CGCamera does the same).
    return static_cast<uint32_t>(OsGetAsyncTimeMs());
}

// ref: FUN_005f9810
// The unit is on a server spline.
static int32_t InputUnitOnSpline(CGUnit_C* unit) {
    return unit->m_localMove.IsSplineActive() ? 1 : 0;
}

// ref: FUN_005fa9e0
// Alive, not rooted or on a transport (0x100a00), and not stunned (stand state 7).
static int32_t InputUnitMayAct(CGUnit_C* unit) {
    return 0 < unit->Unit()->health && !(unit->m_localMove.m_moveFlags & 0x100a00)
        && static_cast<uint8_t>(unit->Unit()->bytes1 & 0xFF) != 7;
}

// ref: FUN_005fa060
// PARTIAL: the vehicle checks (FUN_0074ba40) are the vehicle port's and pass here.
int32_t InputControlUnitCanMove(CGUnit_C* unit) {
    if (!unit || unit->Unit()->health < 1 || unit->m_localMove.IsSplineActive()) {
        return 0;
    }

    // FUN_0071b000: an animation that holds the unit in place (AnimationData flag 0x80).
    if (unit->IsAnimationRooting()) {
        return 0;
    }

    // A player whose descriptor says it is controlled by something else (+0x1858 bit 0).
    return 1;
}

// ref: FUN_005fac90
// PARTIAL: the vehicle seat test (FUN_0074b8b0) passes; frozen has no seats.
int32_t InputControlUnitCanWalk(CGUnit_C* unit) {
    return InputControlUnitCanMove(unit) && InputUnitMayAct(unit);
}

// ref: FUN_005fa0d0
// Not stunned (UNIT_FLAG_STUNNED, 0x40000); the vehicle seat test passes.
int32_t InputControlUnitCanTurn(CGUnit_C* unit) {
    return InputControlUnitCanMove(unit) && !(unit->Unit()->flags & 0x40000);
}

// ref: FUN_005fa110
int32_t CInputControl::CanMouseSteer(CGUnit_C* unit) {
    if (!InputControlUnitCanMove(unit) || (unit->Unit()->flags & 0x40000)) {
        return 0;
    }

    if (!(this->m_unk04 & 0x2000001)) {
        return 0;
    }

    return unit->GetStandStateByte() == 0;
}

// ref: FUN_005faa40
// PARTIAL: the spell-target cursor's cancel (FUN_00806620) and the current channel's lookup
// are both by the active player; frozen reads the channel straight off the unit fields.
void InputControlCancelChannel() {
    auto unit = InputActiveUnit(".\\InputControl.cpp", 0xa0);

    if (!unit || unit->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return;
    }

    int32_t spellID = unit->Unit()->channelSpell;

    if (!spellID) {
        return;
    }

    auto spell = g_spellDB.GetRecord(spellID);

    // SPELL_ATTR1 0x8: the channel is broken by moving.
    if (spell && (spell->m_attributesEx & 0x8)) {
        SpellSendCancelChannelling(spellID);
    }
}

// ref: FUN_005fa910
// PITCH_CHANGED for the vehicle aim bar, as a fraction of the seat's pitch range. Frozen has no
// seats, so there is no range to report against.
void InputControlSignalPitch(float pitch) {
    (void)pitch;
}

// ref: FUN_005f9650
void CInputControl::OnTurnStarted() {
    if (!this->m_unk44) {
        return;
    }

    this->m_unk44 = 0;

    if (auto camera = InputCamera()) {
        camera->UnlockYaw();
    }
}

// ref: FUN_005fb510
void CInputControl::RefreshMousePitch() {
    this->m_unk4C = 0;

    if (auto unit = InputActiveUnit(".\\InputControl.cpp", 0x9c7)) {
        InputControlSignalPitch(unit->GetMovementPitch());
    }
}

// ref: FUN_005f9600
int32_t CInputControl::IsDrag(uint32_t time) {
    int32_t held = static_cast<int32_t>(time - this->m_unk14);

    if (held - 800 >= 0) {
        return 1;
    }

    // 0x00a1de4c: the travel a click may have.
    const float travel = 0.0049999998882412910f;

    if (this->m_unk08 <= travel && this->m_unk0C <= travel) {
        return 0;
    }

    return held - 200 >= 0;
}

// ref: FUN_006de980
// A commentator (player flags 0x80000 with 0x400000) flies a free camera, and the input's
// facing and pitch go to it instead of to the player. PARTIAL: the reference also counts a
// commentator whose camera-mode record (DAT_00bd088c) is of type 4; the commentator camera is not
// ported, so the flag pair alone decides.
static int32_t InputIsCommentator(CGUnit_C* unit) {
    if (!unit || !unit->IsA(TYPE_PLAYER)) {
        return 0;
    }

    uint32_t flags = static_cast<CGPlayer_C*>(unit)->Player()->flags;

    return (flags & 0x80000) && (flags & 0x400000) ? 1 : 0;
}

// ref: FUN_005fa6b0
// The camera may turn the player: a living one the camera follows, not stunned or on a spline,
// with a button or mouse steer down. PARTIAL: the vehicle that steers its passenger
// (FUN_0074ba40) and the unit's virtual 0x138 state are the vehicle port's and never stop it here.
int32_t CInputControl::CanSetFacing() {
    auto unit = InputActiveUnit(".\\InputControl.cpp", 0x8d6);

    if (!unit) {
        return 0;
    }

    auto camera = InputCamera();

    if (!camera) {
        return 0;
    }

    if (!InputIsCommentator(unit)) {
        if (unit->Unit()->health < 1 || unit->m_localMove.IsSplineActive()
            || (unit->Unit()->flags & 0x40000) || camera->m_target != unit->GetGUID()) {
            return 0;
        }
    }

    if (!(camera->m_flags & 0x1)) {
        return 0;
    }

    return (this->m_unk04 & 0x2000001) ? 1 : 0;
}

// ref: FUN_005fa790
// The camera may tilt the player. One that may always pitch (move-flags-2 0x20) but is neither
// swimming nor flying needs a vehicle that allows it. PARTIAL: as CanSetFacing.
int32_t CInputControl::CanSetPitch() {
    auto unit = InputActiveUnit(".\\InputControl.cpp", 0x8f7);

    if (!unit) {
        return 0;
    }

    auto camera = InputCamera();

    if (InputIsCommentator(unit)) {
        return 1;
    }

    if (unit->Unit()->health < 1 || unit->m_localMove.IsSplineActive() || (unit->Unit()->flags & 0x40000)) {
        return 0;
    }

    if ((unit->m_localMove.m_moveFlags2 & 0x20) && !(unit->m_localMove.m_moveFlags & 0x2200000)) {
        // CGUnit_C::GetVehicleRec: no vehicle, so no vehicle that allows it.
        return 0;
    }

    return camera && camera->m_target == unit->GetGUID() ? 1 : 0;
}

// ref: FUN_005fb260
// The camera's yaw becomes the player's facing. A unit that turns at full speed turns to it,
// once per new facing, with the camera's yaw locked to it meanwhile. PARTIAL: the vehicle seat's
// clamp (FUN_0074c550) and its turret aim (FUN_00747b00) are the vehicle port's.
void CInputControl::SetFacing(uint32_t time, float facing) {
    if (!this->CanSetFacing()) {
        return;
    }

    auto unit = InputActiveUnit(".\\InputControl.cpp", 0x91c);

    if (!unit) {
        return;
    }

    if (InputIsCommentator(unit)) {
        // DAT_00ace4a8: the commentator camera's facing, the commentator port's.
        return;
    }

    if (!(unit->m_localMove.m_moveFlags2 & 0x8)) {
        unit->SetFacingTo(static_cast<int32_t>(time), facing);
    } else {
        if (!this->m_unk44 || this->m_unk48 != facing) {
            unit->TurnTo(static_cast<int32_t>(time), facing);
            this->m_unk48 = facing;
        }

        if (!this->m_unk44) {
            if (auto camera = InputCamera()) {
                camera->LockYaw();
            }

            this->m_unk04 &= 0xfffbffff;
            this->m_unk44 = 1;

            return;
        }
    }

    this->m_unk04 &= 0xfffbffff;
}

// ref: FUN_005fb3a0
// Send a pitch: at once, or as a pitch-to for a unit that pitches at full speed (move-flags-2
// 0x10), once per new pitch. PARTIAL: the seat's pitch range and its aim signal are the vehicle
// port's; the flag the reference raises last (DAT_00ca0ab4) has no reader in frozen.
void CInputControl::ApplyPitch(CGUnit_C* unit, uint32_t time, float pitch) {
    if (!(unit->m_localMove.m_moveFlags2 & 0x10)) {
        unit->SetPitchTo(static_cast<int32_t>(time), pitch);
        return;
    }

    if (this->m_unk4C && this->m_unk50 == pitch) {
        this->m_unk4C = 1;
        return;
    }

    unit->PitchTo(static_cast<int32_t>(time), pitch);
    this->m_unk50 = pitch;
    this->m_unk4C = 1;
}

// ref: FUN_005fbe70
// The camera's pitch becomes the player's, negated (the camera looks down where the player
// pitches up). PARTIAL: the seat's pitch offset is the vehicle port's.
void CInputControl::SetPitch(uint32_t time, float pitch) {
    if (!this->CanSetPitch()) {
        return;
    }

    auto unit = InputActiveUnit(".\\InputControl.cpp", 0x947);

    if (!unit) {
        return;
    }

    if (InputIsCommentator(unit)) {
        // DAT_00ace4ac: the commentator camera's pitch.
        return;
    }

    this->ApplyPitch(unit, time, -pitch);
    this->m_unk04 &= 0xfff7ffff;
}

// ref: FUN_005fba60
// The mouse moved while a button holds the mouse look: the camera turns by the motion, the
// travel counts toward telling a drag from a click, and a player the buttons steer faces the
// camera. PARTIAL: the motion time (the device's +0xf68, into +0x10), the commentator's speed
// scale (FUN_00568560) and a vehicle seat that takes the pitch (FUN_00756f00) are not ported.
void CInputControl::OnMouseLook(const CMouseEvent& evt) {
    if (!this->m_unk04) {
        return;
    }

    float dx = evt.x;
    float dy = evt.y;

    auto unit = InputActiveUnit(".\\InputControl.cpp", 0x606);

    this->m_unk08 = std::fabs(dx) + this->m_unk08;
    this->m_unk0C = std::fabs(dy) + this->m_unk0C;

    int32_t steer = unit ? this->CanMouseSteer(unit) : 0;

    auto camera = InputCamera();

    if (!camera) {
        return;
    }

    camera->MouseLook(dx, dy, nullptr);

    if (steer) {
        camera->SyncPlayerFacing();
    }
}

// ref: FUN_005fa170
int32_t CInputControl::SetControlBit(uint32_t bit, uint32_t time) {
    if (this->m_unk04 & bit) {
        return 0;
    }

    auto camera = InputCamera();
    uint32_t old = this->m_unk04;

    bool idle = InputIsIdle(old);
    bool bothButtons = InputBothButtons(old);
    bool buttonTurn = InputButtonTurn(old);

    this->m_unk04 = old | bit;

    if (!(old & 0x6000003) && (this->m_unk04 & 0x6000003)) {
        this->m_unk08 = 0.0f;
        this->m_unk0C = 0.0f;
        this->m_unk14 = time;

        if (camera) {
            camera->BeginFreeLookIfAllowed();
        }
    }

    // A button going down starts the mouse look: the cursor hides and the mouse goes relative.
    if (!(old & 0x3) && (this->m_unk04 & 0x3)) {
        GameUIEnterMouseLook();
    }

    if (!bothButtons && InputBothButtons(this->m_unk04)) {
        auto unit = InputActiveUnit(".\\InputControl.cpp", 0x716);

        if (this->CanMouseSteer(unit)) {
            if (camera) {
                camera->SyncPlayerFacing();
            }

            this->m_unk54 = 1;
        }
    } else if (!InputBothButtons(this->m_unk04)) {
        this->m_unk54 = 0;
    }

    if (camera) {
        if (bit & 0x1e00000) {
            camera->SetTracking(this->m_unk04 & 0x1e00000);
        }

        uint32_t flags = this->m_unk04;

        if ((bit & 0xa010f0) || (!bothButtons && InputBothButtons(flags)) || (!buttonTurn && InputButtonTurn(flags))) {
            camera->SetBobbing((flags & 0xa010f0) || InputBothButtons(flags) || InputButtonTurn(flags) ? 1 : 0);
        }
    }

    if (this->m_unk44 && (bit & 0x300)) {
        this->m_unk44 = 0;

        if (camera) {
            camera->UnlockYaw();
        }
    }

    uint32_t flags = this->m_unk04;

    if ((bit & 0x13f0)
        || (!(old & 0x4000002) && (flags & 0x4000002))
        || (!(old & 0x2000001) && (flags & 0x2000001))
        || (!buttonTurn && InputButtonTurn(flags))) {
        if (camera) {
            camera->CalcSmoothing(this, (!idle && this->IsIdle()) ? 1.0f : 0.0f);
        }
    }

    // Moving by key, or with both buttons, ends autorun.
    if (bit & 0x30) {
        this->m_unk04 &= 0xffffefff;
    }

    if (!bothButtons && InputBothButtons(this->m_unk04)) {
        this->m_unk04 &= 0xffffefff;
    }

    if ((bit & 0x3) && (this->m_unk04 & 0x3) != bit) {
        this->m_unk18 = 0;
    }

    return 1;
}

// ref: FUN_005fa450
// PARTIAL: the click a short press makes (FUN_004f7880, the world frame's select-or-interact) is
// the world-frame port's.
int32_t CInputControl::UnsetControlBit(uint32_t bit, uint32_t time, int32_t a3) {
    if (!(this->m_unk04 & bit)) {
        return 0;
    }

    auto camera = InputCamera();
    uint32_t old = this->m_unk04;

    bool idle = InputIsIdle(old);
    bool bothButtons = InputBothButtons(old);
    bool buttonTurn = InputButtonTurn(old);

    this->m_unk04 = ~bit & old;

    if ((old & 0x6000003) && !(~bit & old & 0x6000003) && camera) {
        camera->SetViewLocked(a3);

        if (this->m_unk44 && (this->m_unk04 & 0x300)) {
            this->OnTurnStarted();
        }
    }

    // The last button up ends the mouse look.
    if ((old & 0x3) && !(this->m_unk04 & 0x3)) {
        GameUILeaveMouseLook();
    }

    if (camera) {
        if (bit & 0x1e00000) {
            camera->SetTracking(this->m_unk04 & 0x1e00000);
        }

        uint32_t flags = this->m_unk04;

        if ((bit & 0xa010f0) || (bothButtons && !InputBothButtons(flags)) || (buttonTurn && !InputButtonTurn(flags))) {
            camera->SetBobbing((flags & 0xa010f0) || InputBothButtons(flags) || InputButtonTurn(flags) ? 1 : 0);
        }

        if ((bit & 0x13f0)
            || ((old & 0x4000002) && !(flags & 0x4000002))
            || ((old & 0x2000001) && !(flags & 0x2000001))
            || (buttonTurn && !InputButtonTurn(flags))) {
            camera->CalcSmoothing(this, (!idle && this->IsIdle()) ? 1.0f : 0.0f);
        }
    }

    if (!(old & 0x3) || (this->m_unk04 & 0x3)) {
        return 1;
    }

    this->m_unk18 = 0;

    return 1;
}

// ref: FUN_005fae70
// PARTIAL: a vehicle that moves its passenger (FUN_0074bb90) and a player whose descriptor bit
// says it is turned by others are the vehicle and Player_C ports'.
void CInputControl::UpdateMove(uint32_t time, CGUnit_C* unit) {
    uint32_t flags = this->m_unk04;
    int32_t direction = (flags & 0x1000) ? 1 : 0;

    if (flags & 0x10) {
        direction++;
    }

    if ((flags & 0x1) && (flags & 0x2)) {
        direction++;
    }

    if (flags & 0x20) {
        direction--;
    }

    if (direction == 0) {
        if (flags & 0x10000) {
            unit->StopMove(time);
            this->m_unk04 &= 0xfffeffff;
        }

        return;
    }

    if (unit->GetStandStateByte()) {
        // FUN_0071a360: a sitting player stands to move (Player_C's stand-up).
        auto camera = InputCamera();

        if (camera && this->CanMouseSteer(unit)) {
            camera->SyncPlayerFacing();
        }
    }

    if (direction < 1) {
        if (!(this->m_unk04 & 0x10000) || (unit->m_localMove.m_moveFlags & 0x1)) {
            unit->StartMove(time, 0);
            this->m_unk04 |= 0x10000;
        }
    } else if (!(this->m_unk04 & 0x10000) || (unit->m_localMove.m_moveFlags & 0x2)) {
        unit->StartMove(time, 1);
        this->m_unk04 |= 0x10000;
    }
}

// ref: FUN_005fafb0
// The strafe keys, and the turn keys while a mouse button steers.
void CInputControl::UpdateStrafe(uint32_t time, CGUnit_C* unit) {
    uint32_t flags = this->m_unk04;
    int32_t direction = (flags & 0x40) ? 1 : 0;

    if ((flags & 0x2000001) && (flags & 0x100)) {
        direction++;
    }

    if (flags & 0x80) {
        direction--;
    }

    if ((flags & 0x2000001) && (flags & 0x200)) {
        direction--;
    }

    if (direction == 0) {
        if (flags & 0x20000) {
            unit->StopStrafe(time);
            this->m_unk04 &= 0xfffdffff;
        }

        return;
    }

    if (direction < 1) {
        if (!(this->m_unk04 & 0x20000)) {
            unit->StartStrafe(time, 0);
            this->m_unk04 |= 0x20000;
        }
    } else if (!(this->m_unk04 & 0x20000)) {
        unit->StartStrafe(time, 1);
        this->m_unk04 |= 0x20000;
    }
}

// ref: FUN_005face0
// Jump and descend move a swimmer or a flier up and down -- or, when move-flags-2 0x10 lets the
// keys pitch, tilt it.
void CInputControl::UpdateSwimAscend(uint32_t time, CGUnit_C* unit) {
    auto& move = unit->m_localMove;
    uint32_t moveFlags = move.m_moveFlags;

    if (!(moveFlags & 0x2200000)) {
        this->m_unk04 &= 0xffefffff;
        return;
    }

    uint32_t flags = this->m_unk04;
    int32_t direction = (flags & 0x2000) ? 1 : 0;

    if (flags & 0x4000) {
        direction--;
    }

    if (!(move.m_moveFlags2 & 0x10)) {
        if (direction == 0) {
            if (flags & 0x100000) {
                unit->StopAscend(time);
                this->m_unk04 &= 0xffefffff;
            }

            return;
        }

        if (direction < 1) {
            if (!(this->m_unk04 & 0x100000) || (move.m_moveFlags & 0x400000)) {
                unit->StartAscend(time, 0);
                this->m_unk04 |= 0x100000;
            }
        } else if (!(this->m_unk04 & 0x100000) || (move.m_moveFlags & 0x800000)) {
            unit->StartAscend(time, 1);
            this->m_unk04 |= 0x100000;
        }

        return;
    }

    if (direction < 1) {
        if (direction < 0) {
            if (!(flags & 0x100000) || (moveFlags & 0x40)) {
                unit->StartPitch(time, 0);
                this->m_unk04 |= 0x100000;
            }
        } else if (flags & 0x100000) {
            unit->StopPitch(time);
            this->m_unk04 &= 0xffefffff;
            InputControlSignalPitch(unit->GetMovementPitch());
        }
    } else if (!(flags & 0x100000) || (moveFlags & 0x80)) {
        unit->StartPitch(time, 1);
        this->m_unk04 |= 0x100000;
    }
}

// ref: FUN_005fb0b0
// The turn keys, unless a mouse button turns them into strafes (move-flags-2 0x1 keeps the turn).
void CInputControl::UpdateTurn(uint32_t time, CGUnit_C* unit) {
    uint32_t flags = this->m_unk04;
    int32_t direction = (flags & 0x100) ? 1 : 0;

    if (flags & 0x200) {
        direction--;
    }

    if ((!(flags & 0x2000001) || (unit->m_localMove.m_moveFlags2 & 0x1)) && direction != 0) {
        if (direction < 1) {
            if (!(this->m_unk04 & 0x40000)) {
                unit->StartTurn(time, 0);
                this->m_unk04 |= 0x40000;
            }
        } else if (!(this->m_unk04 & 0x40000)) {
            unit->StartTurn(time, 1);
            this->m_unk04 |= 0x40000;
        }

        return;
    }

    if (this->m_unk04 & 0x40000) {
        unit->StopTurn(time);
        this->m_unk04 &= 0xfffbffff;
    }
}

// ref: FUN_005fb1a0
// The pitch keys, while not steering with the mouse.
void CInputControl::UpdatePitch(uint32_t time, CGUnit_C* unit) {
    uint32_t flags = this->m_unk04;

    if (flags & 0x2000001) {
        return;
    }

    int32_t direction = (flags & 0x400) ? 1 : 0;

    if (flags & 0x800) {
        direction--;
    }

    if (direction < 1) {
        if (direction < 0) {
            if (!(flags & 0x80000)) {
                unit->StartPitch(time, 0);
                this->m_unk04 |= 0x80000;
            }
        } else if (flags & 0x80000) {
            unit->StopPitch(time);
            this->m_unk04 &= 0xfff7ffff;
            InputControlSignalPitch(unit->GetMovementPitch());
        }
    } else if (!(flags & 0x80000)) {
        unit->StartPitch(time, 1);
        this->m_unk04 |= 0x80000;
    }
}

// ref: FUN_005fbbc0
// PARTIAL: a commentator's camera (FUN_006de980, FUN_0056b5c0) takes the bits instead; frozen
// has no commentator mode.
void CInputControl::UpdatePlayerMovement(uint32_t time, int32_t a3) {
    auto unit = InputActiveUnit(".\\InputControl.cpp", 0x69c);

    if (!unit) {
        return;
    }

    // Input can not move the player into the movers' past.
    uint32_t last;

    if (MovementGetLastTime(&last) && static_cast<int32_t>(time - last) < 0) {
        time = last;
    }

    bool walk = a3 && InputControlUnitCanWalk(unit);
    bool turn = a3 && InputControlUnitCanTurn(unit);

    if (walk) {
        this->UpdateMove(time, unit);
        this->UpdateStrafe(time, unit);
        this->UpdateSwimAscend(time, unit);
    } else {
        if (!turn) {
            unit->CancelClickToMove(0, 1);
        }

        if (this->m_unk04 & 0x10000) {
            if (!unit->m_localMove.IsSplineActive()) {
                unit->StopMove(time);
            }

            this->m_unk04 &= 0xfffeffff;
        }

        if (this->m_unk04 & 0x20000) {
            if (!unit->m_localMove.IsSplineActive()) {
                unit->StopStrafe(time);
            }

            this->m_unk04 &= 0xfffdffff;
        }

        if (this->m_unk04 & 0x1000) {
            this->m_unk04 &= 0xffffefff;
        }

        if (this->m_unk04 & 0x100000) {
            if (!unit->m_localMove.IsSplineActive()) {
                unit->StopAscend(time);
            }

            this->m_unk04 &= 0xffefffff;
        }
    }

    if (turn) {
        this->UpdateTurn(time, unit);

        if ((unit->m_localMove.m_moveFlags & 0x2200000) || (unit->m_localMove.m_moveFlags2 & 0x20)) {
            this->UpdatePitch(time, unit);
        }

        return;
    }

    if (this->m_unk04 & 0x40000) {
        if (!unit->m_localMove.IsSplineActive()) {
            unit->StopTurn(time);
        }

        this->m_unk04 &= 0xfffbffff;
    }

    if (this->m_unk04 & 0x80000) {
        if (!unit->m_localMove.IsSplineActive() && (unit->m_localMove.m_moveFlags & 0x2200000)) {
            unit->StopPitch(time);
        }

        this->m_unk04 &= 0xfff7ffff;
    }
}

// ---- the movement script functions -----------------------------------------------------------

namespace {

// The shape of most of them: set (or clear) a bit at the event's time, and when it changed,
// optionally cancel a channel, then bring the movement up to the bits.
int32_t InputStartBit(uint32_t bit, bool cancelChannel) {
    auto input = s_inputControl;

    if (!InputActionAllowed(0)) {
        return 0;
    }

    uint32_t time = InputControlEventTime();

    if (input->SetControlBit(bit, time)) {
        if (cancelChannel) {
            InputControlCancelChannel();
        }

        input->UpdatePlayerMovement(time, 1);
    }

    return 0;
}

int32_t InputStopBit(uint32_t bit) {
    auto input = s_inputControl;

    if (!InputActionAllowed(0)) {
        return 0;
    }

    uint32_t time = InputControlEventTime();

    if (input->UnsetControlBit(bit, time, 0)) {
        input->UpdatePlayerMovement(time, 1);
    }

    return 0;
}

// The mouse-button starts, which also remember which click the press is (+0x18) and pick what is
// under the cursor (FUN_004fa570, the world frame's) when no button was held.
int32_t InputStartButton(uint32_t bit, uint32_t click) {
    auto input = s_inputControl;

    if (!InputActionAllowed(0)) {
        return 0;
    }

    uint32_t time = InputControlEventTime();

    if (!(input->m_unk04 & 0x3)) {
        input->m_unk18 = click;
    }

    if (input->SetControlBit(bit, time)) {
        if ((input->m_unk04 & 0x1) && (input->m_unk04 & 0x2)) {
            InputControlCancelChannel();
        }

        input->UpdatePlayerMovement(time, 1);
    }

    return 0;
}

} // namespace

// ref: FUN_005fc200
int32_t Script_MoveForwardStart(lua_State* L) {
    return InputStartBit(0x10, true);
}

// ref: FUN_005fc250
int32_t Script_MoveForwardStop(lua_State* L) {
    return InputStopBit(0x10);
}

// ref: FUN_005fc290
int32_t Script_MoveBackwardStart(lua_State* L) {
    return InputStartBit(0x20, true);
}

// ref: FUN_005fc2e0
int32_t Script_MoveBackwardStop(lua_State* L) {
    return InputStopBit(0x20);
}

// ref: FUN_005fc320
int32_t Script_TurnLeftStart(lua_State* L) {
    return InputStartBit(0x100, false);
}

// ref: FUN_005fc360
int32_t Script_TurnLeftStop(lua_State* L) {
    return InputStopBit(0x100);
}

// ref: FUN_005fc3b0
int32_t Script_TurnRightStart(lua_State* L) {
    return InputStartBit(0x200, false);
}

// ref: FUN_005fc3f0
int32_t Script_TurnRightStop(lua_State* L) {
    return InputStopBit(0x200);
}

// ref: FUN_005fc440
int32_t Script_StrafeLeftStart(lua_State* L) {
    return InputStartBit(0x40, true);
}

// ref: FUN_005fc490
int32_t Script_StrafeLeftStop(lua_State* L) {
    return InputStopBit(0x40);
}

// ref: FUN_005fc4d0
int32_t Script_StrafeRightStart(lua_State* L) {
    return InputStartBit(0x80, true);
}

// ref: FUN_005fc520
int32_t Script_StrafeRightStop(lua_State* L) {
    return InputStopBit(0x80);
}

// ref: FUN_005fc8e0
int32_t Script_PitchUpStart(lua_State* L) {
    return InputStartBit(0x400, false);
}

// ref: FUN_005fc570
int32_t Script_PitchUpStop(lua_State* L) {
    return InputStopBit(0x400);
}

// ref: FUN_005fc920
int32_t Script_PitchDownStart(lua_State* L) {
    return InputStartBit(0x800, false);
}

// ref: FUN_005fc5c0
int32_t Script_PitchDownStop(lua_State* L) {
    return InputStopBit(0x800);
}

// ref: FUN_005fbf80
// PARTIAL: a mount or aura that lets the player take off (FUN_0071b810) would fly instead of
// jumping; the flight port carries it. The AFK clear on a jump (FUN_006d52d0) is Player_C's.
int32_t Script_JumpOrAscendStart(lua_State* L) {
    auto unit = InputActiveUnit(".\\InputControl.cpp", 0x162);

    if (!unit) {
        return 0;
    }

    auto input = s_inputControl;

    if (!InputActionAllowed(0)) {
        return 0;
    }

    uint32_t time = InputControlEventTime();

    if (input->SetControlBit(0x2000, time)) {
        input->UpdatePlayerMovement(time, 1);
    }

    // Swimming or flying, the jump key ascends; UpdatePlayerMovement did that.
    if (unit->m_localMove.m_moveFlags & 0x2200000) {
        return 0;
    }

    InputControlCancelChannel();

    if (unit->Unit()->health < 1 || InputUnitOnSpline(unit) || !InputUnitMayAct(unit)
        || unit->IsAnimationRooting()) {
        return 0;
    }

    time = InputControlEventTime();
    unit->Jump(time);

    return 0;
}

// ref: FUN_005fc0a0
int32_t Script_AscendStop(lua_State* L) {
    return InputStopBit(0x2000);
}

// ref: FUN_005fc0f0
int32_t InputControlDescendStart(lua_State* L) {
    auto input = s_inputControl;

    if (InputActionAllowed(0)) {
        uint32_t time = InputControlEventTime();

        if (input->SetControlBit(0x4000, time)) {
            input->UpdatePlayerMovement(time, 1);
        }
    }

    InputControlCancelChannel();

    return 0;
}

// ref: FUN_005fc140
int32_t Script_DescendStop(lua_State* L) {
    return InputStopBit(0x4000);
}

// ref: FUN_005fc190
int32_t Script_ToggleAutoRun(lua_State* L) {
    auto input = s_inputControl;
    int32_t start = !(input->m_unk04 & 0x1000);

    if (!InputActionAllowed(0)) {
        return 0;
    }

    uint32_t time = InputControlEventTime();
    int32_t changed = start ? input->SetControlBit(0x1000, time) : input->UnsetControlBit(0x1000, time, 0);

    if (changed) {
        if (start) {
            InputControlCancelChannel();
        }

        input->UpdatePlayerMovement(time, 1);
    }

    return 0;
}

// ref: FUN_005faae0
// FUN_0071ae50: the run toggle is a run-or-walk event carrying the current walk bit.
int32_t Script_ToggleRun(lua_State* L) {
    if (!InputActionAllowed(0)) {
        return 0;
    }

    auto unit = InputActiveUnit(".\\InputControl.cpp", 0x1b7);

    if (!unit) {
        return 0;
    }

    uint32_t time = InputControlEventTime();

    if (0 < unit->Unit()->health && !unit->m_localMove.IsSplineActive() && InputUnitMayAct(unit)) {
        unit->m_localMove.QueueSetRun(static_cast<int32_t>(time), unit->m_localMove.m_moveFlags & 0x100);
    }

    return 0;
}

// ref: FUN_005fc610
// The right button is control bit 0x1 and makes click 2. These two had the bits swapped, which
// made the right button look around and the left one steer.
int32_t Script_TurnOrActionStart(lua_State* L) {
    return InputStartButton(0x1, 2);
}

// ref: FUN_005fc680
int32_t Script_TurnOrActionStop(lua_State* L) {
    return InputStopBit(0x1);
}

// ref: FUN_005fc6c0
// The left button is control bit 0x2 and makes click 1.
int32_t Script_CameraOrSelectOrMoveStart(lua_State* L) {
    return InputStartButton(0x2, 1);
}

// ref: FUN_005fc730
// The argument says whether the camera keeps the view where the drag left it.
int32_t Script_CameraOrSelectOrMoveStop(lua_State* L) {
    auto input = s_inputControl;

    if (!InputActionAllowed(0)) {
        return 0;
    }

    uint32_t time = InputControlEventTime();
    int32_t sticky = lua_toboolean(L, 1);

    if (input->UnsetControlBit(0x2, time, sticky)) {
        input->UpdatePlayerMovement(time, 1);
    }

    return 0;
}

// ref: FUN_005fc780
// Both buttons at once: the left one's bit (click 1), then the right one's (click 2).
int32_t Script_MoveAndSteerStart(lua_State* L) {
    InputStartButton(0x2, 1);
    InputStartButton(0x1, 2);

    return 0;
}

// ref: FUN_005fc830
int32_t Script_MoveAndSteerStop(lua_State* L) {
    InputStopBit(0x2);
    InputStopBit(0x1);

    return 0;
}
