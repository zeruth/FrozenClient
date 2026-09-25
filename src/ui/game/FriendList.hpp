#ifndef UI_GAME_FRIEND_LIST_HPP
#define UI_GAME_FRIEND_LIST_HPP

#include "util/GUID.hpp"
#include <cstdint>

// Only the members the ported functions touch are named; the rest of each region is unknown.
class FriendList {
    public:
        // Types
        struct FriendEntry {
            uint8_t m_unk0[0x208];
            WOWGUID m_guid;
            uint8_t m_unk210[0x10];
        };

        // Member variables
        FriendEntry m_friends[100];
        uint8_t m_unkD480[0xDAF0 - 0xD480];
        WOWGUID m_complaints[32];

        // Member functions
        void AddComplaint(WOWGUID guid);
        FriendEntry* GetFriend(WOWGUID guid);
        bool HasComplaint(WOWGUID guid);
        bool IsFriend(WOWGUID guid);
};

#endif
