#include "ui/game/CGActionBar.hpp"
#include "net/Types.hpp"
#include "db/Db.hpp"
#include "util/CStatus.hpp"
#include <common/DataStore.hpp>
#include <cstdio>
#include <cstring>

uint32_t CGActionBar::s_bonusBarOffset;
uint32_t CGActionBar::s_currentPage;
uint32_t CGActionBar::s_tempPageActiveFlags;
uint32_t CGActionBar::s_actions[CGActionBar::NUM_ACTION_BUTTONS] = {};

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

// SMSG_UPDATE_ACTION_BUTTONS: uint8 state, then -- unless state is 2 -- 144 packed dwords.
//
// state 2 means "clear the bars" and carries no data; the server sends it during a spec swap before
// it sends the new set. Anything else is a full replacement of all 144 slots.
//
// Format read from the server this client is developed against (AzerothCore
// Player::SendActionButtons), not from memory: low 24 bits are the action, high 8 the type.
int32_t ReceiveActionButtons(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t state = 0;

    if (!msg) {
        return 1;
    }

    msg->Get(state);

    if (state == 2) {
        memset(CGActionBar::s_actions, 0, sizeof(CGActionBar::s_actions));

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

    return 1;
}
