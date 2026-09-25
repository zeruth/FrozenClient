#include "ui/game/CGPartyInfo.hpp"
#include <storm/String.hpp>
#include "ui/game/CGRaidInfo.hpp"

#include "client/ClientServices.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/CGUnit_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/Types.hpp"

#include <common/DataStore.hpp>
#include <common/Time.hpp>
#include <cstring>

WOWGUID CGPartyInfo::m_members[4];
PARTY_MEMBER CGPartyInfo::m_memberInfo[4];
WOWGUID CGPartyInfo::m_leader;

// Group loot at uncommon: the values the reference's party init starts from, and what a player who
// is not in a group keeps seeing.
uint32_t CGPartyInfo::m_lootMethod = 3;
WOWGUID CGPartyInfo::m_masterLooter = 0;
uint32_t CGPartyInfo::m_lootThreshold = 2;
uint32_t CGPartyInfo::m_dungeonDifficulty = 0;
uint32_t CGPartyInfo::m_raidDifficulty = 0;
uint32_t CGPartyInfo::m_ownDungeonDifficulty = 0;
uint32_t CGPartyInfo::m_ownRaidDifficulty = 0;
uint32_t CGPartyInfo::m_realMemberCount = 0;
WOWGUID CGPartyInfo::m_realLeader = 0;

namespace {

// The reference keeps two of these, one for the party list and one for the raid list, and drops a
// packet that repeats a group it has already seen. See ReceiveGroupList.
struct GROUP_LIST_SEEN {
    WOWGUID group = 0;
    uint32_t counter = 0;
    uint32_t receivedMs = 0;
};

GROUP_LIST_SEEN s_seen[2];

} // namespace

uint32_t CGPartyInfo::NumMembers() {
    uint32_t count = 0;

    for (auto& member : CGPartyInfo::m_members) {
        if (member != 0) {
            count++;
        }
    }

    return count;
}

WOWGUID CGPartyInfo::GetMember(uint32_t index) {
    if (index < 1 || index > 4) {
        return 0;
    }

    return CGPartyInfo::m_members[index - 1];
}

const PARTY_MEMBER* CGPartyInfo::GetMemberInfo(uint32_t index) {
    if (index < 1 || index > 4 || !CGPartyInfo::m_members[index - 1]) {
        return nullptr;
    }

    return &CGPartyInfo::m_memberInfo[index - 1];
}

WOWGUID CGPartyInfo::GetLeader() {
    return CGPartyInfo::m_leader;
}

// ref: FUN_0052c8c0
// A null guid is not a member, which matters: an unresolved unit token leaves the guid at zero and
// empty roster slots are zero too, so without this every failed lookup would match slot 4.
bool CGPartyInfo::IsMemberOrPet(WOWGUID guid) {
    if (!guid) {
        return false;
    }

    for (uint32_t slot = 1; slot <= 4; slot++) {
        auto member = CGPartyInfo::GetMember(slot);

        if (!member) {
            continue;
        }

        if (member == guid) {
            return true;
        }

        // The member's pet. The reference keeps a parallel pet-guid array; frozen reads it off the
        // member's object the same way the partypet tokens do, charm before summon.
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

// ref: FUN_0052d310
// Everyone the player's own group frames can show: the player, the player's pet, a party member,
// or a party member's pet. Tested in that order, cheapest first, as the reference does.
bool CGPartyInfo::IsPlayerOrMemberOrPet(WOWGUID guid) {
    if (!guid) {
        return false;
    }

    auto activePlayer = ClntObjMgrGetActivePlayer();

    if (guid == activePlayer) {
        return true;
    }

    auto object = activePlayer
        ? ClntObjMgrObjectPtr(activePlayer, TYPE_UNIT, __FILE__, __LINE__)
        : nullptr;

    if (object) {
        auto data = static_cast<CGUnit_C*>(object)->Unit();
        auto pet = data->charm ? data->charm : data->summon;

        if (pet && pet == guid) {
            return true;
        }
    }

    return CGPartyInfo::IsMemberOrPet(guid);
}

void CGPartyInfo::SetMember(uint32_t slot, const PARTY_MEMBER& member) {
    CGPartyInfo::m_memberInfo[slot] = member;
    CGPartyInfo::m_members[slot] = member.guid;
}

uint32_t CGPartyInfo::GetLootMethod() {
    return CGPartyInfo::m_lootMethod;
}

WOWGUID CGPartyInfo::GetMasterLooter() {
    return CGPartyInfo::m_masterLooter;
}

uint32_t CGPartyInfo::GetLootThreshold() {
    return CGPartyInfo::m_lootThreshold;
}

WOWGUID CGPartyInfo::FindByName(const char* name) {
    if (!name || !*name) {
        return 0;
    }

    for (uint32_t slot = 1; slot <= 4; slot++) {
        auto info = CGPartyInfo::GetMemberInfo(slot);

        if (info && !SStrCmpI(info->name, name, STORM_MAX_STR)) {
            return info->guid;
        }
    }

    return CGRaidInfo::FindByName(name);
}

uint8_t CGPartyInfo::GetMemberFlags(WOWGUID guid) {
    for (uint32_t slot = 1; slot <= 4; slot++) {
        auto info = CGPartyInfo::GetMemberInfo(slot);

        if (info && info->guid == guid) {
            return info->flags;
        }
    }

    return 0;
}

// ref: FUN_006d46d0
// One message carries all three loot settings, so changing the threshold resends the method and
// the master looter unchanged. There is no opcode for one of them alone.
void CGPartyInfo::SendLootSettings(uint32_t method, WOWGUID looter, uint32_t threshold) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_SET_LOOT_METHOD));
    msg.Put(method);
    msg.Put(looter);
    msg.Put(threshold);
    msg.Finalize();

    ClientServices::Send(&msg);
}

