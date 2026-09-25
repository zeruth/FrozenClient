#ifndef UI_GAME_C_G_PARTY_INFO_HPP
#define UI_GAME_C_G_PARTY_INFO_HPP

#include "net/Types.hpp"
#include "util/GUID.hpp"

class CDataStore;

int32_t ReceiveGroupList(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

// The groupType byte at the head of SMSG_GROUP_LIST. Recovered from how the handler branches on
// it: bit 3 gates the LFG block, bit 1 selects the raid path, and bit 0 picks which of the two
// duplicate-detection records the packet is measured against.
enum GROUPTYPE_FLAGS {
    GROUPTYPE_BATTLEGROUND = 0x01,
    GROUPTYPE_RAID         = 0x02,
    GROUPTYPE_LFG          = 0x08,
};

// One row of the group roster as SMSG_GROUP_LIST sends it.
struct PARTY_MEMBER {
    WOWGUID guid = 0;

    // Sent in the packet rather than looked up, so a member's name is known before their object
    // is. The wire buffer is 48 bytes.
    char name[48] = { 0 };

    bool online = false;

    // Which raid subgroup the member is in. Only members sharing the player's own subgroup become
    // party1..party4 -- in a raid the party frames show your own group, not the first four people
    // on the roster.
    uint8_t subgroup = 0;

    uint8_t flags = 0;
    uint8_t roles = 0;
};

// The reference's per-member record block (0x310 bytes a member), copied wholesale when the roster
// is rebuilt. Only the member's pet guid at +0x160 has been identified; the rest is carried as
// raw bytes until the code that reads it is ported. Nothing fills it yet.
struct PARTY_MEMBER_RECORD {
    uint8_t unk000[0x8] = { 0 };
    // Bit 0 says the pet guid below is valid; FUN_0052c6e0 falls back to it when the member's
    // object is not in range.
    uint32_t flags = 0;
    uint8_t unk00C[0x160 - 0xC] = { 0 };
    WOWGUID petGuid = 0;
    uint8_t unk168[0x310 - 0x168] = { 0 };
};

class CGPartyInfo {
    public:
        // Public static functions
        static uint32_t NumMembers();

        // ref: FUN_0052bc50
        // One member's guid by 0-based slot; 0 past the fourth.
        static WOWGUID GetMemberGuid(uint32_t slot);

        // ref: FUN_0052bee0
        // The record block of the member with this guid; null for a null guid or a non-member.
        static PARTY_MEMBER_RECORD* GetMemberRecord(const WOWGUID& guid);

        // ref: FUN_0052bf30
        // The record block of the occupied slot whose member's pet has this guid.
        static PARTY_MEMBER_RECORD* GetMemberRecordByPet(const WOWGUID& pet);

        // ref: FUN_0052c6e0
        // A member's pet, by 0-based slot: the charm or summon off the member's object when it
        // is in range, else the pet guid in the member's record when that is flagged valid.
        static WOWGUID GetMemberPet(uint32_t slot);

        // ref: FUN_0052cb40
        // Stores the setting and sends CMSG_OPT_OUT_OF_LOOT.
        static void SetOptOutOfLoot(uint32_t optOut);

        // ref: FUN_0052c680
        // The active player or one of the four members. A null guid is neither.
        static bool IsPlayerOrMember(const WOWGUID& guid);

        // One member by 1-based index, as the party1..party4 unit tokens number them. Returns 0
        // for an empty slot or an index outside 1..4.
        static WOWGUID GetMember(uint32_t index);

        static const PARTY_MEMBER* GetMemberInfo(uint32_t index);

        static WOWGUID GetLeader();

        // The REAL party, as opposed to the one in force. A battleground puts you in a group of
        // strangers; these two keep reporting the party you actually belong to, because a
        // battleground group list deliberately does not update them.
        static uint32_t GetRealNumMembers();
        static WOWGUID GetRealLeader();

        // The group's loot rules, as the group list sends them. The defaults are the reference's
        // own init values -- group loot at uncommon -- which is what a player not in a group sees.
        static uint32_t GetLootMethod();
        static WOWGUID GetMasterLooter();
        static uint32_t GetLootThreshold();

        static WOWGUID FindByName(const char* name);

        // The group-list flags for a member, by guid. 0 when the guid is not in the party.
        static uint8_t GetMemberFlags(WOWGUID guid);

        // ref: FUN_006d46d0
        static void SendLootSettings(uint32_t method, WOWGUID looter, uint32_t threshold);

        // The group's difficulties, 0-based as the wire sends them. The bindings add one.
        static uint32_t GetDungeonDifficulty();
        static uint32_t GetRaidDifficulty();

        // The player's OWN difficulty settings, which are separate from the group's and are what
        // apply when solo. Both are 0-based like the group's.
        static uint32_t GetOwnDungeonDifficulty();
        static uint32_t GetOwnRaidDifficulty();

        // ref: FUN_00525530 / FUN_005255a0
        // The message carries the value plus two flags saying which of the two settings it
        // applies to, so one packet can change either or both.
        static void ApplyDungeonDifficulty(uint32_t difficulty, bool own, bool group);
        static void ApplyRaidDifficulty(uint32_t difficulty, bool own, bool group);

        // Plain stores into the two dungeon settings, for SetDungeonDifficulty, which writes them
        // directly rather than through ApplyDungeonDifficulty.
        static void SetOwnDungeonDifficulty(uint32_t difficulty);
        static void SetGroupDungeonDifficulty(uint32_t difficulty);

        // ref: FUN_00524720
        // Reports the dungeon difficulty in force (DUNGEON_DIFFICULTY1, 2, ...) as UI error 0x1f7.
        static void DisplayDungeonDifficulty();

        // ref: FUN_0052c8c0
        // Whether a guid belongs to a party member or to a member's pet. The player is NOT a
        // member for this purpose -- "player" and "pet" are their own tokens.
        static bool IsMemberOrPet(WOWGUID guid);

        // ref: FUN_0052d310
        // The WIDER test, and a different function in the reference: this one also matches the
        // player and the player's own pet. IsMemberOrPet above deliberately does not -- the two
        // exist side by side and answer differently for "player".
        static bool IsPlayerOrMemberOrPet(WOWGUID guid);

        static void Clear();

        static void RegisterHandlers();

    private:
        // Written only by the group-list handler, which rebuilds the whole roster.
        friend int32_t ReceiveGroupList(void*, NETMESSAGE, uint32_t, CDataStore*);

        static void SetMember(uint32_t slot, const PARTY_MEMBER& member);
        static void SetLeader(WOWGUID leader);

        // ref: FUN_0052bd60
        static void SetRealParty(uint32_t members, WOWGUID leader);
        static void SetLoot(uint32_t method, WOWGUID looter, uint32_t threshold);
        static void SetDifficulty(uint32_t dungeon, uint32_t raid);

        // Private static variables
        static WOWGUID m_members[];
        static PARTY_MEMBER m_memberInfo[4];
        static PARTY_MEMBER_RECORD m_memberRecords[4];  // ref: DAT_00bd0d08
        static WOWGUID m_leader;
        static uint32_t m_lootMethod;
        static WOWGUID m_masterLooter;
        static uint32_t m_lootThreshold;
        static uint32_t m_dungeonDifficulty;
        static uint32_t m_raidDifficulty;
        static uint32_t m_ownDungeonDifficulty;
        static uint32_t m_ownRaidDifficulty;
        static uint32_t m_optOutOfLoot;  // ref: DAT_00bd19a0
        static uint32_t m_realMemberCount;
        static WOWGUID m_realLeader;
};

#endif
