#ifndef UI_GAME_C_G_ACTION_BAR_HPP
#define UI_GAME_C_G_ACTION_BAR_HPP

#include <cstdint>
#include "net/Types.hpp"

class CDataStore;

class CGActionBar {
    public:
        // 144 in 3.3.5a: MAX_ACTION_BUTTONS, and the count SMSG_UPDATE_ACTION_BUTTONS always sends.
        static const int32_t NUM_ACTION_BUTTONS = 144;

        // The packed action per slot, straight off the wire. Low 24 bits are the action (a spell id,
        // item id or macro index); the top 8 are the type.
        enum ACTION_BUTTON_TYPE {
            ACTION_BUTTON_SPELL  = 0x00,
            ACTION_BUTTON_C      = 0x01,
            ACTION_BUTTON_EQSET  = 0x20,
            ACTION_BUTTON_MACRO  = 0x40,
            ACTION_BUTTON_CMACRO = 0x41,
            ACTION_BUTTON_ITEM   = 0x80,
        };

        // Public static variables
        static uint32_t s_currentPage;
        static uint32_t s_tempPageActiveFlags;
        static uint32_t s_actions[NUM_ACTION_BUTTONS];

        // Public static functions
        static uint32_t GetBonusBarOffset();

        // slot is 0-based here; the script bindings take it 1-based.
        static uint32_t GetAction(int32_t slot);
        static uint32_t GetActionType(int32_t slot);
        static uint32_t GetActionID(int32_t slot);

    private:
        // Private static variables
        static uint32_t s_bonusBarOffset;
};

int32_t ReceiveActionButtons(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
