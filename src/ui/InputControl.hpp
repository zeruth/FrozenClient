#ifndef UI_INPUT_CONTROL_HPP
#define UI_INPUT_CONTROL_HPP

#include <cstdint>

class CVar;
class CGUnit_C;
struct lua_State;

// The control bits at +0x04, as the movement input reads them.
enum INPUTCONTROLBITS : uint32_t {
    INPUT_CONTROL_RIGHT_BUTTON  = 0x1,      // TurnOrAction (0x005fc610 sets bit 0x1)
    INPUT_CONTROL_LEFT_BUTTON   = 0x2,      // CameraOrSelectOrMove (0x005fc6c0 sets bit 0x2)
    INPUT_CONTROL_FORWARD       = 0x10,
    INPUT_CONTROL_BACKWARD      = 0x20,
    INPUT_CONTROL_STRAFE_LEFT   = 0x40,
    INPUT_CONTROL_STRAFE_RIGHT  = 0x80,
    INPUT_CONTROL_TURN_LEFT     = 0x100,
    INPUT_CONTROL_TURN_RIGHT    = 0x200,
    INPUT_CONTROL_PITCH_UP      = 0x400,
    INPUT_CONTROL_PITCH_DOWN    = 0x800,
    INPUT_CONTROL_AUTORUN       = 0x1000,
    INPUT_CONTROL_ASCEND        = 0x2000,
    INPUT_CONTROL_DESCEND       = 0x4000,
    // The movement the input last started, so a release can stop exactly that.
    INPUT_CONTROL_MOVING        = 0x10000,
    INPUT_CONTROL_STRAFING      = 0x20000,
    INPUT_CONTROL_TURNING       = 0x40000,
    INPUT_CONTROL_ASCENDING     = 0x80000,
    INPUT_CONTROL_PITCHING      = 0x100000,
};

// The reference's uiutil/InputControl.h singleton (0x70 bytes). Field names follow their use
// where it has been seen; the rest carry their offsets until the code that reads them is ported.
class CMouseEvent;

class CInputControl {
    public:
        // Member variables
        uint32_t m_lastTimeMs = 0;      // +0x00 OsGetAsyncTimeMs() at construction
        uint32_t m_unk04 = 0;           // +0x04 the control bits (INPUTCONTROLBITS)
        float m_unk08 = 0.0f;           // +0x08 the mouse's travel since a button went down
        float m_unk0C = 0.0f;           // +0x0c
        uint32_t m_unk10 = 0;
        uint32_t m_unk14 = 0;           // +0x14 when a mouse button went down
        uint32_t m_unk18 = 0;           // +0x18 the click the press will make (1 left, 2 right)
        uint8_t m_unk1C[0x28] = {};    // +0x1c a TSHashTable, constructed by FUN_005fcd70 (12 slots)
        uint32_t m_unk44 = 0;           // +0x44 the camera's yaw is locked to the player
        float m_unk48 = 0.0f;           // +0x48 the facing a full-speed turn was last sent to
        uint32_t m_unk4C = 0;           // +0x4c a full-speed pitch has been sent
        float m_unk50 = 0.0f;           // +0x50 the pitch it was sent to
        uint32_t m_unk54 = 0;           // +0x54 both buttons steer: the player faces the camera
        int32_t m_unk58 = 3;
        uint32_t m_unk5C = 0;
        int32_t m_unk60 = 3;
        uint32_t m_unk64 = 0;
        uint32_t m_unk68 = 0;
        void* m_wowMouse = nullptr;     // +0x6c the SteelSeries mouse driver object (FUN_008c2f50)

        // Member functions
        CInputControl();
        void UpdateCursorVisible(int32_t force);
        void OnFocusChanged(int32_t focus);
        void ClearFlagBits12And16();
        void ClearFlagBits16To19();
        int32_t IsIdle();
        void SetWowMouseEnabled(bool enabled);

        // ref: FUN_005fa170
        // Set a control bit; false when it was already set. Starting a mouse look, a steer or a
        // track updates the camera to match.
        int32_t CanSetFacing();
        int32_t CanSetPitch();
        void SetFacing(uint32_t time, float facing);
        void SetPitch(uint32_t time, float pitch);
        void ApplyPitch(CGUnit_C* unit, uint32_t time, float pitch);
        void OnMouseLook(const CMouseEvent& evt);
        int32_t SetControlBit(uint32_t bit, uint32_t time);