void CGPartyInfo::SetLoot(uint32_t method, WOWGUID looter, uint32_t threshold) {
    CGPartyInfo::m_lootMethod = method;
    CGPartyInfo::m_masterLooter = looter;
    CGPartyInfo::m_lootThreshold = threshold;
}

uint32_t CGPartyInfo::GetDungeonDifficulty() {
    return CGPartyInfo::m_dungeonDifficulty;
}

uint32_t CGPartyInfo::GetRaidDifficulty() {
    return CGPartyInfo::m_raidDifficulty;
}

uint32_t CGPartyInfo::GetOwnDungeonDifficulty() {
    return CGPartyInfo::m_ownDungeonDifficulty;
}

uint32_t CGPartyInfo::GetOwnRaidDifficulty() {
    return CGPartyInfo::m_ownRaidDifficulty;
}

// ref: FUN_00525530
// One message, two possible targets. The reference also re-announces the dungeon difficulty in
// chat when the EFFECTIVE value changed as a result -- not ported, that needs the chat path.
void CGPartyInfo::ApplyDungeonDifficulty(uint32_t difficulty, bool own, bool group) {
    if (own) {
        CGPartyInfo::m_ownDungeonDifficulty = difficulty;
    }

    if (group) {
        CGPartyInfo::m_dungeonDifficulty = difficulty;
    }
}

void CGPartyInfo::SetOwnDungeonDifficulty(uint32_t difficulty) {
    CGPartyInfo::m_ownDungeonDifficulty = difficulty;
}

void CGPartyInfo::SetGroupDungeonDifficulty(uint32_t difficulty) {
    CGPartyInfo::m_dungeonDifficulty = difficulty;
}

// ref: FUN_00524720
// The value in force follows the same rule as GetDungeonDifficulty: the group's while in a party
// that is not a raid, the player's own otherwise.
void CGPartyInfo::DisplayDungeonDifficulty() {
    auto difficulty = CGPartyInfo::m_ownDungeonDifficulty;

    if (CGPartyInfo::GetMember(1) && !CGRaidInfo::NumMembers()) {
        difficulty = CGPartyInfo::m_dungeonDifficulty;
    }

    char token[64];
    SStrPrintf(token, sizeof(token), "DUNGEON_DIFFICULTY%d", difficulty + 1);

    auto text = FrameScript_GetText(token, -1, GENDER_NOT_APPLICABLE);
    CGGameUI::DisplayError(0x1f7, text);
}

// ref: FUN_005255a0
void CGPartyInfo::ApplyRaidDifficulty(uint32_t difficulty, bool own, bool group) {
    if (own) {
        CGPartyInfo::m_ownRaidDifficulty = difficulty;
    }

    if (group) {
        // TODO the reference checks the current map's record first: a map flagged 0x100 -- the old
        // two-difficulty raids -- takes a different path that collapses the value to a boolean
        // instead of storing it. Frozen stores it either way.
        CGPartyInfo::m_raidDifficulty = difficulty;
    }
}

void CGPartyInfo::SetDifficulty(uint32_t dungeon, uint32_t raid) {
    CGPartyInfo::m_dungeonDifficulty = dungeon;
    CGPartyInfo::m_raidDifficulty = raid;
}

