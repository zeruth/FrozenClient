#include "ui/game/CGRaidInfo.hpp"
#include <storm/String.hpp>

#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"

uint32_t CGRaidInfo::s_numMembers;
uint32_t CGRaidInfo::s_realNumMembers;
WOWGUID CGRaidInfo::s_members[MAX_RAID_MEMBERS];
char CGRaidInfo::s_names[MAX_RAID_MEMBERS][48];

uint32_t CGRaidInfo::NumMembers() {
    return CGRaidInfo::s_numMembers;
}

uint32_t CGRaidInfo::GetRealNumMembers() {
    return CGRaidInfo::s_realNumMembers;
}

// ref: FUN_00572530
// Clamped to a raid's size. Anything larger is not a raid count and the stored one is left as it
// was, the same shape as the party's own clamp at five.
void CGRaidInfo::SetRealNumMembers(uint32_t members) {
    if (members < 41) {
        CGRaidInfo::s_realNumMembers = members;
    }
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

WOWGUID CGRaidInfo::FindByName(const char* name) {
    if (!name || !*name) {
        return 0;
    }

    for (uint32_t i = 0; i < MAX_RAID_MEMBERS; i++) {
        if (CGRaidInfo::s_members[i] && !SStrCmpI(CGRaidInfo::s_names[i], name, STORM_MAX_STR)) {
            return CGRaidInfo::s_members[i];
        }
    }

    return 0;
}

void CGRaidInfo::SetRoster(const WOWGUID* members, const char* const* names, uint32_t count,
                           bool isRaid) {
    for (uint32_t i = 0; i < MAX_RAID_MEMBERS; i++) {
        CGRaidInfo::s_members[i] = 0;
        CGRaidInfo::s_names[i][0] = '\0';
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
        SStrCopy(CGRaidInfo::s_names[i], names[i], sizeof(CGRaidInfo::s_names[i]));
    }

    // Plus the player, who is not on the wire.
    CGRaidInfo::s_numMembers = count + 1;
}
