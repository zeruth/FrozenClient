#ifndef UI_GAME_C_G_BATTLEFIELD_INFO_HPP
#define UI_GAME_C_G_BATTLEFIELD_INFO_HPP

#include "util/guid/Types.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

class CGBattlefieldInfo {
    public:
        // Types
        struct QueueSlot {
            uint8_t m_unk00[0x28];
            int32_t m_portExpireTime;           // +0x28, OsGetAsyncTimeMs() deadline
            uint32_t m_estimatedWaitTime;       // +0x2C
            int32_t m_queueJoinTime;            // +0x30, OsGetAsyncTimeMs() stamp
            uint8_t m_unk34[0xC];
        };

        struct ScoreEntry {
            WOWGUID m_guid;                     // +0x00
            uint32_t m_killingBlows;            // +0x08
            uint32_t m_honorableKills;          // +0x0C
            uint32_t m_deaths;                  // +0x10
            uint32_t m_honorGained;             // +0x14
            uint32_t m_damageDone;              // +0x18
            uint32_t m_healingDone;             // +0x1C
            int32_t m_faction;                  // +0x20
            int32_t m_stats[9];                 // +0x24
        };

        // Static variables
        static char s_teamNames[2][0x60];
        static WOWGUID s_flagCarriers[2];
        static ScoreEntry* s_scoreList[80];
        static QueueSlot s_queueSlots[2];
        static int32_t s_selectedInstance;
        static uint8_t s_arenaFactionFlag;
        static int32_t s_mapID;
        static int32_t s_instanceExpireTime;
        static int32_t s_instanceStartTime;
        static uint32_t s_scoreListCount;
        static uint32_t s_numScores;
        static int32_t s_hasWinner;
        static uint32_t s_winner;
        static int32_t s_teamOldRating[2];
        static int32_t s_teamNewRating[2];
        static int32_t s_teamRating[2];
        static uint32_t s_numVehicles;
        static uint32_t s_numInstances;
        static int32_t* s_instanceIDs;
        static uint32_t s_numPlayerPositions;
        static C2Vector s_playerPositions[40];
        static C2Vector s_flagPositions[2];
        static int32_t s_numBattlegrounds;
        static int32_t* s_battlegroundIDs;

        // Static functions
        static int32_t GetBattlegroundID(int32_t index);
        static WOWGUID GetFlagCarrier(uint32_t index);
        static int32_t GetFlagPosition(uint32_t index, C3Vector* position);
        static int32_t GetPlayerPosition(uint32_t index, C3Vector* position);
        static void RequestPlayerPositions();
        static void SendMgrEntryInviteResponse(uint32_t battleId, int32_t accept);
        static void SendMgrQueueInviteResponse(uint32_t battleId, int32_t accept);
        static void SendMgrQueueRequest(uint32_t battleId);
        static void SendMgrExitRequest(uint32_t battleId);
};

#endif