        // ref: FUN_005fa450
        // Clear a control bit; false when it was not set.
        int32_t UnsetControlBit(uint32_t bit, uint32_t time, int32_t a3);

        // ref: FUN_005fbbc0
        // Turn the control bits into the player's movement: start or stop moving, strafing,
        // turning, pitching and ascending to match.
        void UpdatePlayerMovement(uint32_t time, int32_t a3);

        // ref: FUN_005fae70
        void UpdateMove(uint32_t time, CGUnit_C* unit);
        // ref: FUN_005fafb0
        void UpdateStrafe(uint32_t time, CGUnit_C* unit);
        // ref: FUN_005face0
        void UpdateSwimAscend(uint32_t time, CGUnit_C* unit);
        // ref: FUN_005fb0b0
        void UpdateTurn(uint32_t time, CGUnit_C* unit);
        // ref: FUN_005fb1a0
        void UpdatePitch(uint32_t time, CGUnit_C* unit);

        // ref: FUN_005fa110
        // The player may be turned by the mouse: it can move, and a mouse button is held.
        int32_t CanMouseSteer(CGUnit_C* unit);

        // ref: FUN_005f9650
        // A turn started: the camera's yaw stops following the player.
        void OnTurnStarted();

        // ref: FUN_005fb510
        void RefreshMousePitch();

        // ref: FUN_005f9600
        // The mouse moved far enough, or was held long enough, that the press was not a click.
        int32_t IsDrag(uint32_t time);
};

extern CInputControl* s_inputControl;

CInputControl* InputControlGetActive();
float InputControlGetStoredFloat();

void InputControlInitialize();

// The time of the input event being handled (the reference's 0x00b499a4).
uint32_t InputControlEventTime();

// ref: FUN_005fa060
// The unit may move itself: alive, not on a spline, not posed by an animation or a vehicle.
int32_t InputControlUnitCanMove(CGUnit_C* unit);

// ref: FUN_005fac90
int32_t InputControlUnitCanWalk(CGUnit_C* unit);

// ref: FUN_005fa0d0
int32_t InputControlUnitCanTurn(CGUnit_C* unit);

// ref: FUN_005faa40
// Moving cancels a channelled spell that breaks on movement.
void InputControlCancelChannel();

// ref: FUN_005fa910
void InputControlSignalPitch(float pitch);

// The movement script functions (InputControl.cpp).
int32_t Script_MoveForwardStart(lua_State* L);
int32_t Script_MoveForwardStop(lua_State* L);
int32_t Script_MoveBackwardStart(lua_State* L);
int32_t Script_MoveBackwardStop(lua_State* L);
int32_t Script_TurnLeftStart(lua_State* L);
int32_t Script_TurnLeftStop(lua_State* L);
int32_t Script_TurnRightStart(lua_State* L);
int32_t Script_TurnRightStop(lua_State* L);
int32_t Script_StrafeLeftStart(lua_State* L);
int32_t Script_StrafeLeftStop(lua_State* L);
int32_t Script_StrafeRightStart(lua_State* L);
int32_t Script_StrafeRightStop(lua_State* L);
int32_t Script_PitchUpStart(lua_State* L);
int32_t Script_PitchUpStop(lua_State* L);
int32_t Script_PitchDownStart(lua_State* L);
int32_t Script_PitchDownStop(lua_State* L);
int32_t Script_JumpOrAscendStart(lua_State* L);
int32_t Script_AscendStop(lua_State* L);
int32_t Script_DescendStop(lua_State* L);
int32_t Script_ToggleAutoRun(lua_State* L);
int32_t Script_ToggleRun(lua_State* L);
int32_t Script_TurnOrActionStart(lua_State* L);
int32_t Script_TurnOrActionStop(lua_State* L);
int32_t Script_CameraOrSelectOrMoveStart(lua_State* L);
int32_t Script_CameraOrSelectOrMoveStop(lua_State* L);
int32_t Script_MoveAndSteerStart(lua_State* L);
int32_t Script_MoveAndSteerStop(lua_State* L);

// ref: FUN_005fc0f0
// Descend: the half of SitStandOrDescendStart that moves.
int32_t InputControlDescendStart(lua_State* L);

#endif
