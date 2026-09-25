#include "ui/InputControl.hpp"
#include "console/Console.hpp"
#include "console/CVar.hpp"
#include <common/Time.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
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

    s_enableWowMouseCvar = CVar::Register("enableWowMouse", "Enable Steelseries RunicWorld        Mouse", 0x1, "0", &EnableWowMouseCallback, DEFAULT, false, nullptr, false);
}
