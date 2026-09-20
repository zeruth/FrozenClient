#include "ui/game/CGRaidInfo.hpp"

#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"

uint32_t CGRaidInfo::s_numMembers;
WOWGUID CGRaidInfo::s_members[MAX_RAID_MEMBERS];

uint32_t CGRaidInfo::NumMembers() {
    return CGRaidInfo::s_numMembers;
}

// ref: FUN_00573200
bool CGRaidInfo::IsMemberOrPet(WOWGUID guid) {
    if (!guid) {
        return false;
    }

    for (uint32_t i = 0; i < MAX_RAID_MEMBERS; i++) {
        auto member = CGRaidInfo::s_members[i];

        if (!member) {
            continue;
        }

        if (member == guid) {
            return true;
        }

        // Charm before summon, as everywhere else a pet is resolved.
        auto object = ClntObjMgrObjectPtr(member, TYPE_UNIT, __FILE__, __LINE__);

        if (object) {
            auto data = static_cast<CGUnit_C*>(object)->Unit();
            auto pet = data->charm ? data->charm : data->summon;

            if (pet && pet == guid) {
                return true;
            }
        }
    }

    return false;
}

void CGRaidInfo::SetRoster(const WOWGUID* members, uint32_t count, bool isRaid) {
    for (uint32_t i = 0; i < MAX_RAID_MEMBERS; i++) {
        CGRaidInfo::s_members[i] = 0;
    }

    if (!isRaid || !count) {
        // The reference reports zero for a group that is not a raid AND for a raid with an empty
        // member list, rather than letting the "+ 1" answer 1 for an empty roster.
        CGRaidInfo::s_numMembers = 0;

        return;
    }

    auto stored = count < MAX_RAID_MEMBERS ? count : MAX_RAID_MEMBERS;

    for (uint32_t i = 0; i < stored; i++) {
        CGRaidInfo::s_members[i] = members[i];
    }

    // Plus the player, who is not on the wire.
    CGRaidInfo::s_numMembers = count + 1;
}
