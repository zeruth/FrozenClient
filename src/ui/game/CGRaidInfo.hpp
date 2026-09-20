#ifndef UI_GAME_C_G_RAID_INFO_HPP
#define UI_GAME_C_G_RAID_INFO_HPP

#include "util/GUID.hpp"

#include <cstdint>

#define MAX_RAID_MEMBERS 40

class CGRaidInfo {
    public:
        // Public static functions

        // What GetNumRaidMembers reports: the roster plus the player when in a raid, and 0 when
        // not. The reference computes it the same way -- memberCount + 1 -- so this is one more
        // than the roster this class actually stores.
        static uint32_t NumMembers();

        // ref: FUN_00573200
        // Whether a guid belongs to a raid member or to a member's pet. The player is not on the
        // roster: the packet does not list them.
        static bool IsMemberOrPet(WOWGUID guid);

        // Replaces the roster from a group list. A non-raid group clears it.
        static void SetRoster(const WOWGUID* members, uint32_t count, bool isRaid);

    private:
        // Private static variables
        static uint32_t s_numMembers;
        static WOWGUID s_members[MAX_RAID_MEMBERS];
};

#endif
