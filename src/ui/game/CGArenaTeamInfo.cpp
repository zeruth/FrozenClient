#include "ui/game/CGArenaTeamInfo.hpp"

ARENATEAMINFO CGArenaTeamInfo::s_teams[NUM_ARENA_TEAMS];

#include "object/client/CGPlayer_C.hpp"

// The team size of each bracket: 2v2, 3v3, 5v5.
static const uint32_t s_bracketSizes[3] = { 2, 3, 5 };     // ref: DAT_00a15abc

// ref: FUN_005a37b0
// Whether one of the player's teams of at least the bracket's size has the player's personal rating
// at `rating` or above, and its team rating too.
bool CGArenaTeamInfo::HasRating(uint32_t rating, int32_t bracket) {
    auto player = CGPlayer_C::GetActivePtr();

    if (!player) {
        return false;
    }

    for (uint32_t i = 0; i < 3; i++) {
        auto& team = player->Player()->arenaTeamInfo[i];

        if (s_bracketSizes[bracket] > team.field2 || rating > team.field7) {
            continue;
        }

        for (uint32_t j = 0; j < NUM_ARENA_TEAMS; j++) {
            if (CGArenaTeamInfo::s_teams[j].teamSize == team.field2 && rating <= CGArenaTeamInfo::s_teams[j].rating) {
                return true;
            }
        }
    }

    return false;
}
