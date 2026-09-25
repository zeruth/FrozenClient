#ifndef UI_GAME_C_G_RAID_INFO_HPP
#define UI_GAME_C_G_RAID_INFO_HPP

#include "util/GUID.hpp"

#include <cstdint>

#define MAX_RAID_MEMBERS 40

// ref: FUN_00572f50
// Whether the current map is a battleground or an arena (Map.dbc instance type 3 or 4).
int32_t InstanceIsBattlegroundOrArena();

class CGRaidInfo {
    public:
        // Public static functions

        // What GetNumRaidMembers reports: the roster plus the player when in a raid, and 0 when
        // not. The reference computes it the same way -- memberCount + 1 -- so this is one more
        // than the roster this class actually stores.
        static uint32_t NumMembers();

        // The REAL raid, the counterpart of CGPartyInfo's real party: a battleground group list
        // updates NumMembers above but deliberately leaves this alone, so it keeps describing the
        // raid you actually belong to.
        static uint32_t GetRealNumMembers();

        // ref: FUN_00572530
        static void SetRealNumMembers(uint32_t members);

        // ref: FUN_00573200
        // Whether a guid belongs to a raid member or to a member's pet. The player is not on the
        // roster: the packet does not list them.
        static bool IsMemberOrPet(WOWGUID guid);

        // Replaces the roster from a group list. A non-raid group clears it.
        static void SetRoster(const WOWGUID* members, const char* const* names,
                              const uint8_t* flags, uint32_t count, bool isRaid);

        // The group-list flags for a raid slot, 1-based. The PLAYER's own slot always reports 0:
        // the packet lists everyone else, so the client is never told its own assignment flags
        // through this message.
        static uint8_t GetMemberFlags(uint32_t index);

        // A member's 1-based raid index, or 0 when the guid is not on the roster.
        static uint32_t IndexOf(WOWGUID guid);

        // A member by name, for the loot bindings, which name the master looter rather than
        // pointing at a unit. Case-insensitive, as the reference compares.
        static WOWGUID FindByName(const char* name);

        // A member by 1-based index, as raid1..raid40 number them. The PLAYER is in this
        // numbering -- see SetRoster for where.
        static WOWGUID GetMember(uint32_t index);

        // ref: FUN_00572690
        // The roster slot (0-based) of the selected member, or 0xFFFFFFFF when nothing is
        // selected or the selection has left the roster.
        static uint32_t GetSelectionIndex();

        // ref: FUN_005726f0
        static int32_t IsMember(WOWGUID guid);

        // The member SetRaidRosterSelection picked (ref: DAT_00beb610). Only the script binding
        // writes it.
        static WOWGUID s_selection;

    private:
        // Private static variables
        static uint32_t s_numMembers;
        static uint32_t s_realNumMembers;
        static WOWGUID s_members[MAX_RAID_MEMBERS];
        static char s_names[MAX_RAID_MEMBERS][48];
        static uint8_t s_flags[MAX_RAID_MEMBERS];
};

#endif
