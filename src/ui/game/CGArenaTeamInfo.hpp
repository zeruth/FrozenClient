#ifndef UI_GAME_C_G_ARENA_TEAM_INFO_HPP
#define UI_GAME_C_G_ARENA_TEAM_INFO_HPP

#include <cstdint>

#define NUM_ARENA_TEAMS 3

// One of the player's arena teams, 0x38 bytes in the reference. Only the team id at the head is
// read by anything ported; the arena team handlers that fill the rest are not ported.
struct ARENATEAMINFO {
    uint32_t teamID;            // +0x00
    uint32_t unk04[4];
    uint32_t teamSize;          // +0x14
    uint32_t unk18[2];
    uint32_t rating;            // +0x20
    uint32_t unk24[5];
};

class CGArenaTeamInfo {
    public:
        // Public static variables
        static ARENATEAMINFO s_teams[NUM_ARENA_TEAMS]; // ref: DAT_00c0f840

        // Public static functions
        static bool HasRating(uint32_t rating, int32_t bracket);
};

#endif
