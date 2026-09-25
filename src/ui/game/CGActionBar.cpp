#include "ui/game/CGActionBar.hpp"
#include "ui/game/Types.hpp"
#include "ui/FrameScript.hpp"
#include "net/Types.hpp"
#include "db/Db.hpp"
#include "util/CStatus.hpp"
#include <common/DataStore.hpp>
#include <cstdio>
#include <cstring>

uint32_t CGActionBar::s_bonusBarOffset;
uint32_t CGActionBar::s_currentPage;
uint32_t CGActionBar::s_tempPageActiveFlags;
uint32_t CGActionBar::s_actions[CGActionBar::NUM_ACTION_BUTTONS] = {};  // ref: DAT_00c1e358
TSGrowableArray<uint32_t> CGActionBar::s_multiCastTotemSpells[4];      // ref: DAT_00be8ea0

uint32_t CGActionBar::GetBonusBarOffset() {
    return CGActionBar::s_bonusBarOffset;
}

uint32_t CGActionBar::GetAction(int32_t slot) {
    if (slot < 0 || slot >= CGActionBar::NUM_ACTION_BUTTONS) {
        return 0;
    }

    return CGActionBar::s_actions[slot];
}

uint32_t CGActionBar::GetActionType(int32_t slot) {
    return (CGActionBar::GetAction(slot) & 0xFF000000) >> 24;
}

uint32_t CGActionBar::GetActionID(int32_t slot) {
    return CGActionBar::GetAction(slot) & 0x00FFFFFF;
}

// ref: FUN_005a7860
bool CGActionBar::IsNibble0Action(int32_t slot) {
    auto action = CGActionBar::s_actions[slot];

    return action && (action & 0xF0000000) == 0;
}

// ref: FUN_005a78c0
bool CGActionBar::IsNibble1Action(int32_t slot) {
    auto action = CGActionBar::s_actions[slot];

    return action && (action & 0xF0000000) == 0x10000000;
}

// ref: FUN_005a78f0
bool CGActionBar::IsEquipmentSetAction(int32_t slot) {
    auto action = CGActionBar::s_actions[slot];

    return action && (action & 0xF0000000) == 0x20000000;
}

// ref: FUN_005a7950
uint32_t CGActionBar::GetEquipmentSetAction(int32_t slot) {
    auto action = CGActionBar::s_actions[slot];

    if (action && (action & 0xF0000000) == 0x20000000) {
        return action & 0xDFFFFFFF;
    }

    return 0xFFFFFFFF;
}

// ref: FUN_005a7a70
void CGActionBar::SignalShowGrid() {
    FrameScript_SignalEvent(SCRIPT_ACTIONBAR_SHOWGRID, nullptr);
}

// ref: FUN_005a7a80
void CGActionBar::SignalHideGrid() {
    FrameScript_SignalEvent(SCRIPT_ACTIONBAR_HIDEGRID, nullptr);
}

// ref: FUN_005a7cb0
void CGActionBar::SignalUpdateState() {
    FrameScript_SignalEvent(SCRIPT_ACTIONBAR_UPDATE_STATE, nullptr);
}

// ref: FUN_005a7cc0
void CGActionBar::SignalUpdateCooldown() {
    FrameScript_SignalEvent(SCRIPT_ACTIONBAR_UPDATE_COOLDOWN, nullptr);
}

// SMSG_UPDATE_ACTION_BUTTONS: uint8 state, then -- unless state is 2 -- 144 packed dwords.
//
// state 2 means "clear the bars" and carries no data; the server sends it during a spec swap before
// it sends the new set. Anything else is a full replacement of all 144 slots.
//
// Format read from the server this client is developed against (AzerothCore
// Player::SendActionButtons), not from memory: low 24 bits are the action, high 8 the type.
// Every button, in one event. ActionButton.lua reads slot 0 as "update all":
//
//     if ( arg1 == 0 or arg1 == tonumber(self.action) ) then ActionButton_Update(self)
//
// so one signal rebuilds the bar rather than a hundred and forty-four. The handler filled
// s_actions and told nobody, which is why the hotbar stayed empty however full the packet was.
static void SignalActionBarChanged() {
    FrameScript_SignalEvent(SCRIPT_ACTIONBAR_SLOT_CHANGED, "%d", 0);
}

int32_t ReceiveActionButtons(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t state = 0;

    if (!msg) {
        return 1;
    }

    msg->Get(state);

    if (state == 2) {
        memset(CGActionBar::s_actions, 0, sizeof(CGActionBar::s_actions));

        SignalActionBarChanged();

        return 1;
    }

    int32_t used = 0;

    for (int32_t slot = 0; slot < CGActionBar::NUM_ACTION_BUTTONS; slot++) {
        uint32_t packed = 0;
        msg->Get(packed);

        CGActionBar::s_actions[slot] = packed;

        if (packed) {
            used++;
        }
    }

    fprintf(stderr, "Action buttons: state %u, %d of %d slots filled\n",
            state, used, CGActionBar::NUM_ACTION_BUTTONS);

    SignalActionBarChanged();

    return 1;
}
