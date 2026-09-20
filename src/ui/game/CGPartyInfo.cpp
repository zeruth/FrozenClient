#include "ui/game/CGPartyInfo.hpp"

WOWGUID CGPartyInfo::m_members[4];

// The party roster is NOT FILLED IN by anything yet, so every caller of this -- and of
// NumMembers above -- currently sees an empty party. Nothing writes m_members: the group roster
// arrives in a server message that frozen does not handle, and until it does the whole party
// surface is dark.
//
// That is worth naming because of how far it reaches. The party1..4 and partypet1..4 unit tokens
// resolve through here, so Script_GetTokenFromGUID can only ever answer "player", "target", "pet"
// or "focus" -- which means the UNIT_AURA, cast, UNIT_NAME_UPDATE and MINIMAP_PING events all
// reach the player and nobody else, the party frames have nothing to draw, and UnitIsFeignDeath
// and UnitPlayerOrPetInParty cannot be answered at all.
//
// The handler is SMSG_GROUP_LIST (0x7D), already in frozen's enum, implemented at FUN_006d8870.
// docs/ref/parity-party.md has the header layout, the globals, the duplicate-packet rule, and how
// the handler was found.
WOWGUID CGPartyInfo::GetMember(uint32_t index) {
    if (index < 1 || index > 4) {
        return 0;
    }

    return CGPartyInfo::m_members[index - 1];
}

uint32_t CGPartyInfo::NumMembers() {
    uint32_t count = 0;

    for (auto& member : CGPartyInfo::m_members) {
        if (member != 0) {
            count++;
        }
    }

    return count;
}