// ref: FUN_0052bc80
// Signals only on an actual change. The reference recomputes the leader's party index here too;
// GetPartyLeaderIndex derives that on demand instead, so there is nothing to keep in step.
void CGPartyInfo::SetLeader(WOWGUID leader) {
    if (CGPartyInfo::m_leader == leader) {
        return;
    }

    CGPartyInfo::m_leader = leader;

    FrameScript_SignalEvent(SCRIPT_PARTY_LEADER_CHANGED, nullptr);
}

uint32_t CGPartyInfo::GetRealNumMembers() {
    return CGPartyInfo::m_realMemberCount;
}

WOWGUID CGPartyInfo::GetRealLeader() {
    return CGPartyInfo::m_realLeader;
}

// ref: FUN_0052bd60
// The count is stored only when it fits a party -- five or more is a raid, and the real PARTY
// count is left alone rather than being overwritten with a raid size. The leader is stored either
// way.
void CGPartyInfo::SetRealParty(uint32_t members, WOWGUID leader) {
    if (members < 5) {
        CGPartyInfo::m_realMemberCount = members;
    }

    CGPartyInfo::m_realLeader = leader;
}

void CGPartyInfo::Clear() {
    for (int32_t i = 0; i < 4; i++) {
        CGPartyInfo::m_members[i] = 0;
        CGPartyInfo::m_memberInfo[i] = PARTY_MEMBER();
    }

    CGPartyInfo::m_leader = 0;
}

