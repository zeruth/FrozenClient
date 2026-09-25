#include "ui/game/ReputationInfo.hpp"
#include "db/Db.hpp"
#include <storm/Array.hpp>

static REPUTATION s_reputations[NUM_REPUTATIONS];          // ref: DAT_00c22b70
static TSGrowableArray<FORCEDREACTION> s_forcedReactions;  // ref: DAT_00c2348c

// ref: FUN_005d04b0
bool ReputationIsAtWar(int32_t factionID) {
    auto rec = g_factionDB.GetRecord(factionID);

    if (rec && static_cast<uint32_t>(rec->m_reputationIndex) < NUM_REPUTATIONS) {
        return (s_reputations[rec->m_reputationIndex].flags >> 1) & 1;
    }

    return false;
}

// ref: FUN_005d05b0
int32_t ReputationGetStanding(int32_t factionID) {
    auto rec = g_factionDB.GetRecord(factionID);

    if (rec && static_cast<uint32_t>(rec->m_reputationIndex) < NUM_REPUTATIONS) {
        auto& reputation = s_reputations[rec->m_reputationIndex];

        return reputation.value0C + reputation.value08;
    }

    return 0;
}

// ref: FUN_005d06a0
bool ReputationGetForcedReaction(int32_t factionID, int32_t* reaction) {
    for (uint32_t i = 0; i < s_forcedReactions.Count(); i++) {
        if (factionID == s_forcedReactions.m_data[i].factionID) {
            *reaction = s_forcedReactions.m_data[i].reaction;

            return true;
        }
    }

    return false;
}
