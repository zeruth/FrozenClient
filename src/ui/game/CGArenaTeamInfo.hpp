#ifndef UI_GAME_C_G_ARENA_TEAM_INFO_HPP
#define UI_GAME_C_G_ARENA_TEAM_INFO_HPP

#include <cstdint>

#define NUM_ARENA_TEAMS 3

// One of the player's arena teams, 0x38 bytes in the reference. Only the team id at the head is
// read by anything ported; the arena team handlers that fill the rest are not ported.
struct ARENATEAMINFO {
    uint32_t teamID;
    uint32_t unk04[13];
};

class CGArenaTeamInfo {
    public:
        // Public static variables
        static ARENATEAMINFO s_teams[NUM_ARENA_TEAMS]; // ref: DAT_00c0f840
};

#endif
