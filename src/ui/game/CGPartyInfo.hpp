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

class CGPartyInfo {
    public:
        // Public static functions
        static uint32_t NumMembers();

        // One member by 1-based index, as the party1..party4 unit tokens number them. Returns 0
        // for an empty slot or an index outside 1..4.
        static WOWGUID GetMember(uint32_t index);

        static const PARTY_MEMBER* GetMemberInfo(uint32_t index);

        static WOWGUID GetLeader();

        // ref: FUN_0052c8c0
        // Whether a guid belongs to a party member or to a member's pet. The player is NOT a
        // member for this purpose -- "player" and "pet" are their own tokens.
        static bool IsMemberOrPet(WOWGUID guid);

        static void Clear();

        static void RegisterHandlers();

    private:
        // Written only by the group-list handler, which rebuilds the whole roster.
        friend int32_t ReceiveGroupList(void*, NETMESSAGE, uint32_t, CDataStore*);

        static void SetMember(uint32_t slot, const PARTY_MEMBER& member);
        static void SetLeader(WOWGUID leader);

        // Private static variables
        static WOWGUID m_members[];
        static PARTY_MEMBER m_memberInfo[4];
        static WOWGUID m_leader;
};

#endif
