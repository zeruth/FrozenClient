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
// The reference's party module lives around 0052bb00-0052e200 and zeroes this state in
// FUN_0052d0e0; the roster array is DAT_00bd1948 and the pet array DAT_00bd0e68. The message
// handler that fills them is registered outside that init, so it still has to be found.
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