// ref: FUN_006d8870
// SMSG_GROUP_LIST. The whole roster arrives at once, so this rebuilds rather than patches.
//
// Field order is taken from the reference's reads in sequence; the readers were identified by how
// many bytes each advances the cursor rather than by name, since frozen's CDataStore getters carry
// no reference tags.
//
// Two things a plainer port would get wrong:
//
// The LFG block is CONDITIONAL on groupType bit 3. Reading it unconditionally shifts every
// following field, which would put the member count where the group guid is.
//
// party1..party4 are not the first four members. Each member carries a subgroup, and only those
// sharing the player's own subgroup are eligible -- in a raid the party frames show your own
// group. The player is skipped as well, since "player" is its own token.
int32_t ReceiveGroupList(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg) {
        return 1;
    }

    uint8_t groupType = 0;
    uint8_t ownSubgroup = 0;
    uint8_t groupFlags = 0;
    uint8_t ownRoles = 0;

    msg->Get(groupType);
    msg->Get(ownSubgroup);
    msg->Get(groupFlags);
    msg->Get(ownRoles);

    if (groupType & GROUPTYPE_LFG) {
        uint8_t lfgState = 0;
        uint32_t lfgFlags = 0;

        msg->Get(lfgState);
        msg->Get(lfgFlags);
    }

    WOWGUID group = 0;
    uint32_t counter = 0;

    msg->Get(group);
    msg->Get(counter);

    // A resend of a group already seen is dropped outright: same group, a counter no higher, and
    // less than a minute since the last one. Without this the roster is rebuilt -- and
    // PARTY_MEMBERS_CHANGED signalled -- on messages the reference ignores.
    //
    // The two records are keyed on the BATTLEGROUND bit, not on raid: a battleground group and a
    // normal one are tracked separately so that entering one does not make the other look stale.
    auto& seen = s_seen[(groupType & GROUPTYPE_BATTLEGROUND) ? 1 : 0];
    auto now = OsGetAsyncTimeMs();

    if (group && counter && seen.group == group && seen.counter && counter <= seen.counter
        && (now - seen.receivedMs) < 60000) {
        return 1;
    }

    if (group && counter) {
        seen.group = group;
        seen.counter = counter;
        seen.receivedMs = now;
    }

    uint32_t memberCount = 0;
    msg->Get(memberCount);

    CGPartyInfo::Clear();

    auto activePlayer = ClntObjMgrGetActivePlayer();
    uint32_t slot = 0;

    // The same records feed the raid roster, which keeps everyone rather than the player's own
    // subgroup.
    WOWGUID raidMembers[MAX_RAID_MEMBERS] = { 0 };
    const char* raidNames[MAX_RAID_MEMBERS] = { nullptr };
    char raidNameStorage[MAX_RAID_MEMBERS][48] = { { 0 } };
    uint8_t raidFlags[MAX_RAID_MEMBERS] = { 0 };
    uint32_t raidCount = 0;

    for (uint32_t i = 0; i < memberCount; i++) {
        PARTY_MEMBER member;

        msg->GetString(member.name, sizeof(member.name));
        msg->Get(member.guid);

        uint8_t online = 0;
        msg->Get(online);
        msg->Get(member.subgroup);
        msg->Get(member.flags);
        msg->Get(member.roles);

        member.online = online != 0;

        if (raidCount < MAX_RAID_MEMBERS) {
            raidMembers[raidCount] = member.guid;
            SStrCopy(raidNameStorage[raidCount], member.name, sizeof(raidNameStorage[raidCount]));
            raidNames[raidCount] = raidNameStorage[raidCount];
            raidFlags[raidCount] = member.flags;
            raidCount++;
        }

        // Read every record even once the four slots are full: the cursor has to reach the leader
        // guid that follows, and the fields after it.
        if (slot < 4 && member.guid != activePlayer && member.subgroup == ownSubgroup) {
            CGPartyInfo::SetMember(slot, member);
            slot++;
        }
    }

    WOWGUID leader = 0;
    msg->Get(leader);

    // The REAL party is updated only by a group list that is not a battleground's. That is what
    // lets GetRealNumPartyMembers keep answering about the party you left behind while you are in
    // a battleground group of strangers.
    if (!(groupType & GROUPTYPE_BATTLEGROUND)) {
        CGPartyInfo::SetRealParty(slot, leader);

        // The real RAID count follows the same rule and the same formula as the effective one:
        // the roster plus the player, and zero when this is not a raid at all.
        auto isRaid = (groupType & GROUPTYPE_RAID) != 0;

        CGRaidInfo::SetRealNumMembers((isRaid && memberCount) ? memberCount + 1 : 0);
    }

    CGPartyInfo::SetLeader(leader);
    CGRaidInfo::SetRoster(raidMembers, raidNames, raidFlags, raidCount,
                          (groupType & GROUPTYPE_RAID) != 0);

    // The loot block is CONDITIONAL on there being members at all -- a second conditional in this
    // packet after the LFG one. An empty group carries none of it and the reference resets the
    // rules to their defaults rather than leaving the last group's in place.
    if (memberCount) {
        uint8_t lootMethod = 0;
        WOWGUID masterLooter = 0;
        uint8_t lootThreshold = 0;

        msg->Get(lootMethod);
        msg->Get(masterLooter);
        msg->Get(lootThreshold);

        CGPartyInfo::SetLoot(lootMethod, masterLooter, lootThreshold);

        uint8_t dungeonDifficulty = 0;
        uint8_t raidDifficulty = 0;
        uint8_t dynamicDifficulty = 0;

        msg->Get(dungeonDifficulty);
        msg->Get(raidDifficulty);
        msg->Get(dynamicDifficulty);

        CGPartyInfo::SetDifficulty(dungeonDifficulty, raidDifficulty);
    } else {
        CGPartyInfo::SetLoot(3, 0, 2);
        CGPartyInfo::SetDifficulty(0, 0);
    }

    FrameScript_SignalEvent(SCRIPT_PARTY_MEMBERS_CHANGED, nullptr);

    return 1;
}

// MSG_SET_DUNGEON_DIFFICULTY / MSG_SET_RAID_DIFFICULTY: the value, then two flags for which of
// the player's own and the group's settings it applies to. Three uint32s, not a byte among them.
int32_t ReceiveSetDungeonDifficulty(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg) {
        return 1;
    }

    uint32_t difficulty = 0;
    uint32_t own = 0;
    uint32_t group = 0;

    msg->Get(difficulty);
    msg->Get(own);
    msg->Get(group);

    CGPartyInfo::ApplyDungeonDifficulty(difficulty, own != 0, group != 0);

    return 1;
}

int32_t ReceiveSetRaidDifficulty(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg) {
        return 1;
    }

    uint32_t difficulty = 0;
    uint32_t own = 0;
    uint32_t group = 0;

    msg->Get(difficulty);
    msg->Get(own);
    msg->Get(group);

    CGPartyInfo::ApplyRaidDifficulty(difficulty, own != 0, group != 0);

    return 1;
}

void CGPartyInfo::RegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_GROUP_LIST, &ReceiveGroupList, nullptr);
    ClientServices::SetMessageHandler(MSG_SET_DUNGEON_DIFFICULTY, &ReceiveSetDungeonDifficulty, nullptr);
    ClientServices::SetMessageHandler(MSG_SET_RAID_DIFFICULTY, &ReceiveSetRaidDifficulty, nullptr);
}
