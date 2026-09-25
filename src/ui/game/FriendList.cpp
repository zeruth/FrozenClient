#include "ui/game/FriendList.hpp"
#include <cstddef>
#include <cstring>

static_assert(sizeof(FriendList::FriendEntry) == 0x220, "FriendEntry is 0x220 bytes in the reference");
static_assert(offsetof(FriendList, m_complaints) == 0xDAF0, "the complaint list sits at +0xdaf0 in the reference");

// Most recent first: the entry moves (or is added) to the front and the ones before it shift down,
// dropping the oldest when the list is full.
// ref: FUN_006b3750
void FriendList::AddComplaint(WOWGUID guid) {
    uint32_t i = 0;

    while (i < 32 && this->m_complaints[i] != guid) {
        i++;
    }

    if (i > 30) {
        i = 31;
    }

    memmove(&this->m_complaints[1], &this->m_complaints[0], i * sizeof(WOWGUID));
    this->m_complaints[0] = guid;
}

// ref: FUN_006b3510
FriendList::FriendEntry* FriendList::GetFriend(WOWGUID guid) {
    if (guid == 0) {
        return nullptr;
    }

    for (uint32_t i = 0; i < 100; i++) {
        if (this->m_friends[i].m_guid == guid) {
            return &this->m_friends[i];
        }
    }

    return nullptr;
}

// ref: FUN_006b3700
bool FriendList::HasComplaint(WOWGUID guid) {
    if (guid != 0) {
        for (uint32_t i = 0; i < 32; i++) {
            if (this->m_complaints[i] == guid) {
                return true;
            }
        }
    }

    return false;
}

// ref: FUN_006b3920
bool FriendList::IsFriend(WOWGUID guid) {
    for (uint32_t i = 0; i < 100; i++) {
        if (this->m_friends[i].m_guid == guid) {
            return true;
        }
    }

    return false;
}
