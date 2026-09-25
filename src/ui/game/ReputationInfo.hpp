#ifndef UI_GAME_REPUTATION_INFO_HPP
#define UI_GAME_REPUTATION_INFO_HPP

#include <cstdint>

#define NUM_REPUTATIONS 128

// One reputation slot, indexed by Faction.dbc's reputation index. Flag 0x2 is "at war". The two
// values are summed for the faction's standing; which of them is the earned part is not
// recovered. Filled by the faction messages, which are not handled yet.
struct REPUTATION {
    uint32_t unk00;
    uint8_t flags;
    int32_t value08;
    int32_t value0C;
};

// A standing the server forces for a faction, whatever the reputation says.
struct FORCEDREACTION {
    int32_t factionID;
    int32_t reaction;
};

bool ReputationGetForcedReaction(int32_t factionID, int32_t* reaction);

int32_t ReputationGetStanding(int32_t factionID);

bool ReputationIsAtWar(int32_t factionID);

#endif
